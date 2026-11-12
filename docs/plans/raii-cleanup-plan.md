# RAII cleanup plan

Status: Step 6 complete; final inventory reconciled and Windows Debug validation
passed. Awaiting final review; no further implementation slice is scheduled.
Filesystem cleanup-error behavior changes require separate approval.
Baseline: the first-party catch-all review of November 11, 2026.

This plan follows the completed lifecycle exception-safety and caller-cleanup
work. Preserve unrelated worktree changes. Implementation through Step 4 and its
review corrections, the Step 5A/5B reviews, Step 6 validation, the concrete
value-guard contract, and using Boost.ScopeExit macros have been approved.
Additional behavior changes, commits, and pushes are not authorized.

Implement one approved slice at a time. After each slice, report the changes,
validation, remaining catches, and next slice, then stop for review.

## 1. Goal and scope

Remove duplicated cleanup performed in both a catch handler and the success
path through automatic ownership or scoped state restoration. Retain ordinary
`try`/`catch` when success and exception paths perform distinct work; replacing
every catch-all is not a goal.

The review found 63 handlers in 26 first-party C++ files:

| Classification | Handlers | Strategy |
| --- | ---: | --- |
| Owned values and references | 22 | Scoped owners, owning members, explicit ownership transfer |
| VM frames, state, and arguments | 17 | Specialized invocation, execution, stack, and argument guards |
| Failure-only rollback and commit | 10 | Transactions that roll back unless committed |
| Simple state and test hooks | 7 | Small non-allocating restoration guards |
| Filesystem transactions | 2 | Checked finalization and nonthrowing emergency rollback |
| Genuine exception handling | 5 | Retain exception capture or diagnostic translation |
| **Total** | **63** | **58 cleanup candidates; 5 intentional boundaries** |

These are reviewed candidates, not a requirement to delete exactly 58 catches.
Every retained cleanup catch needs a specific justification. Preserve observable
behavior rather than optimizing a catch count.

Scope includes first-party source, headers, applications, modules, samples,
templates, and tests. Exclude generated, build, and vendored code.

### Non-goals

- Do not redesign the language, VM instruction set, or exception model.
- Do not make `RuntimeValue` automatically owning or replace its object pointers
  with `std::shared_ptr`.
- Do not add general VM fault recovery or resume previously fatal executions.
- Do not change callback ownership, scalar suspension, aggregate no-yield rules,
  iteration behavior, or filesystem overwrite policies.
- Do not refactor unrelated code or mechanically replace every catch with a
  generic lambda.

## 2. Existing contracts and prior art

- [Type lifecycle operations](../../include/nomad/script/Type.hpp) already promise
  leak-free initialization/copy failure with cleared destination slots when
  metadata is valid. `freeValue` is `noexcept`; `assignValue` preserves an owned
  destination when copying fails. Do not restore redundant caller cleanup.
- [InterpreterEvaluationGuard](../../include/nomad/interpreter/InterpreterContext.hpp)
  demonstrates scoped entry/exit. Preserve nested evaluation lifetimes: retired
  storage is released only at the outermost exit.
- [InterpreterStorage](../../include/nomad/interpreter/InterpreterStorage.hpp) and
  [InterpreterValue](../../include/nomad/interpreter/InterpreterValue.hpp) already
  own complete typed storage. Reuse them where their copying and allocation
  behavior matches the operation.
- [InterpreterCallFrame](../../include/nomad/interpreter/InterpreterCallFrame.hpp)
  owns arguments/results and supports transfer to a suspended VM.
- [RegisterRestoration](../../src/nomad/interpreter/Interpreter.cpp),
  [compiler scopes](../../include/nomad/compiler/CompilerContext.hpp), and
  [debug-console guards](../../src/nomad/game/debug/DebugConsole.cpp) provide
  additional restoration patterns.
- [Script-object creation](../../src/nomad/script/VirtualMachine.cpp) already
  uses `std::unique_ptr` with a `releaseRef` deleter. Reuse that ownership model
  rather than deleting reference-counted objects directly.

Standard C++ has no Java-style `finally`. Destruction at scope exit is the
appropriate mechanism here. The project remains C++20.
`std::experimental::scope_exit` belongs to the Library Fundamentals TS, not
ISO C++23, and cannot be assumed available across supported toolchains.
Boost.ScopeExit supplies the `BOOST_SCOPE_EXIT` and `BOOST_SCOPE_EXIT_ALL`
macros for unconditional cleanup on the project's existing C++20 baseline.

