# Interpreter cleanup plan

Status: Proposed; no implementation authorized or started.
Baseline: November 12, 2026 review of
[Interpreter.cpp](../../src/nomad/interpreter/Interpreter.cpp).

Implement one approved step at a time, report its changes and verification,
then stop for review. Approval of this plan does not authorize autonomous
execution of all steps, commits, or pushes.

## Goal and scope

Remove repeated work, duplicated algorithms, misleading helpers, and overlapping
execution machinery without changing language or host API behavior.

Primary scope:

- [Interpreter implementation](../../src/nomad/interpreter/Interpreter.cpp) and
  [public interface](../../include/nomad/interpreter/Interpreter.hpp).
- Directly coupled interpreter storage, call-frame, and VM execution code.
- Existing focused tests and directly affected documentation.
- Parser, tokenizer, and diagnostic APIs only in the separately approved
  structural steps below.

Preserve unrelated worktree changes. In particular, do not edit the existing
[RAII cleanup plan](raii-cleanup-plan.md) or reverse its ownership and
exception-safety work. This plan does not authorize broader RAII migration.

### Constraints

- Preserve specialized object and struct APIs. Share repeated mechanics only
  when that produces simpler code; modest duplication is preferable to a
  universal invocation framework, templates, or mode-flag dispatch.
- Retain host-boundary validation. Host calls bypass the source compiler.
- Preserve immutable snapshots versus mutation of borrowed live struct storage.
- Preserve raw-pointer reference semantics, reference counts, copy-failure
  cleanup, strong assignment guarantees, and zero-sized values.
- Preserve scalar suspension and suspended-frame ownership, aggregate and
  method no-yield rules, prior yield state, nested calls, receiver bindings,
  stack layout, and register restoration.
- Preserve source execution's diagnostic translation versus dynamic formatting's
  propagation of execution faults.
- Preserve the free formatting bridge, public overloads, diagnostic locations,
  inline markers, and user-visible messages unless a change is separately
  approved.
- Do not add opcodes, language restrictions, NRVO machinery, ownership adoption
  APIs, new dependencies, or unrelated formatting.
- Helper count and line count are not acceptance targets. Do not remove useful
  guards or replace a short duplicate with a larger abstraction.

## Implementation steps

### Step 1: Remove mechanical redundancy

Review findings: 6, 7, 8, 10.

Changes:

1. Make method-receiver validation return `void`; remove assignments of the
   unchanged receiver pointer.
2. Keep the `std::exception` handler in source execution and remove the identical
   `NomadException` handler, whose exception type is already covered.
3. Use `InterpreterContext::getError()` once in format-error reporting instead
   of searching diagnostics and then invoking the same search again. If a
   failed evaluation has no diagnostic, surface that broken invariant explicitly
   rather than dereferencing a null pointer or inventing a success-shaped value.
4. Construct and append expression statements inside their branch; remove the
   staging pointer and unconditional null append.

Verification:

- Host validation still rejects invalid receivers before callback execution.
- Both standard and Nomad execution exceptions retain their existing source
  diagnostics, clear results, and leave the interpreter usable.
- Expression, assignment, statement, empty-input, and format-error behavior
  remains unchanged.

Stop for review.

### Step 2: Resolve properties once and centralize assignment

Review findings: 1, 2. Independent of Step 1.

Changes:

1. Resolve a named property path directly to the existing type-and-offset
   selection. Remove the intermediate property-ID vector and second traversal
   for named access.
2. Retain the single-ID entry points and path validation. Support both name
   and ID selection without forcing either through an allocated container.
3. Keep exactly one implementation of assignment type checking, scratch
   allocation, destination selection, and `Type::assignValue`.
4. Have immutable setters copy the receiver and delegate mutation; borrowed
   setters must continue changing only the selected field in live storage.
5. Remove obsolete private overloads only after all callers are migrated.

Verification:

- Nested paths select the correct physical slots for scalar, reference,
  nonempty struct, and empty struct fields.
- Empty names, missing fields, native properties, invalid owners, and
  mismatched value types retain rejection behavior.
- Immutable inputs remain unchanged; borrowed storage retains its address.
- Assignment failures preserve the old destination and release partial copies.
- Unrelated right-hand-side mutations are not overwritten.
- Inspect the resulting call path to confirm named access performs one metadata
  traversal and no intermediate property-ID allocation.

