/** @file
  * @note    Hangzhou Hikvision Digital Technology Co., Ltd. All rights reserved.
  * @brief   script algo main class
  */
#include "script.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "cjson/cJSON.h"
#include "log/log.h"
#include "lua.hpp"
#include "osal_dir.h"
#include "osal_file.h"
#include "osal_heap.h"

#define DEBUG_GLOBAL_MDC_STRING            "CScriptModule"
#define SCRIPT_INPUT                       "script_input"
#define SCRIPT_SUB_ENTER                   "enter_sub"
#define SCRIPT_PUB_ENTER                   "enter_pub"
#define SCRIPT_CHECK                       "CheckScriptFormat"

namespace {

using mvsc::script::AnalyzeScript;
using mvsc::script::ScriptIssue;
using mvsc::script::ScriptValidationResult;
using mvsc::script::ScriptValueType;
using mvsc::script::ToTypeName;

const char *kScriptChunkName = "@script_operator";
const char *kScriptEntryName = "run";
const size_t kMaxScriptLength = 4096;
const size_t kMaxLuaMemoryBytes = 256 * 1024;
const int kLuaHookInstructionStep = 2000;
const uint64_t kLuaTimeoutMs = 10;

struct LuaMemoryContext {
  size_t used;
  size_t limit;
  bool denied;
};

struct LuaExecContext {
  uint64_t start_ms;
  uint64_t timeout_ms;
  bool timed_out;
};

uint64_t nowMilliseconds() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return static_cast<uint64_t>(tv.tv_sec) * 1000ULL +
         static_cast<uint64_t>(tv.tv_usec) / 1000ULL;
}

void *luaLimitedAllocator(void *ud, void *ptr, size_t osize, size_t nsize) {
  LuaMemoryContext *ctx = static_cast<LuaMemoryContext *>(ud);
  if (ctx == NULL) {
    return NULL;
  }

  if (nsize == 0) {
    if (ptr != NULL) {
      if (ctx->used >= osize) {
        ctx->used -= osize;
      } else {
        ctx->used = 0;
      }
      free(ptr);
    }
    return NULL;
  }

  if (nsize > osize) {
    const size_t grow = nsize - osize;
    if (ctx->used + grow > ctx->limit) {
      ctx->denied = true;
      return NULL;
    }
  }

  void *new_ptr = realloc(ptr, nsize);
  if (new_ptr == NULL) {
    return NULL;
  }

  if (nsize > osize) {
    ctx->used += (nsize - osize);
  } else if (osize > nsize) {
    ctx->used -= (osize - nsize);
  }
  return new_ptr;
}

int luaChunkWriter(lua_State *, const void *p, size_t sz, void *ud) {
  std::vector<char> *chunk = static_cast<std::vector<char> *>(ud);
  if (chunk == NULL || p == NULL) {
    return 1;
  }

  const char *begin = static_cast<const char *>(p);
  chunk->insert(chunk->end(), begin, begin + sz);
  return 0;
}

void removeGlobal(lua_State *L, const char *name) {
  lua_pushnil(L);
  lua_setglobal(L, name);
}

void copyGlobal(lua_State *L, int env_index, const char *name) {
  lua_getglobal(L, name);
  lua_setfield(L, env_index, name);
}

void copyLibrary(lua_State *L, int env_index, const char *name) {
  lua_getglobal(L, name);
  if (lua_istable(L, -1)) {
    lua_newtable(L);
    const int dst_index = lua_gettop(L);

    lua_pushnil(L);
    while (lua_next(L, -3) != 0) {
      const int key_index = lua_gettop(L) - 1;
      lua_pushvalue(L, key_index);
      lua_insert(L, -2);
      lua_settable(L, dst_index);
    }

    if (strcmp(name, "string") == 0) {
      lua_pushnil(L);
      lua_setfield(L, dst_index, "dump");
    }

    lua_setfield(L, env_index, name);
    lua_pop(L, 1);
  } else {
    lua_pop(L, 1);
  }
}

int installSandbox(lua_State *L) {
  luaL_openlibs(L);

  removeGlobal(L, "collectgarbage");
  removeGlobal(L, "debug");
  removeGlobal(L, "dofile");
  removeGlobal(L, "io");
  removeGlobal(L, "load");
  removeGlobal(L, "loadfile");
  removeGlobal(L, "loadstring");
  removeGlobal(L, "os");
  removeGlobal(L, "package");
  removeGlobal(L, "require");

  lua_newtable(L);
  const int env_index = lua_gettop(L);

  copyGlobal(L, env_index, "assert");
  copyGlobal(L, env_index, "error");
  copyGlobal(L, env_index, "ipairs");
  copyGlobal(L, env_index, "next");
  copyGlobal(L, env_index, "pairs");
  copyGlobal(L, env_index, "pcall");
  copyGlobal(L, env_index, "select");
  copyGlobal(L, env_index, "tonumber");
  copyGlobal(L, env_index, "tostring");
  copyGlobal(L, env_index, "type");
  copyGlobal(L, env_index, "xpcall");

  copyLibrary(L, env_index, "math");
  copyLibrary(L, env_index, "string");
  copyLibrary(L, env_index, "table");

  lua_pushvalue(L, env_index);
  lua_setfield(L, env_index, "_G");
  return env_index;
}

bool bindChunkEnv(lua_State *L, int func_index, int env_index) {
  const int abs_func_index = lua_absindex(L, func_index);
  const int abs_env_index = lua_absindex(L, env_index);
  lua_pushvalue(L, abs_env_index);
  return lua_setupvalue(L, abs_func_index, 1) != NULL;
}

int parseLuaErrorLine(const std::string &message) {
  int line = 0;
  bool reading_number = false;
  for (size_t idx = 0; idx < message.size(); ++idx) {
    if (message[idx] == ':') {
      line = 0;
      reading_number = true;
      continue;
    }
    if (!reading_number) {
      continue;
    }
    if (isdigit(static_cast<unsigned char>(message[idx])) != 0) {
      line = line * 10 + (message[idx] - '0');
      continue;
    }
    if (line > 0 && message[idx] == ':') {
      return line;
    }
    if (line > 0) {
      return line;
    }
    reading_number = false;
  }
  return line;
}

