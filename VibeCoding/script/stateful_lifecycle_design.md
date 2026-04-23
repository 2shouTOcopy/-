# Script Operator Script-Driven Topology Design

## Summary
- This document defines the new script-operator baseline after dropping the `inputs/outputs + ctx` ABI.
- Scripts now declare topology through typed helper APIs in `run()`.
- Published outputs are also declared through script calls in `run()`.
- Cross-trigger state is carried by controlled global variables declared in `init()`.

## Baseline ABI
- Required entry:
  - `function run()`
- Optional entries:
  - `function init()`
  - `function cleanup()`

## Exposed Script APIs

### Module Result Read APIs
- `GetBoolValue(moduleNo, paramName)`
- `GetIntValue(moduleNo, paramName)`
- `GetFloatValue(moduleNo, paramName)`
- `GetStringValue(moduleNo, paramName)`
- `GetBoolArrayValue(moduleNo, paramName)`
- `GetIntArrayValue(moduleNo, paramName)`
- `GetFloatArrayValue(moduleNo, paramName)`
- `GetStringArrayValue(moduleNo, paramName)`

### Script Output APIs
- `SetBoolValue(paramName, value)`
- `SetIntValue(paramName, value)`
- `SetFloatValue(paramName, value)`
- `SetStringValue(paramName, value)`
- `SetBoolArrayValue(paramName, value)`
- `SetIntArrayValue(paramName, value)`
- `SetFloatArrayValue(paramName, value)`
- `SetStringArrayValue(paramName, value)`

### Reserved Future Parameter APIs
- `GetModuleBoolParam(moduleNo, paramName)`
- `GetModuleIntParam(moduleNo, paramName)`
- `GetModuleFloatParam(moduleNo, paramName)`
- `GetModuleStringParam(moduleNo, paramName)`
- `SetModuleBoolParam(moduleNo, paramName, value)`
- `SetModuleIntParam(moduleNo, paramName, value)`
- `SetModuleFloatParam(moduleNo, paramName, value)`
- `SetModuleStringParam(moduleNo, paramName, value)`
- Equivalent array forms may be added later.
- This namespace is reserved now so future expansion does not collide with the value/output ABI.

## Design Goals
- Let users author scripts using the same concepts they already know: module number plus parameter name.
- Make script generation easier for LLMs by giving them a short, repetitive, strongly typed API set.
- Generate subscription and publish structure automatically from script text.
- Keep dependency analysis static enough to support precompile validation.
- Allow controlled cross-trigger accumulation without reintroducing hidden state models.

## Topology Extraction Model
- `run()` is the only function that contributes to automatic topology extraction.
- Every `GetXxxValue(moduleNo, paramName)` call in `run()` declares a typed dependency on an upstream module result.
- Every `SetXxxValue(paramName, value)` call in `run()` declares a typed published output of the script operator.
- Repeated identical getter/setter declarations are deduplicated at the metadata level.
- `init()` and `cleanup()` do not add subscriptions or published outputs.

## Static Extraction Constraints
- `moduleNo` must be a static integer literal.
- `paramName` must be a static string literal.
- Output name in `SetXxxValue()` must be a static string literal.
- Dynamic expressions are not valid for dependency extraction.
- Examples that must be rejected:
  - `GetIntValue(id, "FindNum")`
  - `GetIntValue(1, name)`
  - `SetIntValue(output_name, value)`
  - `GetIntValue(1 + 1, "FindNum")`

## Type Consistency Rules
- The same upstream `(moduleNo, paramName)` must not be read through multiple typed getters.
- The same output name must not be written through multiple typed setters.
- A global declared in `init()` has a single stable type after initialization.
- V1 supported global and output types are:
  - `bool`
  - `int`
  - `float`
  - `string`
  - `bool[]`
  - `int[]`
  - `float[]`
  - `string[]`

## State Model
- The active script owns a persistent Lua environment.
- `init()` runs once after activation and declares all legal global variables.
- Globals declared in `init()` may be read and updated in `run()` and `cleanup()`.
- `run()` and `cleanup()` must not create new global variables.
- Globals are the only supported cross-trigger state mechanism.
- Persistence through ad-hoc undeclared globals or dynamic `_G` writes is not supported.

## Global Declaration Rules
- Globals are considered declared only if they are first assigned in `init()`.
- `run()` and `cleanup()` may access only globals declared in `init()`.
- Global names must not collide with:
  - lifecycle names: `init`, `run`, `cleanup`
  - exposed host API names
  - dangerous sandbox escape names

## Array Model
- Arrays are exchanged as Lua tables.
- Getter APIs return Lua tables for array values.
- Setter APIs require Lua tables for array values.
- Valid V1 arrays must be contiguous, homogeneous, and one-dimensional.
- Nested tables, sparse tables, and mixed-type tables are rejected.

## Lifecycle Semantics
- Precompile validates syntax, extracts metadata, and records global declarations.
- Activation creates the Lua environment and binds host APIs.
- `init()` executes once after activation if present.
- `run()` executes once per trigger.
- `cleanup()` executes before script replacement or module destruction if present.
- Activation failure due to `init()` error places the script in a not-ready state.

## Error Ownership Boundary
- Script precompile is responsible for:
  - syntax validation
  - API shape validation
  - static getter/setter extraction
  - global declaration validation
  - static type-conflict detection
- Upper topology-management layers are responsible for:
  - checking that referenced module numbers exist
  - checking that referenced parameter names exist
  - checking that script-declared types match upstream actual types
  - assembling final subscribe/publish structures

## Example
```lua
function init()
    total = 0
    history = {}
end

function run()
    local count = GetIntValue(1, "FindNum")
    local codes = GetStringArrayValue(2, "CodeList")

    total = total + count
    history = codes

    SetIntValue("sum", total)
    SetStringArrayValue("codes", history)
end

function cleanup()
    -- optional finalization hook
end
```

## Migration Rule
- This design supersedes the previous `run(inputs, outputs, ctx)` baseline.
- Documentation, templates, parser rules, runtime hosting, and tests must all align to this script-driven topology model.
