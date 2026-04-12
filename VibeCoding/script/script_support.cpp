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

struct Token {
  std::string text;
  int line = 0;
  int column = 0;
};

const char *kReservedNames[] = {"inputs", "outputs", "run", "init", "cleanup"};

const char *kForbiddenApis[] = {"collectgarbage", "debug",   "dofile",
                                "getmetatable",  "io",      "load",
                                "loadfile",      "loadstring",
                                "os",            "package", "rawget",
                                "rawset",        "require", "setmetatable"};

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

void skipLongLiteral(const std::string &text, Cursor &cursor) {
  if (cursor.index + 1 >= text.size() || text[cursor.index] != '[' ||
      text[cursor.index + 1] != '[') {
    return;
  }

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

void skipQuotedLiteral(const std::string &text, Cursor &cursor) {
  const char quote = text[cursor.index];
  advance(cursor, text[cursor.index]);
  while (cursor.index < text.size()) {
    const char ch = text[cursor.index];
    if (ch == '\\') {
      advance(cursor, ch);
      if (cursor.index < text.size()) {
        advance(cursor, text[cursor.index]);
      }
      continue;
    }
    advance(cursor, ch);
    if (ch == quote) {
      return;
    }
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
  token.line = cursor.line;
  token.column = cursor.column;
  while (cursor.index < text.size() && isIdentifierBody(text[cursor.index])) {
    token.text.push_back(text[cursor.index]);
    advance(cursor, text[cursor.index]);
  }
  return token;
}

void skipSpaces(const std::string &text, Cursor &cursor) {
  while (cursor.index < text.size() &&
         std::isspace(static_cast<unsigned char>(text[cursor.index])) != 0) {
    advance(cursor, text[cursor.index]);
  }
}

bool isReservedName(const std::string &name) {
  for (std::size_t idx = 0; idx < sizeof(kReservedNames) / sizeof(kReservedNames[0]);
       ++idx) {
    if (name == kReservedNames[idx]) {
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

std::map<std::string, ScriptValueType>
buildSymbolTable(const std::vector<ScriptVarDef> &vars) {
  std::map<std::string, ScriptValueType> symbols;
  for (std::size_t idx = 0; idx < vars.size(); ++idx) {
    symbols[vars[idx].name] = vars[idx].type;
  }
  return symbols;
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

    if (ch == '\'' || ch == '"') {
      skipQuotedLiteral(script_text, cursor);
      continue;
    }

    if (ch == '[' && cursor.index + 1 < script_text.size() &&
        script_text[cursor.index + 1] == '[') {
      skipLongLiteral(script_text, cursor);
      continue;
    }

    if (!isIdentifierStart(ch)) {
      advance(cursor, ch);
      continue;
    }

    const Token token = readIdentifier(script_text, cursor);
    if (isForbiddenApi(token.text)) {
      addIssue(result, kScriptIssueForbiddenApi, "forbidden Lua API used",
               token.text, token.line, token.column);
      return result;
    }

    if (token.text != "inputs" && token.text != "outputs") {
      continue;
    }

    skipSpaces(script_text, cursor);
    if (cursor.index >= script_text.size()) {
      continue;
    }

    const char next = script_text[cursor.index];
    if (next == '[') {
      addIssue(result, kScriptIssueDynamicAccess,
               "dynamic access is not allowed for inputs/outputs", token.text,
               token.line, token.column);
      return result;
    }

    if (next != '.') {
      continue;
    }

    advance(cursor, script_text[cursor.index]);
    skipSpaces(script_text, cursor);
    if (cursor.index >= script_text.size() ||
        !isIdentifierStart(script_text[cursor.index])) {
      addIssue(result, kScriptIssueDynamicAccess,
               "field name must be a static identifier", token.text, token.line,
               token.column);
      return result;
    }

    const Token field = readIdentifier(script_text, cursor);
    if (token.text == "inputs") {
      if (input_symbols.find(field.text) == input_symbols.end()) {
        addIssue(result, kScriptIssueUnknownInput,
                 "script references unknown input variable", field.text,
                 field.line, field.column);
      }
    } else {
      if (output_symbols.find(field.text) == output_symbols.end()) {
        addIssue(result, kScriptIssueUnknownOutput,
                 "script references unknown output variable", field.text,
                 field.line, field.column);
      }
    }
  }

  return result;
}

} // namespace script
} // namespace mvsc
