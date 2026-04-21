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
#include <sstream>
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

const char *kScriptChunkName = "@script_operator";
const char *kScriptEntryName = "run";
const char *kScriptInitName = "init";
const char *kScriptCleanupName = "cleanup";
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

struct ScriptLuaRuntime {
  LuaMemoryContext memory_context;
  lua_State *state;
  int env_ref;
  void *active_input;
  void *active_output;

  ScriptLuaRuntime()
      : state(NULL), env_ref(LUA_NOREF), active_input(NULL), active_output(NULL) {}
};

namespace {

using mvsc::script::AnalyzeScript;
using mvsc::script::ScriptAnalysisResult;
using mvsc::script::ScriptIssue;
using mvsc::script::ScriptIssueCode;
using mvsc::script::ScriptOutputDef;
using mvsc::script::ScriptSubscriptionDef;
using mvsc::script::ScriptValueType;
using mvsc::script::ToTypeName;

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

ScriptIssue firstIssue(const ScriptAnalysisResult &result) {
  if (!result.issues.empty()) {
    return result.issues.front();
  }

  ScriptIssue issue;
  issue.message = "unknown script analysis error";
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
  case mvsc::script::kScriptValueBoolArray:
    return 'B';
  case mvsc::script::kScriptValueIntArray:
    return 'D';
  case mvsc::script::kScriptValueFloatArray:
    return 'F';
  case mvsc::script::kScriptValueStringArray:
    return 'S';
  default:
    return '?';
  }
}

CScriptModule *scriptModuleFromLua(lua_State *L) {
  return static_cast<CScriptModule *>(lua_touserdata(L, lua_upvalueindex(1)));
}

ScriptValueType scriptTypeFromLua(lua_State *L) {
  return static_cast<ScriptValueType>(
      static_cast<int>(lua_tointeger(L, lua_upvalueindex(2))));
}

int luaGetValueBridge(lua_State *L) {
  CScriptModule *module = scriptModuleFromLua(L);
  const ScriptValueType type = scriptTypeFromLua(L);
  if (module == NULL) {
    return luaL_error(L, "script module context is unavailable");
  }

  const int module_no = static_cast<int>(luaL_checkinteger(L, 1));
  const char *param_name = luaL_checkstring(L, 2);
  return module->LuaGetValue(L, type, module_no, param_name);
}

int luaSetValueBridge(lua_State *L) {
  CScriptModule *module = scriptModuleFromLua(L);
  const ScriptValueType type = scriptTypeFromLua(L);
  if (module == NULL) {
    return luaL_error(L, "script module context is unavailable");
  }

  const char *output_name = luaL_checkstring(L, 1);
  return module->LuaSetValue(L, type, output_name, 2);
}

} // namespace

