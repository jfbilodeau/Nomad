# Script runtime improvement plan

This plan records the review of `Runtime`, `Interpreter`, `Compiler`, `VirtualMachine`, and `InterpreterContext`. It holds the approved decisions, the staged work, and every finding with its priority, evidence, verification status, and target step. Update the status as each step lands.

**Status:** Decisions D1–D5 are approved. Steps 1 through 4 are implemented or resolved by review. Step 5 is in progress with I1/I2/I3/I7/I8/I12 validated and I4/I6/I9/I10 resolved by review. V5's checked storage query is retained after Windows Debug measurements. The declaration seal was removed. The registration ownership split is in progress. All 13 CTest entries pass.

## Component contract

- **Compiler** owns correctness of emitted OpCodes: identifier validity, native ID resolution, null prevention, and exact stack depth.
- **VirtualMachine** is built for performance and trusts Compiler output. It keeps only the checks the host boundary needs.
- **Interpreter** is built for simplicity and convenience. Evaluating strings is its primary function. It validates host input at the boundary.
- **InterpreterContext** is the host-facing evaluation context. It exposes production APIs only.
- **TypeManager** owns declarations of types, properties, hubs, and events, and allocates their IDs.
- **Runtime** owns execution registrations, including callables, constants, and variables, and allocates their IDs. The target dependency direction is Runtime to TypeManager, not the reverse.

Standing rules:

- Bytecode uses IDs (`CallableId`, `VariableId`) so that it stays portable.
- Dead APIs are removed, not shimmed. Test-only APIs are removed, and their tests are rewritten against production APIs.
- Recursion is legal. Tests cover it in `LanguageFunctionTest.cpp` and `ScriptMethodTest.cpp`.
- Script initialization is a host precondition. The VM does not validate readiness.
- Avoid `friend` declarations. Prefer narrow public APIs.

## Approved decisions

| ID | Decision |
| --- | --- |
| D1 | Support struct-typed class fields. Reproduce the failure with a test first, then route access through storage addressing. |
| D2 | The Compiler computes exact operand-stack depth. The VM drops per-push bounds checks. |
| D3 | Native dispatch keeps `CallableId` operands. The VM calls compiler-resolved accessors with no lookup checks. Debug builds verify the callable kind. |
| D4 | Remove dead and test-only APIs, rewriting their tests against production APIs. Remove `formatDynamicString`; callers use `Interpreter::formatString`. |
| D5 | Keep nullable pointers for native and member lookups (`getNativeFunction`, `getNativeMethod`, `getNativeConstructor`, and similar). Do not migrate to `std::optional`. The project targets C++20, and a pointer already expresses absence. Copy-returning lookups (`getNativePropertyInfo`, `getEventDefinition`) move to pointers in step 8, not to optionals. |

## Staged plan

Each step ends with a full build and CTest run, then stops for review before the next step starts.

| Step | Scope | Findings |
| --- | --- | --- |
| 1 | Native dispatch and VM dead code (D3, D4) | V3, V12 (native paths), I14 |
| 2 | Test-only API removal (D4), about 240 test call sites | R9, R16, C4, V13 (part) |
| 3 | Compiler correctness: struct fields (D1), exact stack depth (D2) | K1–K4, V1, V2 |
| 4 | VM trust boundary and check removal | V5, V7–V13 (remaining parts), V15, V17 |
| 5 | Interpreter correctness and simplification | I1–I4, I6–I10, I12, I13, I15, C1, C3, C4 |
| 6 | Runtime correctness and dead code | R3–R8, R12, R21, R23 |
| 7 | Redundancy and naming refactor | R10, R11, R14, R16, R17–R20, R22, R24, R25, R28, V14, K5, K6 |
| 8 | Performance sweep, measured before and after | R1, R2, R13, R15, R18, R23, R26, R27, R30, V4, V6, V16, I5, I11, C2 |

## Step details

### Step 1: native dispatch and VM dead code (implemented)

- `Runtime` gains inline `getUncheckedNativeFunction`, `getUncheckedNativeMethod`, and `getUncheckedNativeConstructor`. They cast the compiler-resolved ID directly, with no debug check.
- `VirtualMachine::callNativeFunction`, `callNativeMethod`, `createNativeStruct`, and `createNativeObject` use the resolved accessors. `createNativeObject` keeps its null-result fault because it guards host-provided constructors.
- Removed the unused VM members listed under V3, the dead pop helpers, `pushInstructionIndex`, `freeObjectParameter`, and the commented-out `nextInstruction` and `nextShort` declarations.
- Removed `formatDynamicString`. `Expression.cpp`, `OpCode.cpp`, and the format tests call `Interpreter::formatString`. `docs/embedding.md` no longer mentions the free function.
- Validated: the VS Code CMake Tools build (windows-debug preset) succeeded, and all 13 CTest entries passed.

### Step 2: API cleanup (split into 2a and 2b)

Your decisions: keep convenience lookups (`getStringByName`, `getStringContextVariableValue`), remove navigation and typed frame accessors, rename `getCurrentFunction` to `getCurrentScript`, and remove commented-out code.

**Step 2a (Runtime and VirtualMachine): done, all 13 CTest entries passing.**

- Removed `Runtime::getFunctionAtLine` and `Runtime::getFunctionByStartAddress`. Tests use `getScriptCallableAtLine` and `getScriptCallableByStartAddress`. The line-8 check in `SourceCompilationTest` now asserts the innermost callable (the method). This is a deliberate change, because the old function-only filter has no production use.
- Renamed `VirtualMachine::getCurrentFunction` to `getCurrentScript`.
- Removed `getIntegerEnclosingParameter`, `setIntegerVariableValue`, `getFunctionIntegerVariableValue`, and `getFunctionStringVariableValue`. Tests use `getEnclosingParameter`, `setFunctionVariableValue`, and `getFunctionVariableValue`.
- Kept `Runtime::getStringByName` and `Runtime::getStringContextVariableValue` as convenience helpers.
- Removed commented-out code in `Scene.cpp`, `Scene.update.cpp`, `Decompiler.cpp`, `Entity.hpp`, `Logger.hpp`, and `Tokenizer.cpp`.

**Step 2b (InterpreterContext): done, all 13 CTest entries passing.**

- Removed `getVariableValueById`, `setParameterResult`, and `getReceiverStorage` (const). They were ID- or index-based navigation. Replacements: `getVariableStorageById(id)[0]`, `setResult(type, getParameterStorage(id))`, and `getMutableReceiverStorage()`.
- Kept the name-based convenience helpers `setVariableProperty`, `setReceiverProperty`, `getVariableStorage(name)`, and `hasWarning`. They have no production callers, but they're kept per your decision.
- Dropped one duplicate struct-restriction check in `InterpreterTest`, since the name-based check on the line above it covers the same path.

