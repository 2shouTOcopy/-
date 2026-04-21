# Script Operator Script-Driven Topology Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the current `inputs/outputs + ctx` script-operator model with a script-driven topology model based on typed helper APIs, persistent Lua state via `init()` globals, and automatic subscription/publish extraction.

**Architecture:** Precompile becomes a metadata extraction stage that parses typed getter/setter calls and global declarations. Runtime moves from per-trigger ephemeral Lua states to a persistent Lua environment per activated script version, with bound host APIs for typed value access and output publication.

**Tech Stack:** C++, Lua 5.3, existing script operator runtime, script_support static analyzer, framework subscribe/publish metadata, module JSON/XML metadata.

---

## File Structure

- Modify: `source/algos/modules/script/script_support.hpp`
  - Expand script-analysis output from simple var checks to lifecycle/API/global extraction metadata.
- Modify: `source/algos/modules/script/script_support.cpp`
  - Parse `init/run/cleanup`, typed getter/setter calls, global declarations, and type conflicts.
- Modify: `source/algos/modules/script/script.h`
  - Replace old IO-centric fields with parsed subscription/output/global metadata and persistent Lua state ownership.
- Modify: `source/algos/modules/script/script.cpp`
  - Remove `inputs/outputs + ctx` runtime assumptions, install new host APIs, manage persistent Lua environment, generate dynamic subscribe/publish data from script parse results.
- Modify: `source/algos/so/script/json/user_function.lua`
  - Replace template with the new `init()/run()/cleanup()` plus typed helper API example.
- Modify: `source/algos/so/script/json/algo_private.json`
  - Remove `InputList` / `OutputList` as baseline declarations if no longer required by the new design, or reduce them to script text storage only.
- Modify: `source/algos/so/script/json/pm_conf.json`
  - Remove obsolete `script_input0..31` parameter definitions from the formal configuration path.
- Modify: `source/algos/so/script/json/script.xml`
  - Remove obsolete `script_input0..31` register exposure if still required only for the retired model.
- Modify: `source/algos/modules/script/script_support_test.cpp`
  - Replace old `inputs/outputs/ctx` validation tests with new lifecycle/API/global extraction tests.
- Modify: `source/algos/modules/script/test_cases.md`
  - Keep aligned with actual automated-coverage targets as implementation proceeds.

## Chunk 1: Parser And Metadata Model

### Task 1: Define the new parse-result structures

**Files:**
- Modify: `source/algos/modules/script/script_support.hpp`

- [ ] Replace old `ScriptVarDef`-centric analysis outputs with explicit metadata structs for:
  - lifecycle presence
  - typed subscriptions
  - typed published outputs
  - declared globals
  - validation issues
- [ ] Keep issue reporting line/column capable.
- [ ] Reserve enum coverage for scalar and array types.
- [ ] Document in comments which metadata is compile-time only vs runtime-consumed.

### Task 2: Add failing parser tests for the new ABI

**Files:**
- Modify: `source/algos/modules/script/script_support_test.cpp`

- [ ] Add tests for valid `run()` with `GetIntValue()` and `SetIntValue()`.
- [ ] Add tests for valid `init()` global declarations and later use in `run()`.
- [ ] Add tests for array getter/setter extraction.
- [ ] Add tests that reject:
  - missing `run()`
  - dynamic module number
  - dynamic parameter name
  - output name type conflicts
  - input getter type conflicts
  - global use without `init()` declaration
  - new global creation in `run()`
- [ ] Run only the affected unit tests and confirm they fail for the expected missing-behavior reasons.

### Task 3: Implement parser support

**Files:**
- Modify: `source/algos/modules/script/script_support.cpp`

- [ ] Teach the analyzer to detect lifecycle function declarations.
- [ ] Teach the analyzer to collect typed getter/setter calls from `run()`.
- [ ] Teach the analyzer to collect legal global declarations from `init()`.
- [ ] Reject dynamic call arguments that prevent static extraction.
- [ ] Reject unsupported globals and type conflicts.
- [ ] Keep dangerous-API rejection intact.
- [ ] Re-run the targeted parser tests and make them pass.

## Chunk 2: Runtime Hosting Rewrite

### Task 4: Define persistent-runtime state in `script.h`

**Files:**
- Modify: `source/algos/modules/script/script.h`