CScriptModule::CScriptModule()
    : m_nRunMode(IMG_RUN_MODE_RUN), m_bDebugEnable(false),
      m_bCompileValid(false), m_nCompileErrCode(IMVS_EC_NOT_READY),
      m_nCompileErrLine(0), m_pRuntime(NULL) {
  memset(m_szLuaScriptPath, 0, sizeof(m_szLuaScriptPath));
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
  DestroyLuaRuntime();
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

void CScriptModule::ResetDynamicParams() {
  memset(&m_stDynamicSubScribeParamList, 0, sizeof(m_stDynamicSubScribeParamList));
  memset(&m_stDynamicPublishParamList, 0, sizeof(m_stDynamicPublishParamList));
}

int CScriptModule::BuildDynamicParamsFromAnalysis() {
  ResetDynamicParams();

  for (std::size_t idx = 0; idx < m_scriptAnalysis.subscriptions.size(); ++idx) {
    if (idx >= MAX_SUB_NUM) {
      return IMVS_EC_DATA_OVER_SIZE;
    }

    const ScriptSubscriptionDef &subscription = m_scriptAnalysis.subscriptions[idx];
    DYNAMIC_SUBSCRIBE_PARAM &sub =
        m_stDynamicSubScribeParamList.subScribeParamList[idx];
    memset(&sub, 0, sizeof(sub));

    sub.pubParam.nSrcModuleId = subscription.module_no;
    snprintf(sub.pubParam.bySrcModuleName, MODULE_NAME_LEN, "module%d",
             subscription.module_no);
    snprintf(sub.pubParam.byPubName, MAX_KEY_NAME_LEN, "%s",
             subscription.param_name.c_str());

    sub.subParam.nDstModuleId = m_nModuleId;
    snprintf(sub.subParam.byDstModuleName, MODULE_NAME_LEN, "%s", ALGO_NAME);
    snprintf(sub.subParam.bySubName, MAX_KEY_NAME_LEN, "input%zu", idx);
    m_stDynamicSubScribeParamList.nNum = static_cast<int>(idx + 1);
  }

  for (std::size_t idx = 0; idx < m_scriptAnalysis.outputs.size(); ++idx) {
    if (m_stDynamicPublishParamList.nNum >= MAX_SUB_NUM) {
      return IMVS_EC_DATA_OVER_SIZE;
    }

    const ScriptOutputDef &output = m_scriptAnalysis.outputs[idx];
    DYNAMIC_PUBLISH_PARAM &pub =
        m_stDynamicPublishParamList.pubParamList[m_stDynamicPublishParamList.nNum];
    memset(&pub, 0, sizeof(pub));
    snprintf(pub.szName, MAX_KEY_NAME_LEN, "%s", output.name.c_str());
    snprintf(pub.szType, MAX_KEY_NAME_LEN, "%s", ToTypeName(output.type));
    pub.nNum = 1;
    pub.nShow = 1;
    pub.nForce = 0;
    pub.nDynamic = 1;
    ++m_stDynamicPublishParamList.nNum;
  }

  return IMVS_EC_OK;
}

void CScriptModule::ResetCompileState() {
  m_bCompileValid = false;
  m_nCompileErrCode = IMVS_EC_NOT_READY;
  m_nCompileErrLine = 0;
  memset(m_szCompileErrMsg, 0, sizeof(m_szCompileErrMsg));
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
  DestroyLuaRuntime();

  int nErrCode = IMVS_EC_OK;
  std::string script_text;
  nErrCode = LoadScriptText(script_text);
  if (nErrCode != IMVS_EC_OK) {
    SetCompileFailure(nErrCode, "failed to load Lua script file", 0);
    return nErrCode;
  }

  m_scriptAnalysis = AnalyzeScript(script_text, kMaxScriptLength);
  if (!m_scriptAnalysis.ok) {
    const ScriptIssue issue = firstIssue(m_scriptAnalysis);
    SetCompileFailure(IMVS_EC_PARAM_NOT_VALID, issue.message, issue.line);
    return IMVS_EC_PARAM_NOT_VALID;
  }

  nErrCode = BuildDynamicParamsFromAnalysis();
  if (nErrCode != IMVS_EC_OK) {
    SetCompileFailure(nErrCode, "failed to build script subscription metadata", 0);
    return nErrCode;
  }

  nErrCode = CreateLuaRuntime(script_text);
  if (nErrCode != IMVS_EC_OK) {
    return nErrCode;
  }

  m_bCompileValid = true;
  m_nCompileErrCode = IMVS_EC_OK;
  snprintf(m_szCompileErrMsg, sizeof(m_szCompileErrMsg), "%s", "script compiled");
  return IMVS_EC_OK;
}

int CScriptModule::GetScriptInfo() {
  ResetDynamicParams();
  return IMVS_EC_OK;
}

int CScriptModule::FindSubscriptionIndex(int moduleNo, const char *paramName,
                                         ScriptValueType type) const {
  if (paramName == NULL) {
    return -1;
  }

  for (std::size_t idx = 0; idx < m_scriptAnalysis.subscriptions.size(); ++idx) {
    const ScriptSubscriptionDef &subscription = m_scriptAnalysis.subscriptions[idx];
    if (subscription.module_no == moduleNo &&
        subscription.param_name == paramName && subscription.type == type) {
      return static_cast<int>(idx);
    }
  }
  return -1;
}

const ScriptOutputDef *CScriptModule::FindOutputDef(const char *outputName) const {
  if (outputName == NULL) {
    return NULL;
  }

  for (std::size_t idx = 0; idx < m_scriptAnalysis.outputs.size(); ++idx) {
    if (m_scriptAnalysis.outputs[idx].name == outputName) {
      return &m_scriptAnalysis.outputs[idx];
    }
  }
  return NULL;
}

std::string CScriptModule::DebugValueToString(const ScriptHostValue &value) const {
  std::ostringstream oss;
  switch (value.type) {
  case mvsc::script::kScriptValueBool:
    return value.bool_value ? "true" : "false";
  case mvsc::script::kScriptValueInt:
    return std::to_string(value.int_value);
  case mvsc::script::kScriptValueFloat:
    return std::to_string(value.float_value);
  case mvsc::script::kScriptValueString:
    return value.string_value;
  case mvsc::script::kScriptValueBoolArray:
    oss << "[";
    for (std::size_t idx = 0; idx < value.bool_array_value.size(); ++idx) {
      if (idx > 0) {
        oss << ",";
      }
      oss << (value.bool_array_value[idx] != 0 ? "true" : "false");
    }
    oss << "]";
    return oss.str();
  case mvsc::script::kScriptValueIntArray:
    oss << "[";
    for (std::size_t idx = 0; idx < value.int_array_value.size(); ++idx) {
      if (idx > 0) {
        oss << ",";
      }
      oss << value.int_array_value[idx];
    }
    oss << "]";
    return oss.str();
  case mvsc::script::kScriptValueFloatArray:
    oss << "[";
    for (std::size_t idx = 0; idx < value.float_array_value.size(); ++idx) {
      if (idx > 0) {
        oss << ",";
      }
      oss << value.float_array_value[idx];
    }
    oss << "]";
    return oss.str();
  case mvsc::script::kScriptValueStringArray:
    oss << "[";
    for (std::size_t idx = 0; idx < value.string_array_value.size(); ++idx) {
      if (idx > 0) {
        oss << ",";
      }
      oss << value.string_array_value[idx];
    }
    oss << "]";
    return oss.str();
  default:
    return "";
  }
}

int CScriptModule::InstallHostApis(lua_State *L, int env_index) {
  struct ApiBinding {
    const char *name;
    ScriptValueType type;
    bool getter;
  };

  static const ApiBinding kBindings[] = {
      {"GetBoolValue", mvsc::script::kScriptValueBool, true},
      {"GetIntValue", mvsc::script::kScriptValueInt, true},
      {"GetFloatValue", mvsc::script::kScriptValueFloat, true},
      {"GetStringValue", mvsc::script::kScriptValueString, true},
      {"GetBoolArrayValue", mvsc::script::kScriptValueBoolArray, true},
      {"GetIntArrayValue", mvsc::script::kScriptValueIntArray, true},
      {"GetFloatArrayValue", mvsc::script::kScriptValueFloatArray, true},
      {"GetStringArrayValue", mvsc::script::kScriptValueStringArray, true},
      {"SetBoolValue", mvsc::script::kScriptValueBool, false},
      {"SetIntValue", mvsc::script::kScriptValueInt, false},
      {"SetFloatValue", mvsc::script::kScriptValueFloat, false},
      {"SetStringValue", mvsc::script::kScriptValueString, false},
      {"SetBoolArrayValue", mvsc::script::kScriptValueBoolArray, false},
      {"SetIntArrayValue", mvsc::script::kScriptValueIntArray, false},
      {"SetFloatArrayValue", mvsc::script::kScriptValueFloatArray, false},
      {"SetStringArrayValue", mvsc::script::kScriptValueStringArray, false},
  };

  for (std::size_t idx = 0; idx < sizeof(kBindings) / sizeof(kBindings[0]); ++idx) {
    lua_pushlightuserdata(L, this);
    lua_pushinteger(L, static_cast<lua_Integer>(kBindings[idx].type));
    lua_pushcclosure(L, kBindings[idx].getter ? luaGetValueBridge
                                              : luaSetValueBridge,
                     2);
    lua_setfield(L, env_index, kBindings[idx].name);
  }
  return IMVS_EC_OK;
}

int CScriptModule::ExecuteLifecycleFunction(const char *name, bool fail_on_missing,
                                            std::string *pErrorMessage) {
  if (m_pRuntime == NULL || m_pRuntime->state == NULL) {
    return IMVS_EC_NOT_READY;
  }

  lua_State *L = m_pRuntime->state;
  lua_rawgeti(L, LUA_REGISTRYINDEX, m_pRuntime->env_ref);
  const int env_index = lua_gettop(L);
  lua_getfield(L, env_index, name);
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 2);
    return fail_on_missing ? IMVS_EC_PARAM_NOT_FOUND : IMVS_EC_OK;
  }

  LuaExecContext exec_context;
  exec_context.start_ms = nowMilliseconds();
  exec_context.timeout_ms = kLuaTimeoutMs;
  exec_context.timed_out = false;
  setLuaExecContext(L, &exec_context);
  lua_sethook(L, luaTimeoutHook, LUA_MASKCOUNT, kLuaHookInstructionStep);

  if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
    const std::string lua_error = lua_tostring(L, -1);
    lua_sethook(L, NULL, 0, 0);
    lua_pop(L, 2);
    if (pErrorMessage != NULL) {
      *pErrorMessage = exec_context.timed_out ? "script execution timeout"
                                              : lua_error;
    }
    return exec_context.timed_out ? IMVS_EC_WAIT_TIMEOUT : IMVS_EC_ALGO_PROCESS_ERR;
  }

  lua_sethook(L, NULL, 0, 0);
  lua_pop(L, 1);
  return IMVS_EC_OK;
}