void luaTimeoutHook(lua_State *L, lua_Debug *) {
  LuaExecContext *ctx =
      *static_cast<LuaExecContext **>(lua_getextraspace(L));
  if (ctx == NULL) {
    return;
  }

  if (nowMilliseconds() - ctx->start_ms > ctx->timeout_ms) {
    ctx->timed_out = true;
    luaL_error(L, "script execution timeout");
  }
}

void setLuaExecContext(lua_State *L, LuaExecContext *ctx) {
  *static_cast<LuaExecContext **>(lua_getextraspace(L)) = ctx;
}

int createLuaState(lua_State **ppState, LuaMemoryContext *pMemoryContext) {
  if (ppState == NULL || pMemoryContext == NULL) {
    return IMVS_EC_NULL_PTR;
  }

  pMemoryContext->used = 0;
  pMemoryContext->limit = kMaxLuaMemoryBytes;
  pMemoryContext->denied = false;

  lua_State *L = lua_newstate(luaLimitedAllocator, pMemoryContext);
  if (L == NULL) {
    return IMVS_EC_OUTOFMEMORY;
  }

  *ppState = L;
  return IMVS_EC_OK;
}

ScriptIssue firstIssue(const ScriptValidationResult &result) {
  if (!result.issues.empty()) {
    return result.issues.front();
  }

  ScriptIssue issue;
  issue.message = "unknown script validation error";
  return issue;
}

char typeToFormat(ScriptValueType type) {
  switch (type) {
  case mvsc::script::kScriptValueBool:
    return 'b';
  case mvsc::script::kScriptValueInt:
    return 'd';
  case mvsc::script::kScriptValueFloat:
    return 'f';
  case mvsc::script::kScriptValueString:
    return 's';
  default:
    return '?';
  }
}

} // namespace

CScriptModule::CScriptModule()
    : m_nRunMode(IMG_RUN_MODE_RUN), m_bDebugEnable(false),
      m_bCompileValid(false), m_nCompileErrCode(IMVS_EC_NOT_READY),
      m_nCompileErrLine(0) {
  memset(m_szLuaScriptPath, 0, sizeof(m_szLuaScriptPath));
  memset(&m_stScriptInfo, 0, sizeof(m_stScriptInfo));
  memset(m_szInputResult, 0, sizeof(m_szInputResult));
  memset(m_szOutputResult, 0, sizeof(m_szOutputResult));
  memset(&m_stDynamicSubScribeParamList, 0, sizeof(m_stDynamicSubScribeParamList));
  memset(&m_stDynamicPublishParamList, 0, sizeof(m_stDynamicPublishParamList));
  memset(m_szCompileErrMsg, 0, sizeof(m_szCompileErrMsg));
  pthread_mutex_init(&m_execMutex, NULL);
}

CScriptModule::~CScriptModule() { DeInit(); }

int CScriptModule::Process(IN void *hInput, IN void *hOutput) {
  int nErrCode = IMVS_EC_OK;

  pthread_mutex_lock(&m_execMutex);
  SyncRunMode(hInput);
  memset(m_szInputResult, 0, sizeof(m_szInputResult));
  memset(m_szOutputResult, 0, sizeof(m_szOutputResult));
  nErrCode = ExecuteCompiledScript(hInput, hOutput);
  pthread_mutex_unlock(&m_execMutex);

  return nErrCode;
}

int CScriptModule::SetPwd(IN const char *szPwd) {
  snprintf(m_moduleName, sizeof(m_moduleName), "%s", ALGO_NAME);
  m_pwd = szPwd;
  return CAlgo::Init();
}

int CScriptModule::SetParam(IN const char *szParamName, IN const char *pData,
                            IN int nDataLen) {
  if (szParamName == NULL || strlen(szParamName) == 0) {
    return IMVS_EC_PARAM;
  }
  if (pData == NULL || nDataLen < 0) {
    return IMVS_EC_PARAM;
  }

  int nErrCode = m_paramManage->CheckParam(szParamName, pData);
  if (nErrCode != IMVS_EC_OK) {
    return nErrCode;
  }

  if (strncmp(SCRIPT_SUB_ENTER, szParamName, strlen(SCRIPT_SUB_ENTER)) == 0 ||
      strncmp(SCRIPT_PUB_ENTER, szParamName, strlen(SCRIPT_PUB_ENTER)) == 0) {
    nErrCode = GetScriptInfo();
    if (nErrCode != IMVS_EC_OK) {
      return nErrCode;
    }
    nErrCode = CompileScript();
    if (nErrCode != IMVS_EC_OK) {
      return nErrCode;
    }
  } else if (strncmp(SCRIPT_CHECK, szParamName, strlen(SCRIPT_CHECK)) == 0) {
    nErrCode = CompileScript();
    if (nErrCode != IMVS_EC_OK) {
      return nErrCode;
    }
  } else if (strncmp(SCRIPT_INPUT, szParamName, strlen(SCRIPT_INPUT)) == 0) {
    char *pEndChar = NULL;
    const int index =
        static_cast<int>(strtol(szParamName + strlen(SCRIPT_INPUT), &pEndChar, 10));
    if (pEndChar == NULL || *pEndChar != '\0') {
      return IMVS_EC_PARAM;
    }
    nErrCode = UpdateDynamicSubInfo(pData, index);
    if (nErrCode != IMVS_EC_OK) {
      return IMVS_EC_ALGO_UPDATE_DYN_SUB_PARAM_ERR;
    }
  }

  return m_paramManage->SetParam(szParamName, pData);
}

int CScriptModule::GetParam(IN const char *szParamName, OUT char *pBuff,
                            IN int nBuffSize, OUT int *pDataLen) {
  if (szParamName == NULL || pBuff == NULL || pDataLen == NULL || nBuffSize <= 0) {
    return IMVS_EC_PARAM;
  }

  const std::string value = m_paramManage->GetParam(szParamName);
  snprintf(pBuff, nBuffSize, "%s", value.c_str());
  *pDataLen = static_cast<int>(strlen(pBuff));
  return IMVS_EC_OK;
}

int CScriptModule::GetDynamicPublishParamNum() {
  return m_stDynamicPublishParamList.nNum;
}

