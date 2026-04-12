# Script Operator Progress

## Intent
- Preserve enough context in-tree so a new conversation can continue without relying on chat history.

## Direction Locked
- Treat old script operator implementation as legacy
- Redesign the operator around a new contract
- Preferred script ABI is `run(inputs, outputs)`
- V1 is stateless and scalar-only

## Work Completed In This Session
- Added static validation helper module:
- `script_support.hpp`
- `script_support.cpp`
- Added unit tests in framework test target for:
- invalid variable names
- duplicate names
- unknown input/output references
- dynamic indexing rejection
- dangerous API rejection
- safe-script acceptance
- Reworked default Lua template toward the new ABI
- Added this handoff documentation set under `source/algos/modules/script/`

## Verification Completed
- Built `framework_tests`
- Ran `./framework_tests`
- Result at end of session: `109` tests passed

## Important Caveat
- Native `make -C source/algos/modules/script` was not fully verifiable in this workspace because the current repo/toolchain wiring for that module is incomplete or environment-sensitive.
- Observed blockers included:
- legacy include path / platform parameter dependence
- existing unrelated warnings promoted to errors in external headers

## Engineering Guidance For Next Session
- Continue using `script_support.*` as the clean core
- Do not spend effort preserving the old `user_function(in0, in1, ...)` behavior
- Prefer replacing legacy assumptions rather than wrapping them
- Reconcile runtime/operator code with config metadata under `source/algos/so/script/json/`

## Recommended Next Steps
1. Normalize all config metadata to the new ABI and failure model.
2. Add operator-level tests closer to config parsing and execution.
3. Clean up or replace remaining legacy runtime code paths that still assume old behavior.
4. Add explicit compile-status outputs/query fields for UI integration.
