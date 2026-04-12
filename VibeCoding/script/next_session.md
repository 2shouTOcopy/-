# Next Session Handoff

## Read Order
1. `requirements.md`
2. `implementation_plan.md`
3. `progress.md`
4. `test_cases.md`

## Critical Decision
- Use `inputs.xxx` and `outputs.xxx`
- Do not expose configured values as direct global variable names

## Critical Constraint
- The old script operator code in this area is legacy.
- New work should be a redesign, not a compatibility-preserving extension of the old custom implementation.

## What To Continue
- Finish aligning runtime implementation with the redesigned requirements
- Move remaining validation / integration behavior out of legacy assumptions
- Add operator-local tests or script-local verification assets as needed

## What Not To Do
- Do not reintroduce unrestricted `luaL_openlibs` execution semantics
- Do not revive `user_function(in0, in1, ...)`
- Do not use direct global variable exposure as the primary script ABI