### Step 3: compiler correctness

1. Write failing tests first: a struct-typed class field read and write (K1), and a `-0.0` literal (K3).
2. K1: route struct-typed class fields through storage addressing, so they no longer reach the single-slot field accessors (D1).
3. K2: a verifier runs before linking, computes the exact number of reserved stack slots for each callable from declared opcode stack effects, checks that control-flow joins agree and that returns leave no operands, and stores the count on the callable. Script call opcodes carry the callee's reserved slot count as a second operand, patched during linking, so the frame-entry check reads it from the instruction stream without a callable lookup. Per-push checks stay removed; host-side reservations keep their checks.
4. K3: use `std::signbit` to detect negative-zero literals.
5. K4: bound the `inferTypes` fixed-point loop, and report a diagnostic when the bound is reached.

### Step 4: VM trust boundary (complete)

- V12/V13 keep compiler-resolved callable metadata trusted while retaining checks for host-provided receiver values. `fault` is now `[[noreturn]]` (V17), and `getCurrentScript` returns `const ScriptFunction*`.
- V5 review found that the compiler selects the stored-property operation and offsets, but does not establish the actual C++ storage kind for every host/native receiver. The implemented query below replaces RTTI conversion while retaining receiver, owner-type, and actual-allocation checks.
- V11 is fixed with a stored-property bounds regression. V10 was not reproduced.
- Deduplicate capture logic (V9), save and restore guards (V8), and the six execution entry points (V7). Keep the host-facing validation.
- V8 is resolved by review: `InterpreterCallFrame` already preserves result/intermediate registers for callable invocation. The separate register guard class is removed. Raw interpreter evaluation and VM creation retain local scope-exit restoration because they also run outside call frames; borrowed ownership and restoration order are unchanged. This small duplication is accepted instead of adding another guard abstraction.
- V8 validation after review: all 13 CTest entries pass on `windows-debug`. Behavioral coverage checks borrowed-register restoration and cleanup during direct indexer evaluation and formatting, on success and exceptions; existing constructor and call-frame tests remain green.
- V8 follow-up: `createNativeObject` combines register and native-frame restoration in one scope-exit, after partial-result cleanup. Both `nomad_test_language` and `nomad_test_remaining` pass on `windows-debug`, including constructor failure/reentry and object-creation allocation coverage.
- V9 is implemented: removed the test-only current-frame snapshot API and its duplicate capture-source switch. Tests exercise current-frame copies through the existing `InterpreterCallFrame` used by compiled formatting, alongside enclosing-frame closure creation. Struct copies, empty capture positions, receiver reference identity, and partial-copy cleanup remain covered. No shared capture abstraction was added.
- V9 validation: all 13 CTest entries pass on `windows-debug`, including migrated frame/closure capture tests and existing compiled-format capture coverage. The removed API has no remaining references in engine code, tests, apps, modules, or documentation.
- V7 is implemented: host execution wrappers share function-ID lookup and result copy/discard handling, while raw script and closure runners remain specialized. Existing validation and logging/exception policies are unchanged. Result copying uses the existing `RuntimeValueGuard` so a throwing host copy also releases the completed VM result; a regression reproduced this cleanup gap for both function-ID and closure calls before the fix.
- V7 validation: all 13 CTest entries pass on `windows-debug`. The focused regression verifies copy-free discard, independent owned returns, and completed-result cleanup when copying throws for both function-ID and closure calls.
- V12 is resolved by review: script construction and struct initializer dispatch already trust the compiler-selected callable ID/kind and compiled body. No new null checks or lookup APIs are needed. The absent-initializer branch remains necessary for native structs, and native object constructors still reject null callback results.
- V13 is implemented: receiver-copy operations no longer revalidate an already-bound object. Receiver lookup consumes the checked `getThis()` result without an ignored-return idiom. `getThis()`, native property invocation, and `bindThisFromStack` retain null checks: class type resolution cannot prove that a native callback supplied a non-null receiver.
- V12/V13 validation: all 13 CTest entries pass on `windows-debug`. Coverage verifies checked missing-receiver lookup, borrowed receiver identity/reference counts, and rejection of a null native-function result before a compiled method body runs; existing receiver-copy, native-property, and constructor tests remain green.
- V5 is implemented and measured as described below. Step 4 is complete; the remaining registration ownership split stays separate.
- V15 is implemented: `runNativeIndexer` preserves argument order, releases every successfully copied argument and its retained receiver on success or failure, and restores the enclosing stack and receiver without allocating cleanup storage. The early result reset stays because argument copying can fail before `callNativeIndexer` resets it.
- V15 validation: all 13 CTest entries pass on `windows-debug`, including existing partial-copy, callback-failure, frame-restoration, and allocation-free indexer tests. Stopped for review after this sub-step.
- Resolve Q3 (`friend` declaration).

#### V5: checked boundary and trusted storage

**Status:** Implemented after approval to proceed with the prototype. All 13 CTest entries pass on `windows-debug`; retained after the before/after comparison below. No new opcodes or field-table changes.

Review evidence:

- `VirtualMachine::getProperty` and `setProperty` are checked host/interpreter entry points as well as dispatch targets. They must reject missing storage and objects of the wrong owner type.
- Compiled physical-offset access reaches `getStoredPropertyAddress` through `getStoredProperty` or `op_stored_property_address_push`. Its input is still a `NomadObject*`; declared class metadata alone does not establish a `ScriptObject` representation.
- Existing tests cover native objects without script storage and instances allocated before an open class gains fields (`ScriptObjectTest.cpp`: property dispatch and specialized field errors; `StructTypeTest.cpp`: stored-property address failures).
- `ScriptObject::getStorageValue` is already the typed, unchecked physical-slot fast path. Do not introduce a second storage-access abstraction.

Implemented scope:

1. Kept checked VM entry points, error messages, owner-type checks, and actual-allocation bounds. No arbitrary `RuntimeValue` is downcast with `static_cast<ScriptObject*>`.
2. Added mutable/const `NomadObject::asScriptObject()` queries: the base returns `nullptr`, and `ScriptObject` returns `this`. The three VM stored-property paths use the query in place of RTTI, checking the object pointer before invoking it.
3. Reused the existing typed storage APIs after conversion and bounds validation. This removes RTTI conversion, not receiver validation, and adds no separate unchecked VM entry point.
4. Preserved `ScriptObject`'s per-instance `StoredField` snapshot and value buffer. Destruction and host access still use the layout the instance actually owns, including instances predating later declarations. No seal, receiver cache, value discriminator, or registration restriction was added.
5. Kept bytecode, call-frame layout, property IDs, native callbacks, and value ownership unchanged.

