# Script Operator Progress

## Intent
- Preserve enough context in-tree so a new conversation can continue without relying on chat history.

## Direction Locked
- Treat old script operator implementation as legacy.
- The new baseline ABI is script-driven:
  - `init()` optional
  - `run()` required
  - `cleanup()` optional
- `run()` uses typed helper APIs instead of `inputs/outputs`.
- Dependency topology is derived from getter calls in script text.
- Published outputs are derived from setter calls in script text.
- Cross-trigger state comes from globals declared in `init()`.
- Arrays are part of the new baseline for all four primitive element types.

## Work Completed In This Session
- Replaced the old design documentation that assumed `run(inputs, outputs, ctx)`.
- Wrote the new baseline requirements for:
  - typed getter APIs
  - typed setter APIs
  - script-driven topology extraction
  - `init()`-declared global state
  - array support
  - future reserved module-parameter APIs
- Rewrote `implementation_plan.md`, `test_cases.md`, `next_session.md`, and this progress file to align to the new design.
- Updated `script_support.hpp/.cpp` to the new parser/analyzer model:
  - parse `init()` / `run()` / `cleanup()`
  - extract subscriptions from `GetXxxValue(moduleNo, paramName)`
  - extract outputs from `SetXxxValue(paramName, value)`
  - support scalar and array types for `bool/int/float/string`
  - reject missing `run()`, dynamic module numbers, dynamic names, type conflicts, dangerous APIs, and undeclared globals
- Updated `script_support_test.cpp` to cover the new baseline ABI and static rules.
- Reworked `script.h/.cpp` runtime hosting:
  - removed old `inputs/outputs` execution path
  - switched to persistent Lua runtime per active script version
  - execute `init()` once after successful compile
  - execute `run()` on each trigger
  - execute `cleanup()` during runtime teardown
  - bind typed getter/setter APIs into the Lua sandbox
  - marshal arrays as Lua tables on both read and write paths
  - derive dynamic subscribe/publish metadata from script analysis results
- Removed legacy runtime-only helpers tied to the old ABI:
  - old `ReadInputValue`
  - old output-table execution model
  - old `Analyse` / `UpdateDynamicSubInfo` path
  - old compiled-chunk-per-trigger execution logic
- Updated shipped script assets to the new ABI baseline:
  - `user_function.lua` now demonstrates `init()` / `run()` / `cleanup()`
  - `algo_private.json` no longer declares legacy `InputList` / `OutputList`
  - `pm_conf.json` no longer exposes `script_input0..31`
  - `script.xml` no longer exposes `script_input0..31` features or registers
- Added a dedicated runtime-test harness for `script.cpp` in
  `source/fwk/manager/tests`:
  - `script_runtime_tests` compiles `script.cpp` and `script_support.cpp`
    against test-only VM/algo/osal mocks
  - the harness builds Lua 5.4.8 directly from
    `modules/lua/lua-5.4.8/src` in the local host environment
  - `script_runtime_test.cpp` covers persistent `init()` state, string-array
    marshaling, `init()` runtime failure, setter type mismatch rejection, and
    sparse int-array rejection
- Extended `script_runtime_test.cpp` coverage for the remaining runtime gaps:
  - bool/int/float array marshaling in addition to string arrays
  - numeric values rejected by string setters instead of being Lua-coerced
  - malformed and mixed-type array setter rejection without partial publication
  - `run()` runtime error reporting
  - missing subscribed input reporting
  - `cleanup()` failure still allowing runtime teardown
  - `run()` timeout reporting
  - Lua memory-limit error reporting
- Tightened runtime string output validation in `script.cpp`:
  - `SetStringValue()` now requires an actual Lua string, not a Lua number that
    can be coerced to a string
  - array setters now validate the entire table before writing output values, so
    malformed arrays cannot leave partially published results
- Added a shared Host API registry for future Lua API expansion:
  - `script_host_api.hpp/.cpp` define each exposed API name, kind, value type,
    arity, and lifecycle mask
  - `script_support.cpp` now uses the registry for static API validation and
    topology/publish extraction instead of keeping a private duplicate list
  - `script.cpp` now binds Lua host APIs by iterating the same registry
  - context getter APIs are separated from topology getters and output setters
- Documented the runtime implementation path for topology getter APIs such as
  `GetBoolValue()` / `GetIntValue()`:
  - API definition lives in `script_host_api.cpp` as
    `kScriptHostApiTopologyGetter`
  - `CScriptModule::InstallHostApis()` binds the registry entry into the Lua
    sandbox
  - `luaHostApiBridge()` dispatches topology getters to
    `CScriptModule::LuaGetValue()`
  - `LuaGetValue()` validates the call against parsed subscription metadata,
    calls `ReadSubscriptionValue()`, then pushes the typed Lua return value
  - `ReadSubscriptionValue()` reads host data through the existing dynamic VM
    getter APIs; bool values currently share the int dynamic path
    `VM_M_GetInt_Dynamic()` and are converted to Lua booleans by nonzero/zero
- Added `GetGlobalCommunicationData()` as the first context getter example:
  - static analysis accepts the zero-argument API without generating
    subscription or publish metadata
  - wrong arity is rejected during analysis
  - runtime calls a weak C provider named `Script_GetGlobalCommunicationData`
    and returns a clear Lua runtime error if the provider is not linked
