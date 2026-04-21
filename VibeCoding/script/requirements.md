# Script Operator Requirements

## Status
- This directory previously contained a years-old custom implementation.
- That legacy implementation is not the design baseline for the new script operator.
- The current code still reflects older `inputs/outputs` assumptions in places.
- This document defines the new baseline requirement set that implementation must converge to.

## Product Goal
- Add a lightweight Lua script operator for embedded platforms.
- Make script authoring easier for users and for LLM-generated scripts by exposing direct, typed helper APIs instead of `inputs.xxx` / `outputs.xxx`.
- Let the script text itself declare runtime dependencies and published outputs.
- Keep execution deterministic, bounded, sandboxed, and diagnosable.

## Scope
- Script language: Lua 5.3
- Lifecycle ABI:
  - `init()` optional
  - `run()` required
  - `cleanup()` optional
- Trigger model:
  - one trigger, one `run()` execution
- State model:
  - Lua runtime environment is persistent for the active script version
  - global variables declared in `init()` may persist across triggers
- Supported scalar data types:
  - `bool`
  - `int`
  - `float`
  - `string`
- Supported array data types:
  - `bool[]`
  - `int[]`
  - `float[]`
  - `string[]`

## Explicit Non-Goals
- No file access
- No system command execution
- No network access
- No dynamic loading
- No image object / userdata exposure
- No nested arrays or object graphs in V1
- No unrestricted global variable creation outside `init()`
- No dynamic topology inference at runtime
- No manual `script_input0..31` subscription model as the formal baseline
- No implementation of module-parameter write APIs in this version

## Script Interface Baseline

### Read Module Result Values
- `GetBoolValue(moduleNo, paramName)`
- `GetIntValue(moduleNo, paramName)`
- `GetFloatValue(moduleNo, paramName)`
- `GetStringValue(moduleNo, paramName)`
- `GetBoolArrayValue(moduleNo, paramName)`
- `GetIntArrayValue(moduleNo, paramName)`
- `GetFloatArrayValue(moduleNo, paramName)`
- `GetStringArrayValue(moduleNo, paramName)`

### Publish Script Outputs
- `SetBoolValue(paramName, value)`
- `SetIntValue(paramName, value)`
- `SetFloatValue(paramName, value)`
- `SetStringValue(paramName, value)`
- `SetBoolArrayValue(paramName, value)`
- `SetIntArrayValue(paramName, value)`
- `SetFloatArrayValue(paramName, value)`
- `SetStringArrayValue(paramName, value)`

### Reserved Future Extension
- `GetModuleXxxParam(moduleNo, paramName)`
- `SetModuleXxxParam(moduleNo, paramName, value)`
- These names are reserved in V1 so the ABI can later grow without renaming the existing value/output functions.
- Future parameter-setting APIs are allowed to execute inside `run()`, but they are not part of the current implementation target.

## Recommended Lua Interface
```lua
function init()
    total = 0
    last_codes = {}
end

function run()
    local count = GetIntValue(1, "FindNum")
    local codes = GetStringArrayValue(2, "CodeList")

    total = total + count
    last_codes = codes

    SetIntValue("sum", total)
    SetStringArrayValue("codes", last_codes)
end
```

## Key Decisions
- `run()` has no `inputs` / `outputs` arguments.
- Scripts use direct typed helper functions instead of `inputs.xxx` / `outputs.xxx`.
- Dependency topology is derived from `GetXxxValue(...)` calls in script text.
- Output publish definitions are derived from `SetXxxValue(...)` calls in script text.
- Cross-trigger state is carried only by global variables declared in `init()`.
- Arrays are represented as Lua tables.

## Why This ABI
- Easier for users to write by hand because they only need module numbers, parameter names, and a small set of helper APIs.
- Easier for LLMs to generate reliably because the interface surface is explicit and repetitive.
- Removes the need for users to manually map `in0/out0` style variables.
- Keeps topology declaration static enough for precompile analysis.
- Preserves a path for future side-effect APIs such as module-parameter reads and writes.

## Precompile / Validation Rules
- Save triggers validation and a precompile attempt automatically.
- Any script text change requires re-parse and re-precompile.
- Validation covers:
  - Lua syntax compile
  - required `run()` presence
  - allowed lifecycle function names only: `init`, `run`, `cleanup`
  - allowed host API usage only
  - forbidden API usage
  - static extraction of subscriptions from `GetXxxValue(moduleNo, paramName)`
  - static extraction of published outputs from `SetXxxValue(paramName, value)`
  - global variable declarations originating only from `init()`
  - type consistency for repeated input/output/global usage
- For `GetXxxValue(...)`:
  - `moduleNo` must be a static integer constant
  - `paramName` must be a static string constant
  - the same `(moduleNo, paramName)` must not be read through multiple type APIs
- For `SetXxxValue(...)`:
  - `paramName` must be a static string constant
  - the same output name must not be written through multiple type APIs
- For globals:
  - a global used in `run()` or `cleanup()` must be declared in `init()`
  - `run()` and `cleanup()` must not create new globals
  - global names must not collide with reserved lifecycle names or exposed host API names

## Topology Generation Rules
- The script operator precompile stage is responsible for producing:
  - a subscription list
  - a publish list
- The precompile stage does not decide whether a module number or parameter name is actually valid in the current top-level topology.
- Upper layers consume the parsed lists and decide:
  - whether module numbers exist
  - whether parameter names exist
  - whether declared types match actual upstream parameter types
  - whether the assembled topology is valid

## Runtime Rules
- The active script version owns a persistent Lua environment.
- `init()` executes once after a script version is compiled, activated, and its environment is created.
- `run()` executes once per trigger.
- `cleanup()` executes before environment teardown when the script is replaced or the module is destroyed.
- If `init()` fails:
  - the script enters a not-ready state
  - later triggers fail fast
  - the host does not retry `init()` on every trigger
  - recovery happens only after a fresh script activation
- If a required upstream value is unavailable at trigger time, `run()` fails.
- If a setter receives the wrong value type at runtime, `run()` fails.
- If an array setter receives a non-table or a mixed-type table, `run()` fails.
- `cleanup()` failure is reported but does not block environment teardown.

## Array Rules
- Arrays are plain Lua tables.
- V1 arrays must be:
  - homogeneous
  - contiguous
  - one-dimensional
- Sparse tables, nested tables, and mixed scalar types are invalid as array outputs.

## Safety Constraints
- Sandbox Lua standard library.
- Allow only a minimal safe subset such as `math`, `string`, `table`, `assert`, `pairs`, `ipairs`, `tonumber`, `tostring`, `type`.
- Forbid `os`, `io`, `package`, `require`, `dofile`, `load*`, `debug`, raw metatable manipulation, and dynamic code-loading APIs.
- Enforce:
  - max script length
  - max memory
  - max instruction steps / timeout

## Error Observability
- Report precompile and runtime error message.
- Report line number when available.
- Report offending function, output name, module number, or parameter name when available.
- Report lifecycle phase when available: `init`, `run`, `cleanup`.
- Expose parsed subscription list and publish list for diagnostics.