- [ ] Remove or deprecate fields dedicated only to `InputList` / `OutputList` / `ctx`.
- [ ] Add stored parse results for subscriptions, outputs, and globals.
- [ ] Add persistent Lua-state ownership fields.
- [ ] Add flags for environment readiness, `init()` success, and teardown status.
- [ ] Keep mutex ownership explicit because runtime is no longer per-trigger ephemeral.

### Task 5: Add failing runtime tests or executable checks

**Files:**
- Modify: `source/algos/modules/script/test_cases.md`
- Modify: `source/algos/modules/script/script_support_test.cpp`

- [ ] If full operator-level automated tests are not yet practical, add documented executable checks for:
  - one-time `init()`
  - cross-trigger global accumulation
  - array getter/setter handling
  - fail-fast after `init()` failure
- [ ] If a practical operator test harness exists, add at least one failing end-to-end test for accumulation and one for dynamic-subscription extraction.

### Task 6: Implement persistent Lua-environment lifecycle

**Files:**
- Modify: `source/algos/modules/script/script.cpp`

- [ ] Replace per-trigger `lua_newstate()`/`lua_close()` execution with a persistent activated environment.
- [ ] Create helper functions for:
  - environment creation
  - host API binding
  - `init()` execution
  - `run()` execution
  - `cleanup()` execution
  - environment teardown
- [ ] Preserve sandbox restrictions and timeout/memory controls.
- [ ] Make `Process()` fail fast if activation never completed successfully.
- [ ] Ensure script replacement destroys the old environment after optional `cleanup()`.

## Chunk 3: Host API Binding And Data Marshaling

### Task 7: Bind typed getter and setter APIs

**Files:**
- Modify: `source/algos/modules/script/script.cpp`

- [ ] Add Lua-callable bindings for all scalar getters.
- [ ] Add Lua-callable bindings for all array getters, returning Lua tables.
- [ ] Add Lua-callable bindings for all scalar output setters.
- [ ] Add Lua-callable bindings for all array output setters, validating table shape and element type.
- [ ] Reserve but do not yet implement module-parameter API names.
- [ ] Emit clear runtime errors when setters receive mismatched value types.

### Task 8: Replace metadata generation from JSON IO lists with script parse results

**Files:**
- Modify: `source/algos/modules/script/script.cpp`

- [ ] Build dynamic subscribe metadata directly from parsed getter calls.
- [ ] Build dynamic publish metadata directly from parsed setter calls.
- [ ] Deduplicate repeated script references at the metadata layer.
- [ ] Keep upper-layer topology validation as a separate responsibility.
- [ ] Remove old `script_input0..31` assumptions from the script-module control flow.

### Task 9: Verify runtime behavior

**Files:**
- Modify: `source/algos/modules/script/test_cases.md`

- [ ] Run parser tests.
- [ ] Run any available script-operator smoke verification.
- [ ] Manually verify:
  - `init()` executes once
  - `run()` accumulates globals across triggers
  - parsed subscribe/publish structures match script text
  - array outputs reject malformed tables

## Chunk 4: Config And Template Alignment

### Task 10: Update shipped template and metadata

**Files:**
- Modify: `source/algos/so/script/json/user_function.lua`
- Modify: `source/algos/so/script/json/algo_private.json`
- Modify: `source/algos/so/script/json/pm_conf.json`
- Modify: `source/algos/so/script/json/script.xml`

- [ ] Replace the default script template with the new helper-function ABI.
- [ ] Remove or retire obsolete manual input subscription parameters from the formal shipped configuration.
- [ ] Keep only the config surface that is still required to store script text and compile/check actions.
- [ ] Ensure metadata no longer instructs users toward `inputs/outputs` or `script_input0..31`.

### Task 11: Sync in-tree docs after code lands

**Files:**
- Modify: `source/algos/modules/script/progress.md`
- Modify: `source/algos/modules/script/next_session.md`

- [ ] Update implementation status after code and tests actually pass.
- [ ] Record remaining gaps, especially if module-parameter APIs remain reserved-only.

## Acceptance Criteria

- `run()` is the only required lifecycle function.
- Scripts can read upstream typed values using `GetXxxValue(moduleNo, paramName)`.
- Scripts can publish typed outputs using `SetXxxValue(paramName, value)`.
- Arrays work through Lua tables for all four element types.
- Subscriptions and publishes are generated from parsed script calls, not manual `script_input0..31` config.
- Globals used in `run()` are only legal if declared in `init()`.
- The active script environment persists across triggers and preserves legal globals.
- `init()` failure leaves the operator not-ready until reactivation.
- Existing docs and default templates match the new ABI.
