#include "script_host_api.hpp"

namespace mvsc {
namespace script {

namespace {

const int kRunOnly = kScriptLifecycleRun;

const ScriptHostApiDef kHostApis[] = {
    {"GetBoolValue", kScriptHostApiTopologyGetter, kScriptValueBool, 2, 2,
     kRunOnly},
    {"GetIntValue", kScriptHostApiTopologyGetter, kScriptValueInt, 2, 2,
     kRunOnly},
    {"GetFloatValue", kScriptHostApiTopologyGetter, kScriptValueFloat, 2, 2,
     kRunOnly},
    {"GetStringValue", kScriptHostApiTopologyGetter, kScriptValueString, 2, 2,
     kRunOnly},
    {"GetBoolArrayValue", kScriptHostApiTopologyGetter, kScriptValueBoolArray, 2,
     2, kRunOnly},
    {"GetIntArrayValue", kScriptHostApiTopologyGetter, kScriptValueIntArray, 2,
     2, kRunOnly},
    {"GetFloatArrayValue", kScriptHostApiTopologyGetter, kScriptValueFloatArray,
     2, 2, kRunOnly},
    {"GetStringArrayValue", kScriptHostApiTopologyGetter,
     kScriptValueStringArray, 2, 2, kRunOnly},

    {"SetBoolValue", kScriptHostApiOutputSetter, kScriptValueBool, 2, 2,
     kRunOnly},
    {"SetIntValue", kScriptHostApiOutputSetter, kScriptValueInt, 2, 2,
     kRunOnly},
    {"SetFloatValue", kScriptHostApiOutputSetter, kScriptValueFloat, 2, 2,
     kRunOnly},
    {"SetStringValue", kScriptHostApiOutputSetter, kScriptValueString, 2, 2,
     kRunOnly},
    {"SetBoolArrayValue", kScriptHostApiOutputSetter, kScriptValueBoolArray, 2,
     2, kRunOnly},
    {"SetIntArrayValue", kScriptHostApiOutputSetter, kScriptValueIntArray, 2, 2,
     kRunOnly},
    {"SetFloatArrayValue", kScriptHostApiOutputSetter, kScriptValueFloatArray, 2,
     2, kRunOnly},
    {"SetStringArrayValue", kScriptHostApiOutputSetter, kScriptValueStringArray,
     2, 2, kRunOnly},

    {"GetGlobalCommunicationData", kScriptHostApiContextGetter,
     kScriptValueString, 0, 0, kRunOnly},

    {"GetModuleBoolParam", kScriptHostApiReserved, kScriptValueBool, 0, 0,
     kRunOnly},
    {"GetModuleIntParam", kScriptHostApiReserved, kScriptValueInt, 0, 0,
     kRunOnly},
    {"GetModuleFloatParam", kScriptHostApiReserved, kScriptValueFloat, 0, 0,
     kRunOnly},
    {"GetModuleStringParam", kScriptHostApiReserved, kScriptValueString, 0, 0,
     kRunOnly},
    {"SetModuleBoolParam", kScriptHostApiReserved, kScriptValueBool, 0, 0,
     kRunOnly},
    {"SetModuleIntParam", kScriptHostApiReserved, kScriptValueInt, 0, 0,
     kRunOnly},
    {"SetModuleFloatParam", kScriptHostApiReserved, kScriptValueFloat, 0, 0,
     kRunOnly},
    {"SetModuleStringParam", kScriptHostApiReserved, kScriptValueString, 0, 0,
     kRunOnly},
};

} // namespace

const ScriptHostApiDef *GetScriptHostApis(std::size_t *count) {
  if (count != NULL) {
    *count = sizeof(kHostApis) / sizeof(kHostApis[0]);
  }
  return kHostApis;
}

const ScriptHostApiDef *FindScriptHostApi(const std::string &name) {
  std::size_t count = 0;
  const ScriptHostApiDef *apis = GetScriptHostApis(&count);
  for (std::size_t idx = 0; idx < count; ++idx) {
    if (name == apis[idx].name) {
      return &apis[idx];
    }
  }
  return NULL;
}

bool IsScriptHostApiName(const std::string &name) {
  return FindScriptHostApi(name) != NULL;
}

} // namespace script
} // namespace mvsc
