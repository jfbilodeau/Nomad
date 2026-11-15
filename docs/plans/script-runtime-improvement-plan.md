# Script runtime improvement plan

This plan records the review of `Runtime`, `Interpreter`, `Compiler`, `VirtualMachine`, and `InterpreterContext`. It holds the approved decisions, the staged work, and every finding with its priority, evidence, verification status, and target step. Update the status as each step lands.

**Status:** Decisions D1–D5 are approved. Steps 1 and 2 (2a and 2b) are implemented and validated. All 13 CTest entries passed. Step 3 (compiler correctness) is in progress: K1, K3, and K4 are addressed; K2 is next.

## Component contract

- **Compiler** owns correctness of emitted OpCodes: identifier validity, native ID resolution, null prevention, and exact stack depth.
- **VirtualMachine** is built for performance and trusts Compiler output. It keeps only the checks the host boundary needs.
- **Interpreter** is built for simplicity and convenience. Evaluating strings is its primary function. It validates host input at the boundary.
- **InterpreterContext** is the host-facing evaluation context. It exposes production APIs only.

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
3. K2: compute exact operand-stack depth during emission and store it with the callable's frame layout (D2). The per-push checks (V1, V2) are already removed. Keep one frame-capacity check per script call, because recursion makes depth unbounded across calls.
4. K3: use `std::signbit` to detect negative-zero literals.
5. K4: bound the `inferTypes` fixed-point loop, and report a diagnostic when the bound is reached.

### Step 4: VM trust boundary

- Remove checks the Compiler already guarantees (V12 script paths, V13 `getThis` idioms). Mark `fault` as `[[noreturn]]` (V17).
- Replace `dynamic_cast<ScriptObject*>` on property access with the compiler-guaranteed type (V5).
- Fix correctness bugs first: V10 (closure creation reads moved storage) and V11 (bounds check uses the wrong index).
- Deduplicate capture logic (V9), save and restore guards (V8), and the six execution entry points (V7). Keep the host-facing validation.
- Simplify `runNativeIndexer` (V15).
- Resolve Q3 (`friend` declaration).

### Step 5: Interpreter correctness and simplification