void CScriptModule::DestroyLuaRuntime() {
  if (m_pRuntime == NULL) {
    return;
  }

  if (m_pRuntime->state != NULL) {
    std::string cleanup_error;
    if (m_pRuntime->env_ref != LUA_NOREF) {
      const int cleanup_ret =
          ExecuteLifecycleFunction(kScriptCleanupName, false, &cleanup_error);
      if (cleanup_ret != IMVS_EC_OK && !cleanup_error.empty()) {
        LOGW("cleanup script failed: %s\n", cleanup_error.c_str());
      }
      luaL_unref(m_pRuntime->state, LUA_REGISTRYINDEX, m_pRuntime->env_ref);
    }
    lua_close(m_pRuntime->state);
  }

  delete m_pRuntime;
  m_pRuntime = NULL;
}

int CScriptModule::CreateLuaRuntime(const std::string &script_text) {
  DestroyLuaRuntime();

  m_pRuntime = new ScriptLuaRuntime();
  if (m_pRuntime == NULL) {
    SetCompileFailure(IMVS_EC_OUTOFMEMORY, "failed to allocate Lua runtime", 0);
    return IMVS_EC_OUTOFMEMORY;
  }

  int nErrCode = createLuaState(&m_pRuntime->state, &m_pRuntime->memory_context);
  if (nErrCode != IMVS_EC_OK) {
    SetCompileFailure(nErrCode, "failed to create Lua state", 0);
    DestroyLuaRuntime();
    return nErrCode;
  }

  lua_State *L = m_pRuntime->state;
  const int env_index = installSandbox(L);
  InstallHostApis(L, env_index);

  lua_pushvalue(L, env_index);
  m_pRuntime->env_ref = luaL_ref(L, LUA_REGISTRYINDEX);

  if (luaL_loadbuffer(L, script_text.c_str(), script_text.size(), kScriptChunkName) !=
      LUA_OK) {
    const std::string lua_error = lua_tostring(L, -1);
    const int line = parseLuaErrorLine(lua_error);
    SetCompileFailure(IMVS_EC_FILE_FORMAT, lua_error, line);
    DestroyLuaRuntime();
    return IMVS_EC_FILE_FORMAT;
  }

  if (!bindChunkEnv(L, -1, env_index)) {
    SetCompileFailure(IMVS_EC_NOT_SUPPORT, "failed to bind Lua sandbox", 0);
    DestroyLuaRuntime();
    return IMVS_EC_NOT_SUPPORT;
  }

  if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
    const std::string lua_error = lua_tostring(L, -1);
    const int line = parseLuaErrorLine(lua_error);
    SetCompileFailure(IMVS_EC_PARAM_NOT_VALID, lua_error, line);
    DestroyLuaRuntime();
    return IMVS_EC_PARAM_NOT_VALID;
  }

  lua_getfield(L, env_index, kScriptEntryName);
  const bool has_run = lua_isfunction(L, -1);
  lua_pop(L, 1);
  if (!has_run) {
    SetCompileFailure(IMVS_EC_PARAM_NOT_FOUND, "script must define function run()",
                      0);
    DestroyLuaRuntime();
    return IMVS_EC_PARAM_NOT_FOUND;
  }

  if (m_scriptAnalysis.lifecycle.has_init) {
    std::string error_message;
    nErrCode = ExecuteLifecycleFunction(kScriptInitName, false, &error_message);
    if (nErrCode != IMVS_EC_OK) {
      SetCompileFailure(nErrCode,
                        error_message.empty() ? "script init() failed"
                                              : error_message,
                        0);
      DestroyLuaRuntime();
      return nErrCode;
    }
  }

  lua_pop(L, 1);
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