## 3. Design decisions to settle before migration

Step 1A's concrete `RuntimeValueGuard` contract is approved and recorded below.
Review remaining contracts before their implementation slices. Other names below
describe roles, not approved public APIs.

### Owned temporaries

- Prefer existing owners or a narrowly scoped typed-storage owner/guard.
  Adopt or protect the original owned slots instead of creating an extra copy
  merely to arrange cleanup.
- Use `const Type*` to release complete typed storage. The descriptor must
  outlive the owner. Borrowed slots and owned contents are distinct concepts.
- Support an initially cleared output guarded before invoking a native getter;
  such callbacks can acquire resources and then throw.
- Transfer ownership explicitly only after a container insertion, binding
  construction, or result handoff succeeds.
- Keep single-slot ownership stack-only. Do not introduce heap allocation for
  guard bookkeeping or add allocations to existing allocation-free VM paths.
- Distinguish borrowed formatting/context/receiver values from owned getter
  results. Never attach an owning guard to a borrowed reference.

### Restoration and rollback

- Separate unconditional scope exit from rollback-unless-committed. A failed
  operation can require cleanup that would be wrong on success.
- Keep exception-only rollback in readable `try`/`catch` blocks rather than
  introducing scope guards and commit flags solely to remove a catch.
- Execution exceptions remain fatal. Constructor opcodes do not unwind argument
  ownership on failure; ordinary successful bytecode still releases arguments.
- Prefer specialized VM guards over a universal guard with mode flags and
  type-condition branches.
- For unconditional simple local restoration, use `BOOST_SCOPE_EXIT` or
  `BOOST_SCOPE_EXIT_ALL` from `<boost/scope_exit.hpp>`. Step 2 uses
  `BOOST_SCOPE_EXIT_ALL` to avoid MSVC shadowing diagnostics from the legacy
  macro form and enables the header-only `scope_exit` component
  in the existing pinned [Boost dependency](../../cmake/Dependencies.cmake),
  privately linked to the language library and tests. Do not add a custom
  `ScopeExit` template for this purpose.
- `BOOST_SCOPE_EXIT_ALL` stores its callable through Boost.Function. Keep
  captures small and retain allocation checks when changing them; do not assume
  arbitrary capture lists are allocation-free.
- Do not use `std::function` or heap allocation just to store a cleanup action.
- Delete copying; introduce moving only when required, preserving exactly-once
  cleanup. Make discardable guard temporaries difficult to misuse.
- Guard acquisition must not leave mutated state unprotected if acquisition
  throws. Acquire first, or make the state-changing construction exception-safe.
- Destructors and cleanup callbacks must not throw. Verify the actual restore
  operations before declaring them `noexcept`; preserve fatal invariant checks.
- Respect reverse destruction order. Release current results and argument
  resources before restoring the enclosing registers, stack, and receiver.
- Preserve scoped timing, not just eventual cleanup. Use nested scopes when
  restoration must finish before formatting or another callback begins.
- Put nontrivial guards in matching header/source files, not registration files.
  Avoid friendship or expanded public APIs unless discussed and approved.

### Filesystem failure reporting

Filesystem cleanup can fail. A destructor cannot safely replace a throwing
cleanup path without deciding how those failures are reported.

- Use explicit checked finalization on the normal path.
- Use nonthrowing emergency cleanup during unwinding.
- Preserve the original failure; report cleanup failures through an agreed
  diagnostic path and retain recoverable backup artifacts.
- Track only paths created or owned by the operation. Do not broaden deletion
  scope or remove pre-existing content.

The precise secondary-error reporting policy is a review decision before the
filesystem slice, not an implicit behavior change.

## 4. Reviewable implementation slices

### Step 1A: establish ownership contracts with one pilot

1. Agree on the minimal scoped-owner API and adoption/transfer semantics.
2. Reuse existing storage owners where appropriate. Add only a missing,
   narrowly scoped mechanism for guarding existing typed slots or references.
3. Migrate the temporary string in
   [ScriptBindingObject::setString](../../src/nomad/modules/ScriptBindingObject.cpp).
4. Add focused success, exception, early-return, adoption, and transfer tests.

Acceptance: original references are released exactly once, borrowed references
are untouched, and arranging single-slot cleanup allocates nothing.