ABI and verification:

- Adding virtual queries changes the native C++ ABI. The existing module contract requires the same SDK/compiler/runtime settings, but `ModuleHostImpl::loadModules` checks only the registration export, not an ABI version. Rebuild C++ hosts, custom object subclasses, and all native modules; do not mix old module binaries with this runtime. No new ABI-versioning scheme was introduced.
- CTest rebuilt the workspace and all 13 entries pass, including module/runtime/package integration. Existing missing/native/wrong-owner receiver, old-instance bounds, struct-storage, ownership, self-assignment, and allocation regressions were kept unchanged.
- Measurement used MSVC 19.44 x64 Debug engine DLLs with an identical `/O2 /MDd` C++20 standalone consumer harness. Saved baseline and prototype DLLs were loaded beside their respective executables; the harness did not load native modules. Three paired comparisons alternated A/B, B/A, A/B. Each run warmed up with 10,000 operations and reported the median of seven 100,000-operation samples per workload. The table gives the median of each variant's three run medians.
- All six paired runs returned the same checksum (`186300000`), reported zero CRT allocations in separate 100,000-operation batches, and passed stack, reference-count, and live-object/string ownership checks. These are Windows Debug microbenchmarks, not Release or gameplay measurements.

| Workload | RTTI ns/op | Query ns/op | Reduction |
| --- | ---: | ---: | ---: |
| Opcode scalar read | 36.479 | 29.260 | 19.79% |
| Opcode scalar write | 41.965 | 29.843 | 28.89% |
| Opcode reference read | 61.778 | 48.186 | 22.00% |
| Opcode reference write | 69.463 | 51.621 | 25.69% |
| Opcode struct-field address | 75.778 | 62.954 | 16.92% |
| Checked scalar read | 84.469 | 74.051 | 12.33% |
| Checked scalar write | 134.864 | 128.531 | 4.70% |
| Checked reference read | 122.440 | 117.452 | 4.07% |
| Checked reference write | 189.949 | 178.839 | 5.85% |

Step 5's I1/I2 running-state review and approved change are recorded below. Release and other-platform performance remain unmeasured; do not extrapolate these microbenchmark results.

### Registration ownership

- The declaration seal (a `TypeManager` freeze after a successful Emit compile) was removed. It was not required and was the wrong solution to the problem it targeted. Declarations are no longer blocked after compilation.
- Registration sub-step 1 (immutable IDs) is implemented. Callable and property IDs are constructor arguments and cannot be changed afterward. `Callable::setId` and `Property::setId` are removed. `Runtime::getNextCallableId()` and `TypeManager::getNextPropertyId()` supply the ID that the next registered member receives. Rejected members do not consume an ID.
- The old `TypeManager` member entry points (for example `registerNativeClassMethod`) still call `Runtime`. That back-call is temporary and is removed in registration sub-steps 2 through 4.
- Verified with `RunCtest_CMakeTools` after the removal: the build passes and all 13 CTest entries pass. The 26 failures the seal caused (`nomad_test_bindings` 4, `nomad_test_language` 8, `nomad_test_remaining` 14) are gone. Only seal-specific assertions were removed from the tests.
- Member descriptors still hold their own ownership. Descriptor ownership remains with the declaring type.
- The high-level member registration entry points still live on TypeManager and still orchestrate Runtime operations. Moving execution registration orchestration to Runtime is a subsequent review step; the full one-way dependency split is not complete.
- After review, move the remaining declaration and execution responsibilities, including hub and event declarations, in separately approved steps. Do not revive the superseded name-check-only refactoring.
- Registration-before-compile setup, Game API setup, and fresh runtimes for language-server analysis batches existed only to satisfy the seal and are dropped. Whether the Interpreter or VM should require successful compilation is an open question (Q4).
- The earlier blanket V5 `static_cast` and ScriptObject field-table proposals were not implemented. The checked query preserves receiver validation and per-instance layouts; registration ownership changes do not authorize changing those contracts.

### Step 5: Interpreter correctness and simplification (in progress)