int CScriptModule::ReadSubscriptionValue(void *hInput, int index,
                                         ScriptHostValue *pValue) {
  if (hInput == NULL || pValue == NULL || index < 0 ||
      index >= static_cast<int>(m_scriptAnalysis.subscriptions.size())) {
    return IMVS_EC_PARAM;
  }

  const ScriptSubscriptionDef &subscription = m_scriptAnalysis.subscriptions[index];
  const char *src_name = m_stDynamicSubScribeParamList.subScribeParamList[index]
                             .subParam.bySubName;
  if (src_name == NULL || strlen(src_name) == 0) {
    return IMVS_EC_MODULE_INPUT_CFG_UNDONE;
  }

  ScriptHostValue value;
  value.type = subscription.type;
  int count = 0;
  switch (subscription.type) {
  case mvsc::script::kScriptValueBool:
  case mvsc::script::kScriptValueInt:
  case mvsc::script::kScriptValueBoolArray:
  case mvsc::script::kScriptValueIntArray: {
    int scalar_value = 0;
    std::vector<int> values;
    const int nErrCode =
        VM_M_GetInt_Dynamic(hInput, src_name, count, scalar_value, values);
    if (nErrCode != IMVS_EC_OK || (count < 1 && values.empty())) {
      return IMVS_EC_ALGO_NO_DATA;
    }

    if (subscription.type == mvsc::script::kScriptValueBool ||
        subscription.type == mvsc::script::kScriptValueInt) {
      value.bool_value = (scalar_value != 0);
      value.int_value = scalar_value;
    } else {
      if (values.empty() && count > 0) {
        values.push_back(scalar_value);
      }
      if (subscription.type == mvsc::script::kScriptValueBoolArray) {
        value.bool_array_value = values;
      } else {
        value.int_array_value = values;
      }
    }

    *pValue = value;
    return IMVS_EC_OK;
  }
  case mvsc::script::kScriptValueFloat:
  case mvsc::script::kScriptValueFloatArray: {
    float scalar_value = 0.0f;
    std::vector<float> values;
    const int nErrCode =
        VM_M_GetFloat_Dynamic(hInput, src_name, count, scalar_value, values);
    if (nErrCode != IMVS_EC_OK || (count < 1 && values.empty())) {
      return IMVS_EC_ALGO_NO_DATA;
    }

    if (subscription.type == mvsc::script::kScriptValueFloat) {
      value.float_value = scalar_value;
    } else {
      if (values.empty() && count > 0) {
        values.push_back(scalar_value);
      }
      value.float_array_value = values;
    }

    *pValue = value;
    return IMVS_EC_OK;
  }
  case mvsc::script::kScriptValueString:
  case mvsc::script::kScriptValueStringArray: {
    std::string scalar_value;
    std::vector<std::string> values;
    const int nErrCode =
        VM_M_GetString_Dynamic(hInput, src_name, count, scalar_value, values);
    if (nErrCode != IMVS_EC_OK || (count < 1 && values.empty())) {
      return IMVS_EC_ALGO_NO_DATA;
    }

    if (subscription.type == mvsc::script::kScriptValueString) {
      value.string_value = scalar_value;
    } else {
      if (values.empty() && count > 0) {
        values.push_back(scalar_value);
      }
      value.string_array_value = values;
    }

    *pValue = value;
    return IMVS_EC_OK;
  }
  default:
    return IMVS_EC_NOT_SUPPORT;
  }
}