Step 1A outcome:

- Added [RuntimeValueGuard](../../include/nomad/script/RuntimeValueGuard.hpp) with
  `RuntimeValueGuard(const Type&, RuntimeValue*) noexcept`. It adopts cleanup of
  existing cleared/owned slots; it does not allocate, copy, retain, or own the
  storage itself.
- Its `noexcept` destructor calls `freeValue`. `release()` only disarms cleanup
  and does not change slot contents or reference counts. Copying and moving are
  deleted; the type and storage must outlive the guard.
- Migrated only the temporary string in the binding setter. No other production
  cleanup handlers changed.
- Added six ownership tests covering adoption, normal/early/exceptional exit,
  transfer, borrowed-source references, nested full-width storage, and cleared
  and zero-slot values. Added a Windows Debug CRT allocation check that measured
  zero allocations for guard bookkeeping, and checked the generated string
  setter's retained-reference count.
- The workspace CTest action built current Windows Debug binaries.
  `nomad_test_bindings` and `nomad_test_remaining` passed; the test log confirms
  all seven new guard/allocation cases executed. Whitespace validation passed.
- After Step 1A, the first-party catch-all count was 62, down from 63. The
  remaining 21 owned-value/reference candidates were assigned to Step 1B.

Stop for review before migrating more sites.

### Step 1B: migrate owned values and references

Apply the approved ownership model to the remaining sites in this family:

- Collection key/value construction, list insertion, map keys/entries/insertion.
- Compiler initial values and interpreter loop defaults.
- Formatted strings, indexer read results, dynamic getters, empty context strings.
- Interpreter function/property results and dynamic formatting values.
- OS snapshots/environment/arguments, logger registration, and binding creation.

Preserve copy-before-insertion for self-insertion and reference stability.
Successful insertion transfers the copied reference; failed insertion leaves it
owned by the temporary. Constructors must unwind already completed members.
Do not assume the containing object's destructor runs if its constructor throws.

Acceptance: cover first/second-field failures, each staged copy failure, partial
getter output, source reference counts, and successful ownership transfer.
Existing assignment-preservation and container failure tests must still pass.

Step 1B outcome:

- Replaced all 21 targeted cleanup handlers across collections, compiler and
  interpreter expressions, format evaluation, OS snapshots, logger registration,
  and binding construction.
- Typed temporaries use `RuntimeValueGuard`; constructor and container guards
  disarm only after successful ownership transfer. Binding construction uses
  `std::unique_ptr` with a `releaseRef` deleter for its temporary object reference.
- Conditional guards protect only owned dynamic-getter and indexer-read
  results. Borrowed variables, receivers, and indexer-write results remain
  unguarded. Existing VM restoration and yield policies were not changed.
- Compiled formatting now protects the dynamic getter invocation itself, not
  just conversion, so output acquired before a getter throws is also released.
- Added regressions for partial dynamic-getter failure in compiled formatting,
  dynamic formatting, and direct interpreter reads, plus interpreter indexer
  read/write failure with owned operands and partial read results.
- `nomad_test_bindings`, `nomad_test_language`, `nomad_test_tooling`, and
  `nomad_test_remaining` passed on freshly built Windows Debug core, module, and
  test binaries. Test logs confirm both new regression cases ran. Existing
  constructor, aliased-insertion, snapshot, getter, and ownership tests passed.
- Updated the embedding formatting contract. Whitespace validation passed.
  The first-party catch-all count is now 41, down from 62; all remaining cleanup
  handlers belong to later slices, alongside the five intentional boundaries.

Stop for review.

### Step 2: replace simple state restoration and test-hook catches

Replace:

- The logger's delivery flag reset.
- The temporary module compilation callback reset.
- Temporary VM receiver restoration.
- The four CRT allocation-hook restoration handlers.

Use the approved scope-exit mechanism or a small dedicated restoration class.
Keep the logger flag restoration inside the mutex lifetime. Remove the temporary
compilation callback before its captured local variables expire.

Acceptance: nested/reentrant calls and failures restore prior state; the CRT hook
is restored before assertions or unrelated work, without allocating inside the
allocation-measurement scope.

Step 2 outcome:

- At the user's request, use `BOOST_SCOPE_EXIT_ALL` rather than
  `boost::scope::defer_guard` or a custom utility. Explicit snapshot/reference
  captures preserve cleanup lifetimes; all saved allocation hooks remain
  captured by value.
  The existing Boost package and C++20 baseline are unchanged; the newly selected
  `scope_exit` component is header-only. Corrected the earlier standard-library note:
  `std::experimental::scope_exit` is in the Library Fundamentals TS, not ISO C++23.
- Replaced all seven targeted catches: logger delivery state, the temporary
  compilation callback, the VM's temporary receiver, and four CRT hook handlers.
  The guard-allocation benchmark's existing hook now uses the same restoration
  mechanism.
- Every cleanup action is nonthrowing. Logger state resets while its mutex is
  held; the compilation callback is cleared before checking invocation and
  before its captured locals expire. Receiver restoration does not retain or
  release borrowed objects.
- Added five focused regression cases for normal/exceptional exit, nested
  restoration order, coordinator success/before/after/skipped compilation,
  nested property receiver success/failure, and actual CRT-hook restoration
  during unwinding. Extended the allocation benchmark to include scope guards;
  macro guard bookkeeping measured zero allocations after the requested switch.
- `nomad_test_language`, `nomad_test_tooling`, and `nomad_test_remaining` passed
  with freshly built Windows Debug core, fixture, and test binaries. Existing
  logger failure/reentry recovery and allocation-count tests passed.
- The first-party catch-all count is now 34, down from 41. No native-call frame,
  execution-state, rollback, or filesystem migration from later slices was
  included.

Stop for review.

### Step 3A: guard native calls and hidden stack entries

Introduce or reuse specialized guards for native frame restoration, saved
registers, and successful hidden storage-address pushes.

Migrate native functions, methods, constructors, indexer callback invocation,
and struct initialization's scoped address push.

Native construction needs an owned uncommitted result as well as register/frame
restoration. Transfer the object only after successful validation.

Acceptance: nested native reentry, exceptions after result allocation, receiver
restoration, zero-sized storage, and existing zero-allocation checks all pass.

Step 3A outcome:

- Replaced the six targeted handlers in
  [VirtualMachine.cpp](../../src/nomad/script/VirtualMachine.cpp): native
  function/method/indexer calls, native object/struct construction, and the
  temporary storage-address push used by script struct initialization.
- Used separate `BOOST_SCOPE_EXIT_ALL` blocks for native frames and saved
  registers, keeping captures small and preserving restoration order.
  `RuntimeValueGuard` releases a failed native object's partial result before
  restoring the enclosing frame/registers; successful construction explicitly
  transfers the result.
- Added three direct-call regressions in
  [VirtualMachineStorageTest.cpp](../../tests/VirtualMachineStorageTest.cpp).
  They cover nested function/method success and failure, indexer read/write
  frame restoration, and empty/nonempty struct initialization and construction
  failures with owned enclosing registers and balanced caller stack slots.
- The new cases and existing struct/object/native-call allocation tests ran and
  passed with freshly built Windows Debug binaries. Native object failure and
  reentry regressions also passed. No bookkeeping allocation increase was
  detected.
- `nomad_test_remaining` and `nomad_test_tooling` passed on their initial runs.
  `nomad_test_language` initially reported a Boost.JSON out-of-range exception
  in the existing `language_server/signature_help_lists_overloads_and_filters_member_receivers`
  test in [LanguageServerTest.cpp](../../tests/LanguageServerTest.cpp).
  The language group passed on an unchanged rerun. The intermittent LSP failure
  was not hidden or fixed as part of this VM cleanup slice; its cause remains
  uninvestigated beyond inspecting the failing test and diagnostic.
- The first-party catch-all count is now 28, down from 34. No public API,
  opcode, execution-state/suspension, argument-block, or filesystem changes from
  later slices were included.

Stop for review.

### Step 3B: guard script/interpreter execution state

Migrate script invocation, creation-body execution, interpreter callable helpers,
and compiled format-expression invocation.

Keep distinct contracts for:

- Scalar calls that may suspend and leave state owned by the VM.
- Aggregate, constructor, method, and format calls that must not yield.
- Native scalar callbacks whose running/yielded flags intentionally survive.
- Full execution-state restoration versus the existing narrower script restore.

Transfer/disarm guards on suspension explicitly; do not restore away a live
suspended frame. Ensure format-call restoration occurs before value conversion.

Acceptance: test success, exception, stop, yield/resume where supported, forbidden
yield, nested callbacks, and exact register/stack/receiver restoration.

