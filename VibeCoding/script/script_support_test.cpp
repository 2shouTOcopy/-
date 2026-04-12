#include "script_support.hpp"

#include <gtest/gtest.h>

using namespace mvsc::script;

namespace {

ScriptVarDef makeVar(const char *name, ScriptValueType type) {
  ScriptVarDef def;
  def.name = name;
  def.type = type;
  return def;
}

} // namespace

TEST(ScriptSupportTest, RejectsInvalidVariableDefinitions) {
  ScriptValidationResult result =
      ValidateVarDefs({makeVar("1bad", kScriptValueInt)},
                      {makeVar("result", kScriptValueInt)}, 4096);

  EXPECT_FALSE(result.ok);
  ASSERT_FALSE(result.issues.empty());
  EXPECT_EQ(result.issues.front().code, kScriptIssueInvalidName);
}

TEST(ScriptSupportTest, RejectsDuplicateVariablesAcrossInputsAndOutputs) {
  ScriptValidationResult result =
      ValidateVarDefs({makeVar("value", kScriptValueInt)},
                      {makeVar("value", kScriptValueInt)}, 4096);

  EXPECT_FALSE(result.ok);
  ASSERT_FALSE(result.issues.empty());
  EXPECT_EQ(result.issues.front().code, kScriptIssueDuplicateName);
}

TEST(ScriptSupportTest, RejectsUnknownInputAndOutputReferences) {
  const char *script =
      "function run(inputs, outputs)\n"
      "  outputs.result = inputs.missing + 1\n"
      "  outputs.ghost = 1\n"
      "end\n";

  ScriptValidationResult result = AnalyzeScript(
      script, {makeVar("value", kScriptValueInt)},
      {makeVar("result", kScriptValueInt)}, 4096);

  EXPECT_FALSE(result.ok);
  ASSERT_EQ(result.issues.size(), 2U);
  EXPECT_EQ(result.issues[0].code, kScriptIssueUnknownInput);
  EXPECT_EQ(result.issues[0].symbol, "missing");
  EXPECT_EQ(result.issues[1].code, kScriptIssueUnknownOutput);
  EXPECT_EQ(result.issues[1].symbol, "ghost");
}

TEST(ScriptSupportTest, RejectsDynamicAccessOnInputsAndOutputs) {
  const char *script =
      "function run(inputs, outputs)\n"
      "  local key = 'value'\n"
      "  outputs.result = inputs[key]\n"
      "end\n";

  ScriptValidationResult result = AnalyzeScript(
      script, {makeVar("value", kScriptValueInt)},
      {makeVar("result", kScriptValueInt)}, 4096);

  EXPECT_FALSE(result.ok);
  ASSERT_FALSE(result.issues.empty());
  EXPECT_EQ(result.issues.front().code, kScriptIssueDynamicAccess);
}

TEST(ScriptSupportTest, RejectsDangerousApis) {
  const char *script =
      "function run(inputs, outputs)\n"
      "  outputs.result = os.execute('echo 1')\n"
      "end\n";

  ScriptValidationResult result = AnalyzeScript(
      script, {makeVar("value", kScriptValueInt)},
      {makeVar("result", kScriptValueInt)}, 4096);

  EXPECT_FALSE(result.ok);
  ASSERT_FALSE(result.issues.empty());
  EXPECT_EQ(result.issues.front().code, kScriptIssueForbiddenApi);
  EXPECT_EQ(result.issues.front().symbol, "os");
}

TEST(ScriptSupportTest, AcceptsSafeScript) {
  const char *script =
      "function run(inputs, outputs)\n"
      "  local sum = inputs.left + inputs.right\n"
      "  outputs.result = sum\n"
      "  outputs.label = string.format('%d', sum)\n"
      "end\n";

  ScriptValidationResult result =
      AnalyzeScript(script,
                    {makeVar("left", kScriptValueInt),
                     makeVar("right", kScriptValueInt)},
                    {makeVar("result", kScriptValueInt),
                     makeVar("label", kScriptValueString)},
                    4096);

  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(result.issues.empty());
}
