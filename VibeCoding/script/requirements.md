# Script Operator Requirements

## Status
- This directory previously contained a years-old custom implementation.
- That legacy implementation is **not** the design baseline for the new script operator.
- Future work should treat the legacy behavior as reference-only for integration constraints, not as the source of truth for architecture or interfaces.

## Product Goal
- Add a lightweight script operator for embedded platforms.
- Let users perform light logic, transformation, calculation, and composition on upstream module outputs.
- Keep the operator deterministic, bounded, sandboxed, and diagnosable.

## Scope
- Script language: Lua 5.3
- V1 data types: `bool`, `int`, `float`, `string`
- V1 execution model: one trigger, one script execution
- V1 state model: stateless across triggers
- V1 script entry: `run(inputs, outputs)`

## Explicit Non-Goals
- No file access
- No system command execution
- No network access
- No dynamic loading
- No image object / userdata exposure
- No arrays / structs / nested objects in V1
- No long-running or compute-heavy logic

## Input/Output Model
- Inputs are configured explicitly in UI and bound to upstream published results.
- Outputs are configured explicitly in UI and declared with static types.
- Variable naming rule: `[A-Za-z_][A-Za-z0-9_]*`, max length `32`
- Quantity limit: input `<= 32`, output `<= 32`
- String length target: `<= 256B` per value in V1

## Recommended Lua Interface
```lua
function run(inputs, outputs)
    outputs.result = inputs.value + 1
end
```

### Decision
- Use `inputs.xxx` and `outputs.xxx`
- Do **not** expose configured variables as direct global names

### Why
- Avoids global namespace pollution
- Makes static validation feasible
- Makes missing/unknown variable errors much easier to report
- Prevents accidental state leakage through globals
- Leaves room for future `ctx` / lifecycle extension without breaking user scripts

## Compile / Validation Rules
- Save triggers validation and compile attempt automatically
- Any script text change, input/output definition change, or binding change requires recompile
- Validation covers:
- Variable naming and duplicate checks
- Static symbol checks for `inputs.xxx` and `outputs.xxx`
- Dangerous API checks
- Lua syntax compile
- Required `run(inputs, outputs)` presence

## Runtime Rules
- If config is invalid, runtime fails fast
- If required input is missing at trigger time, runtime fails
- If output is missing or type mismatched, runtime fails
- Script failure marks operator NG and reports a reason

## Safety Constraints
- Sandbox Lua standard library
- Allow only minimal safe subset such as `math`, `string`, `table`, `assert`, `pairs`, `ipairs`, `tonumber`, `tostring`, `type`
- Forbid `os`, `io`, `package`, `require`, `dofile`, `load*`, `debug`, raw metatable manipulation
- Enforce:
- max script length
- max memory
- max instruction steps / timeout

## Error Observability
- Report compile/runtime error message
- Report line number when available
- Report offending variable name when available
- Publish status output and human-readable message for diagnosis