int CScriptModule::GetDynamicSinglePublishParam(
    IN int nParamIdx, OUT DYNAMIC_PUBLISH_PARAM *pstPublishParam) {
  if (pstPublishParam == NULL) {
    return IMVS_EC_PARAM;
  }
  if (nParamIdx < 0 || nParamIdx >= m_stDynamicPublishParamList.nNum) {
    return IMVS_EC_PARAM_NOT_VALID;
  }

  memcpy(pstPublishParam, &m_stDynamicPublishParamList.pubParamList[nParamIdx],
         sizeof(DYNAMIC_PUBLISH_PARAM));
  return IMVS_EC_OK;
}

int CScriptModule::GetDynamicSubScribeParamNum() {
  return m_stDynamicSubScribeParamList.nNum;
}

int CScriptModule::GetDynamicSingleSubScribeParam(
    IN int nParamIdx, OUT DYNAMIC_SUBSCRIBE_PARAM *pstSubScribeParam) {
  if (pstSubScribeParam == NULL) {
    return IMVS_EC_PARAM;
  }
  if (nParamIdx < 0 || nParamIdx >= m_stDynamicSubScribeParamList.nNum) {
    return IMVS_EC_PARAM_NOT_VALID;
  }

  memcpy(pstSubScribeParam,
         &m_stDynamicSubScribeParamList.subScribeParamList[nParamIdx],
         sizeof(DYNAMIC_SUBSCRIBE_PARAM));
  return IMVS_EC_OK;
}

int CScriptModule::InitAlgoPrivate() {
  int nErrCode = m_resultManage.SetModuleType(algo::ModuleType::MODULE_TYPE_EXIST);
  if (nErrCode != IMVS_EC_OK) {
    return nErrCode;
  }

  nErrCode = InitPrivJson();
  if (nErrCode != IMVS_EC_OK) {
    return nErrCode;
  }

  nErrCode = GetScriptInfo();
  if (nErrCode != IMVS_EC_OK) {
    return nErrCode;
  }

  ParseLuaScript();
  return IMVS_EC_OK;
}

void CScriptModule::DeInit() {
  pthread_mutex_destroy(&m_execMutex);
}

int CScriptModule::InitPrivJson() {
  int nRet = 0;
  int nErrCode = IMVS_EC_OK;
  char szAlgoPrivJsonPath[MAX_PATH_LEN] = {0};

  snprintf(szAlgoPrivJsonPath, MAX_PATH_LEN, "%s/%s", m_pwd.c_str(),
           ALGO_PRIV_JSON_NAME);
  if (!osal_is_file_exist(szAlgoPrivJsonPath)) {
    nRet = osal_copy(ALGO_PRIV_JSON_PATH, szAlgoPrivJsonPath);
    if (nRet != 0) {
      nErrCode = IMVS_EC_ALGO_PM_JSON_FILE_ERR;
    }
  }

  snprintf(m_szLuaScriptPath, MAX_PATH_LEN, "%s/%s", m_pwd.c_str(),
           ALGO_USER_LUA_SCRIPT_NAME);
  if (!osal_is_file_exist(m_szLuaScriptPath)) {
    nRet = osal_copy(ALGO_USER_LUA_SCRIPT_PATH, m_szLuaScriptPath);
    if (nRet != 0) {
      nErrCode = IMVS_EC_ALGO_PM_JSON_FILE_ERR;
    }
  }

  return nErrCode;
}

int CScriptModule::LoadScriptText(std::string &script_text) const {
  script_text.clear();
  const int file_size = osal_get_file_size(m_szLuaScriptPath);
  if (file_size < 0) {
    return IMVS_EC_FILE_NOT_FOUND;
  }

  char *buffer = static_cast<char *>(osal_malloc(file_size + 1, 4));
  if (buffer == NULL) {
    return IMVS_EC_OUTOFMEMORY;
  }

  memset(buffer, 0, file_size + 1);
  if (osal_load_file(m_szLuaScriptPath, 0, buffer, file_size) < 0) {
    osal_free(buffer);
    return IMVS_EC_FILE_OPEN;
  }

  script_text.assign(buffer, buffer + file_size);
  osal_free(buffer);
  return IMVS_EC_OK;
}

int CScriptModule::BuildVarDefs() {
  m_inputVarDefs.clear();
  m_outputVarDefs.clear();

  for (int idx = 0; idx < m_stScriptInfo.InCnt; ++idx) {
    mvsc::script::ScriptVarDef def;
    def.name = m_stScriptInfo.InParam[idx].szVarName;
    def.type = m_stScriptInfo.InParam[idx].eType;
    m_inputVarDefs.push_back(def);
  }

  for (int idx = 0; idx < m_stScriptInfo.OutCnt; ++idx) {
    mvsc::script::ScriptVarDef def;
    def.name = m_stScriptInfo.OutParam[idx].szVarName;
    def.type = m_stScriptInfo.OutParam[idx].eType;
    m_outputVarDefs.push_back(def);
  }

  if (m_outputVarDefs.empty()) {
    SetCompileFailure(IMVS_EC_MODULE_OUT_NOT_FOUND,
                      "script output variables are not configured", 0);
    return IMVS_EC_MODULE_OUT_NOT_FOUND;
  }

  return IMVS_EC_OK;
}

void CScriptModule::ResetCompileState() {
  m_bCompileValid = false;
  m_nCompileErrCode = IMVS_EC_NOT_READY;
  m_nCompileErrLine = 0;
  memset(m_szCompileErrMsg, 0, sizeof(m_szCompileErrMsg));
  m_compiledChunk.clear();
}

void CScriptModule::SetCompileFailure(int err_code, const std::string &message,
                                      int line) {
  m_bCompileValid = false;
  m_nCompileErrCode = err_code;
  m_nCompileErrLine = line;
  snprintf(m_szCompileErrMsg, sizeof(m_szCompileErrMsg), "%s", message.c_str());
  LOGE("compile script failed: err=0x%x line=%d msg=%s\n", err_code, line,
       m_szCompileErrMsg);
}

int CScriptModule::ParseLuaScript() { return CompileScript(); }