Stop for review.

### Step 3: Consolidate common method metadata validation

Review finding: 5. Depends on Step 1's receiver-validation signature.

Changes:

1. Share repeated method lookup and common callable/implementation validation.
2. Keep object and struct resolvers specialized, with explicit receiver-kind
   checks and their existing error precedence.
3. Do not broaden this into generalized callable dispatch or indiscriminately
   remove validation already present in lower-level call-frame code.

Verification:

- Unknown IDs, wrong callable categories, foreign runtimes, incompatible owners,
  invalid arguments, uncompiled methods, and unsupported implementations are
  rejected before execution and without changing VM state.
- Valid native and script object/struct calls retain their results and mutations.

Stop for review.

### Step 4: Give synchronous native invocation one implementation

Review finding: 3. Independent of Steps 2 and 3.

Changes:

1. Introduce or reuse one narrowly scoped synchronous-native execution path
   for struct methods and aggregate object methods.
2. Save enclosing execution state once, enter the native frame with the correct
   receiver, isolate the invocation's yield flag, reject yielding, and restore
   the original state on success or exception.
3. Retain the separate scalar-native behavior that propagates yield/stop state.
4. Remove redundant nested snapshots and the obsolete struct-only wrapper.
   Preserve existing fault messages without adding a universal policy object.

Verification:

- Exercise struct and aggregate object callbacks with both initially yielded
  and initially non-yielded VMs, normal returns, stop, yield, and exceptions.
- Verify stack, instruction position, running/yielded flags, receivers, result,
  and intermediate registers after each case.
- Exercise native reentry from an active script/class frame and partial
  argument/result-copy failures.
- Confirm scalar-native behavior has not been made synchronous accidentally.

Stop for review.

### Step 5: Consolidate ordinary script-function execution

Review finding: 4. Schedule after Step 4; no code dependency on property cleanup.

Approval gate:

- Compare the existing scalar, aggregate, and closure routes against
  `InterpreterCallFrame` and `VirtualMachine::executeFunction`.
- Present the proposed ownership/restoration sequence before implementation.
  Reuse is conditional on preserving suspension and cleanup contracts; do not
  assume the routes are interchangeable because they execute the same bytecode.

Changes, if the gate is approved:

1. Use the existing typed call frame and VM execution machinery for ordinary
   functions wherever their contracts match.
2. Transfer argument ownership to the VM only for a suspended scalar frame.
3. Remove the interpreter's temporary raw-argument vector and redundant
   result/register machinery only where the frame replaces their obligations.
4. Keep closure capture handling specialized. Do not expand this step into
   redesigning all VM host APIs or deleting their independent validation.
5. Preserve aggregate fault text and restoration behavior explicitly.

Verification:

- Scalar calls can suspend, retain their arguments, and resume correctly.
- Aggregate arguments/results, including empty structs, reject suspension.
- Existing prior-yield state and class-construction restrictions are preserved.
- Nested calls restore registers, stack, active callable, and receiver correctly.
- Argument/capture copying, reference counts, partial failures, and result
  snapshot failures satisfy the existing ownership tests.
- Confirm the ordinary function path no longer maintains separate argument
  packing and result ownership solely for scalar values.

Stop for review; retain the existing split if consolidation cannot stay simple.

### Step 6: Simplify format-expression diagnostic transfer

Review finding: 9. Independent of invocation cleanup.

Changes:

1. Transfer compiler diagnostics at the relevant return boundaries rather than
   copying them early and using evaluation-diagnostic count as a compiler cursor.
2. Ensure each warning/error is retained exactly once, in reporting order.
3. Keep strict one-expression parsing and exception-only result clearing.
   Do not merge this path with statement execution merely to share setup.

Verification:

- Tokenization, parsing, resolution, void-expression, and trailing-input failures
  retain source names and locations without duplicate diagnostics.
- Warnings followed by errors retain their order.
- Nested templates continue after syntax/type errors without executing invalid
  placeholders; execution exceptions still propagate.

Stop for review.

### Step 7: Move interactive-input classification into the parser

Review finding: 11. Separate architectural approval required.

Before implementation, agree on a parser entry point and document how it handles
single-line values, void calls, assignments, registered statements, multiline
input, and member expressions. Preserve current grammar and resolution priority.

Changes:

- Move the interpreter's syntax/function-return-type classification and repeated
  expression-statement construction into the parser's existing resolution paths.
