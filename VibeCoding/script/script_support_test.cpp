#include "script_support.hpp"

#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

using namespace mvsc::script;

namespace {

std::string loadShippedTemplate() {
  const std::string current_file = __FILE__;
  const std::string::size_type slash = current_file.find_last_of("/\\");
  const std::string current_dir =
      slash == std::string::npos ? std::string(".")
                                 : current_file.substr(0, slash);
  const std::string template_path =
      current_dir + "/../../so/script/json/user_function.lua";

  std::ifstream file(template_path.c_str(), std::ios::in | std::ios::binary);
  EXPECT_TRUE(file.is_open()) << template_path;
  if (!file.is_open()) {
    return std::string();
  }

  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

const ScriptSubscriptionDef *findSubscription(const ScriptAnalysisResult &result,
                                              int module_no,
                                              const char *param_name) {
  for (std::size_t idx = 0; idx < result.subscriptions.size(); ++idx) {
    if (result.subscriptions[idx].module_no == module_no &&
        result.subscriptions[idx].param_name == param_name) {
      return &result.subscriptions[idx];
    }
  }
  return NULL;
}

const ScriptOutputDef *findOutput(const ScriptAnalysisResult &result,
                                  const char *name) {
  for (std::size_t idx = 0; idx < result.outputs.size(); ++idx) {
    if (result.outputs[idx].name == name) {
      return &result.outputs[idx];
    }
  }
  return NULL;
}

const ScriptGlobalDef *findGlobal(const ScriptAnalysisResult &result,
                                  const char *name) {
  for (std::size_t idx = 0; idx < result.globals.size(); ++idx) {
    if (result.globals[idx].name == name) {
      return &result.globals[idx];
    }
  }
  return NULL;
}

} // namespace

TEST(ScriptSupportTest, ShippedTemplateUsesNewLifecycleAbi) {
  const std::string script = loadShippedTemplate();
  ASSERT_FALSE(script.empty());

  ScriptAnalysisResult result = AnalyzeScript(script.c_str(), 4096);

  ASSERT_TRUE(result.ok);
  EXPECT_TRUE(result.lifecycle.has_init);
  EXPECT_TRUE(result.lifecycle.has_run);
  EXPECT_TRUE(result.lifecycle.has_cleanup);
  ASSERT_EQ(result.subscriptions.size(), 2U);
  ASSERT_EQ(result.outputs.size(), 2U);

  const ScriptSubscriptionDef *count_sub =
      findSubscription(result, 1, "FindNum");
  ASSERT_TRUE(count_sub != NULL);
  EXPECT_EQ(count_sub->type, kScriptValueInt);

  const ScriptSubscriptionDef *codes_sub =
      findSubscription(result, 2, "CodeList");
  ASSERT_TRUE(codes_sub != NULL);
  EXPECT_EQ(codes_sub->type, kScriptValueStringArray);

  const ScriptOutputDef *sum_out = findOutput(result, "sum");
  ASSERT_TRUE(sum_out != NULL);
  EXPECT_EQ(sum_out->type, kScriptValueInt);

  const ScriptOutputDef *codes_out = findOutput(result, "codes");
  ASSERT_TRUE(codes_out != NULL);
  EXPECT_EQ(codes_out->type, kScriptValueStringArray);
}

TEST(ScriptSupportTest, ExtractsLifecycleSubscriptionsOutputsAndGlobals) {
  const char *script =
      "function init()\n"
      "  total = 0\n"
      "  labels = {\"seed\"}\n"
      "end\n"
      "\n"
      "function run()\n"
      "  local count = GetIntValue(1, \"FindNum\")\n"
      "  local names = GetStringArrayValue(2, \"CodeList\")\n"
      "  total = total + count\n"
      "  labels = names\n"
      "  SetIntValue(\"sum\", total)\n"
      "  SetStringArrayValue(\"codes\", labels)\n"
      "end\n"
      "\n"
      "function cleanup()\n"
      "  total = total\n"
      "end\n";

  ScriptAnalysisResult result = AnalyzeScript(script, 4096);

  ASSERT_TRUE(result.ok);
  EXPECT_TRUE(result.lifecycle.has_init);
  EXPECT_TRUE(result.lifecycle.has_run);
  EXPECT_TRUE(result.lifecycle.has_cleanup);

  ASSERT_EQ(result.subscriptions.size(), 2U);
  const ScriptSubscriptionDef *count_sub =
      findSubscription(result, 1, "FindNum");
  ASSERT_TRUE(count_sub != NULL);
  EXPECT_EQ(count_sub->type, kScriptValueInt);
  const ScriptSubscriptionDef *codes_sub =
      findSubscription(result, 2, "CodeList");
  ASSERT_TRUE(codes_sub != NULL);
  EXPECT_EQ(codes_sub->type, kScriptValueStringArray);

  ASSERT_EQ(result.outputs.size(), 2U);
  const ScriptOutputDef *sum_out = findOutput(result, "sum");
  ASSERT_TRUE(sum_out != NULL);
  EXPECT_EQ(sum_out->type, kScriptValueInt);
  const ScriptOutputDef *codes_out = findOutput(result, "codes");
  ASSERT_TRUE(codes_out != NULL);
  EXPECT_EQ(codes_out->type, kScriptValueStringArray);

  ASSERT_EQ(result.globals.size(), 2U);
  const ScriptGlobalDef *labels_global = findGlobal(result, "labels");
  ASSERT_TRUE(labels_global != NULL);
  EXPECT_EQ(labels_global->type, kScriptValueStringArray);
  const ScriptGlobalDef *total_global = findGlobal(result, "total");
  ASSERT_TRUE(total_global != NULL);
  EXPECT_EQ(total_global->type, kScriptValueInt);
}

TEST(ScriptSupportTest, ContextGetterDoesNotCreateTopologyMetadata) {
  const char *script =
      "function run()\n"
      "  local comm = GetGlobalCommunicationData()\n"
      "  SetStringValue(\"comm\", comm)\n"
      "end\n";

  ScriptAnalysisResult result = AnalyzeScript(script, 4096);

  ASSERT_TRUE(result.ok);
  EXPECT_EQ(result.subscriptions.size(), 0U);
  ASSERT_EQ(result.outputs.size(), 1U);
  EXPECT_EQ(result.outputs[0].name, "comm");
  EXPECT_EQ(result.outputs[0].type, kScriptValueString);
}

TEST(ScriptSupportTest, RejectsContextGetterWithWrongArity) {
  const char *script =
      "function run()\n"
      "  local comm = GetGlobalCommunicationData(\"bad\")\n"
      "  SetStringValue(\"comm\", comm)\n"
      "end\n";

  ScriptAnalysisResult result = AnalyzeScript(script, 4096);

  EXPECT_FALSE(result.ok);
  ASSERT_FALSE(result.issues.empty());
  EXPECT_EQ(result.issues.front().code, kScriptIssueUnsupportedHostApi);
  EXPECT_EQ(result.issues.front().symbol, "GetGlobalCommunicationData");
}

TEST(ScriptSupportTest, RejectsMissingRunFunction) {
  const char *script =
      "function init()\n"
      "  total = 0\n"
      "end\n";

  ScriptAnalysisResult result = AnalyzeScript(script, 4096);

  EXPECT_FALSE(result.ok);
  ASSERT_FALSE(result.issues.empty());
  EXPECT_EQ(result.issues.front().code, kScriptIssueMissingRun);
}

TEST(ScriptSupportTest, RejectsDynamicModuleNumber) {
  const char *script =
      "function run()\n"
      "  local module_no = 1\n"
      "  local count = GetIntValue(module_no, \"FindNum\")\n"
      "  SetIntValue(\"sum\", count)\n"
      "end\n";

  ScriptAnalysisResult result = AnalyzeScript(script, 4096);

  EXPECT_FALSE(result.ok);
  ASSERT_FALSE(result.issues.empty());
  EXPECT_EQ(result.issues.front().code, kScriptIssueDynamicModuleNumber);
}

TEST(ScriptSupportTest, RejectsInputTypeConflicts) {
  const char *script =
      "function run()\n"
      "  local a = GetIntValue(1, \"FindNum\")\n"
      "  local b = GetStringValue(1, \"FindNum\")\n"
      "  SetIntValue(\"sum\", a)\n"
      "end\n";

  ScriptAnalysisResult result = AnalyzeScript(script, 4096);

  EXPECT_FALSE(result.ok);
  ASSERT_FALSE(result.issues.empty());
  EXPECT_EQ(result.issues.front().code, kScriptIssueInputTypeConflict);
  EXPECT_EQ(result.issues.front().symbol, "FindNum");
}

TEST(ScriptSupportTest, RejectsOutputTypeConflicts) {
  const char *script =
      "function run()\n"
      "  local a = GetIntValue(1, \"FindNum\")\n"
      "  SetIntValue(\"out1\", a)\n"
      "  SetStringValue(\"out1\", \"bad\")\n"
      "end\n";

  ScriptAnalysisResult result = AnalyzeScript(script, 4096);

  EXPECT_FALSE(result.ok);
  ASSERT_FALSE(result.issues.empty());
  EXPECT_EQ(result.issues.front().code, kScriptIssueOutputTypeConflict);
  EXPECT_EQ(result.issues.front().symbol, "out1");
}

TEST(ScriptSupportTest, RejectsGlobalUseWithoutInitDeclaration) {
  const char *script =
      "function run()\n"
      "  local count = GetIntValue(1, \"FindNum\")\n"
      "  SetIntValue(\"sum\", total)\n"
      "end\n";

  ScriptAnalysisResult result = AnalyzeScript(script, 4096);

  EXPECT_FALSE(result.ok);
  ASSERT_FALSE(result.issues.empty());
  EXPECT_EQ(result.issues.front().code, kScriptIssueUndeclaredGlobal);
  EXPECT_EQ(result.issues.front().symbol, "total");
}

TEST(ScriptSupportTest, RejectsNewGlobalCreatedInRun) {
  const char *script =
      "function init()\n"
      "  total = 0\n"
      "end\n"
      "function run()\n"
      "  total = total + 1\n"
      "  new_value = GetIntValue(1, \"FindNum\")\n"
      "  SetIntValue(\"sum\", total)\n"
      "end\n";

  ScriptAnalysisResult result = AnalyzeScript(script, 4096);

  EXPECT_FALSE(result.ok);
  ASSERT_FALSE(result.issues.empty());
  EXPECT_EQ(result.issues.front().code, kScriptIssueGlobalDeclaredOutsideInit);
  EXPECT_EQ(result.issues.front().symbol, "new_value");
}

TEST(ScriptSupportTest, RejectsDangerousApis) {
  const char *script =
      "function run()\n"
      "  os.execute(\"echo 1\")\n"
      "  SetIntValue(\"sum\", 1)\n"
      "end\n";

  ScriptAnalysisResult result = AnalyzeScript(script, 4096);

  EXPECT_FALSE(result.ok);
  ASSERT_FALSE(result.issues.empty());
  EXPECT_EQ(result.issues.front().code, kScriptIssueForbiddenApi);
  EXPECT_EQ(result.issues.front().symbol, "os");
}