int CScriptModule::CompileScript() {
  ResetCompileState();

  int nErrCode = BuildVarDefs();
  if (nErrCode != IMVS_EC_OK) {
    return nErrCode;
  }

  std::string script_text;
  nErrCode = LoadScriptText(script_text);
  if (nErrCode != IMVS_EC_OK) {
    SetCompileFailure(nErrCode, "failed to load Lua script file", 0);
    return nErrCode;
  }

  const ScriptValidationResult validate_result =
      AnalyzeScript(script_text, m_inputVarDefs, m_outputVarDefs, kMaxScriptLength);
  if (!validate_result.ok) {
    const ScriptIssue issue = firstIssue(validate_result);
    SetCompileFailure(IMVS_EC_PARAM_NOT_VALID, issue.message, issue.line);
    return IMVS_EC_PARAM_NOT_VALID;
  }

  LuaMemoryContext memory_context;
  lua_State *L = NULL;
  nErrCode = createLuaState(&L, &memory_context);
  if (nErrCode != IMVS_EC_OK) {
    SetCompileFailure(nErrCode, "failed to create Lua state", 0);
    return nErrCode;
  }

  const int env_index = installSandbox(L);
  if (luaL_loadbuffer(L, script_text.c_str(), script_text.size(), kScriptChunkName) !=
      LUA_OK) {
    const std::string lua_error = lua_tostring(L, -1);
    const int line = parseLuaErrorLine(lua_error);
    lua_close(L);
    SetCompileFailure(IMVS_EC_FILE_FORMAT, lua_error, line);
    return IMVS_EC_FILE_FORMAT;
  }

  if (!bindChunkEnv(L, -1, env_index)) {
    lua_close(L);
    SetCompileFailure(IMVS_EC_NOT_SUPPORT, "failed to bind Lua sandbox", 0);
    return IMVS_EC_NOT_SUPPORT;
  }

  m_compiledChunk.clear();
  if (lua_dump(L, luaChunkWriter, &m_compiledChunk, 0) != 0) {
    lua_close(L);
    SetCompileFailure(IMVS_EC_SYSTEM_INNER_ERR, "failed to cache compiled chunk",
                      0);
    return IMVS_EC_SYSTEM_INNER_ERR;
  }

  if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
    const std::string lua_error = lua_tostring(L, -1);
    const int line = parseLuaErrorLine(lua_error);
    lua_close(L);
    SetCompileFailure(IMVS_EC_PARAM_NOT_VALID, lua_error, line);
    return IMVS_EC_PARAM_NOT_VALID;
  }

  lua_getfield(L, env_index, kScriptEntryName);
  if (!lua_isfunction(L, -1)) {
    lua_close(L);
    SetCompileFailure(IMVS_EC_PARAM_NOT_FOUND,
                      "script must define function run(inputs, outputs)", 0);
    return IMVS_EC_PARAM_NOT_FOUND;
  }

  lua_close(L);
  m_bCompileValid = true;
  m_nCompileErrCode = IMVS_EC_OK;
  snprintf(m_szCompileErrMsg, sizeof(m_szCompileErrMsg), "%s", "script compiled");
  return IMVS_EC_OK;
}

int CScriptModule::GetScriptInfo() {
  memset(&m_stScriptInfo, 0, sizeof(m_stScriptInfo));
  memset(&m_stDynamicSubScribeParamList, 0, sizeof(m_stDynamicSubScribeParamList));
  memset(&m_stDynamicPublishParamList, 0, sizeof(m_stDynamicPublishParamList));

  for (int idx = 0; idx < m_struModuleIoInfo.output_num; ++idx) {
    snprintf(m_stDynamicPublishParamList.pubParamList[m_stDynamicPublishParamList.nNum]
                 .szName,
             MODULE_NAME_LEN, "%s", m_struModuleIoInfo.output_info[idx].name);
    snprintf(m_stDynamicPublishParamList.pubParamList[m_stDynamicPublishParamList.nNum]
                 .szType,
             MODULE_NAME_LEN, "%s", m_struModuleIoInfo.output_info[idx].type);
    m_stDynamicPublishParamList.pubParamList[m_stDynamicPublishParamList.nNum].nNum =
        m_struModuleIoInfo.output_info[idx].num;
    m_stDynamicPublishParamList.pubParamList[m_stDynamicPublishParamList.nNum].nShow =
        m_struModuleIoInfo.output_info[idx].show;
    m_stDynamicPublishParamList.pubParamList[m_stDynamicPublishParamList.nNum].nForce =
        m_struModuleIoInfo.output_info[idx].force;
    ++m_stDynamicPublishParamList.nNum;
  }

  for (int idx = 0; idx < MAX_SCRIPT_NUM; ++idx) {
    char param_name[64] = {0};
    snprintf(param_name, sizeof(param_name), "%s%d", SCRIPT_INPUT, idx);
    m_paramManage->SetParam(param_name, "");
  }

  char path[MAX_PATH_LEN] = {0};
  snprintf(path, sizeof(path), "%s/%s", m_pwd.c_str(), ALGO_PRIV_JSON_NAME);
  const int file_size = osal_get_file_size(path);
  if (file_size < 0) {
    return IMVS_EC_FILE_NOT_FOUND;
  }

  char *json_buffer = static_cast<char *>(osal_malloc(file_size + 1, 4));
  if (json_buffer == NULL) {
    return IMVS_EC_OUTOFMEMORY;
  }
  memset(json_buffer, 0, file_size + 1);
  if (osal_load_file(path, 0, json_buffer, file_size) < 0) {
    osal_free(json_buffer);
    return IMVS_EC_FILE_OPEN;
  }

  cJSON *root = cJSON_Parse(json_buffer);
  osal_free(json_buffer);
  if (root == NULL) {
    return IMVS_EC_ALGO_JSON_FILE_FORMAT;
  }

  cJSON *input_array = cJSON_GetObjectItem(root, "InputList");
  if (input_array != NULL && cJSON_IsArray(input_array)) {
    const int input_count = cJSON_GetArraySize(input_array);
    for (int idx = 0; idx < input_count && idx < MAX_SCRIPT_NUM; ++idx) {
      cJSON *item = cJSON_GetArrayItem(input_array, idx);
      cJSON *var_name = cJSON_GetObjectItem(item, "VariableName");
      cJSON *param_value = cJSON_GetObjectItem(item, "ParamValue");
      cJSON *data_type = cJSON_GetObjectItem(item, "DataType");
      if (!cJSON_IsString(var_name) || !cJSON_IsString(param_value) ||
          !cJSON_IsString(data_type)) {
        cJSON_Delete(root);
        return IMVS_EC_ALGO_JSON_FILE_FORMAT;
      }

      const ScriptValueType type =
          mvsc::script::ParseValueType(data_type->valuestring);
      if (type == mvsc::script::kScriptValueInvalid) {
        cJSON_Delete(root);
        return IMVS_EC_NOT_SUPPORT;
      }

      snprintf(m_stScriptInfo.InParam[idx].szVarName, MAX_NAME_LEN, "%s",
               var_name->valuestring);
      m_stScriptInfo.InParam[idx].eType = type;
      m_stScriptInfo.InParam[idx].byFormat = typeToFormat(type);

      if (strlen(param_value->valuestring) > 0) {
        int nErrCode = UpdateDynamicSubInfo(param_value->valuestring, idx);
        if (nErrCode != IMVS_EC_OK) {
          cJSON_Delete(root);
          return nErrCode;
        }
      }

      char param_name[64] = {0};
      snprintf(param_name, sizeof(param_name), "%s%d", SCRIPT_INPUT, idx);
      m_paramManage->SetParam(param_name, param_value->valuestring);
      ++m_stScriptInfo.InCnt;
    }
  }

  cJSON *output_array = cJSON_GetObjectItem(root, "OutputList");
  if (output_array != NULL && cJSON_IsArray(output_array)) {
    const int output_count = cJSON_GetArraySize(output_array);
    for (int idx = 0; idx < output_count && idx < MAX_SCRIPT_NUM; ++idx) {
      cJSON *item = cJSON_GetArrayItem(output_array, idx);
      cJSON *var_name = cJSON_GetObjectItem(item, "VariableName");
      cJSON *data_type = cJSON_GetObjectItem(item, "DataType");
      if (!cJSON_IsString(var_name) || !cJSON_IsString(data_type)) {
        cJSON_Delete(root);
        return IMVS_EC_ALGO_JSON_FILE_FORMAT;
      }

      const ScriptValueType type =
          mvsc::script::ParseValueType(data_type->valuestring);
      if (type == mvsc::script::kScriptValueInvalid) {
        cJSON_Delete(root);
        return IMVS_EC_NOT_SUPPORT;
      }

      snprintf(m_stScriptInfo.OutParam[idx].szVarName, MAX_NAME_LEN, "%s",
               var_name->valuestring);
      m_stScriptInfo.OutParam[idx].eType = type;
      m_stScriptInfo.OutParam[idx].byFormat = typeToFormat(type);

      snprintf(m_stDynamicPublishParamList.pubParamList[m_stDynamicPublishParamList.nNum]
                   .szName,
               MODULE_NAME_LEN, "%s", var_name->valuestring);
      snprintf(m_stDynamicPublishParamList.pubParamList[m_stDynamicPublishParamList.nNum]
                   .szType,
               MODULE_NAME_LEN, "%s", ToTypeName(type));
      m_stDynamicPublishParamList.pubParamList[m_stDynamicPublishParamList.nNum].nNum =
          1;
      m_stDynamicPublishParamList.pubParamList[m_stDynamicPublishParamList.nNum].nShow =
          1;
      m_stDynamicPublishParamList.pubParamList[m_stDynamicPublishParamList.nNum]
          .nForce = 0;
      ++m_stDynamicPublishParamList.nNum;
      ++m_stScriptInfo.OutCnt;
    }
  }

  cJSON_Delete(root);
  return IMVS_EC_OK;
}