- I1/I2 policy was reviewed before implementation. The user chose to restore the caller's running state for all Interpreter native function/method calls, replacing the former propagation behavior of native free functions and scalar-only native class methods. Direct bytecode execution, constructors, and the object model are unchanged.
- I1/I2 are implemented with the existing `ExecutionStateGuard`. Removed the duplicate synchronous-native helper and the struct-signature heuristic; all native function/method paths use one restoration policy without adding a guard class or flag.
- Regressions first reproduced the approved behavior gap: stopped/throwing nested native calls left the caller stopped, and real bytecode reentry returned 41 instead of reaching 42. Coverage also verifies that a direct native `stop` still terminates bytecode execution.
- I1/I2 validation: all 13 CTest entries pass on `windows-debug`. Coverage includes running/stopped callers, scalar and empty-struct native function/method signatures, normal return, `stop`, exceptions, `stop` followed by an exception, actual bytecode reentry, and unchanged direct bytecode stop behavior. Existing struct-method, ownership, frame-restoration, and constructor tests remain green.
- I9 is resolved by review under the current valid-metadata contract. `InterpreterValue` rejects storage whose slot count differs from `type->getSize()` and owns the complete immutable receiver. `StructType::getSize()` requires a finalized layout; finalization recursively fixes inline struct layouts, checks size overflow, and rejects later stored-field additions. Property resolution validates the owner and selected fields before slicing. No supported incomplete receiver reaches `getStoredProperty`, and zero-sized fields still return empty storage without pointer arithmetic. A second read-side bounds guard would duplicate these existing guarantees; no C++ change or artificial invalid-metadata regression was added.
- I9 validation: `nomad_test_remaining` passes on `windows-debug`, including existing value/property, zero-sized struct, ownership, and layout-finalization coverage. Only this plan changed; the last full 13-entry run remains the I1/I2 validation above.
- I3 caller review confirmed that dynamic-format bytecode uses a fresh frame-backed context, while interpreted dynamic templates and format literals use their live caller context. The user chose disposable transient evaluation state, not preservation: log current-call template diagnostics and keep the structured return, without retaining diagnostic history or adding a snapshot/restore guard. Broader Interpreter diagnostic API cleanup remains separate.
- I3 is implemented: reset transient state at the public formatting boundary and after each segment, rather than only when an expression placeholder is evaluated. This fixes stale diagnostics/results on empty templates and stale loop control on literal-only templates while preserving live bindings and placeholder side effects. Template segments and the source name are owned before the old result is released, so input views may safely alias it.
- The new regression first reproduced stale context state, including a retained old result string. Coverage verifies current-call diagnostic logging through `log::error`, clean repeated calls, variable-binding identity, and aliased template/source-name input lifetimes. No new guard, context copy, or diagnostic return type was added.
- I3 validation: all 13 CTest entries pass on `windows-debug`. Existing nested-template, partial-result exception cleanup, structured-marker/location, live-frame/receiver, and VM register-restoration tests remain green.
- I6 is resolved by review: both `InterpreterContext` construction paths allocate owned callable metadata; copy/transfer is unavailable and no API clears that metadata. `getCallable()` therefore returns a valid pointer or throws `NomadBug` when a borrowed frame no longer matches. No nullable-callable guard is needed.
- I6's two catches cover different scopes, not conflicting execution policies. The string overload catches frame validation failure while obtaining the default source name, before it can delegate. The `SourceText` overload catches standard exceptions, including `NomadBug`, during source execution and reports them through the supplied source name. Existing source-execution and invalid-frame tests require these diagnostic results; direct typed host calls still propagate exceptions. No catch consolidation or exception-policy change was warranted.
- I6 validation: `nomad_test_remaining` passes on `windows-debug`, including `interpreter_reports_source_execution_failures_as_diagnostics` and `frame_context_receivers_survive_native_receiver_overrides_and_validate_frame_access`. Only this plan changed; the last full 13-entry run remains the I3 validation above.
- I10 is resolved by review: `MemberAssignmentStatement::onResolve`, `Compiler::resolveStructField`/`resolveObjectField`, and `StorageLocation::emitAssignment` require exact type-descriptor identity, matching `validateStoredPropertyAssignment` for both functional and in-place Interpreter property updates. Callable argument resolution instead uses `acceptsArgument` in both the Compiler and Interpreter. This is a distinct argument-acceptance policy, not an assignment conversion rule. No implicit conversions, relaxed assignments, or new compatibility helper were added.
- I10 validation: `nomad_test_remaining` passes on `windows-debug`, including stored-property replacement/input-preservation, interpreted struct assignment, and compiled struct-property type-mismatch coverage. Only this plan changed; the last full 13-entry run remains the I3 validation above.
- I4 is resolved by explicit review: the user chose to keep specialized source and format-expression compilation. General input supports assignments, statements, empty input, and multiline execution; placeholders require exactly one non-void expression and propagate execution exceptions rather than translating them into source-execution diagnostics. The modest setup/diagnostic-handling duplication is accepted instead of adding a shared compilation-session class or mode-based pipeline.
- Compiler creation remains per dynamic placeholder, preserving the existing setup-callback contract and AST/compiler lifetimes. `compiled_literals_do_not_create_runtime_compilers_but_dynamic_templates_do` verifies this behavior. Compiler reuse or caching requires a separately reviewed, measured performance change; no C++ abstraction, parser change, or compiler reuse was added for I4.
- I4 validation: both `nomad_test_language` and `nomad_test_remaining` pass on `windows-debug`, including compiler-setup counts, strict placeholder grammar, source classification, source locations, structured diagnostics, and execution-exception coverage. Only this plan changed; the last full 13-entry run remains the I3 validation above.
- I7 is implemented for equivalent count validation: `Closure::acceptsArgumentCount` owns the shared explicit-argument/capture-count check, including safe subtraction. Runtime ownership, type/storage compatibility, raw scalar-only arguments/results, and caller-specific error messages remain in their specialized paths; merging those checks would change the raw VM or typed Interpreter contract.
- I8 is implemented: `ScriptCallableBase::hasCompiledBody` owns the shared nonempty instruction-range predicate used by Interpreter function/method/constructor calls, raw VM closure calls, and compiled format segments. The regression first reproduced a real gap: a valid start plus an unset end passed all three function/closure host entry checks and executed the callback. Both endpoints must now be set; existing empty/reversed-range rejection is preserved. No checks were added to prepared VM execution or script initialization readiness.
- I7/I8 validation: all 13 CTest entries pass on `windows-debug`. The new regression rejects unfinished function/closure bodies before execution or VM state changes and accepts the body once its end is restored. Existing wrong-count, foreign-runtime, raw aggregate-restriction, typed aggregate-call, method/constructor, compiled-format, and copy-failure coverage remains green.
- I12 review confirmed that each path segment needs its own struct-owner, property-kind, and empty-name validation; direct-ID and dotted-path selectors already resolve once before value access/assignment. Those checks and their existing diagnostics remain specialized rather than adding a generalized resolver.
- I12 is implemented for avoidable name copies: the existing `CompositeType::getProperty` name overload now accepts a borrowed `NomadStringView`. Stored-property path and receiver metadata lookups pass their existing segment views directly, without allocating an owning name for successful metadata lookup. No cache, extra property API, field-layout change, or new resolver class was added. C++ consumers must rebuild against the changed lookup signature.
- The allocation regression first measured 500 allocations for 100 long nested-path reads versus 100 for equivalent returned-value storage through direct-ID reads on MSVC Debug. It compares against returned-value storage costs rather than assuming Debug containers allocate nothing, and uses a non-null-terminated path subview.
- I12 validation: all 13 CTest entries pass on `windows-debug`. Long nested-path lookup now matches the direct-read allocation baseline. Existing name/ID lookup, invalid/empty path, native-property rejection, exact-type assignment, empty-field, ownership, compiled-source, and module integration coverage remains green.
- Next reviewable sub-step: I13 (unambiguous functional versus in-place property-update names).
- Remaining simplification: unambiguous `setProperty` names (I13), const-correct VM access (I15).
- InterpreterContext: const-correct storage resolvers (C1), API cleanup (C3), and the remaining name-keyed accessors (C4).

### Step 6: Runtime correctness and dead code

- Invalid IDs and publication: R3, R4, R6, R21.
- Null-safe dynamic variables: R7.
- Ownership: R5 (verify first).
- Exception-safe string setters: R23.
- Dead `NOMAD_FUNCTION_DOC` blocks: R8.
- Identifier syntax validation moves to the Compiler: R12.

### Step 7: redundancy and naming

No behavior change. Consolidate duplicated helpers and align names and style with the repository rules.

### Step 8: performance sweep

Measure each change before and after on the same workload.