int CScriptModule::LuaGetValue(lua_State *L, ScriptValueType type, int moduleNo,
                               const char *paramName) {
  if (m_pRuntime == NULL || m_pRuntime->active_input == NULL) {
    return luaL_error(L, "script input context is unavailable");
  }

  const int subscription_index = FindSubscriptionIndex(moduleNo, paramName, type);
  if (subscription_index < 0) {
    return luaL_error(L, "subscription is not declared in script metadata");
  }

  ScriptHostValue value;
  const int nErrCode =
      ReadSubscriptionValue(m_pRuntime->active_input, subscription_index, &value);
  if (nErrCode != IMVS_EC_OK) {
    return luaL_error(L, "failed to read subscribed value");
  }

  std::ostringstream input_name;
  input_name << "module" << moduleNo << "." << paramName;
  AppendDebugValue(input_name.str().c_str(), DebugValueToString(value),
                   m_szInputResult, sizeof(m_szInputResult));

  switch (type) {
  case mvsc::script::kScriptValueBool:
    lua_pushboolean(L, value.bool_value ? 1 : 0);
    return 1;
  case mvsc::script::kScriptValueInt:
    lua_pushinteger(L, value.int_value);
    return 1;
  case mvsc::script::kScriptValueFloat:
    lua_pushnumber(L, value.float_value);
    return 1;
  case mvsc::script::kScriptValueString:
    lua_pushstring(L, value.string_value.c_str());
    return 1;
  case mvsc::script::kScriptValueBoolArray:
    lua_newtable(L);
    for (std::size_t idx = 0; idx < value.bool_array_value.size(); ++idx) {
      lua_pushboolean(L, value.bool_array_value[idx] != 0 ? 1 : 0);
      lua_rawseti(L, -2, static_cast<lua_Integer>(idx + 1));
    }
    return 1;
  case mvsc::script::kScriptValueIntArray:
    lua_newtable(L);
    for (std::size_t idx = 0; idx < value.int_array_value.size(); ++idx) {
      lua_pushinteger(L, value.int_array_value[idx]);
      lua_rawseti(L, -2, static_cast<lua_Integer>(idx + 1));
    }
    return 1;
  case mvsc::script::kScriptValueFloatArray:
    lua_newtable(L);
    for (std::size_t idx = 0; idx < value.float_array_value.size(); ++idx) {
      lua_pushnumber(L, value.float_array_value[idx]);
      lua_rawseti(L, -2, static_cast<lua_Integer>(idx + 1));
    }
    return 1;
  case mvsc::script::kScriptValueStringArray:
    lua_newtable(L);
    for (std::size_t idx = 0; idx < value.string_array_value.size(); ++idx) {
      lua_pushstring(L, value.string_array_value[idx].c_str());
      lua_rawseti(L, -2, static_cast<lua_Integer>(idx + 1));
    }
    return 1;
  default:
    return luaL_error(L, "unsupported subscribed value type");
  }
}