int CScriptModule::UpdateDynamicSubInfo(const char *pFormatStr, int nCnt) {
  if (pFormatStr == NULL || nCnt < 0 || nCnt >= MAX_SCRIPT_NUM) {
    return IMVS_EC_PARAM;
  }

  if (strlen(pFormatStr) == 0) {
    if (nCnt < m_stDynamicSubScribeParamList.nNum) {
      for (int idx = nCnt; idx < m_stDynamicSubScribeParamList.nNum - 1; ++idx) {
        memcpy(&m_stDynamicSubScribeParamList.subScribeParamList[idx],
               &m_stDynamicSubScribeParamList.subScribeParamList[idx + 1],
               sizeof(DYNAMIC_SUBSCRIBE_PARAM));
        snprintf(
            m_stDynamicSubScribeParamList.subScribeParamList[idx].subParam.bySubName,
            MODULE_NAME_LEN, "input%d", idx);
      }
      memset(
          &m_stDynamicSubScribeParamList
               .subScribeParamList[m_stDynamicSubScribeParamList.nNum - 1],
          0, sizeof(DYNAMIC_SUBSCRIBE_PARAM));
      --m_stDynamicSubScribeParamList.nNum;
    }
    return IMVS_EC_OK;
  }

  char local_copy[64] = {0};
  snprintf(local_copy, sizeof(local_copy), "%s", pFormatStr);
  char *pEnd = local_copy + strlen(local_copy) - 1;
  const int analyse_ret = Analyse(&m_stScriptInfo.InParam[nCnt], local_copy, pEnd);

  DYNAMIC_SUBSCRIBE_PARAM &sub =
      m_stDynamicSubScribeParamList.subScribeParamList[nCnt];
  memset(&sub, 0, sizeof(sub));
  sub.subParam.nDstModuleId = m_nModuleId;
  snprintf(sub.subParam.byDstModuleName, MODULE_NAME_LEN, "%s", ALGO_NAME);
  snprintf(sub.subParam.bySubName, MODULE_NAME_LEN, "input%d", nCnt);

  if (analyse_ret == -1) {
    sub.pubParam.nSrcModuleId = 0;
    snprintf(sub.pubParam.bySrcModuleName, MODULE_NAME_LEN, "%s", "image");
    snprintf(sub.pubParam.byPubName, MODULE_NAME_LEN, "%s",
             (atoi(pFormatStr) != 0) ? "SINGLE_const_int_1" : "SINGLE_const_int_0");
  } else if (analyse_ret == 0) {
    sub.pubParam.nSrcModuleId = m_stScriptInfo.InParam[nCnt].nAlgoId;
    snprintf(sub.pubParam.bySrcModuleName, MODULE_NAME_LEN, "%s",
             m_stScriptInfo.InParam[nCnt].szAlgoName);
    snprintf(sub.pubParam.byPubName, MODULE_NAME_LEN, "%s",
             m_stScriptInfo.InParam[nCnt].szParamName);
  } else {
    return IMVS_EC_DATA_ERROR;
  }

  if (m_stDynamicSubScribeParamList.nNum <= nCnt) {
    m_stDynamicSubScribeParamList.nNum = nCnt + 1;
  }
  return IMVS_EC_OK;
}

