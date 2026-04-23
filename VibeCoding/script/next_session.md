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
- A dedicated `script_runtime_tests` target now exists under
  `source/fwk/manager/tests` and builds Lua 5.4.8 from
  `modules/lua/lua-5.4.8/src` for host-side runtime verification.
- `script_runtime_test.cpp` already covers:
  - cross-trigger state initialized in `init()`
  - string-array marshaling
  - `init()` runtime failure
  - output setter type mismatch handling
  - sparse int-array rejection at runtime
- `framework_opt_info.c` has been tightened so name-based metadata queries no
  longer expose static outputs for `dynamicpub` modules:
  - `VM_M_GetModuleSubPubList`
  - `VM_M_GetModulePubNum`
  - `VM_M_GetModuleSinglePubInfo`
- A dedicated `framework_opt_info_tests` target now verifies:
  - dynamic-publish modules hide stale static outputs by name
  - static-publish modules keep configured outputs
  - `ByID` APIs still return dynamic publish metadata
- Real upper-layer publish-source enumeration has also been traced:
  - `source/app/comif/communication_interface.cpp` uses
    `fwif_is_module_dynamic_pub()` to switch dynamic modules onto
    `fwif_get_mod_rslt_info_num()` / `fwif_get_mod_rslt_info()`
  - `source/fwk/interface/framework_if.c` forwards those calls to
    `scfw_get_module_rslt_info_num()` / `scfw_get_module_rslt_info()`
  - `source/fwk/manager/src/framework_sub.c` fills the runtime output table
    from `VM_M_GetModuleDynamicSinglePublishParam()` in
    `vm_module_dynamic_pub_init()`

## What To Continue
- Extend runtime tests if needed for:
  - more array element types
  - explicit `cleanup()` failure behavior
  - broader `run()` runtime error cases beyond current setter mismatch and
    sparse-array validation
- Add automated coverage for the comif/adapter publish-source path if that
  layer needs stronger regression protection.
- Decide whether native module-build verification must be restored, or whether
  the host-side harness remains the accepted verifier for this phase.

## Known Gaps
- `make -C source/algos/modules/script` is still blocked by repository-local build/include issues unrelated to the new logic.
- Future `GetModuleXxxParam` / `SetModuleXxxParam` APIs are still design-only and must not be mixed into the current baseline landing.
- Shipped template/config alignment is landed, and the old name-based static
  publish view has been trimmed.
- The remaining upper-layer gap is test coverage for the comif/adapter path,
  not an identified routing bug in publish-source enumeration.
- Remaining runtime coverage gaps are concentrated in `cleanup()` failure,
  malformed arrays beyond the current sparse-int case, and resource-limit paths.

## Verification Snapshot
- `framework_tests` passes.
- `ScriptSupportTest.*` passes.
- `script_runtime_tests` passes.
- `framework_opt_info_tests` passes.

## What Not To Do
- Do not preserve `run(inputs, outputs, ctx)` as the baseline ABI.
- Do not keep `ctx` as the formal state mechanism.
- Do not require users to maintain `script_input0..31`.
- Do not allow undeclared globals to become hidden state.
- Do not reintroduce the old per-trigger Lua state creation path.
- Do not implement module-parameter writes opportunistically without first stabilizing the current baseline ABI.