int CScriptModule::LuaSetValue(lua_State *L, ScriptValueType type,
                               const char *outputName, int valueIndex) {
  if (m_pRuntime == NULL || m_pRuntime->active_output == NULL) {
    return luaL_error(L, "script output context is unavailable");
  }

  const ScriptOutputDef *pOutput = FindOutputDef(outputName);
  if (pOutput == NULL) {
    return luaL_error(L, "output is not declared in script metadata");
  }
  if (pOutput->type != type) {
    return luaL_error(L, "output setter type does not match declared output type");
  }

  ScFramePtr pOutFrame = VM_M_Get_Frame_ByHOutput(m_pRuntime->active_output);
  if (!pOutFrame) {
    return luaL_error(L, "script output frame is unavailable");
  }

  const int abs_value_index = lua_absindex(L, valueIndex);
  ScriptHostValue debug_value;
  debug_value.type = type;

  switch (type) {
  case mvsc::script::kScriptValueBool:
    if (!lua_isboolean(L, abs_value_index)) {
      return luaL_error(L, "SetBoolValue expects a boolean");
    }
    debug_value.bool_value = lua_toboolean(L, abs_value_index) != 0;
    pOutFrame->setVal(outputName, debug_value.bool_value ? 1 : 0);
    break;
  case mvsc::script::kScriptValueInt:
    if (!lua_isinteger(L, abs_value_index)) {
      return luaL_error(L, "SetIntValue expects an integer");
    }
    debug_value.int_value = static_cast<int>(lua_tointeger(L, abs_value_index));
    pOutFrame->setVal(outputName, debug_value.int_value);
    break;
  case mvsc::script::kScriptValueFloat:
    if (!lua_isnumber(L, abs_value_index)) {
      return luaL_error(L, "SetFloatValue expects a number");
    }
    debug_value.float_value =
        static_cast<float>(lua_tonumber(L, abs_value_index));
    pOutFrame->setVal(outputName, debug_value.float_value);
    break;
  case mvsc::script::kScriptValueString:
    if (!lua_isstring(L, abs_value_index)) {
      return luaL_error(L, "SetStringValue expects a string");
    }
    debug_value.string_value = lua_tostring(L, abs_value_index);
    pOutFrame->setVal(outputName, debug_value.string_value);
    break;
  case mvsc::script::kScriptValueBoolArray: {
    if (!lua_istable(L, abs_value_index)) {
      return luaL_error(L, "SetBoolArrayValue expects a table");
    }
    const lua_Integer len = lua_rawlen(L, abs_value_index);
    for (lua_Integer idx = 1; idx <= len; ++idx) {
      lua_rawgeti(L, abs_value_index, idx);
      if (!lua_isboolean(L, -1)) {
        lua_pop(L, 1);
        return luaL_error(L, "SetBoolArrayValue expects boolean elements");
      }
      const int bool_value = lua_toboolean(L, -1) != 0 ? 1 : 0;
      debug_value.bool_array_value.push_back(bool_value);
      pOutFrame->setVal(outputName, static_cast<int>(idx - 1), bool_value);
      lua_pop(L, 1);
    }
    break;
  }
  case mvsc::script::kScriptValueIntArray: {
    if (!lua_istable(L, abs_value_index)) {
      return luaL_error(L, "SetIntArrayValue expects a table");
    }
    const lua_Integer len = lua_rawlen(L, abs_value_index);
    for (lua_Integer idx = 1; idx <= len; ++idx) {
      lua_rawgeti(L, abs_value_index, idx);
      if (!lua_isinteger(L, -1)) {
        lua_pop(L, 1);
        return luaL_error(L, "SetIntArrayValue expects integer elements");
      }
      const int int_value = static_cast<int>(lua_tointeger(L, -1));
      debug_value.int_array_value.push_back(int_value);
      pOutFrame->setVal(outputName, static_cast<int>(idx - 1), int_value);
      lua_pop(L, 1);
    }
    break;
  }
  case mvsc::script::kScriptValueFloatArray: {
    if (!lua_istable(L, abs_value_index)) {
      return luaL_error(L, "SetFloatArrayValue expects a table");
    }
    const lua_Integer len = lua_rawlen(L, abs_value_index);
    for (lua_Integer idx = 1; idx <= len; ++idx) {
      lua_rawgeti(L, abs_value_index, idx);
      if (!lua_isnumber(L, -1)) {
        lua_pop(L, 1);
        return luaL_error(L, "SetFloatArrayValue expects numeric elements");
      }
      const float float_value = static_cast<float>(lua_tonumber(L, -1));
      debug_value.float_array_value.push_back(float_value);
      pOutFrame->setVal(outputName, static_cast<int>(idx - 1), float_value);
      lua_pop(L, 1);
    }
    break;
  }
  case mvsc::script::kScriptValueStringArray: {
    if (!lua_istable(L, abs_value_index)) {
      return luaL_error(L, "SetStringArrayValue expects a table");
    }
    const lua_Integer len = lua_rawlen(L, abs_value_index);
    for (lua_Integer idx = 1; idx <= len; ++idx) {
      lua_rawgeti(L, abs_value_index, idx);
      if (!lua_isstring(L, -1)) {
        lua_pop(L, 1);
        return luaL_error(L, "SetStringArrayValue expects string elements");
      }
      const std::string string_value = lua_tostring(L, -1);
      debug_value.string_array_value.push_back(string_value);
      pOutFrame->setVal(outputName, static_cast<int>(idx - 1), string_value);
      lua_pop(L, 1);
    }
    break;
  }
  default:
    return luaL_error(L, "unsupported output type");
  }

  AppendDebugValue(outputName, DebugValueToString(debug_value), m_szOutputResult,
                   sizeof(m_szOutputResult));
  return 0;
}