int CScriptModule::SendOutputParam(IN void *hOutput, IN const int nStatus) {
  if (hOutput == NULL) {
    return IMVS_EC_NULL_PTR;
  }

  ScFramePtr pOutFrame = VM_M_Get_Frame_ByHOutput(hOutput);
  if (!pOutFrame) {
    return IMVS_EC_NULL_PTR;
  }

  pOutFrame->setVal(O_SINGLE_STATUS, (0 == nStatus) ? 1 : 0);
  if (IMG_RUN_MODE_RUN == m_nRunMode) {
    m_resultManage.AddResultInfo((0 == nStatus) ? 1 : 0, (0 == nStatus) ? 1 : 0);
  }

  pOutFrame->setVal(O_SINGLE_PARAM_STATUS,
                    (0 == nStatus) ? MODULE_PARAM_STATUS_OK
                                   : MODULE_PARAM_STATUS_NG);
  pOutFrame->setVal(O_SINGLE_PARAM_STATUS_STR,
                    (0 == nStatus) ? MODULE_PARAM_STATUS_OK_STR
                                   : MODULE_PARAM_STATUS_NG_STR);
  pOutFrame->setVal(O_SINGLE_SHOW_TEXT_X, 0.0f);
  pOutFrame->setVal(O_SINGLE_SHOW_TEXT_Y, 0.0f);
  pOutFrame->setVal(O_SINGLE_SHOW_TEXT_ANG, 0.0f);

  char szRstStrCn[PARAM_VALUE_LEN] = {0};
  char szRstStrEn[PARAM_VALUE_LEN] = {0};
  snprintf(szRstStrCn, sizeof(szRstStrCn), "%s", "脚本输出结果");
  snprintf(szRstStrEn, sizeof(szRstStrEn), "%s", "Script output result");
  m_resultManage.SendResultInfoExist((0 == nStatus) ? 1 : 0, 0, szRstStrCn,
                                     szRstStrEn, pOutFrame);

  return IMVS_EC_OK;
}

int CScriptModule::SendScriptFailure(void *hOutput, int err_code,
                                     const std::string &message) {
  int nErrCode = SendOutputParam(hOutput, err_code);
  if (nErrCode != IMVS_EC_OK) {
    return nErrCode;
  }

  ScFramePtr pOutFrame = VM_M_Get_Frame_ByHOutput(hOutput);
  if (!pOutFrame) {
    return IMVS_EC_NULL_PTR;
  }

  pOutFrame->setVal(O_SINGLE_SHOW_TEXT_CN, message);
  pOutFrame->setVal(O_SINGLE_SHOW_TEXT_EN, message);
  pOutFrame->setVal(O_RST_STRING_CN, message);
  pOutFrame->setVal(O_RST_STRING_EN, message);
  pOutFrame->setVal(O_RST_STRING_X, 0.0f);
  pOutFrame->setVal(O_RST_STRING_Y, 0.0f);
  pOutFrame->setVal(O_RST_STRING_ANG, 0.0f);
  pOutFrame->setVal("SINGLE_input_result", m_szInputResult);
  pOutFrame->setVal("SINGLE_output_result", message);
  return err_code;
}

void CScriptModule::AppendDebugValue(const char *name, const std::string &value,
                                     char *buffer, size_t buffer_size) const {
  const int offset = static_cast<int>(strlen(buffer));
  if (offset >= static_cast<int>(buffer_size)) {
    return;
  }

  snprintf(buffer + offset, buffer_size - offset, "%s$%s$$", name, value.c_str());
}

void CScriptModule::SyncRunMode(void *hInput) {
  ScFramePtr pImgFrame = VM_M_Get_Frame_ByID(hInput, 0);
  if (!pImgFrame) {
    return;
  }

  int run_mode = IMG_RUN_MODE_RUN;
  if (pImgFrame->getIntVal(I_IMG_MODE, run_mode) == IMVS_EC_OK) {
    m_nRunMode = run_mode;
  }
}

int CScriptModule::ReadInputValue(void *hInput, int index, ScriptHostValue *pValue) {
  if (hInput == NULL || pValue == NULL || index < 0 || index >= m_stScriptInfo.InCnt) {
    return IMVS_EC_PARAM;
  }

  const char *src_name =
      m_stDynamicSubScribeParamList.subScribeParamList[index].subParam.bySubName;
  if (src_name == NULL || strlen(src_name) == 0) {
    return IMVS_EC_MODULE_INPUT_CFG_UNDONE;
  }

  int count = 0;
  switch (m_stScriptInfo.InParam[index].eType) {
  case mvsc::script::kScriptValueBool:
  case mvsc::script::kScriptValueInt: {
    int value = 0;
    const int nErrCode = VM_M_GetInt(hInput, src_name, 0, &value, &count);
    if (nErrCode != IMVS_EC_OK || count < 1) {
      return IMVS_EC_ALGO_NO_DATA;
    }

    pValue->type = m_stScriptInfo.InParam[index].eType;
    pValue->bool_value = (value != 0);
    pValue->int_value = value;
    AppendDebugValue(m_stScriptInfo.InParam[index].szVarName, std::to_string(value),
                     m_szInputResult, sizeof(m_szInputResult));
    return IMVS_EC_OK;
  }
  case mvsc::script::kScriptValueFloat: {
    float value = 0.0f;
    const int nErrCode = VM_M_GetFloat(hInput, src_name, 0, &value, &count);
    if (nErrCode != IMVS_EC_OK || count < 1) {
      return IMVS_EC_ALGO_NO_DATA;
    }

    pValue->type = mvsc::script::kScriptValueFloat;
    pValue->float_value = value;
    AppendDebugValue(m_stScriptInfo.InParam[index].szVarName, std::to_string(value),
                     m_szInputResult, sizeof(m_szInputResult));
    return IMVS_EC_OK;
  }
  case mvsc::script::kScriptValueString: {
    char value[MAX_GET_STRING_VALUE] = {0};
    int str_len = 0;
    const int nErrCode =
        VM_M_GetString(hInput, src_name, 0, value, sizeof(value), &str_len, &count);
    if (nErrCode != IMVS_EC_OK || count < 1) {
      return IMVS_EC_ALGO_NO_DATA;
    }

    pValue->type = mvsc::script::kScriptValueString;
    pValue->string_value = value;
    AppendDebugValue(m_stScriptInfo.InParam[index].szVarName, pValue->string_value,
                     m_szInputResult, sizeof(m_szInputResult));
    return IMVS_EC_OK;
  }
  default:
    return IMVS_EC_NOT_SUPPORT;
  }
}