Outcome:

- Replaced eight cleanup catches in script invocation, creation-body execution,
  interpreter callable helpers, and compiled format-expression invocation with
  Boost.ScopeExit macros.
- Scalar suspension retains live frames and owned arguments for resumption.
  Synchronous aggregate calls restore the full execution state; native scalar
  callbacks retain their stop/yield effects, including when they subsequently
  throw. Compiled format calls restore state before converting their results.
- Added regressions for owned arguments across suspension/resumption, errors
  after yield, native errors after stop/yield, conversion-time state restoration
  and conversion failure, and allocation-free prepared script execution.
- Windows Debug CMake Tools CTest passed `nomad_test_language` (35.10 s),
  `nomad_test_remaining` (40.23 s), and `nomad_test_tooling` (21.74 s).
  The rebuilt language DLL is newer than the current VM source; the test binary
  is newer than the changed test sources. These are unit-test results, not
  gameplay or cross-platform validation.
- The first-party catch-all count is now 20, down from 28. Invocation argument
  ownership and failure-only rollback remain assigned to later slices.

Stop for review.

### Step 3C: own invocation argument blocks

Migrate native indexer argument cleanup, the opcode indexer helper, and the
object-construction test helper.

Track only successfully acquired arguments and receiver references. Keep partial
pushes exception-safe. An indexer read result is discarded on failure, not on
successful return. Do not conflate argument release with stack/register restore.

Acceptance: fail at every argument-copy position and inside the callback;
verify reference counts and original stack position after each failure.

Outcome:

- Replaced the three duplicated argument-cleanup catches in host native indexer
  invocation, native indexer opcodes, and the object-construction test helper
  with Boost.ScopeExit macros. Each macro captures its cleanup action by
  reference rather than capturing all of the action's dependencies.
- Host indexer argument ownership advances only after each successful copy.
  The construction helper guards a copied value until its ownership transfers
  to the argument stack. Both paths release all previously acquired arguments
  when a later copy or callback throws.
- Indexer opcodes guard read results until successful return, preserving
  successful result ownership while discarding partial results on failure.
  Host indexer invocation retains its existing caller-owned result contract,
  including when a callback throws after setting a result.
- Added a shared test-only failing-copy descriptor and first/middle/last
  argument-copy failure coverage for host indexers and construction. Regressions
  also verify callback failure, receiver and argument reference counts, enclosing
  frame restoration, original stack positions, and opcode result ownership.
- Windows Debug CMake Tools CTest passed `nomad_test_language` (34.06 s) and
  `nomad_test_remaining` (38.04 s) with fresh library and test binaries.
  After adding the allocation regression, `nomad_test_remaining` passed again
  (38.78 s); its log confirms execution of both new indexer ownership tests and
  the zero-allocation test. The latter covers 100 iterations of host and opcode
  reads/writes. These are unit tests, not gameplay or cross-platform validation.
- The first-party catch-all count is now 17, down from 20. Failure-only rollback
  and filesystem transactions are unchanged and remain assigned to later slices.

Stop for review.

### Step 4: retain distinct exception paths

Review decision: retain the original exception-only `try`/`catch` in struct
initialization/copy, field-layout finalization, iterator advancement, closure
creation, and format-expression evaluation. These paths do not duplicate normal
cleanup, so scope guards and commit flags make them less readable.

Remove `freeCreationArguments` and constructor-opcode failure cleanup of
arguments: execution exceptions remain fatal, not recoverable argument-unwinding
paths. Preserve partial struct-destination guards and ordinary successful
bytecode argument cleanup. Indexer scope guards still deduplicate normal and
exceptional cleanup and are unchanged.

Acceptance: first/middle/last failure positions, nested and empty structs,
recursive layout failure/retry, iterator failure, closure transfer, and direct
clear-on-failure lifecycle checks pass without changing successful behavior.

Reviewed outcome (supersedes the initial guard conversion):

- Restored all six exception-only catches, preserving lifecycle resource release
  and destination clearing, layout retry, iterator partial-output cleanup,
  closure capture rollback, and evaluation-result clearing.
- Removed closure capture-size caching and its extra allocation, and removed the
  test that required metadata queries to fail after copying.