- Correctness first: I1 (running flag not restored), I9 (receiver storage bounds), I3 (confirm caller contexts before changing the reset), I6 (exception policy and null check), I10 (confirm assignability rules).
- Simplify: one compile pipeline (I4), one native invocation policy (I2), one closure and compiled-function validation path (I7, I8), stored-property paths resolved once (I12), unambiguous `setProperty` names (I13), const-correct VM access (I15).
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
- **Q2 (step 8):** Cache compiled format-template segments (I5), or keep the Interpreter simpler and accept re-parsing? Measure first.
- **Q3 (step 4):** `VirtualMachine.hpp` declares `friend class InterpreterContext;`, which conflicts with the no-`friend` rule. Replace it with a narrow public API.

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
| I1 | P2 | correctness | `runNativeCallable` sets `state.running` from the current VM state before restore, so the running flag is never restored; the comment is misleading | `Interpreter.cpp`: runNativeCallable `BOOST_SCOPE_EXIT` block | verified-text | 5 |
| I2 | P2 | consistency | Three native invocation policies: runFunction always uses runNativeCallable; runStructMethod is always synchronous; runObjectMethod chooses by a struct-return and argument heuristic | `Interpreter.cpp`: runFunction, runStructMethod, runObjectMethod | verified | 5 |
| I3 | P2 | correctness | evaluateFormatExpression calls resetEvaluation (clears diagnostics, result, loop control). Safe for the fresh context in `OpCode.cpp`; caller contexts at `Expression.cpp`:346,1752 are unverified | `Interpreter.cpp`: evaluateFormatExpression, formatString; `Expression.cpp`:346,1752 | partly-verified | 5 |
| I4 | P2 | redundancy | Compile pipeline duplicated in execute(SourceText) and evaluateFormatExpression; each dynamic-format segment creates a new Compiler (which runs setup callbacks) | `Interpreter.cpp`: execute, evaluateFormatExpression; `Runtime.cpp`: createCompiler | verified | 5 |
| I5 | P2 | perf | Dynamic format templates are re-tokenized and re-parsed on every execution; no compiled-segment cache | `Interpreter.cpp`: formatString loop; `OpCode.cpp`:1215–1226 | verified | 8 (Q2) |
| I6 | P3 | correctness | execute(NomadString) catches NomadBug, while execute(SourceText) catches std::exception; getCallable() is dereferenced without a null check | `Interpreter.cpp`: execute overloads | partly-verified | 5 |
| I7 | P2 | redundancy | Closure call validation duplicated between Interpreter::runFunction(Closure) and VirtualMachine::run(Closure); argument validation loops repeated | `Interpreter.cpp`: runFunction(Closure), validateCallArguments; `VirtualMachine.cpp`: run(Closure) | verified | 5 |
| I8 | P3 | redundancy | Compiled-function check (functionStart invalid or >= functionEnd) repeated in four or more places | `Interpreter.cpp`: runFunction (x2), construct, validateMethodImplementation; `VirtualMachine.cpp`: run(Closure) | verified | 5 |
| I9 | P2 | correctness | getStoredProperty's subspan does not validate that receiver storage covers offset + size | `Interpreter.cpp`: getStoredProperty | verified | 5 |
| I10 | P2 | consistency | Stored-property assignment requires exact type equality; call arguments use acceptsArgument. Verify against the Compiler's assignability rules | `Interpreter.cpp`: validateStoredPropertyAssignment vs validateCallArgument | unverified | 5 |
| I11 | P2 | perf | assignStoredProperty allocates a std::vector scratch buffer on every struct field write | `Interpreter.cpp`: assignStoredProperty | verified | 8 |
| I12 | P3 | redundancy | resolveStoredPropertyPath repeats resolveStoredProperty checks per segment and allocates a NomadString per segment | `Interpreter.cpp`: resolveStoredPropertyPath | verified | 5 |
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
| V4 | P1 | perf | getCurrentCallable() is a linear scan over all callables; reached from closure creation, captureCurrentFrame, freeObjectVariable, and dynamic formatting | `Runtime.cpp`: getScriptCallableAtInstructionIndex; `VirtualMachine.cpp`: getCurrentCallable, getFunctionVariableStorage; `OpCode.cpp`: op_dynamic_format_execute; `FormatString.cpp`:113 | verified | 8 |
| V5 | P1 | perf | dynamic_cast<ScriptObject*> on every stored-property access | `VirtualMachine.cpp`: getProperty, setProperty, getStoredPropertyAddress | verified | 4 |
| V6 | P1 | perf | Native property access copies NativePropertyInfo (with std::function members) by value on every access | `VirtualMachine.cpp`: getNativeProperty, setNativeProperty; `Runtime.cpp`: getNativePropertyInfo returns std::optional by value; `NativeProperty.cpp`:36 getInfo | verified | 8 |
| V7 | P2 | redundancy | Six execution entry points with near-identical result copy and free logic, and inconsistent error policy (log and return, invalid_argument, VirtualMachineException) | `VirtualMachine.cpp`: executeFunction overloads, runClosure, run(ScriptFunction), run(Closure) | verified | 4 |
| V8 | P2 | redundancy | Save and restore of m_result and m_intermediate duplicated; BOOST_SCOPE_EXIT used instead of RAII guards | `VirtualMachine.cpp`: executeCreationBody, createNativeStruct, createNativeObject; `Interpreter.cpp`: RegisterRestoration | verified | 4 |
| V9 | P2 | redundancy | Capture-source switch duplicated between captureCurrentFrame and createClosure, including struct receiver layout logic | `VirtualMachine.cpp`: captureCurrentFrame, createClosure | verified | 4 |
| V10 | P2 | correctness | createClosure's catch block reads storage after `std::move(storage)` into the Closure | `VirtualMachine.cpp`: createClosure try/catch | verified | 4 |
| V11 | P2 | correctness | getStoredPropertyAddress bounds-checks lastOffset but indexes firstOffset; callers always pass equal values | `VirtualMachine.cpp`: getStoredPropertyAddress; `VirtualMachine.hpp` declaration; verify all callers | partly-verified | 4 |
| V12 | P2 | correctness | Missing null checks: asScriptMethod and asScriptConstructor results, getReturnType dereference in createNativeObject, getNativeFunction in callNativeFunction | `VirtualMachine.cpp`: initializeStructStorage, createScriptObject, createScriptStruct, createNativeObject, callNativeFunction | verified | 1 (native, done), 4 (script) |
| V13 | P3 | const-correctness | getCurrentFunction() const returns a non-const ScriptFunction*; the `(void)getThis()` validation idiom is repeated; bindThisFromStack duplicates the getThis null check | `VirtualMachine.hpp`:120; `VirtualMachine.cpp`: getThis, bindThisFromStack, copyThisToResult | verified | 2 (getCurrentFunction), 4 |
| V14 | P3 | naming | `stack_size` parameter; `integer_value` and `float_value` locals; memberId used for both CallableId and PropertyId; get*Function*VariableValue and set*VariableValue are asymmetric | `VirtualMachine.hpp`: constructor and accessors; `VirtualMachine.cpp`: nextInteger, nextFloat, callNativeMethod | verified | 7 |
| V15 | P2 | quality | runNativeIndexer uses magic offsets (+2, +3) and convoluted release index arithmetic; duplicates the m_result reset in callNativeIndexer | `VirtualMachine.cpp`: runNativeIndexer, callNativeIndexer | verified | 4 |
| V16 | P3 | perf | run(ScriptFunction) allocates a `vector<const Type*>` per host call; the closure-argument path has unclear callers | `VirtualMachine.cpp`: run(ScriptFunction), run(Closure) overloads; verify closure-argument callers | partly-verified | 8 |
| V17 | P3 | quality | fault() is not `[[noreturn]]`; many fault-then-continue patterns | `VirtualMachine.hpp`: fault declaration | verified | 4 |