- Lookups: R1, R2, R13, R15, R26, R27 (name and interning indexes).
- Hot paths: V4 (current callable without scanning), V6 (native property accessors without copying), V16 (no allocation per host call), I11 (no scratch vector per write), R30 (no event copies), R23 (move instead of copy), R18 (cache well-known type IDs).
- Storage: C2.
- Format templates: I5, subject to Q2.

## Corrections to earlier review notes

- R9 said three Runtime accessors have no callers. They have test-only callers, so they move to step 2.
- `getIntegerEnclosingParameter` was treated as production-used. It is test-only (`ObjectConstructionTest.cpp`), so it moves to step 2.
- I14 is a redundancy finding, not dead code. `formatDynamicString` had production callers in `Expression.cpp` and `OpCode.cpp`. Under D4 it is removed, and those callers are migrated.
- V12 flagged missing null checks in `callNativeFunction`. Under D3 the Compiler owns native ID validity, so the VM no longer checks it. The host-facing null-result check in `createNativeObject` stays.
- R29 is dismissed. `EventType` holds a copy of `EventDefinition`, so `&m_events.back()` does not dangle.
- Recursion is legal. `Compiler::isRecursiveFunctionCall` only rejects recursive return-type inference, so it is not a bug.

## Open questions

- **Q1 (resolved):** The debug-only kind check was removed. The accessors are `getUncheckedNative*`, inline in `Runtime.hpp`.
- **Q2 (step 8):** Cache compiled format-template segments (I5), or keep the Interpreter simpler and accept re-parsing? Measured (Debug build, 20,000 calls each): plain return about 5–7 µs; dynamic format with static text (no placeholders) about 33 µs; dynamic format with two `{value}` placeholders about 650 µs. The context copy accounts for about 28 µs of the static-text cost, so per-placeholder work dominates. Next measurement: break down `Interpreter::evaluateFormatExpression` (compiler creation, tokenizing, parsing, resolving, evaluation) per placeholder.
- **Q3 (step 4):** Resolved. `InterpreterContext` reads the frame indexes through the read-only `getParameterIndex()` and `getVariableIndex()` accessors, and the `friend` declaration is removed.
- **Q4 (open):** Should the Interpreter or VM reject a runtime whose script compilation did not succeed? The standing rule says script initialization is a host precondition and the VM does not validate readiness. Confirm that rule or change it explicitly.

## Debug-only checks

Checks compiled only under `NOMAD_DEBUG`. Status after the follow-up change:

| Check | Location | Proposed action |
| --- | --- | --- |
| Stack overflow and underflow macros (26 use sites) | `VirtualMachine.cpp`, `TEST_STACK_*` | Removed. The explicit `Stack overflow` checks in `pushScriptFrame` and `reserveLocals` remain. |
| Native callable kind verification | `Runtime.cpp`, `getUnchecked*` | Removed. The accessors are inline and unchecked. |
| Variable ID range check, log and continue | `Variable.cpp`, `VariableList` | Replaced. `setVariableValue` and `getVariableValue` return `nullptr` for an invalid ID. The `getUnchecked*` accessors are inline and unchecked. |
| Commented `BOOST_SPIRIT_DEBUG` blocks | `Tokenizer.cpp` | Removed. |
| Live string count | `NomadStringObject.cpp` | Not a check. It is instrumentation that tests read through `getLiveCount`. Keep. |

## Verification

- Build and test: run CTest from VS Code on the `windows-debug` preset. CTest triggers the build itself, so do not run a separate build first. Debug configurations define `NOMAD_DEBUG`.
- Steps 3, 5, and 6 start with failing regression tests for each fix.
- Report compile checks separately from runtime results.
- No commits or pushes without an explicit request.

## Findings register

Priority: P1 = correctness or hot-path cost. P2 = correctness risk, API problem, redundancy, or significant quality issue. P3 = cleanup. Kind and status come from the review record. `K` marks Compiler findings, since `C` is used by `InterpreterContext`. Evidence reflects the review snapshot, so line numbers may shift after step 1.

### Runtime

