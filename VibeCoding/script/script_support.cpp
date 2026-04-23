#include "script_support.hpp"

#include <cctype>
#include <map>
#include <set>

namespace mvsc {
namespace script {

namespace {

struct Cursor {
  std::size_t index = 0;
  int line = 1;
  int column = 1;
};

enum TokenKind {
  kTokenIdentifier = 0,
  kTokenNumber,
  kTokenString,
  kTokenSymbol,
  kTokenKeyword,
};

struct Token {
  TokenKind kind = kTokenIdentifier;
  std::string text;
  int line = 0;
  int column = 0;
};

enum FunctionKind {
  kFunctionNone = 0,
  kFunctionInit,
  kFunctionRun,
  kFunctionCleanup,
  kFunctionOther,
};

struct HostApiDef {
  const char *name;
  ScriptValueType type;
  bool is_getter;
  bool is_setter;
  bool is_reserved_only;
};

const char *kReservedNames[] = {"init", "run", "cleanup"};

const char *kForbiddenApis[] = {"collectgarbage", "debug",   "dofile",
                                "getmetatable",  "io",      "load",
                                "loadfile",      "loadstring",
                                "os",            "package", "rawget",
                                "rawset",        "require", "setmetatable"};

const char *kBuiltinNames[] = {"assert",   "error",   "ipairs",  "math",
                               "next",     "pairs",   "pcall",   "select",
                               "string",   "table",   "tonumber","tostring",
                               "type",     "xpcall"};

const HostApiDef kHostApis[] = {
    {"GetBoolValue", kScriptValueBool, true, false, false},
    {"GetIntValue", kScriptValueInt, true, false, false},
    {"GetFloatValue", kScriptValueFloat, true, false, false},
    {"GetStringValue", kScriptValueString, true, false, false},
    {"GetBoolArrayValue", kScriptValueBoolArray, true, false, false},
    {"GetIntArrayValue", kScriptValueIntArray, true, false, false},
    {"GetFloatArrayValue", kScriptValueFloatArray, true, false, false},
    {"GetStringArrayValue", kScriptValueStringArray, true, false, false},
    {"SetBoolValue", kScriptValueBool, false, true, false},
    {"SetIntValue", kScriptValueInt, false, true, false},
    {"SetFloatValue", kScriptValueFloat, false, true, false},
    {"SetStringValue", kScriptValueString, false, true, false},
    {"SetBoolArrayValue", kScriptValueBoolArray, false, true, false},
    {"SetIntArrayValue", kScriptValueIntArray, false, true, false},
    {"SetFloatArrayValue", kScriptValueFloatArray, false, true, false},
    {"SetStringArrayValue", kScriptValueStringArray, false, true, false},
    {"GetModuleBoolParam", kScriptValueBool, false, false, true},
    {"GetModuleIntParam", kScriptValueInt, false, false, true},
    {"GetModuleFloatParam", kScriptValueFloat, false, false, true},
    {"GetModuleStringParam", kScriptValueString, false, false, true},
    {"SetModuleBoolParam", kScriptValueBool, false, false, true},
    {"SetModuleIntParam", kScriptValueInt, false, false, true},
    {"SetModuleFloatParam", kScriptValueFloat, false, false, true},
    {"SetModuleStringParam", kScriptValueString, false, false, true},
};

bool isIdentifierStart(char ch) {
  return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

bool isIdentifierBody(char ch) {
  return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

void advance(Cursor &cursor, char ch) {
  ++cursor.index;
  if (ch == '\n') {
    ++cursor.line;
    cursor.column = 1;
  } else {
    ++cursor.column;
  }
}

bool isKeyword(const std::string &text) {
  static const char *kKeywords[] = {
      "and",    "break", "do",    "else",   "elseif", "end",   "false",
      "for",    "function", "if", "in",     "local",  "nil",   "not",
      "or",     "repeat", "return","then",  "true",   "until", "while"};

  for (std::size_t idx = 0; idx < sizeof(kKeywords) / sizeof(kKeywords[0]);
       ++idx) {
    if (text == kKeywords[idx]) {
      return true;
    }
  }
  return false;
}

bool isReservedName(const std::string &name) {
  for (std::size_t idx = 0; idx < sizeof(kReservedNames) / sizeof(kReservedNames[0]);
       ++idx) {
    if (name == kReservedNames[idx]) {
      return true;
    }
  }

  for (std::size_t idx = 0; idx < sizeof(kHostApis) / sizeof(kHostApis[0]); ++idx) {
    if (name == kHostApis[idx].name) {
      return true;
    }
  }
  return false;
}

bool isForbiddenApi(const std::string &name) {
  for (std::size_t idx = 0;
       idx < sizeof(kForbiddenApis) / sizeof(kForbiddenApis[0]); ++idx) {
    if (name == kForbiddenApis[idx]) {
      return true;
    }
  }
  return false;
}

bool isBuiltinName(const std::string &name) {
  for (std::size_t idx = 0; idx < sizeof(kBuiltinNames) / sizeof(kBuiltinNames[0]);
       ++idx) {
    if (name == kBuiltinNames[idx]) {
      return true;
    }
  }
  return false;
}

const HostApiDef *findHostApi(const std::string &name) {
  for (std::size_t idx = 0; idx < sizeof(kHostApis) / sizeof(kHostApis[0]); ++idx) {
    if (name == kHostApis[idx].name) {
      return &kHostApis[idx];
    }
  }
  return NULL;
}

void addIssue(ScriptValidationResult &result, ScriptIssueCode code,
              const std::string &message, const std::string &symbol, int line,
              int column) {
  ScriptIssue issue;
  issue.code = code;
  issue.message = message;
  issue.symbol = symbol;
  issue.line = line;
  issue.column = column;
  result.issues.push_back(issue);
  result.ok = false;
}

void addIssue(ScriptAnalysisResult &result, ScriptIssueCode code,
              const std::string &message, const std::string &symbol, int line,
              int column) {
  ScriptIssue issue;
  issue.code = code;
  issue.message = message;
  issue.symbol = symbol;
  issue.line = line;
  issue.column = column;
  result.issues.push_back(issue);
  result.ok = false;
}

void skipLongLiteral(const std::string &text, Cursor &cursor) {
  advance(cursor, text[cursor.index]);
  advance(cursor, text[cursor.index]);
  while (cursor.index + 1 < text.size()) {
    if (text[cursor.index] == ']' && text[cursor.index + 1] == ']') {
      advance(cursor, text[cursor.index]);
      advance(cursor, text[cursor.index]);
      return;
    }
    advance(cursor, text[cursor.index]);
  }
}

void skipComment(const std::string &text, Cursor &cursor) {
  if (cursor.index + 3 < text.size() && text[cursor.index] == '-' &&
      text[cursor.index + 1] == '-' && text[cursor.index + 2] == '[' &&
      text[cursor.index + 3] == '[') {
    advance(cursor, text[cursor.index]);
    advance(cursor, text[cursor.index]);
    skipLongLiteral(text, cursor);
    return;
  }

  while (cursor.index < text.size()) {
    const char ch = text[cursor.index];
    advance(cursor, ch);
    if (ch == '\n') {
      return;
    }
  }
}

Token readIdentifier(const std::string &text, Cursor &cursor) {
  Token token;
  token.kind = kTokenIdentifier;
  token.line = cursor.line;
  token.column = cursor.column;
  while (cursor.index < text.size() && isIdentifierBody(text[cursor.index])) {
    token.text.push_back(text[cursor.index]);
    advance(cursor, text[cursor.index]);
  }
  if (isKeyword(token.text)) {
    token.kind = kTokenKeyword;
  }
  return token;
}

Token readNumber(const std::string &text, Cursor &cursor) {
  Token token;
  token.kind = kTokenNumber;
  token.line = cursor.line;
  token.column = cursor.column;

  bool seen_dot = false;
  while (cursor.index < text.size()) {
    const char ch = text[cursor.index];
    if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
      token.text.push_back(ch);
      advance(cursor, ch);
      continue;
    }
    if (ch == '.' && !seen_dot) {
      seen_dot = true;
      token.text.push_back(ch);
      advance(cursor, ch);
      continue;
    }
    break;
  }
  return token;
}

Token readString(const std::string &text, Cursor &cursor) {
  Token token;
  token.kind = kTokenString;
  token.line = cursor.line;
  token.column = cursor.column;

  const char quote = text[cursor.index];
  advance(cursor, text[cursor.index]);
  while (cursor.index < text.size()) {
    const char ch = text[cursor.index];
    if (ch == '\\') {
      advance(cursor, ch);
      if (cursor.index < text.size()) {
        token.text.push_back(text[cursor.index]);
        advance(cursor, text[cursor.index]);
      }
      continue;
    }
    advance(cursor, ch);
    if (ch == quote) {
      break;
    }
    token.text.push_back(ch);
  }
  return token;
}

std::vector<Token> tokenize(const std::string &script_text) {
  std::vector<Token> tokens;
  Cursor cursor;
  while (cursor.index < script_text.size()) {
    const char ch = script_text[cursor.index];

    if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
      advance(cursor, ch);
      continue;
    }

    if (ch == '-' && cursor.index + 1 < script_text.size() &&
        script_text[cursor.index + 1] == '-') {
      skipComment(script_text, cursor);
      continue;
    }

    if (ch == '[' && cursor.index + 1 < script_text.size() &&
        script_text[cursor.index + 1] == '[') {
      skipLongLiteral(script_text, cursor);
      continue;
    }

    if (ch == '\'' || ch == '"') {
      tokens.push_back(readString(script_text, cursor));
      continue;
    }

    if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
      tokens.push_back(readNumber(script_text, cursor));
      continue;
    }

    if (isIdentifierStart(ch)) {
      tokens.push_back(readIdentifier(script_text, cursor));
      continue;
    }

    Token token;
    token.kind = kTokenSymbol;
    token.line = cursor.line;
    token.column = cursor.column;
    token.text.assign(1, ch);
    tokens.push_back(token);
    advance(cursor, ch);
  }
  return tokens;
}

std::map<std::string, ScriptValueType>
buildSymbolTable(const std::vector<ScriptVarDef> &vars) {
  std::map<std::string, ScriptValueType> symbols;
  for (std::size_t idx = 0; idx < vars.size(); ++idx) {
    symbols[vars[idx].name] = vars[idx].type;
  }
  return symbols;
}

bool isBlockStartKeyword(const std::string &text) {
  return text == "if" || text == "for" || text == "while" || text == "do" ||
         text == "repeat";
}

size_t findClosingParen(const std::vector<Token> &tokens, size_t open_index) {
  int depth = 0;
  for (size_t idx = open_index; idx < tokens.size(); ++idx) {
    if (tokens[idx].text == "(") {
      ++depth;
    } else if (tokens[idx].text == ")") {
      --depth;
      if (depth == 0) {
        return idx;
      }
    }
  }
  return tokens.size();
}

ScriptValueType inferArrayLiteralType(const std::vector<Token> &tokens, size_t start,
                                      size_t end) {
  ScriptValueType element_type = kScriptValueInvalid;
  int table_depth = 0;
  for (size_t idx = start; idx < end; ++idx) {
    if (tokens[idx].text == "{") {
      ++table_depth;
      if (table_depth > 1) {
        return kScriptValueInvalid;
      }
      continue;
    }
    if (tokens[idx].text == "}") {
      --table_depth;
      continue;
    }
    if (table_depth != 1 || tokens[idx].text == "," || tokens[idx].text == ";") {
      continue;
    }

    ScriptValueType current_type = kScriptValueInvalid;
    if (tokens[idx].kind == kTokenString) {
      current_type = kScriptValueString;
    } else if (tokens[idx].kind == kTokenNumber) {
      current_type = tokens[idx].text.find('.') == std::string::npos
                         ? kScriptValueInt
                         : kScriptValueFloat;
    } else if (tokens[idx].kind == kTokenKeyword &&
               (tokens[idx].text == "true" || tokens[idx].text == "false")) {
      current_type = kScriptValueBool;
    } else {
      return kScriptValueInvalid;
    }

    if (element_type == kScriptValueInvalid) {
      element_type = current_type;
      continue;
    }
    if (element_type != current_type) {
      return kScriptValueInvalid;
    }
  }

  switch (element_type) {
  case kScriptValueBool:
    return kScriptValueBoolArray;
  case kScriptValueInt:
    return kScriptValueIntArray;
  case kScriptValueFloat:
    return kScriptValueFloatArray;
  case kScriptValueString:
    return kScriptValueStringArray;
  default:
    return kScriptValueInvalid;
  }
}

ScriptValueType inferAssignedType(const std::vector<Token> &tokens, size_t start,
                                  size_t end) {
  if (start >= end) {
    return kScriptValueInvalid;
  }

  const Token &token = tokens[start];
  if (token.kind == kTokenKeyword &&
      (token.text == "true" || token.text == "false")) {
    return kScriptValueBool;
  }
  if (token.kind == kTokenNumber) {
    return token.text.find('.') == std::string::npos ? kScriptValueInt
                                                     : kScriptValueFloat;
  }
  if (token.kind == kTokenString) {
    return kScriptValueString;
  }
  if (token.text == "{") {
    return inferArrayLiteralType(tokens, start, end);
  }

  const HostApiDef *api = findHostApi(token.text);
  if (api != NULL && api->is_getter && start + 1 < end &&
      tokens[start + 1].text == "(") {
    return api->type;
  }

  return kScriptValueInvalid;
}

FunctionKind toFunctionKind(const std::string &name) {
  if (name == "init") {
    return kFunctionInit;
  }
  if (name == "run") {
    return kFunctionRun;
  }
  if (name == "cleanup") {
    return kFunctionCleanup;
  }
  return kFunctionOther;
}

std::string makeSubscriptionKey(int module_no, const std::string &param_name) {
  return std::to_string(module_no) + "#" + param_name;
}

} // namespace

bool IsValidVarName(const std::string &name) {
  if (name.empty() || !isIdentifierStart(name[0]) || isReservedName(name)) {
    return false;
  }

  for (std::size_t idx = 1; idx < name.size(); ++idx) {
    if (!isIdentifierBody(name[idx])) {
      return false;
    }
  }
  return true;
}

ScriptValueType ParseValueType(const std::string &type_name) {
  if (type_name == "bool") {
    return kScriptValueBool;
  }
  if (type_name == "int") {
    return kScriptValueInt;
  }
  if (type_name == "float") {
    return kScriptValueFloat;
  }
  if (type_name == "string") {
    return kScriptValueString;
  }
  if (type_name == "bool[]") {
    return kScriptValueBoolArray;
  }
  if (type_name == "int[]") {
    return kScriptValueIntArray;
  }
  if (type_name == "float[]") {
    return kScriptValueFloatArray;
  }
  if (type_name == "string[]") {
    return kScriptValueStringArray;
  }
  return kScriptValueInvalid;
}

const char *ToTypeName(ScriptValueType type) {
  switch (type) {
  case kScriptValueBool:
    return "bool";
  case kScriptValueInt:
    return "int";
  case kScriptValueFloat:
    return "float";
  case kScriptValueString:
    return "string";
  case kScriptValueBoolArray:
    return "bool[]";
  case kScriptValueIntArray:
    return "int[]";
  case kScriptValueFloatArray:
    return "float[]";
  case kScriptValueStringArray:
    return "string[]";
  default:
    return "invalid";
  }
}

ScriptValidationResult ValidateVarDefs(const std::vector<ScriptVarDef> &inputs,
                                       const std::vector<ScriptVarDef> &outputs,
                                       std::size_t /*max_script_length*/) {
  ScriptValidationResult result;
  std::set<std::string> names;
  const std::vector<ScriptVarDef> *groups[] = {&inputs, &outputs};

  for (std::size_t group_idx = 0; group_idx < 2; ++group_idx) {
    const std::vector<ScriptVarDef> &group = *groups[group_idx];
    for (std::size_t idx = 0; idx < group.size(); ++idx) {
      const ScriptVarDef &def = group[idx];
      if (!IsValidVarName(def.name)) {
        addIssue(result, kScriptIssueInvalidName,
                 "invalid script variable name", def.name, 0, 0);
        return result;
      }
      if (def.type == kScriptValueInvalid) {
        addIssue(result, kScriptIssueUnsupportedType,
                 "unsupported script variable type", def.name, 0, 0);
        return result;
      }
      if (!names.insert(def.name).second) {
        addIssue(result, kScriptIssueDuplicateName,
                 "duplicate script variable name", def.name, 0, 0);
        return result;
      }
    }
  }

  return result;
}

ScriptValidationResult AnalyzeScript(const std::string &script_text,
                                     const std::vector<ScriptVarDef> &inputs,
                                     const std::vector<ScriptVarDef> &outputs,
                                     std::size_t max_script_length) {
  ScriptValidationResult result = ValidateVarDefs(inputs, outputs, max_script_length);
  if (!result.ok) {
    return result;
  }

  if (script_text.empty()) {
    addIssue(result, kScriptIssueEmptyScript, "script text is empty", "", 0, 0);
    return result;
  }
  if (script_text.size() > max_script_length) {
    addIssue(result, kScriptIssueScriptTooLong, "script text is too long", "", 0,
             0);
    return result;
  }

  const std::map<std::string, ScriptValueType> input_symbols =
      buildSymbolTable(inputs);
  const std::map<std::string, ScriptValueType> output_symbols =
      buildSymbolTable(outputs);
  const std::vector<Token> tokens = tokenize(script_text);

  for (std::size_t idx = 0; idx < tokens.size(); ++idx) {
    if (tokens[idx].kind != kTokenIdentifier) {
      continue;
    }
    if (isForbiddenApi(tokens[idx].text)) {
      addIssue(result, kScriptIssueForbiddenApi, "forbidden Lua API used",
               tokens[idx].text, tokens[idx].line, tokens[idx].column);
      return result;
    }
    if (tokens[idx].text != "inputs" && tokens[idx].text != "outputs") {
      continue;
    }

    if (idx + 1 >= tokens.size()) {
      continue;
    }
    if (tokens[idx + 1].text == "[") {
      addIssue(result, kScriptIssueDynamicAccess,
               "dynamic access is not allowed for inputs/outputs",
               tokens[idx].text, tokens[idx].line, tokens[idx].column);
      return result;
    }
    if (tokens[idx + 1].text != ".") {
      continue;
    }
    if (idx + 2 >= tokens.size() || tokens[idx + 2].kind != kTokenIdentifier) {
      addIssue(result, kScriptIssueDynamicAccess,
               "field name must be a static identifier", tokens[idx].text,
               tokens[idx].line, tokens[idx].column);
      return result;
    }

    const Token &field = tokens[idx + 2];
    if (tokens[idx].text == "inputs") {
      if (input_symbols.find(field.text) == input_symbols.end()) {
        addIssue(result, kScriptIssueUnknownInput,
                 "script references unknown input variable", field.text,
                 field.line, field.column);
      }
    } else if (output_symbols.find(field.text) == output_symbols.end()) {
      addIssue(result, kScriptIssueUnknownOutput,
               "script references unknown output variable", field.text, field.line,
               field.column);
    }
  }

  return result;
}

ScriptAnalysisResult AnalyzeScript(const std::string &script_text,
                                   std::size_t max_script_length) {
  ScriptAnalysisResult result;
  if (script_text.empty()) {
    addIssue(result, kScriptIssueEmptyScript, "script text is empty", "", 0, 0);
    return result;
  }
  if (script_text.size() > max_script_length) {
    addIssue(result, kScriptIssueScriptTooLong, "script text is too long", "", 0,
             0);
    return result;
  }

  const std::vector<Token> tokens = tokenize(script_text);
  std::map<std::string, std::size_t> subscription_index;
  std::map<std::string, std::size_t> output_index;
  std::map<std::string, std::size_t> global_index;

  FunctionKind current_function = kFunctionNone;
  int block_depth = 0;
  std::set<std::string> local_names;

  for (std::size_t idx = 0; idx < tokens.size(); ++idx) {
    const Token &token = tokens[idx];

    if (token.kind == kTokenIdentifier && isForbiddenApi(token.text)) {
      addIssue(result, kScriptIssueForbiddenApi, "forbidden Lua API used",
               token.text, token.line, token.column);
      return result;
    }

    if (current_function == kFunctionNone) {
      if (token.kind == kTokenKeyword && token.text == "function" &&
          idx + 1 < tokens.size() && tokens[idx + 1].kind == kTokenIdentifier) {
        current_function = toFunctionKind(tokens[idx + 1].text);
        block_depth = 1;
        local_names.clear();
        if (current_function == kFunctionInit) {
          result.lifecycle.has_init = true;
        } else if (current_function == kFunctionRun) {
          result.lifecycle.has_run = true;
        } else if (current_function == kFunctionCleanup) {
          result.lifecycle.has_cleanup = true;
        }
        ++idx;
      }
      continue;
    }

    if (token.kind == kTokenKeyword && token.text == "function") {
      ++block_depth;
      continue;
    }
    if (token.kind == kTokenKeyword && isBlockStartKeyword(token.text)) {
      ++block_depth;
      continue;
    }
    if (token.kind == kTokenKeyword &&
        (token.text == "end" || token.text == "until")) {
      --block_depth;
      if (block_depth == 0) {
        current_function = kFunctionNone;
        local_names.clear();
      }
      continue;
    }

    if (token.kind == kTokenKeyword && token.text == "local") {
      std::size_t local_idx = idx + 1;
      while (local_idx < tokens.size()) {
        if (tokens[local_idx].kind == kTokenIdentifier) {
          local_names.insert(tokens[local_idx].text);
          ++local_idx;
          if (local_idx < tokens.size() && tokens[local_idx].text == ",") {
            ++local_idx;
            continue;
          }
        }
        break;
      }
      continue;
    }

    if (token.kind != kTokenIdentifier) {
      continue;
    }

    const HostApiDef *host_api = findHostApi(token.text);
    if (host_api != NULL && idx + 1 < tokens.size() && tokens[idx + 1].text == "(") {
      if (host_api->is_reserved_only) {
        addIssue(result, kScriptIssueUnsupportedHostApi,
                 "host API is reserved for future use", token.text, token.line,
                 token.column);
        return result;
      }

      const size_t close_index = findClosingParen(tokens, idx + 1);
      if (close_index >= tokens.size()) {
        addIssue(result, kScriptIssueDynamicAccess,
                 "unterminated host API call", token.text, token.line,
                 token.column);
        return result;
      }

      if (current_function == kFunctionRun && host_api->is_getter) {
        if (idx + 4 >= close_index) {
          addIssue(result, kScriptIssueDynamicParamName,
                   "getter arguments must be static", token.text, token.line,
                   token.column);
          return result;
        }
        const Token &module_token = tokens[idx + 2];
        const Token &comma_token = tokens[idx + 3];
        const Token &param_token = tokens[idx + 4];
        if (module_token.kind != kTokenNumber) {
          addIssue(result, kScriptIssueDynamicModuleNumber,
                   "module number must be a static integer literal", token.text,
                   module_token.line, module_token.column);
          return result;
        }
        if (comma_token.text != "," || param_token.kind != kTokenString) {
          addIssue(result, kScriptIssueDynamicParamName,
                   "parameter name must be a static string literal", token.text,
                   param_token.line, param_token.column);
          return result;
        }

        const int module_no = std::atoi(module_token.text.c_str());
        const std::string key = makeSubscriptionKey(module_no, param_token.text);
        std::map<std::string, std::size_t>::const_iterator existing =
            subscription_index.find(key);
        if (existing == subscription_index.end()) {
          ScriptSubscriptionDef def;
          def.module_no = module_no;
          def.param_name = param_token.text;
          def.type = host_api->type;
          def.line = token.line;
          def.column = token.column;
          subscription_index[key] = result.subscriptions.size();
          result.subscriptions.push_back(def);
        } else if (result.subscriptions[existing->second].type != host_api->type) {
          addIssue(result, kScriptIssueInputTypeConflict,
                   "input value read through multiple typed getters",
                   param_token.text, token.line, token.column);
          return result;
        }
      } else if (current_function == kFunctionRun && host_api->is_setter) {
        if (idx + 2 >= close_index || tokens[idx + 2].kind != kTokenString) {
          addIssue(result, kScriptIssueDynamicOutputName,
                   "output name must be a static string literal", token.text,
                   token.line, token.column);
          return result;
        }
        const std::string &output_name = tokens[idx + 2].text;
        std::map<std::string, std::size_t>::const_iterator existing =
            output_index.find(output_name);
        if (existing == output_index.end()) {
          ScriptOutputDef def;
          def.name = output_name;
          def.type = host_api->type;
          def.line = token.line;
          def.column = token.column;
          output_index[output_name] = result.outputs.size();
          result.outputs.push_back(def);
        } else if (result.outputs[existing->second].type != host_api->type) {
          addIssue(result, kScriptIssueOutputTypeConflict,
                   "output written through multiple typed setters", output_name,
                   token.line, token.column);
          return result;
        }
      }
      continue;
    }

    if (local_names.find(token.text) != local_names.end() || isBuiltinName(token.text)) {
      continue;
    }
    if (idx > 0 && tokens[idx - 1].text == ".") {
      continue;
    }
    if (idx + 1 < tokens.size() && tokens[idx + 1].text == "(") {
      continue;
    }

    const bool is_assignment =
        idx + 1 < tokens.size() && tokens[idx + 1].text == "=";

    if (current_function == kFunctionInit) {
      if (!is_assignment) {
        continue;
      }
      if (isReservedName(token.text)) {
        addIssue(result, kScriptIssueReservedNameCollision,
                 "global name collides with reserved script name", token.text,
                 token.line, token.column);
        return result;
      }

      std::size_t expr_end = idx + 2;
      while (expr_end < tokens.size() && tokens[expr_end].text != "," &&
             tokens[expr_end].kind != kTokenKeyword) {
        if (tokens[expr_end].text == "}" && idx + 2 < expr_end) {
          ++expr_end;
          break;
        }
        if (tokens[expr_end].text == ")" || tokens[expr_end].text == ";") {
          break;
        }
        ++expr_end;
      }

      const ScriptValueType inferred_type =
          inferAssignedType(tokens, idx + 2, expr_end);
      std::map<std::string, std::size_t>::iterator existing =
          global_index.find(token.text);
      if (existing == global_index.end()) {
        ScriptGlobalDef def;
        def.name = token.text;
        def.type = inferred_type;
        def.line = token.line;
        def.column = token.column;
        global_index[token.text] = result.globals.size();
        result.globals.push_back(def);
      } else if (inferred_type != kScriptValueInvalid &&
                 result.globals[existing->second].type != kScriptValueInvalid &&
                 result.globals[existing->second].type != inferred_type) {
        addIssue(result, kScriptIssueUnsupportedType,
                 "global variable changes type inside init", token.text,
                 token.line, token.column);
        return result;
      } else if (result.globals[existing->second].type == kScriptValueInvalid) {
        result.globals[existing->second].type = inferred_type;
      }
      continue;
    }

    const bool declared_global = global_index.find(token.text) != global_index.end();
    if (is_assignment) {
      if (!declared_global) {
        addIssue(result, kScriptIssueGlobalDeclaredOutsideInit,
                 "globals must be declared in init", token.text, token.line,
                 token.column);
        return result;
      }
      continue;
    }

    if (!declared_global) {
      addIssue(result, kScriptIssueUndeclaredGlobal,
               "global variable must be declared in init", token.text, token.line,
               token.column);
      return result;
    }
  }

  if (!result.lifecycle.has_run) {
    addIssue(result, kScriptIssueMissingRun,
             "script must define function run()", "run", 0, 0);
    return result;
  }

  return result;
}

} // namespace script
} // namespace mvsc
