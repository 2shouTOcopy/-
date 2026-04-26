#pragma once

#include <cstddef>
#include <string>

#include "script_support.hpp"

namespace mvsc {
namespace script {

enum ScriptHostApiKind {
  kScriptHostApiTopologyGetter = 0,
  kScriptHostApiOutputSetter,
  kScriptHostApiContextGetter,
  kScriptHostApiReserved,
};

enum ScriptLifecycleMask {
  kScriptLifecycleInit = 1 << 0,
  kScriptLifecycleRun = 1 << 1,
  kScriptLifecycleCleanup = 1 << 2,
};

struct ScriptHostApiDef {
  const char *name;
  ScriptHostApiKind kind;
  ScriptValueType value_type;
  int min_args;
  int max_args;
  int lifecycle_mask;
};

const ScriptHostApiDef *GetScriptHostApis(std::size_t *count);
const ScriptHostApiDef *FindScriptHostApi(const std::string &name);
bool IsScriptHostApiName(const std::string &name);

} // namespace script
} // namespace mvsc