int CScriptModule::ExecuteCompiledScript(void *hInput, void *hOutput) {
  if (!m_bCompileValid) {
    std::string error_message = m_szCompileErrMsg;
    if (m_nCompileErrLine > 0) {
      error_message += " (line " + std::to_string(m_nCompileErrLine) + ")";
    }
    return SendScriptFailure(hOutput, m_nCompileErrCode, error_message);
  }

  if (m_pRuntime == NULL || m_pRuntime->state == NULL ||
      m_pRuntime->env_ref == LUA_NOREF) {
    return SendScriptFailure(hOutput, IMVS_EC_NOT_READY, "script runtime is not ready");
  }

  lua_State *L = m_pRuntime->state;
  m_pRuntime->active_input = hInput;
  m_pRuntime->active_output = hOutput;

  lua_rawgeti(L, LUA_REGISTRYINDEX, m_pRuntime->env_ref);
  const int env_index = lua_gettop(L);
  lua_getfield(L, env_index, kScriptEntryName);
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 2);
    m_pRuntime->active_input = NULL;
    m_pRuntime->active_output = NULL;
    return SendScriptFailure(hOutput, IMVS_EC_PARAM_NOT_FOUND,
                             "script must define function run()");
  }

  LuaExecContext exec_context;
  exec_context.start_ms = nowMilliseconds();
  exec_context.timeout_ms = kLuaTimeoutMs;
  exec_context.timed_out = false;
  setLuaExecContext(L, &exec_context);
  lua_sethook(L, luaTimeoutHook, LUA_MASKCOUNT, kLuaHookInstructionStep);

  if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
    const std::string lua_error = lua_tostring(L, -1);
    lua_sethook(L, NULL, 0, 0);
    lua_pop(L, 2);
    m_pRuntime->active_input = NULL;
    m_pRuntime->active_output = NULL;
    if (exec_context.timed_out) {
      return SendScriptFailure(hOutput, IMVS_EC_WAIT_TIMEOUT,
                               "script execution timeout");
    }
    return SendScriptFailure(hOutput, IMVS_EC_ALGO_PROCESS_ERR, lua_error);
  }

  lua_sethook(L, NULL, 0, 0);
  lua_pop(L, 1);
  m_pRuntime->active_input = NULL;
  m_pRuntime->active_output = NULL;

  int nErrCode = SendOutputParam(hOutput, IMVS_EC_OK);
  if (nErrCode != IMVS_EC_OK) {
    return nErrCode;
  }

  ScFramePtr pOutFrame = VM_M_Get_Frame_ByHOutput(hOutput);
  if (!pOutFrame) {
    return IMVS_EC_NULL_PTR;
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


CAbstractUserModule *CreateModule(void *hModule) {
  assert(hModule != NULL);
  return new CScriptModule;
}

void DestroyModule(void *hModule, CAbstractUserModule *pUserModule) {
  assert(hModule != NULL);
  assert(pUserModule != NULL);
  delete pUserModule;
}