int CScriptModule::ExecuteCompiledScript(void *hInput, void *hOutput) {
  if (!m_bCompileValid) {
    std::string error_message = m_szCompileErrMsg;
    if (m_nCompileErrLine > 0) {
      error_message += " (line " + std::to_string(m_nCompileErrLine) + ")";
    }
    return SendScriptFailure(hOutput, m_nCompileErrCode, error_message);
  }

  LuaMemoryContext memory_context;
  lua_State *L = NULL;
  int nErrCode = createLuaState(&L, &memory_context);
  if (nErrCode != IMVS_EC_OK) {
    return SendScriptFailure(hOutput, nErrCode, "failed to create Lua state");
  }
  if (m_compiledChunk.empty()) {
    lua_close(L);
    return SendScriptFailure(hOutput, IMVS_EC_NOT_READY,
                             "compiled script cache is empty");
  }

  const int env_index = installSandbox(L);
  if (luaL_loadbuffer(L, &m_compiledChunk[0], m_compiledChunk.size(), kScriptChunkName) !=
      LUA_OK) {
    const std::string lua_error = lua_tostring(L, -1);
    lua_close(L);
    return SendScriptFailure(hOutput, IMVS_EC_FILE_FORMAT, lua_error);
  }
  if (!bindChunkEnv(L, -1, env_index)) {
    lua_close(L);
    return SendScriptFailure(hOutput, IMVS_EC_NOT_SUPPORT,
                             "failed to bind runtime sandbox");
  }
  if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
    const std::string lua_error = lua_tostring(L, -1);
    lua_close(L);
    return SendScriptFailure(hOutput, IMVS_EC_PARAM_NOT_VALID, lua_error);
  }

  lua_getfield(L, env_index, kScriptEntryName);
  if (!lua_isfunction(L, -1)) {
    lua_close(L);
    return SendScriptFailure(hOutput, IMVS_EC_PARAM_NOT_FOUND,
                             "script entry run(inputs, outputs) is missing");
  }

  lua_newtable(L);
  for (int idx = 0; idx < m_stScriptInfo.InCnt; ++idx) {
    ScriptHostValue input_value;
    nErrCode = ReadInputValue(hInput, idx, &input_value);
    if (nErrCode != IMVS_EC_OK) {
      lua_close(L);
      return SendScriptFailure(hOutput, nErrCode,
                               "required script input is unavailable");
    }

    switch (input_value.type) {
    case mvsc::script::kScriptValueBool:
      lua_pushboolean(L, input_value.bool_value ? 1 : 0);
      break;
    case mvsc::script::kScriptValueInt:
      lua_pushinteger(L, input_value.int_value);
      break;
    case mvsc::script::kScriptValueFloat:
      lua_pushnumber(L, input_value.float_value);
      break;
    case mvsc::script::kScriptValueString:
      lua_pushstring(L, input_value.string_value.c_str());
      break;
    default:
      lua_close(L);
      return SendScriptFailure(hOutput, IMVS_EC_NOT_SUPPORT,
                               "unsupported input type");
    }
    lua_setfield(L, -2, m_stScriptInfo.InParam[idx].szVarName);
  }

  lua_newtable(L);
  lua_pushvalue(L, -1);
  const int outputs_ref = luaL_ref(L, LUA_REGISTRYINDEX);

  LuaExecContext exec_context;
  exec_context.start_ms = nowMilliseconds();
  exec_context.timeout_ms = kLuaTimeoutMs;
  exec_context.timed_out = false;
  setLuaExecContext(L, &exec_context);
  lua_sethook(L, luaTimeoutHook, LUA_MASKCOUNT, kLuaHookInstructionStep);

  if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
    const std::string lua_error = lua_tostring(L, -1);
    luaL_unref(L, LUA_REGISTRYINDEX, outputs_ref);
    lua_close(L);
    if (exec_context.timed_out) {
      return SendScriptFailure(hOutput, IMVS_EC_WAIT_TIMEOUT, "script execution timeout");
    }
    return SendScriptFailure(hOutput, IMVS_EC_ALGO_PROCESS_ERR, lua_error);
  }

  lua_sethook(L, NULL, 0, 0);
  lua_rawgeti(L, LUA_REGISTRYINDEX, outputs_ref);
  const int outputs_index = lua_gettop(L);

  ScFramePtr pOutFrame = VM_M_Get_Frame_ByHOutput(hOutput);
  if (!pOutFrame) {
    luaL_unref(L, LUA_REGISTRYINDEX, outputs_ref);
    lua_close(L);
    return IMVS_EC_NULL_PTR;
  }

  std::set<std::string> allowed_outputs;
  for (int idx = 0; idx < m_stScriptInfo.OutCnt; ++idx) {
    allowed_outputs.insert(m_stScriptInfo.OutParam[idx].szVarName);
  }

  lua_pushnil(L);
  while (lua_next(L, outputs_index) != 0) {
    if (!lua_isstring(L, -2)) {
      luaL_unref(L, LUA_REGISTRYINDEX, outputs_ref);
      lua_close(L);
      return SendScriptFailure(hOutput, IMVS_EC_PARAM_NOT_VALID,
                               "script output key must be a string");
    }

    const std::string output_name = lua_tostring(L, -2);
    if (allowed_outputs.find(output_name) == allowed_outputs.end()) {
      luaL_unref(L, LUA_REGISTRYINDEX, outputs_ref);
      lua_close(L);
      return SendScriptFailure(hOutput, IMVS_EC_PARAM_NOT_VALID,
                               "script wrote an undeclared output: " + output_name);
    }
    lua_pop(L, 1);
  }

  for (int idx = 0; idx < m_stScriptInfo.OutCnt; ++idx) {
    const SCRIPT_OUT_PARAM &output = m_stScriptInfo.OutParam[idx];
    lua_getfield(L, outputs_index, output.szVarName);
    if (lua_isnil(L, -1)) {
      luaL_unref(L, LUA_REGISTRYINDEX, outputs_ref);
      lua_close(L);
      return SendScriptFailure(hOutput, IMVS_EC_ALGO_NO_RESULT,
                               std::string("script did not write output ") +
                                   output.szVarName);
    }

    switch (output.eType) {
    case mvsc::script::kScriptValueBool:
      if (!lua_isboolean(L, -1)) {
        luaL_unref(L, LUA_REGISTRYINDEX, outputs_ref);
        lua_close(L);
        return SendScriptFailure(hOutput, IMVS_EC_PARAM_NOT_VALID,
                                 std::string("output type mismatch: ") +
                                     output.szVarName + " expects bool");
      }
      pOutFrame->setVal(output.szVarName, lua_toboolean(L, -1) ? 1 : 0);
      AppendDebugValue(output.szVarName,
                       lua_toboolean(L, -1) ? "true" : "false", m_szOutputResult,
                       sizeof(m_szOutputResult));
      break;
    case mvsc::script::kScriptValueInt:
      if (!lua_isinteger(L, -1)) {
        luaL_unref(L, LUA_REGISTRYINDEX, outputs_ref);
        lua_close(L);
        return SendScriptFailure(hOutput, IMVS_EC_PARAM_NOT_VALID,
                                 std::string("output type mismatch: ") +
                                     output.szVarName + " expects int");
      }
      pOutFrame->setVal(output.szVarName,
                        static_cast<int>(lua_tointeger(L, -1)));
      AppendDebugValue(output.szVarName,
                       std::to_string(static_cast<int>(lua_tointeger(L, -1))),
                       m_szOutputResult, sizeof(m_szOutputResult));
      break;
    case mvsc::script::kScriptValueFloat:
      if (!lua_isnumber(L, -1)) {
        luaL_unref(L, LUA_REGISTRYINDEX, outputs_ref);
        lua_close(L);
        return SendScriptFailure(hOutput, IMVS_EC_PARAM_NOT_VALID,
                                 std::string("output type mismatch: ") +
                                     output.szVarName + " expects float");
      }
      pOutFrame->setVal(output.szVarName,
                        static_cast<float>(lua_tonumber(L, -1)));
      AppendDebugValue(output.szVarName,
                       std::to_string(static_cast<float>(lua_tonumber(L, -1))),
                       m_szOutputResult, sizeof(m_szOutputResult));
      break;
    case mvsc::script::kScriptValueString:
      if (!lua_isstring(L, -1)) {
        luaL_unref(L, LUA_REGISTRYINDEX, outputs_ref);
        lua_close(L);
        return SendScriptFailure(hOutput, IMVS_EC_PARAM_NOT_VALID,
                                 std::string("output type mismatch: ") +
                                     output.szVarName + " expects string");
      }
      pOutFrame->setVal(output.szVarName, std::string(lua_tostring(L, -1)));
      AppendDebugValue(output.szVarName, lua_tostring(L, -1), m_szOutputResult,
                       sizeof(m_szOutputResult));
      break;
    default:
      luaL_unref(L, LUA_REGISTRYINDEX, outputs_ref);
      lua_close(L);
      return SendScriptFailure(hOutput, IMVS_EC_NOT_SUPPORT,
                               "unsupported output type");
    }
    lua_pop(L, 1);
  }

  luaL_unref(L, LUA_REGISTRYINDEX, outputs_ref);
  lua_close(L);

  nErrCode = SendOutputParam(hOutput, IMVS_EC_OK);
  if (nErrCode != IMVS_EC_OK) {
    return nErrCode;
  }

  pOutFrame->setVal("SINGLE_input_result", m_szInputResult);
  pOutFrame->setVal("SINGLE_output_result", m_szOutputResult);
  pOutFrame->setVal(O_SINGLE_SHOW_TEXT_CN, m_szOutputResult);
  pOutFrame->setVal(O_SINGLE_SHOW_TEXT_EN, m_szOutputResult);
  pOutFrame->setVal(O_RST_STRING_CN, m_szOutputResult);
  pOutFrame->setVal(O_RST_STRING_EN, m_szOutputResult);
  pOutFrame->setVal(O_RST_STRING_X, 0.0f);
  pOutFrame->setVal(O_RST_STRING_Y, 0.0f);
  pOutFrame->setVal(O_RST_STRING_ANG, 0.0f);
  return IMVS_EC_OK;
}

