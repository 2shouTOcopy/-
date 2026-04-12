# Script Operator Test Cases

## Validation Cases
- Reject input name starting with digit
- Reject output name starting with digit
- Reject duplicate variable names across input/output
- Reject unsupported type
- Reject empty script
- Reject script exceeding max length
- Reject script without `run(inputs, outputs)`
- Reject unknown `inputs.xxx`
- Reject unknown `outputs.xxx`
- Reject `inputs[key]`
- Reject `outputs[key]`
- Reject forbidden APIs:
- `os`
- `io`
- `package`
- `require`
- `dofile`
- `load`
- `loadfile`
- `loadstring`
- `debug`

## Runtime Success Cases
- `int -> int` arithmetic
- `float -> float` arithmetic
- `bool -> bool` forwarding
- `string -> string` formatting
- mixed read of multiple inputs with multiple outputs

## Runtime Failure Cases
- required input configured but not available on trigger
- compile succeeded but script does not assign a declared output
- script writes undeclared output
- script writes wrong output type
- runtime Lua error
- timeout / dead loop
- memory over limit

## Integration Cases
- input source deleted after configuration
- output variable deleted from config while script still references it
- variable renamed in config while script still references old name
- import/export preserves script text and IO definitions
- save triggers recompile
- invalid compile state causes runtime fail-fast

## Suggested Future Automated Tests
- Add operator-level tests that parse a synthetic `algo_private.json`
- Add runtime tests for `run(inputs, outputs)` end-to-end
- Add tests for compile-status persistence and error text formatting
