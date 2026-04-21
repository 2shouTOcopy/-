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

## Current State
- Parser/analyzer and runtime baseline have both been partially landed in code.
- The checked-in code now reflects the new lifecycle ABI and typed helper API direction.
- Runtime currently reads subscribed values through host dynamic getters and writes outputs directly to `ScFrame`.
- Dynamic publish metadata now only includes script-declared outputs from analysis results.

## Remaining Implementation Tasks
- Add or enable a runtime-test harness that can link Lua in the local/dev test environment.
- Review whether static outputs from module metadata still need to coexist with script-derived outputs, and if not, remove the remaining assumptions cleanly.
- Add focused runtime/integration tests around:
  - persistent `init()` state across frames
  - array getter/setter marshaling
  - runtime error handling in `init()` / `run()` / `cleanup()`
  - output type mismatch handling
- Confirm upper-layer subscription wiring uses the new script-derived metadata as intended.
- Reserve extension points for future `GetModuleXxxParam` / `SetModuleXxxParam` APIs without implementing them yet.

## Verification Completed
- `cmake --build source/fwk/manager/tests/build --target framework_tests`
- `./source/fwk/manager/tests/build/framework_tests '--gtest_filter=ScriptSupportTest.*'`
- `./source/fwk/manager/tests/build/framework_tests`
- Targeted syntax verification for `script.cpp` with a manual `clang++ -fsyntax-only` command and real include paths

## Verification Result
- `framework_tests` passed, including all `ScriptSupportTest.*` cases.
- The targeted syntax check for `script.cpp` passed after the runtime refactor.
- Direct module build via `make -C source/algos/modules/script` is still blocked by local make/include environment issues and existing external header/toolchain assumptions, so it is not a reliable final verifier yet.
- Local runtime-unit-test bring-up is currently blocked because this workspace does not expose the Lua library path expected by the module build (`-llua` link check fails, and the referenced local `libs` / `modules/lua` paths are absent here).

## Engineering Guidance For Next Session
- Treat the new parser/analyzer result as the source of truth for runtime metadata.
- Keep upper-layer topology validation out of the script runtime.
- Avoid reintroducing any compatibility logic around `inputs/outputs` or `script_input0..31`.
- Keep future module-parameter APIs reserved-only until the baseline lands cleanly.

## Recommended Next Steps
1. Add or repair a test/build path that can link Lua for script runtime tests.
2. Add runtime-focused tests for persistent state, arrays, and error paths.
3. Verify and, if needed, simplify remaining publish metadata assumptions around built-in/static outputs.
4. Validate end-to-end upper-layer subscription/publication wiring against script-derived metadata.
5. Start a separate implementation phase for future module-parameter APIs only after the current baseline is stable.