| ID | Priority | Kind | Finding | Evidence | Status | Step |
| --- | --- | --- | --- | --- | --- | --- |
| R1 | P1 | perf | Name-keyed linear scans over all callables (`getCallableId`, overload counts, `resolveFunction`, `resolveMethod`, `resolveConstructor` per call site, multi-pass `registerNativeFunction`); no name index | `Runtime.cpp`: getCallableId, getCallableOverloads, getCallableOverloadCount, resolveFunction, resolveMethod, resolveConstructor, registerNativeFunction | verified | 8 |
| R2 | P1 | perf | Linear interning and lookups: `getStringId` O(S), `internContextVariable` O(C), `getEventId`, `getKeywordId`, `getDynamicVariableId`, `getFormatStringId`, `getContextId`, `getInstructionId(name)` | `Runtime.cpp`: getStringId, internContextVariable, getEventId, getKeywordId, getDynamicVariableId, getFormatStringId, getContextId, getInstructionId | verified | 8 |
| R3 | P1 | correctness | `getString(id)` and `getStringByName(missing)` index `m_strings` with the INVALID id (out of bounds) | `Runtime.cpp`: getString, getStringByName | verified | 6 |
| R4 | P1 | correctness | `registerNativeFunction` and `registerScriptFunction` publish a hub member even when `addCallableToTable` returned INVALID | `Runtime.cpp`: registerNativeFunction tail, registerScriptFunction tail | verified | 6 |
| R5 | P2 | correctness | Iterator current getter copies into owned (retained) storage, then raw-assigns the result; possible leak for object values | `Runtime.cpp`: constructor, registerNativeClassProperty getter lambda; verify `RuntimeValue` assignment semantics | unverified | 6 |
| R6 | P2 | correctness | Unchecked accessors index vectors with invalid ids (instruction, dynamic variable, context name, constants) while sibling accessors validate | `Runtime.cpp`: getInstructionFn, getInstructionName, getInstructionOperands, getDynamicVariable*, getContextName, getString | verified | 6 |
| R7 | P2 | correctness | `setDynamicVariable` and `getDynamicVariableValue` call `std::function` without a null check; VM `getVariableValue` skips `canGetDynamicVariable` | `Runtime.cpp`: setDynamicVariable, getDynamicVariableValue; `VirtualMachine.cpp`: getVariableValue | verified | 6 |
| R8 | P2 | dead-code | `NOMAD_FUNCTION_DOC` blocks reference commented-out docs; the macro is never defined; keyword docs are discarded | `Runtime.cpp`: registerKeyword and registerDynamicVariable (`/*doc*/`), `#if defined(NOMAD_FUNCTION_DOC)` blocks | verified | 6 |
| R9 | P2 | dead-code | getFunctionAtLine, getFunctionByStartAddress, and getStringContextVariableValue were recorded as having no callers. **Corrected:** they have test-only callers | `Runtime.cpp`; `Runtime.hpp` declarations | verified (corrected) | 2 |
| R10 | P2 | redundancy | Hub qualification repeated in registerHubConstant, registerHubEnum, and registerHubNativeFunction instead of `qualifyHubMemberName` | `Runtime.cpp`: registerHub* functions | verified | 7 |
| R11 | P2 | redundancy | Name-conflict check chains duplicated (registerHub, registerHubVariable, registerEvent, registerDynamicVariable); publishTypeNamespace repeats canRegisterTypeName | `Runtime.cpp`: registerHub, registerHubVariable, publishTypeNamespace, canRegisterTypeName | verified | 7 |
| R12 | P2 | policy | registerHub validates identifier segments in Runtime. This conflicts with the decision that the Compiler owns identifier syntax validation | `Runtime.cpp`: registerHub name loop | verified | 6 |
| R13 | P2 | perf | rebuildHubMembers rebuilds all hub maps on each registerHub, duplicating incremental registerHubMember; per-member linear hub lookups | `Runtime.cpp`: rebuildHubMembers, registerHub | verified | 8 |
| R14 | P2 | quality | Can-register predicates log errors as side effects; callers log again | `Runtime.cpp`: canRegisterHubMember, registerNativeFunction, registerConstant, registerEvent | verified | 7 |
| R15 | P2 | perf | Registration makes several full passes over callables (quadratic startup) | `Runtime.cpp`: registerNativeFunction, getCallableOverloadCount, canOverloadCallable | verified | 8 |
| R16 | P2 | redundancy | Duplicate lookup pairs: getScriptCallableAtLine vs getFunctionAtLine; getScriptCallableAtInstructionIndex vs getFunctionAtInstructionIndex; getScriptCallableByStartAddress vs getFunctionByStartAddress | `Runtime.cpp` | verified | 2, 7 |
| R17 | P2 | const-correctness | Const methods return mutable pointers or mutate state: getFunctionAtInstructionIndex, getFormatString, getVariableContext, setContextVariable* | `Runtime.hpp`, `Runtime.cpp` | verified | 7 |
| R18 | P3 | typing | Well-known type getters perform string-keyed lookups on every call (about 490 call sites, mostly registration); getStringType returns a different pointer type; unchecked `static_cast` downcasts where other code uses `asX()` | `Runtime.cpp`: getVoidType through getLineNumberType, constructor iterator lambda | verified | 7, 8 |
| R19 | P3 | redundancy | getParameterShape mirrors the parser's parameter classification; drift risk | `Runtime.cpp`: getParameterShape comment; verify the Parser counterpart | partly-verified | 7 |
| R20 | P3 | redundancy | Six context-variable accessors repeat the same log-and-return pattern | `Runtime.cpp`: getContextVariableName, getContextVariableType, setContextVariableWritten, setContextVariableRead, setContextVariableValue, getContextVariableValue | verified | 7 |
| R21 | P3 | correctness | `getConstantValue` silently returns on an invalid id, leaving the value unchanged | `Runtime.cpp`: getConstantValue | verified | 6 |
| R22 | P3 | consistency | `dynamic_cast` in registration code (isDeferred, validateNativeFunctionSignatures) while other code uses `asX()` | `Runtime.cpp`: registerNativeFunction, validateNativeFunctionSignatures | verified | 7 |
| R23 | P3 | perf, correctness | registerDynamicVariable pushes a copy instead of moving; string setters free manually, which is not exception safe | `Runtime.cpp`: registerDynamicVariable, setStringDynamicVariable, setStringContextVariableValue | verified | 6, 8 |
| R24 | P3 | style | Mixed style: `ScriptFunction *` spacing, reference alignment, brace placement, single-line `if` without braces, trailing whitespace, snake_case loop variable `op_code`, parameter lists not one per line | `Runtime.cpp`: registerEvent, getFirstConstantId, getFunctionAtLine, getFunctionByStartAddress, registerNativeFunction, getInstructionFn loops, registerInstruction signature | verified | 7 |
| R25 | P3 | typing | getNativeMemberId returns a `MemberReference` with a `PropertyId::INVALID` sentinel; `size_t` converts implicitly to `NomadIndex` | `Runtime.cpp`: getNativeMemberId, getNativeMemberOverloadCount | verified | 7 |
| R26 | P3 | perf | catalogNativeProperty and catalogNativeMethod scan `m_nativeMembers` linearly; validateNativePropertyTypes looks up the same property again | `Runtime.cpp`: catalogNativeProperty, catalogNativeMethod, validateNativePropertyTypes | verified | 8 |
| R27 | P3 | perf | getVariableContextIdByPrefix computes `getHubId(getContainingHub)` twice | `Runtime.cpp`: getVariableContextIdByPrefix | verified | 8 |
| R28 | P3 | naming | getCallableId matches only Function type despite its name; getFunctionId, getNativeFunctionId, and getFunctionTableSize (counts all callables) differ from getFunctionCount (script only) | `Runtime.cpp` naming | verified | 7 |
| R29 | — | dismissed | EventType stores `&m_events.back()`. Not a dangling pointer, because EventType copies the definition | `Event.hpp:46` `const EventDefinition m_event` | verified-not-a-bug | — |
| R30 | P3 | perf | getEventDefinition returns an optional copy of EventDefinition on each call | `Runtime.cpp`: getEventDefinition | verified | 8 |

### Interpreter