- Added focused `framework_opt_info` coverage in
  `source/fwk/manager/tests/framework_opt_info_test.cpp` and cleaned up the
  remaining name-based static publish view:
  - `dynamicpub` modules no longer expose stale static outputs through
    `VM_M_GetModuleSubPubList` / `VM_M_GetModulePubNum` /
    `VM_M_GetModuleSinglePubInfo`
  - legacy name-based queries still preserve static inputs
  - `ByID` query APIs remain the path that exposes dynamic subscribe/publish
    metadata to runtime callers
- Confirmed the real upper-layer publish-source query path already consumes
  dynamic publish metadata instead of static `io_conf.json` outputs:
  - `comif_get_subscribe_source()` switches on
    `fwif_is_module_dynamic_pub()` and reads outputs through
    `fwif_get_mod_rslt_info_num()` / `fwif_get_mod_rslt_info()`
  - those framework-if wrappers forward to `scfw_get_module_rslt_info_num()` /
    `scfw_get_module_rslt_info()`
  - runtime-side output descriptors are populated from
    `VM_M_GetModuleDynamicSinglePublishParam()` in
    `vm_module_dynamic_pub_init()`

## Current State
- Parser/analyzer and runtime baseline have landed in code and are covered by
  host-side regression tests.
- The checked-in code now reflects the new lifecycle ABI and typed helper API
  direction.
- Runtime currently reads subscribed values through host dynamic getters and writes outputs directly to `ScFrame`.
- Dynamic publish metadata now only includes script-declared outputs from analysis results.
- Host API extension now has a single registry path shared by static analysis
  and runtime binding.
- Dedicated runtime unit coverage now exists for the core persistent-Lua execution path.
- The legacy name-based framework metadata query path has been tightened so
  dynamic-publish modules no longer leak `io_conf.json` outputs.
- Real upper-layer subscribe-source enumeration already uses dynamic publish
  runtime metadata for dynamic modules; the remaining gap there is automated
  coverage for the comif/adapter path rather than a known functional mismatch.

## Remaining Implementation Tasks
- Extend runtime/integration tests further only if new edge cases are identified
  beyond the current scalar/array/error/resource-limit coverage.
- Add focused automated coverage, if practical, for the comif/adapter publish
  source path that already switches dynamic modules onto runtime result
  metadata.
- Unblock or replace the remaining native module build verification path if
  `make -C source/algos/modules/script` must become part of the final signoff.
- Reserve extension points for future `GetModuleXxxParam` / `SetModuleXxxParam` APIs without implementing them yet.

## Verification Completed
- `cmake --build source/fwk/manager/tests/build --target framework_tests`
- `cmake --build source/fwk/manager/tests/build --target script_runtime_tests`
- `cmake --build source/fwk/manager/tests/build --target framework_opt_info_tests`
- `./source/fwk/manager/tests/build/framework_tests '--gtest_filter=ScriptSupportTest.*'`
- `./source/fwk/manager/tests/build/framework_opt_info_tests '--gtest_filter=FrameworkOptInfoTest.*'`
- `./source/fwk/manager/tests/build/script_runtime_tests`

## Verification Result
- `framework_tests` passed, including all `ScriptSupportTest.*` cases.
- `framework_opt_info_tests` passed, including:
  - `DynamicPublishModulesDoNotExposeStaticOutputsByName`
  - `StaticPublishModulesKeepConfiguredOutputsByName`
  - `ByIdApisReturnDynamicOutputs`
- `script_runtime_tests` passed:
  - `ProcessKeepsInitStateAcrossTriggers`
  - `ProcessMarshalsStringArraysBetweenHostAndLua`
  - `ProcessMarshalsBoolIntAndFloatArraysBetweenHostAndLua`
  - `ProcessReadsGlobalCommunicationData`
  - `CompileScriptFailsWhenInitRaisesRuntimeError`
  - `ProcessRejectsSetterTypeMismatchAtRuntime`
  - `ProcessRejectsNumericValueForStringSetterAtRuntime`
  - `ProcessRejectsRunRuntimeError`
  - `ProcessRejectsRunTimeout`
  - `ProcessRejectsMemoryLimitExceeded`
  - `ProcessRejectsMissingSubscribedValueAtRuntime`
  - `ProcessRejectsSparseIntArrayTablesAtRuntime`
  - `ProcessRejectsMalformedArrayTablesAtRuntime`
  - `ProcessRejectsMixedTypeStringArrayTablesAtRuntime`
  - `CleanupFailureStillDestroysRuntime`
- Direct module build via `make -C source/algos/modules/script` is still blocked
  by existing native build environment issues, so it is not a reliable final
  verifier yet:
  - `ScFrame.h` trips `-Werror,-Wreturn-type` in two existing non-void helper
    paths
  - `mvb_types_ex.h` includes missing external header `mvb_types.h`

## Engineering Guidance For Next Session
- Treat the new parser/analyzer result as the source of truth for runtime metadata.
- Add future Lua APIs through `script_host_api.hpp/.cpp` first, then implement
  only the API-specific runtime handler needed for that API kind.
- Keep upper-layer topology validation out of the script runtime.
- Avoid reintroducing any compatibility logic around `inputs/outputs` or `script_input0..31`.
- Keep future module-parameter APIs reserved-only until the baseline lands cleanly.
- If you need to revisit upper-layer publish-source behavior, start at:
  - `source/app/comif/communication_interface.cpp`
  - `source/fwk/interface/framework_if.c`
  - `source/fwk/manager/src/framework_sub.c`

## Recommended Next Steps
1. Add focused automated coverage for the comif/adapter publish-source query path if that layer will be actively evolved.
2. Decide whether to repair native `make -C source/algos/modules/script` verification or accept the host-side harness for this phase.
3. Start a separate implementation phase for future module-parameter APIs only after the current baseline is stable.