- Removed constructor-opcode argument rollback. Native/script class and
  nonempty/empty struct opcode tests verify that arguments remain owned after
  failure; the test harness explicitly reclaims them without resuming execution.
  Successful argument cleanup and partial struct-destination cleanup still pass.
- Retained layout-retry, iterator-failure, interpreter-result, nested lifecycle,
  and closure-copy regression coverage. The compiled constructor-failure test
  now uses a fresh VM per failed execution and checks abandoned argument ownership
  rather than asserting automatic unwinding.
- Windows Debug CMake Tools CTest passed `nomad_test_language` (85.08 s),
  `nomad_test_remaining` (65.96 s), and `nomad_value_cleanup_contract` (0.24 s).
  Direct out-of-memory injection, gameplay, and cross-platform behavior are not
  validated.
- The first-party catch-all count is 13: six intentionally retained exception-only
  paths, two filesystem transactions for Steps 5A/5B, and five diagnostic or
  exception-handling boundaries. Catch count is an inventory, not a completion
  target. No filesystem behavior was changed.

Stop for review.

### Step 5A: review project initialization cleanup

Reviewed outcome:

- `initializeProject` already tracks temporary files, published files, and newly
  created directories. Its single catch removes those artifacts only on failure,
  then rethrows. Successful initialization retains the files and returns their
  paths; there is no duplicated normal-path cleanup to extract.
- Retain this catch under the Step 4 review decision. Introducing an RAII
  transaction and commit flag solely to replace it would not serve the clarified
  goal. No production or test changes are needed for this slice.
- Existing tests cover successful project creation, wrapper omission, preserving
  pre-existing project files, and rollback after an unknown template placeholder.
  Coverage was inspected, not rerun for this documentation-only decision; it does
  not establish failure injection at every filesystem stage.
- The existing rollback uses error-code filesystem operations and does not
  report secondary removal errors. Changing that reporting policy is separate
  work requiring approval, not part of cleanup deduplication.
- Catch-all count remains 13. Only the plan changed; no C++ build or runtime
  validation was performed for this review.

Stop for review.

### Step 5B: review package finalization and emergency cleanup

Reviewed outcome:

- Retain the catch in `packageProject`. Normal finalization removes staging
  after creating the temporary archive and before preserving/replacing an
  existing output. Failure cleanup removes staging and any temporary archive
  after an interrupted operation. These are distinct checked phases, despite
  both attempting staging removal.
- Moving removal into a scope-exit destructor would change its timing or require
  a separate decision about reporting filesystem errors without throwing.
  Extracting a callback merely to wrap the two differently diagnosed removal
  attempts would not simplify ownership or error handling.
- Preserve explicit archive publication, backup restoration on publication
  failure, backup retention when restoration fails, and dry-run behavior.
  No production or test changes are needed under the clarified goal.
- Existing tests cover dry runs, successful packaging, forced replacement,
  staging cleanup after a missing runtime file, and preservation of a previous
  archive when a module-generation hook fails. Coverage was inspected, not
  rerun for this documentation-only decision; it does not establish injected
  publication, backup-restoration, or filesystem-cleanup failure coverage.
- The existing catch can replace the original exception with a cleanup error
  and stop before attempting subsequent cleanup. That reporting/recovery policy
  remains unchanged; revising it is separate work requiring approval.
- Catch-all count remains 13. Only the plan changed; no C++ build or runtime
  validation was performed for this review.

Stop for review.

### Step 6: reconcile the inventory and complete validation

1. Re-run the first-party catch-all inventory and reconcile every baseline site.
2. Record each removed or retained handler and its reason. Preserve the five
   exception-handling boundaries below unless separately approved.
3. Check directly coupled public ownership documentation and update it only
   where a contract or host-facing API changed.
4. Run integrated validation using the coverage below.
5. Report runtime-test results separately from compilation, platform checks,
   sanitizer results, and any untested gameplay behavior.

Outcome:

- Reconciled all 63 baseline catches in 26 files. Forty-six now use typed owners
  or scope cleanup, and four constructor-opcode catches were removed under the
  fatal-execution contract. Thirteen catches remain intentionally: six distinct
  exception-only rollback paths, two filesystem exception paths, and five
  diagnostic/exception-capture boundaries.
- Rechecked first-party C++ sources, headers, applications, modules, samples,
  templates, tests, and ancillary tooling directories, excluding build/vendor
  trees. No new or unexplained catch-all sites were found. The baseline table
  below records the final disposition of every original site.