| ID | Priority | Kind | Finding | Evidence | Status | Step |
| --- | --- | --- | --- | --- | --- | --- |
| I1 | P2 | correctness | `runNativeCallable` preserves callback changes to the running flag rather than restoring its caller | `Interpreter.cpp`: runNativeCallable | Fixed after policy approval: existing ExecutionStateGuard restores complete caller state on return/throw | 5 |
| I2 | P2 | consistency | Native function/method stop behavior depends on owner and struct-signature heuristics | `Interpreter.cpp`: runFunction, runStructMethod, runObjectMethod | Fixed with I1: one native function/method restoration policy; duplicate helper/heuristic removed | 5 |
| I3 | P2 | correctness | Format reset policy differs for expression, literal-only, and empty templates using fresh or live caller contexts | `Interpreter.cpp`: evaluateFormatExpression, formatString; `Expression.cpp`: DynamicFormatExpression/FormatStringLiteral::onEvaluate; `OpCode.cpp`: op_dynamic_format_execute | Fixed under approved disposable-context/current-call-diagnostics policy: boundary/segment resets, live bindings retained, no preservation guard | 5 |
| I4 | P2 | redundancy | Specialized source and placeholder compilation repeat setup/diagnostic handling; each dynamic expression creates a Compiler and runs setup callbacks | `Interpreter.cpp`: execute, evaluateFormatExpression; `Runtime.cpp`: createCompiler; `LanguageFormatStringFunctionTest.cpp`: compiler-creation regression | Resolved by user decision: keep specialized parsers and modest duplication; retain per-placeholder setup behavior, no shared-session abstraction or reuse | 5 |
| I5 | P2 | perf | Dynamic format templates are re-tokenized and re-parsed on every execution; no compiled-segment cache | `Interpreter.cpp`: formatString loop; `OpCode.cpp`:1215–1226 | verified | 8 (Q2) |
| I6 | P3 | correctness | execute overloads use different catch scopes; the string wrapper dereferences getCallable without a null guard | `Interpreter.cpp`: execute overloads; `InterpreterContext.cpp`: constructors, getCallable, validateFrame; `InterpreterTest.cpp`, `InterpreterReceiverTest.cpp` | Resolved by review: callable is owned/non-null; stale-frame validation throws; both source paths report failures as diagnostics while typed calls propagate | 5 |
| I7 | P2 | redundancy | Closure count validation is duplicated; raw and typed argument validation have distinct contracts | `Closure.cpp`: acceptsArgumentCount; `Interpreter.cpp`: runFunction(Closure); `VirtualMachine.cpp`: run(Closure) | Shared equivalent count check; specialized runtime/type/storage/result checks retained intentionally | 5 |
| I8 | P3 | redundancy | Compiled-body range check repeated at host boundaries and accepts an unset end sentinel | `ScriptCallableBase.cpp`: hasCompiledBody; `Interpreter.cpp`: callable entry points; `VirtualMachine.cpp`: run(Closure); `FormatString.cpp`: appendFunction | Fixed: shared range predicate rejects unset endpoints, empty/reversed ranges; no new prepared-VM readiness check | 5 |
| I9 | P2 | correctness | getStoredProperty's subspan has no local receiver-range guard | `Interpreter.cpp`: getStoredProperty; `InterpreterValue.cpp`: constructor; `StructType.cpp`: getSize; `CompositeType.cpp`: finalizeFieldLayout, registerProperty | Resolved by review: complete owned receiver storage and finalized nested layouts establish the range; no supported reproducer or extra guard | 5 |
| I10 | P2 | consistency | Stored-property assignment requires exact type equality; call arguments use acceptsArgument. Verify against the Compiler's assignability rules | `Interpreter.cpp`: validateStoredPropertyAssignment, validateCallArgument; `SyntaxTree.cpp`: MemberAssignmentStatement::onResolve; `Compiler.cpp`: resolveStructField/resolveObjectField; `Argument.cpp`: ExpressionArgument::onResolve | Resolved by review: Compiler and Interpreter both use exact descriptor identity for stored-field assignments and acceptsArgument for calls; no behavior change | 5 |
| I11 | P2 | perf | assignStoredProperty allocates a std::vector scratch buffer on every struct field write | `Interpreter.cpp`: assignStoredProperty | verified | 8 |
| I12 | P3 | redundancy | Stored-property path lookup copies each segment into a NomadString; per-owner validation is required | `CompositeType.cpp`: getProperty(NomadStringView); `Interpreter.cpp`: resolveStoredPropertyPath; `InterpreterReceiver.cpp`: resolveProperty | Name-copy removal implemented using existing borrowed-view lookup; required segment/owner checks and diagnostics retained | 5 |
| I13 | P2 | api | setProperty name shared by a functional overload (returns new value) and an in-place overload (void); size-check order differs between PropertyId and path variants | `Interpreter.cpp`: setProperty overloads | verified | 5 |
| I14 | P3 | redundancy | Free function formatDynamicString forwards to Interpreter::formatString | `Interpreter.cpp`: formatDynamicString; `FormatString.hpp`:34 | verified | 1 (done) |
| I15 | P2 | const-correctness | Interpreter::getVirtualMachine() const returns VirtualMachine&; many const methods mutate the VM | `Interpreter.hpp`, `Interpreter.cpp` | verified | 5 |

### InterpreterContext

| ID | Priority | Kind | Finding | Evidence | Status | Step |
| --- | --- | --- | --- | --- | --- | --- |
| C1 | P2 | const-correctness | resolveVariableStorage and resolveParameterStorage are const but return mutable spans | `InterpreterContext.hpp`: resolve* declarations | verified-text | 5 |
| C2 | P2 | perf | One heap allocation per variable (`vector<unique_ptr<InterpreterStorage>>`); retired storage accumulates until the outermost evaluation ends | `InterpreterContext.hpp`: m_variables, m_retiredStorage | verified-text | 8 |
| C3 | P3 | api | Unconventional deleted `const Interpreter&&` overloads; clearError also clears warnings; setError, reportInputError, and addDiagnostic overlap | `InterpreterContext.hpp`: constructors and diagnostics API | verified-text | 5 |
| C4 | P3 | api | Name-keyed accessors alongside ById variants; name lookups cost on every call | `InterpreterContext.hpp`: getVariable*, setVariable* | verified-text | 2, 5 |

### VirtualMachine

