# Next Session Handoff

## Read Order
1. `requirements.md`
2. `stateful_lifecycle_design.md`
3. `implementation_plan.md`
4. `test_cases.md`
5. `progress.md`

## Critical Decisions
- The baseline ABI is:
  - `init()` optional
  - `run()` required
  - `cleanup()` optional
- `run()` no longer uses `inputs`, `outputs`, or `ctx`.
- Scripts declare dependencies through:
  - `GetXxxValue(moduleNo, paramName)`
- Scripts declare published outputs through:
  - `SetXxxValue(paramName, value)`
- Arrays are supported for all four element types and are represented as Lua tables.
- Cross-trigger state is carried by globals declared in `init()`.
- `run()` and `cleanup()` must not create new globals.
- Future module-parameter APIs are reserved but not part of the current implementation target.

## Critical Constraint
- The old script operator code in this area is legacy.
- New work should be a redesign, not a compatibility-preserving extension of the `inputs/outputs + script_input0..31` model.
- Upper layers remain responsible for validating actual topology existence and type compatibility.

## What Already Landed
- `script_support.*` has been switched to the new lifecycle-aware parser/analyzer model.
- `script_support_test.cpp` covers the new static rules and passes.
- `script.cpp/.h` has been refactored to:
  - host a persistent Lua runtime
  - bind typed getter/setter APIs
  - execute `init()` / `run()` / `cleanup()`
  - read subscriptions dynamically from host APIs
  - write scalar and array outputs directly to `ScFrame`
- Old `run(inputs, outputs)` execution logic and related helper paths have been removed from `script.cpp`.
- Shipped script assets are aligned to the new ABI:
  - `user_function.lua` uses `init()` / `run()` / `cleanup()`
  - `algo_private.json` no longer carries legacy `InputList` / `OutputList`
  - `pm_conf.json` / `script.xml` no longer expose `script_input0..31`

## What To Continue
- Add or restore a runtime-test path that can link Lua in this environment.
- Add runtime/integration tests for:
  - cross-trigger global state initialized in `init()`
  - array marshaling
  - runtime failures in lifecycle functions
  - output setter type validation
- Verify upper-layer wiring consumes script-derived subscribe/publish metadata correctly.
- Revisit whether any remaining static output metadata assumptions should be removed.

## Known Gaps
- No dedicated runtime unit tests exist yet for the new persistent Lua execution path.
- `make -C source/algos/modules/script` is still blocked by repository-local build/include issues unrelated to the new logic.
- Local runtime-test bring-up is additionally blocked in this workspace because the Lua library path referenced by the script module build is not present here.
- Future `GetModuleXxxParam` / `SetModuleXxxParam` APIs are still design-only and must not be mixed into the current baseline landing.
- Shipped template/config alignment is landed, but upper-layer consumption of the slimmer config surface still needs end-to-end validation.

## Verification Snapshot
- `framework_tests` passes.
- `ScriptSupportTest.*` passes.
- Targeted `script.cpp` syntax check passes with real include paths.

## What Not To Do
- Do not preserve `run(inputs, outputs, ctx)` as the baseline ABI.
- Do not keep `ctx` as the formal state mechanism.
- Do not require users to maintain `script_input0..31`.
- Do not allow undeclared globals to become hidden state.
- Do not reintroduce the old per-trigger Lua state creation path.
- Do not implement module-parameter writes opportunistically without first stabilizing the current baseline ABI.
