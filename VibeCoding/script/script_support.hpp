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

} // namespace script
} // namespace mvsc