| ID | Priority | Kind | Finding | Evidence | Status | Step |
| --- | --- | --- | --- | --- | --- | --- |
| V1 | P1 | correctness | Stack bound checks run after the write and are compiled out in release | `VirtualMachine.cpp`: pushValue, pushBoolean, pushInteger, pushIndex, pushId, pushFloat write `m_stack[m_stackIndex]` before TEST_STACK_OVERFLOW; the macros exist only under NOMAD_DEBUG | verified | 3 |
| V2 | P2 | quality | TEST_STACK_* macros are not hygienic; UNDERFLOW uses the overflow condition | `VirtualMachine.cpp`: macro block at the top of the file | verified | 3 |
| V3 | P2 | dead-code | Unused VM accessors: freeObjectVariable, typed setXVariableValue, initStringVariable, getFunction*VariableValue, setVariableValue forwarder | `VirtualMachine.hpp` (about lines 223–258, 272); `VirtualMachine.cpp`: freeObjectVariable | verified | 1 (done) |
| V4 | P1 | perf | getCurrentCallable() is a linear scan over all callables; reached from closure creation, freeObjectVariable, and dynamic formatting | `Runtime.cpp`: getScriptCallableAtInstructionIndex; `VirtualMachine.cpp`: getCurrentCallable, getFunctionVariableStorage; `OpCode.cpp`: op_dynamic_format_execute; `FormatString.cpp`:113 | verified | 8 |
| V5 | P1 | perf | RTTI conversion on stored-property access; removing it requires a safe receiver-storage boundary | `VirtualMachine.cpp`: getProperty, setProperty, getStoredPropertyAddress; `NomadObject.cpp`, `ScriptObject.cpp`: storage query and allocated field snapshot | Fixed: measured checked storage query replaces VM RTTI; existing guards/layouts retained, native ABI rebuild required | 4 (done) |
| V6 | P1 | perf | Native property access copies NativePropertyInfo (with std::function members) by value on every access | `VirtualMachine.cpp`: getNativeProperty, setNativeProperty; `Runtime.cpp`: getNativePropertyInfo returns std::optional by value; `NativeProperty.cpp`:36 getInfo | verified | 8 |
| V7 | P2 | redundancy | Six execution entry points with near-identical result copy and free logic, and inconsistent error policy (log and return, invalid_argument, VirtualMachineException) | `VirtualMachine.cpp`: executeFunction overloads, runFunction, runClosure, run(ScriptFunction), run(Closure) | Fixed: shared lookup/result finalization; specialized runner contracts retained, host result-copy failures release VM results | 4 |
| V8 | P2 | redundancy | Save and restore of m_result and m_intermediate duplicated; BOOST_SCOPE_EXIT used instead of RAII guards | `VirtualMachine.cpp`: executeCreationBody, createNativeStruct, createNativeObject; `Interpreter.cpp`: evaluation entry points | Resolved by review: existing call-frame protection retained; local restoration outside call frames, no separate register guard | 4 |
| V9 | P2 | redundancy | Obsolete current-frame snapshot API duplicates closure capture-source resolution, including struct receiver layout logic | `VirtualMachine.cpp`: createClosure; `InterpreterCallFrame.cpp`: current-frame capture preparation | Fixed: removed test-only snapshot API; migrated tests to production frame and closure paths | 4 |
| V10 | P2 | correctness | Reported as a catch block reading moved-from storage. Not reproduced: the Closure constructor takes the vector only as its last statement, so the catch frees storage only when ownership has not transferred | `VirtualMachine.cpp`: createClosure try/catch; `Closure.cpp`: constructor | not reproduced | 4 |
| V11 | P2 | correctness | getStoredPropertyAddress bounds-checked only lastOffset and indexed firstOffset | `VirtualMachine.cpp`: getStoredPropertyAddress | Fixed: checks firstOffset <= lastOffset < storage size; regression in StructTypeTest | 4 |
| V12 | P2 | correctness | Missing null checks: asScriptMethod and asScriptConstructor results, getReturnType dereference in createNativeObject, getNativeFunction in callNativeFunction | `VirtualMachine.cpp`: initializeStructStorage, createScriptObject, createScriptStruct, createNativeObject, callNativeFunction | Resolved: compiler-selected metadata is trusted; absent initializer and native null-result guards remain | 1 (native, done), 4 (reviewed) |
| V13 | P3 | const-correctness | getCurrentFunction() const returns a non-const ScriptFunction*; receiver validation repeats on copies and binding | `VirtualMachine.hpp`: getCurrentScript; `VirtualMachine.cpp`: getThis, bindThisFromStack, copyThisToResult, copyThisToIntermediate | Fixed: const script getter and unchecked bound-receiver copies; host/receiver-binding null checks intentionally retained | 2 (done), 4 (done) |
| V14 | P3 | naming | `stack_size` parameter; `integer_value` and `float_value` locals; memberId used for both CallableId and PropertyId; get*Function*VariableValue and set*VariableValue are asymmetric | `VirtualMachine.hpp`: constructor and accessors; `VirtualMachine.cpp`: nextInteger, nextFloat, callNativeMethod | verified | 7 |
| V15 | P2 | quality | runNativeIndexer uses magic offsets (+2, +3) and convoluted release index arithmetic; duplicates the m_result reset in callNativeIndexer | `VirtualMachine.cpp`: runNativeIndexer, callNativeIndexer | Implemented: named receiver slot count and position, explicit copied-argument count; early reset retained for copy failures | 4 |
| V16 | P3 | perf | run(ScriptFunction) allocates a `vector<const Type*>` per host call; the closure-argument path has unclear callers | `VirtualMachine.cpp`: run(ScriptFunction), run(Closure) overloads; verify closure-argument callers | partly-verified | 8 |
| V17 | P3 | quality | fault() is not `[[noreturn]]`; many fault-then-continue patterns | `VirtualMachine.hpp`: fault declaration | Fixed: `[[noreturn]]` | 4 |

### Compiler

| ID | Priority | Kind | Finding | Evidence | Status | Step |
| --- | --- | --- | --- | --- | --- | --- |
| K1 | P1 | correctness | Struct-typed class fields compile to native property get and set ops, but `ScriptObject::getField` and `setField` reject struct-typed fields | `Compiler.cpp`: addPropertyAccess, resolveObjectField; `ScriptObject.cpp`: validateSingleSlotFieldAccess | Script paths pass the K1 repro; direct struct-field fallback now reports an internal error | 3 |
| K2 | P1 | correctness | No reserved stack slot count is computed; the VM relies on debug-only checks | Compiler.cpp: verifyReservedStackSlots, computeReservedStackSlots | Fixed: verifier computes the reserved slots per callable; script call opcodes carry the count as an operand and frame entry checks it | 3 |
| K3 | P2 | correctness | `addLoadFloatValue` compares `value == 0`, so `-0.0` compiles to `+0.0` | `Compiler.cpp`: addLoadFloatValue | from review | 3 |
| K4 | P3 | robustness | `Compiler::inferTypes` iterates to a fixed point with no iteration cap | `Compiler.cpp`: inferTypes | Fixed: pass count bounded, diagnostic on overflow | 3 |
| K5 | P3 | redundancy | Call emitters repeat the script and native split and operand setup (addFunctionCallInstruction through addNativeMethodCall) | `Compiler.cpp`: call-emission block | from review | 7 |
| K6 | P3 | redundancy | getIdentifierType and getIdentifierDefinition duplicate identifier resolution | `Compiler.cpp`: getIdentifierType, getIdentifierDefinition | from review | 7 |