### Compiler

| ID | Priority | Kind | Finding | Evidence | Status | Step |
| --- | --- | --- | --- | --- | --- | --- |
| K1 | P1 | correctness | Struct-typed class fields compile to native property get and set ops, but `ScriptObject::getField` and `setField` reject struct-typed fields | `Compiler.cpp`: addPropertyAccess, resolveObjectField; `ScriptObject.cpp`: validateSingleSlotFieldAccess | Script paths pass the K1 repro; direct struct-field fallback now reports an internal error | 3 |
| K2 | P1 | correctness | No maximum operand-stack depth is computed; the VM relies on debug-only checks | `Compiler.cpp`: emission paths; `VirtualMachine.cpp`: TEST_STACK_* macros | from review | 3 |
| K3 | P2 | correctness | `addLoadFloatValue` compares `value == 0`, so `-0.0` compiles to `+0.0` | `Compiler.cpp`: addLoadFloatValue | from review | 3 |
| K4 | P3 | robustness | `Compiler::inferTypes` iterates to a fixed point with no iteration cap | `Compiler.cpp`: inferTypes | Fixed: pass count bounded, diagnostic on overflow | 3 |
| K5 | P3 | redundancy | Call emitters repeat the script and native split and operand setup (addFunctionCallInstruction through addNativeMethodCall) | `Compiler.cpp`: call-emission block | from review | 7 |
| K6 | P3 | redundancy | getIdentifierType and getIdentifierDefinition duplicate identifier resolution | `Compiler.cpp`: getIdentifierType, getIdentifierDefinition | from review | 7 |
