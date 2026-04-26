#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace mvsc {
namespace script {

enum ScriptValueType {
  kScriptValueInvalid = 0,
  kScriptValueBool,
  kScriptValueInt,
  kScriptValueFloat,
  kScriptValueString,
  kScriptValueBoolArray,
  kScriptValueIntArray,
  kScriptValueFloatArray,
  kScriptValueStringArray,
};

enum ScriptIssueCode {
  kScriptIssueInvalidName = 1,
  kScriptIssueDuplicateName,
  kScriptIssueUnsupportedType,
  kScriptIssueScriptTooLong,
  kScriptIssueEmptyScript,
  kScriptIssueUnknownInput,
  kScriptIssueUnknownOutput,
  kScriptIssueDynamicAccess,
  kScriptIssueForbiddenApi,
  kScriptIssueMissingRun,
  kScriptIssueDynamicModuleNumber,
  kScriptIssueDynamicParamName,
  kScriptIssueDynamicOutputName,
  kScriptIssueInputTypeConflict,
  kScriptIssueOutputTypeConflict,
  kScriptIssueUndeclaredGlobal,
  kScriptIssueGlobalDeclaredOutsideInit,
  kScriptIssueReservedNameCollision,
  kScriptIssueUnsupportedHostApi,
};

struct ScriptVarDef {
  std::string name;
  ScriptValueType type = kScriptValueInvalid;
};

struct ScriptIssue {
  ScriptIssueCode code = kScriptIssueInvalidName;
  std::string message;
  std::string symbol;
  int line = 0;
  int column = 0;
};

struct ScriptValidationResult {
  bool ok = true;
  std::vector<ScriptIssue> issues;
};

struct ScriptLifecycleInfo {
  bool has_init = false;
  bool has_run = false;
  bool has_cleanup = false;
};

struct ScriptSubscriptionDef {
  int module_no = -1;
  std::string param_name;
  ScriptValueType type = kScriptValueInvalid;
  int line = 0;
  int column = 0;
};

struct ScriptOutputDef {
  std::string name;
  ScriptValueType type = kScriptValueInvalid;
  int line = 0;
  int column = 0;
};

struct ScriptGlobalDef {
  std::string name;
  ScriptValueType type = kScriptValueInvalid;
  int line = 0;
  int column = 0;
};

struct ScriptAnalysisResult {
  bool ok = true;
  ScriptLifecycleInfo lifecycle;
  std::vector<ScriptSubscriptionDef> subscriptions;
  std::vector<ScriptOutputDef> outputs;
  std::vector<ScriptGlobalDef> globals;
  std::vector<ScriptIssue> issues;
};

bool IsValidVarName(const std::string &name);
ScriptValueType ParseValueType(const std::string &type_name);
const char *ToTypeName(ScriptValueType type);

ScriptValidationResult ValidateVarDefs(const std::vector<ScriptVarDef> &inputs,
                                       const std::vector<ScriptVarDef> &outputs,
                                       std::size_t max_script_length);

ScriptValidationResult AnalyzeScript(const std::string &script_text,
                                     const std::vector<ScriptVarDef> &inputs,
                                     const std::vector<ScriptVarDef> &outputs,
                                     std::size_t max_script_length);

ScriptAnalysisResult AnalyzeScript(const std::string &script_text,
                                   std::size_t max_script_length);

} // namespace script
} // namespace mvsc
