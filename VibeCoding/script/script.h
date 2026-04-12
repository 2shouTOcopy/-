/** @file
  * @note    Hangzhou Hikvision Digital Technology Co., Ltd. All rights reserved.
  * @brief   script algo main class
  */
#pragma once

#include <pthread.h>

#include <string>
#include <vector>

#include "VmModuleBase.h"
#include "adapter/ScheErrorCodeDefine.h"
#include "VmTypeDef.h"
#include "algo_common.h"
#include "json_tool.h"
#include "algo.h"
#include "script_support.hpp"

#define MAX_NAME_LEN                (32)
#define MAX_SCRIPT_NUM              (32)
#define MAX_GET_STRING_VALUE        (256)
#define MAX_RESLUT_STRING_LEN       (1024)

#define ALGO_PRIV_JSON_NAME         "algo_private.json"
#define ALGO_USER_LUA_SCRIPT_NAME   "user_function.lua"
#define ALGO_DIR                    ALGO_ROOT_DIR"script/json/"
#define ALGO_ABI_JSON_PATH          ALGO_DIR ALGO_ABI_JSON_NAME
#define ALGO_IO_JSON_PATH           ALGO_DIR ALGO_IO_JSON_NAME
#define ALGO_PM_JSON_PATH           ALGO_DIR ALGO_PM_JSON_NAME
#define ALGO_DISP_JSON_PATH         ALGO_DIR ALGO_DISP_JSON_NAME
#define ALGO_PRIV_JSON_PATH         ALGO_DIR ALGO_PRIV_JSON_NAME
#define ALGO_USER_LUA_SCRIPT_PATH   ALGO_DIR ALGO_USER_LUA_SCRIPT_NAME
#define ALGO_NAME                   "script"

struct lua_State;

typedef struct {
  int nAlgoId;
  char byFormat;
  char szAlgoName[MAX_NAME_LEN];
  char szParamName[MAX_NAME_LEN];
  char szVarName[MAX_NAME_LEN];
  mvsc::script::ScriptValueType eType;
} SCRIPT_IN_PARAM;

typedef struct {
  char byFormat;
  char szVarName[MAX_NAME_LEN];
  mvsc::script::ScriptValueType eType;
} SCRIPT_OUT_PARAM;

typedef struct script_list {
  int InCnt;
  int OutCnt;
  SCRIPT_IN_PARAM InParam[MAX_SCRIPT_NUM];
  SCRIPT_OUT_PARAM OutParam[MAX_SCRIPT_NUM];
} SCRIPT_LIST;

struct ScriptHostValue {
  mvsc::script::ScriptValueType type;
  bool bool_value;
  int int_value;
  float float_value;
  std::string string_value;

  ScriptHostValue()
      : type(mvsc::script::kScriptValueInvalid), bool_value(false),
        int_value(0), float_value(0.0f) {}
};

class CScriptModule : public CAlgo {
public:
  CScriptModule();
  ~CScriptModule();

public:
  int Process(IN void *hInput, IN void *hOutput);
  int SetPwd(IN const char *szPwd);
  int GetParam(IN const char *szParamName, OUT char *pBuff, IN int nBuffSize,
               OUT int *pDataLen);
  int SetParam(IN const char *szParamName, IN const char *pData, IN int nDataLen);
  int GetDynamicPublishParamNum();
  int GetDynamicSubScribeParamNum();
  int GetDynamicSingleSubScribeParam(
      IN int nParamIdx, OUT DYNAMIC_SUBSCRIBE_PARAM *pstSubScribeParam);
  int GetDynamicSinglePublishParam(
      IN int nParamIdx, OUT DYNAMIC_PUBLISH_PARAM *pstPublishParam);

private:
  int InitAlgoPrivate();
  void DeInit();
  int InitPrivJson();
  int GetScriptInfo();
  int UpdateDynamicSubInfo(const char *pFormatStr, int nCnt);
  int Analyse(SCRIPT_IN_PARAM *pList, char *pStart, char *pEnd);
  int SendOutputParam(IN void *hOutput, IN const int nStatus);
  int ParseLuaScript();
  int CompileScript();
  int LoadScriptText(std::string &script_text) const;
  int BuildVarDefs();
  void ResetCompileState();
  void SetCompileFailure(int err_code, const std::string &message, int line);
  int SendScriptFailure(void *hOutput, int err_code, const std::string &message);
  int ReadInputValue(void *hInput, int index, ScriptHostValue *pValue);
  int ExecuteCompiledScript(void *hInput, void *hOutput);
  void AppendDebugValue(const char *name, const std::string &value, char *buffer,
                        size_t buffer_size) const;
  void SyncRunMode(void *hInput);

  int m_nRunMode;
  bool m_bDebugEnable;
  char m_szLuaScriptPath[MAX_PATH_LEN];
  SCRIPT_LIST m_stScriptInfo;
  char m_szInputResult[MAX_RESLUT_STRING_LEN];
  char m_szOutputResult[MAX_RESLUT_STRING_LEN];
  DYNAMIC_SUBSCRIBE_PARAM_LIST m_stDynamicSubScribeParamList;
  DYNAMIC_PUBLISH_PARAM_LIST m_stDynamicPublishParamList;
  std::vector<mvsc::script::ScriptVarDef> m_inputVarDefs;
  std::vector<mvsc::script::ScriptVarDef> m_outputVarDefs;
  std::vector<char> m_compiledChunk;
  bool m_bCompileValid;
  int m_nCompileErrCode;
  int m_nCompileErrLine;
  char m_szCompileErrMsg[MAX_RESLUT_STRING_LEN];
  pthread_mutex_t m_execMutex;
};

#ifdef __cplusplus
extern "C" {
#endif

CAbstractUserModule *CreateModule(void *hModule);
void DestroyModule(void *hModule, CAbstractUserModule *pUserModule);

#ifdef __cplusplus
};
#endif