- Verified that embedding documentation matches typed guard ownership, lifecycle
  failure guarantees, and fatal bytecode argument behavior. No public API or
  production changes were needed for this final review.
- All 13 CTest entries passed through CMake Tools using Windows Debug:

  | Test | Result | Seconds |
  | --- | --- | ---: |
  | `nomad_value_cleanup_contract` | Passed | 0.22 |
  | `nomad_test_bindings` | Passed | 1.21 |
  | `nomad_test_audio` | Passed | 9.64 |
  | `nomad_test_language` | Passed | 31.29 |
  | `nomad_test_tooling` | Passed | 29.28 |
  | `nomad_test_remaining` | Passed | 93.39 |
  | `nomad_module_script_staging` | Passed | 0.32 |
  | `nomad_compiler_exports` | Passed | 0.56 |
  | `nomad_lsp_process` | Passed | 3.49 |
  | `nomad_module_runtime_integration` | Passed | 30.18 |
  | `nomad_distribution_integration` | Passed | 33.49 |
  | `nomad_package_graphical_smoke` | Passed | 7.16 |
  | `nomad_package_integration` | Passed | 21.07 |

- Unit tests and process/integration tests were run in separate batches.
  Packaging/distribution tests ran serially. Test and language-library timestamps
  are newer than their corresponding C++ sources; these are results against
  current binaries, not compilation-only checks.
- Graphical packaging smoke compiles, packages, extracts, and executes the fixture
  using SDL dummy video/audio drivers and checks its completion marker. It is
  headless runtime validation, not visual inspection or interactive gameplay.
- Linux Debug, Linux sanitizer, other platform builds, and direct out-of-memory
  injection were not run in this Windows session. Existing filesystem rollback
  error-reporting limitations documented in Steps 5A/5B are unchanged.
- Only this plan changed during Step 6. No commits or pushes were made.

Stop for final review. Commit or push only on explicit request.

## 5. Validation and completion criteria

Start focused validation through the workspace CTest action; do not run a
separate build first merely because tests are planned. Ensure the action built
current sources or verify executable freshness before accepting the results.
If it ran stale binaries, use the workspace build task and rerun the tests.

Use the smallest relevant test selection from
[tests/CMakeLists.txt](../../tests/CMakeLists.txt):

| Changed surface | Required coverage |
| --- | --- |
| Guard/owner primitives | Exactly-once release, no release of borrowed data, failure and transfer tests |
| Collections, construction, interpreter | Relevant `nomad_test_language` and `nomad_test_remaining` cases |
| Host/generated bindings | `nomad_test_bindings`, including generated consumer compilation |
| Logging, module coordination, OS | Relevant `nomad_test_remaining` and `nomad_test_tooling` cases |
| Lifecycle rollback/invariants | `nomad_value_cleanup_contract` and focused struct/value tests |
| VM bookkeeping | Existing allocation-count checks and invocation/yield/reentry regressions |
| Project/filesystem transactions | Project unit tests and affected packaging/distribution integrations |

For final integration, run all unit-labeled groups:

```powershell
ctest --preset windows-debug -L unit --output-on-failure
```

Run affected packaging/distribution integrations separately and preserve their
serial execution requirements. Validate Linux with the existing Debug and
sanitizer presets when that environment is available; explicitly report if it
is not. Do not claim sanitizer coverage from Windows Debug counters alone.

Completion requires:

- No unexplained cleanup-only catch-all handlers within the reviewed scope.
- No extra reference leaks, double releases, or changed source/destination
  preservation guarantees.
- No new allocations in paths already covered by zero-allocation tests.
- Preserve reviewed yield/resume, error propagation, and native callback behavior;
  constructor-opcode argument unwinding is intentionally absent after Step 4 review.
- No newly introduced throwing cleanup destructors or silently lost cleanup
  errors; existing filesystem reporting limitations remain documented.
- Current binaries passing the selected tests and build checks.
- Existing unrelated worktree changes preserved.

Saving this documentation requires no C++ build.

## 6. Baseline inventory

Line numbers identify the reviewed baseline and will move during implementation.
Classify by the handler's behavior when revisiting a site, not its line alone.

