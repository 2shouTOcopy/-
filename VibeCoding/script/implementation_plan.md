# Script Operator Implementation Plan

## Principle
- Redesign from scratch inside `script/`
- Do not inherit the old positional-argument or unrestricted-Lua execution model

## Target Architecture
- `script_support.hpp/.cpp`
  - Pure validation and static analysis helpers
- `script.h/.cpp`
  - Operator integration, config loading, compile cache, runtime execution
- `requirements.md`
  - Product and behavior boundary
- `progress.md`
  - Current execution status and blockers
- `test_cases.md`
  - Test scenarios for next sessions

## Phase 1
- Lock V1 ABI to `run(inputs, outputs)`
- Support scalar types only
- Implement variable definition validation
- Implement static symbol checks for `inputs.xxx` and `outputs.xxx`
- Implement dangerous API checks
- Add sandboxed Lua execution
- Cache compiled chunk after successful validation
- Fail fast on invalid config or runtime output mismatch

## Phase 2
- Replace remaining legacy assumptions in operator config and UI metadata
- Align `algo_private.json`, default script template, and runtime outputs
- Add explicit compile status / error reporting model
- Add operator-facing compile diagnostics for UI/query layer

## Phase 3
- Add stronger integration tests around operator config parsing and runtime execution
- Add optional dual-version model: draft vs active
- Consider future lifecycle extension:
- `init(ctx)`
- `run(inputs, outputs)`
- `cleanup(ctx)`

## Current Interface Decision
- Chosen interface: `inputs.xxx` and `outputs.xxx`
- Rejected interface: direct global variables such as `value`, `result`

## Rejection Reasons For Direct Globals
- Hard to validate statically
- Easy to collide with standard-library symbols
- Easy to accidentally preserve state
- Harder to evolve ABI later

## Delivery Rule For Future Sessions
- New implementation work should read `requirements.md`, then `progress.md`, then `test_cases.md`
- If legacy code conflicts with requirements, requirements win