- Keep strict formatting expressions separate from interactive input.
- Remove obsolete classification code from the interpreter, not merely relocate
  it behind another interpreter helper.

Verification:

- Add a focused input-classification matrix covering the cases above, unknown
  names, qualified calls, malformed input, and lexical/member receiver lookup.
- Confirm public execution results and diagnostic behavior remain unchanged.

Stop for review.

### Step 8: Replace newline padding with an explicit source origin

Review finding: 12. Separate tokenizer API approval required.

Changes:

- Agree on a minimal optional source-line origin with a behavior-preserving
  default for existing tokenizer callers.
- Tokenize the actual placeholder text rather than prepended fake lines.
- Validate the one-expression requirement independently of the source origin.

Verification:

- Check line origins 1 and a large valid line, plus current zero/invalid-index
  normalization.
- Verify lexical and parser error locations, nested templates, multiline
  rejection, and unchanged default tokenizer behavior.
- Confirm placeholder tokenization no longer allocates/scans a prefix whose
  length is proportional to the containing source line.

Stop for review.

### Step 9: Remove diagnostic-message parsing

Review finding: 13. Separate diagnostic API approval required.

Changes:

- First inventory the exact diagnostic producers used for missing braces, empty
  expressions, and unknown identifiers.
- Propose the smallest structured diagnostic distinction that removes these
  text comparisons. Avoid a repository-wide diagnostic taxonomy migration.
- Preserve existing inline markers, logged messages, structured format results,
  and compatibility-bridge behavior.

Verification:

- Assert the existing output text, message, source name, line, column, offset,
  and length for each affected error category.
- Confirm presentation no longer depends on exact compiler diagnostic wording.

Stop for review.

## Verification workflow

This saved plan changes documentation only; do not build or run C++ tests for it.

For approved implementation steps:

- Establish the relevant baseline using CTest directly, without a separate build
  first, through the workspace's `CMake: run tests` task or the active test preset.
- Use the smallest relevant existing CTest groups. Current registration places
  interpreter suites in `nomad_test_remaining`, language-format suites in
  `nomad_test_language`, and parser/tokenizer cases according to their existing
  suite registration. Confirm selectors using test discovery rather than
  assuming filenames are CTest names.
- Record compilation separately using the workspace CMake build tasks. CTest
  success against stale binaries does not validate edited code; establish that
  implementation test results cover current sources before marking a step done.
- Extend existing tests instead of creating parallel test infrastructure.
- Run the relevant interpreter and language groups together after invocation or
  parser changes. Run the broader unit suite for final reconciliation.
- Use available sanitizer coverage for ownership-sensitive steps; report when
  it was not run. Do not claim cross-platform or gameplay validation from a
  Windows compilation or unit-test result.
- After two unsuccessful build attempts, stop and report the remaining errors.

Relevant regression suites:

- [InterpreterTest](../../tests/InterpreterTest.cpp)
- [InterpreterStructPropertyTest](../../tests/InterpreterStructPropertyTest.cpp)
- [InterpreterStructAssignmentTest](../../tests/InterpreterStructAssignmentTest.cpp)
- [InterpreterStructCallTest](../../tests/InterpreterStructCallTest.cpp)
- [InterpreterStructMethodTest](../../tests/InterpreterStructMethodTest.cpp)
- [InterpreterReceiverTest](../../tests/InterpreterReceiverTest.cpp)
- [LanguageFormatStringFunctionTest](../../tests/LanguageFormatStringFunctionTest.cpp)
- [ParserTest](../../tests/ParserTest.cpp)
- [TokenizerTest](../../tests/TokenizerTest.cpp)
- [CTest registration](../../tests/CMakeLists.txt)

## Completion criteria

- All 13 review findings are implemented or explicitly retained with a concrete
  reason approved during review.
- Each step removes repeated mechanics or makes an existing contract clearer,
  without adding a larger abstraction than it replaces.
- Public host APIs, language semantics, ownership, suspension, error text, and
  diagnostic locations remain compatible unless separately approved.
- Focused regression tests exercise current implementation artifacts; compilation
  and runtime verification are reported separately.
- Directly affected public documentation is updated when necessary. Internal
  cleanup alone does not justify expanding user documentation.
- Final report lists completed steps, retained machinery, verification results,
  and any deferred structural decisions.
