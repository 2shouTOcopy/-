# Script Operator Test Cases

## Precompile Validation Cases
- Reject script without `run()`
- Accept script with only `run()` and no globals
- Accept script with `init()` and `run()`
- Accept script with `cleanup()` present
- Reject dynamic module number in `GetXxxValue(moduleNo, paramName)`
- Reject dynamic input parameter name in `GetXxxValue(...)`
- Reject dynamic output name in `SetXxxValue(...)`
- Reject unsupported host API names
- Reject forbidden Lua APIs:
  - `os`
  - `io`
  - `package`
  - `require`
  - `dofile`
  - `load`
  - `loadfile`
  - `loadstring`
  - `debug`
- Reject same `(moduleNo, paramName)` read through multiple typed getters
- Reject same output name written through multiple typed setters
- Reject global used in `run()` without declaration in `init()`
- Reject new global creation in `run()`
- Reject reserved-name collision with host API names or lifecycle names

## Runtime Success Cases
- `GetIntValue()` reads a scalar input and `SetIntValue()` publishes a scalar output
- `GetFloatValue()` and `SetFloatValue()` pass floating-point values correctly
- `GetStringValue()` and `SetStringValue()` pass strings correctly
- `GetBoolValue()` and `SetBoolValue()` pass booleans correctly
- `GetIntArrayValue()` returns a Lua table and `SetIntArrayValue()` publishes it
- `GetFloatArrayValue()` returns a Lua table and `SetFloatArrayValue()` publishes it
- `GetStringArrayValue()` returns a Lua table and `SetStringArrayValue()` publishes it
- `GetBoolArrayValue()` returns a Lua table and `SetBoolArrayValue()` publishes it
- `init()` executes once per activation
- Global accumulation across multiple `run()` triggers works
- `cleanup()` executes during script teardown

## Runtime Failure Cases
- `init()` runtime error leaves the operator not-ready
- `run()` runtime error marks the trigger NG
- `cleanup()` runtime error emits diagnostics but teardown still proceeds
- required upstream value is missing at trigger time
- scalar setter receives the wrong runtime type
- array setter receives a non-table value
- array setter receives a mixed-type table
- array setter receives a sparse table
- array setter receives a nested table
- script exceeds timeout
- script exceeds memory limit

## Topology Extraction Cases
- repeated identical getter calls produce one subscription record
- repeated identical setter calls produce one published output definition
- subscriptions contain:
  - module number
  - parameter name
  - declared type
- publishes contain:
  - output name
  - declared type
- `init()` globals do not create subscriptions
- `cleanup()` logic does not create publishes

## Upper-Layer Integration Cases
- parsed module number does not exist in upper topology
- parsed parameter name does not exist on the referenced module
- parsed type does not match upstream actual parameter type
- upper layer can build subscribe structures from parsed getter calls
- upper layer can build publish structures from parsed setter calls
- no manual `script_input0..31` configuration is required for the new baseline

## Future-Compatibility Cases
- reserved `GetModuleXxxParam` / `SetModuleXxxParam` names remain blocked or stubbed cleanly in V1
- future parameter-setting extension does not collide with `SetXxxValue()` output semantics