int CScriptModule::Analyse(SCRIPT_IN_PARAM *pList, char *pStart, char *pEnd) {
  char *pArgv[3];
  char *pEndChar = NULL;

  if (pList == NULL || pStart == NULL || pEnd == NULL) {
    return IMVS_EC_NULL_PTR;
  }

  if (isdigit(static_cast<unsigned char>(pStart[0])) == 0) {
    return -2;
  }
  if (pStart == pEnd) {
    return -1;
  }

  strtol(pStart, &pEndChar, 10);
  if (pEndChar != NULL && *pEndChar == '\0') {
    return -1;
  }

  pArgv[0] = pStart;
  ++pStart;
  while (*pStart != ' ' && pStart != pEnd) {
    ++pStart;
  }
  if (pStart == pEnd) {
    return -3;
  }
  *pStart = '\0';

  ++pStart;
  pArgv[1] = pStart;
  while (*pStart != '.' && pStart != pEnd) {
    ++pStart;
  }
  if (pStart == pEnd) {
    return -4;
  }
  *pStart = '\0';

  ++pStart;
  pArgv[2] = pStart;

  pList->nAlgoId = static_cast<int>(strtol(pArgv[0], &pEndChar, 10));
  if (pEndChar != NULL && *pEndChar != '\0') {
    return IMVS_EC_PARAM;
  }

  snprintf(pList->szAlgoName, MAX_NAME_LEN, "%s", pArgv[1]);
  snprintf(pList->szParamName, MAX_NAME_LEN, "%s", pArgv[2]);
  return IMVS_EC_OK;
}

CAbstractUserModule *CreateModule(void *hModule) {
  assert(hModule != NULL);
  return new CScriptModule;
}

void DestroyModule(void *hModule, CAbstractUserModule *pUserModule) {
  assert(hModule != NULL);
  assert(pUserModule != NULL);
  delete pUserModule;
}