| File | Catch lines | Disposition |
| --- | --- | --- |
| [KeyValueObject.cpp](../../src/nomad/collections/KeyValueObject.cpp) | 20, 42 | Removed 2: typed construction ownership (1B) |
| [ListObject.cpp](../../src/nomad/collections/ListObject.cpp) | 68 | Removed 1: typed insertion ownership (1B) |
| [MapObject.cpp](../../src/nomad/collections/MapObject.cpp) | 59, 106, 165 | Removed 3: typed temporary/insertion ownership (1B) |
| [Compiler.cpp](../../src/nomad/compiler/Compiler.cpp) | 2290 | Removed 1: typed initial-value ownership (1B) |
| [Expression.cpp](../../src/nomad/compiler/Expression.cpp) | 351, 1287, 1766, 2121, 2150 | Removed 5: typed expression/getter results (1B) |
| [LoopStatement.cpp](../../src/nomad/compiler/LoopStatement.cpp) | 145 | Removed 1: typed loop-default ownership (1B) |
| [Decompiler.cpp](../../src/nomad/debug/Decompiler.cpp) | 506 | Keep: nonstandard exception to diagnostic text |
| [Game.cpp](../../src/nomad/game/Game.cpp) | 47, 138, 149 | Keep: diagnostic text, worker exception capture, combined failure handling |
| [Interpreter.cpp](../../src/nomad/interpreter/Interpreter.cpp) | 306; 384, 407, 424, 488; 707 | Removed 5: result ownership (1B) and execution-state scopes (3B). Keep 707: exception-only result rollback |
| [InterpreterObjectReceiver.cpp](../../src/nomad/interpreter/InterpreterObjectReceiver.cpp) | 73 | Removed 1: typed getter result ownership (1B) |
| [LogModule.cpp](../../src/nomad/log/LogModule.cpp) | 277 | Removed 1: typed logger reference ownership (1B) |
| [Logger.cpp](../../src/nomad/log/Logger.cpp) | 193 | Removed 1: delivery-state scope (2) |
| [ScriptBindingObject.cpp](../../src/nomad/modules/ScriptBindingObject.cpp) | 65; 144 | Removed 2: object owner (1B) and typed string guard (1A) |
| [OsModule.cpp](../../src/nomad/os/OsModule.cpp) | 67, 83, 143 | Removed 3: snapshot/string ownership (1B) |
| [ProjectInitializer.cpp](../../src/nomad/project/ProjectInitializer.cpp) | 425 | Keep after Step 5A review: distinct failure-only filesystem rollback |
| [ProjectPackager.cpp](../../src/nomad/project/ProjectPackager.cpp) | 856 | Keep after Step 5B review: checked finalization and distinct emergency cleanup |
| [CompositeType.cpp](../../src/nomad/script/CompositeType.cpp) | 291 | Keep: distinct exception-only layout-state rollback |
| [FormatString.cpp](../../src/nomad/script/FormatString.cpp) | 88; 169 | Removed 2: call-state scope (3B) and dynamic getter ownership (1B) |
| [IteratorObject.cpp](../../src/nomad/script/IteratorObject.cpp) | 26 | Keep: distinct exception-only partial-current rollback |
| [OpCode.cpp](../../src/nomad/script/OpCode.cpp) | 70; 267, 280, 300, 314 | Removed 5: indexer argument scope (3C); four constructor argument catches removed under fatal-execution contract (4) |
| [StructType.cpp](../../src/nomad/script/StructType.cpp) | 83, 109 | Keep 2: exception-only resource release and destination clearing |
| [VirtualMachine.cpp](../../src/nomad/script/VirtualMachine.cpp) | 299, 406, 494; 534, 560, 641, 660, 690, 830; 715; 881; 1073 | Removed 11: execution scopes (3B), native scopes (3A), receiver scope (2), argument scope (3C). Keep 1073: exception-only closure capture rollback |
| [ModuleHostImpl.cpp](../../src/nomad/system/ModuleHostImpl.cpp) | 325 | Removed 1: temporary callback scope (2) |
| [ObjectConstructionTest.cpp](../../tests/ObjectConstructionTest.cpp) | 93 | Removed 1: argument-block ownership (3C) |
| [ValueCleanupContractTest.cpp](../../tests/ValueCleanupContractTest.cpp) | 59 | Keep: unexpected nonstandard exception to process diagnostic |
| [VirtualMachineStorageTest.cpp](../../tests/VirtualMachineStorageTest.cpp) | 632, 668, 681, 712 | Removed 4: CRT allocation-hook scopes (2) |
