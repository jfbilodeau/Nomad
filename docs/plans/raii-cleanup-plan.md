# RAII cleanup plan

Status: Step 4 is implemented and validated on Windows Debug, awaiting review.
Next slice: Step 5A; implementation and filesystem cleanup-error policy require
separate approval.
Baseline: the first-party catch-all review of November 11, 2026.

This plan follows the completed lifecycle exception-safety and caller-cleanup
work. Preserve unrelated worktree changes. Steps 1A, 1B, 2, 3A, 3B, 3C, and 4,
the concrete value-guard contract, and using Boost.ScopeExit macros have been
approved. Later slices, commits, and pushes are not authorized.

Implement one approved slice at a time. After each slice, report the changes,
validation, remaining catches, and next slice, then stop for review.

## 1. Goal and scope

Replace cleanup-only `catch (...)` handlers with automatic ownership, scoped
state restoration, or explicit rollback/commit objects. In particular, remove
the duplicated cleanup performed in both a catch handler and the success path.

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

### Step 4: replace failure-only rollback handlers

Use explicit commit/disarm transactions for:

- Struct initialization/copy rollback and clearing all destination slots.
- Composite layout state while resolving recursive layouts.
- Partial iterator-current values when virtual advancement throws.
- Constructor opcode arguments and partial struct destinations.
- Heterogeneous closure capture buffers before transfer into a closure.
- Clearing an interpreter evaluation result only when evaluation fails.

Successful constructor bytecode still owns its normal cleanup; do not free its
arguments twice. Partial capture cleanup needs trusted type/offset metadata and
must remain safe if construction or allocation fails.

Acceptance: first/middle/last failure positions, nested and empty structs,
recursive layout failure/retry, iterator failure, closure transfer, and direct
clear-on-failure lifecycle checks pass without changing successful behavior.

Outcome:

- Replaced all ten failure-only rollback catches with Boost.ScopeExit
  commit/disarm guards and existing typed value guards. Struct lifecycle failure
  still releases resources and clears every destination slot. Successful
  constructor bytecode retains ownership of its ordinary argument cleanup.
- Recursive field-layout failure resets unresolved state. Iterator advancement
  releases partial output on exceptions without marking iteration completed.
  Dynamic format evaluation clears partial context results on failure and
  preserves successful output and existing post-format context cleanup.
- Closure creation prevalidates and caches capture sizes before acquiring owned
  values. Rollback uses that cache rather than querying potentially throwing
  metadata. Ownership transfers only after successful closure construction.
  The cache adds one metadata-buffer allocation for a nonempty capture list.
- Added regressions for nested layout failure/retry, recursive layout rollback,
  iterator partial-output failure and retry, constructor opcode success/failure
  for native/script classes and nonempty/empty structs, closure construction
  rejection after capture copying, and partial interpreter-result cleanup.
  Existing first/middle/last nested lifecycle and capture-copy failures also pass.
- Windows Debug CMake Tools CTest passed `nomad_test_language` (35.29 s),
  `nomad_test_remaining` (32.23 s on the final run), and
  `nomad_value_cleanup_contract` (0.29 s). The tested library is newer than the
  changed production sources; the final test binary is newer than all changed
  test sources. The remaining-group log confirms the new rollback regressions
  and existing nested lifecycle/capture-copy cases executed.
- The initial language run exposed an incorrect new test expectation about
  successful post-format context cleanup; corrected the assertion without
  changing production behavior. An optional Windows CRT allocation-denial probe
  caused diagnostic recursion and then a stalled test run. Removed the probe
  and its hook changes; no test process remained running before final validation.
  Direct out-of-memory fault injection is not validated. Constructor rejection
  after copying and ordinary ownership transfer are covered. These results are
  unit tests, not gameplay or cross-platform validation.
- The first-party catch-all count is now 7, down from 17: two filesystem
  transactions remain for Steps 5A/5B, plus five intentional exception-handling
  boundaries. No filesystem behavior was changed in this slice.

Stop for review.

### Step 5A: make project initialization transactional

Track created temporary files, published files, and new directories in an
operation-owned rollback object. Remove only those artifacts on failure and
commit after all files are published. Preserve reverse directory removal order
and executable-wrapper permissions.

Acceptance: failures at each creation/publication stage remove owned artifacts
without changing pre-existing files; cleanup failures follow the approved policy.

Stop for review.

### Step 5B: separate package finalization from emergency cleanup

Use a package transaction to own staging, temporary archives, and publication
state. Keep checked publication and previous-output restoration explicit.

Do not simply move the existing throwing catch body into a destructor. Preserve
backup recovery, overwrite and dry-run behavior, and diagnostic visibility for
both the primary error and secondary cleanup failure.

Acceptance: test staging/archive/publication failure, restoration failure,
cleanup failure, successful replacement, and dry runs. No destructor throws;
existing outputs and recovery backups remain safe.

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
- No changed yield/resume, error propagation, or native callback behavior.
- No throwing cleanup destructors or silently lost cleanup errors.
- Current binaries passing the selected tests and build checks.
- Existing unrelated worktree changes preserved.

Saving this documentation requires no C++ build.

## 6. Baseline inventory

Line numbers identify the reviewed baseline and will move during implementation.
Classify by the handler's behavior when revisiting a site, not its line alone.

| File | Catch lines | Disposition |
| --- | --- | --- |
| [KeyValueObject.cpp](../../src/nomad/collections/KeyValueObject.cpp) | 20, 42 | Step 1B: construction ownership |
| [ListObject.cpp](../../src/nomad/collections/ListObject.cpp) | 68 | Step 1B: insertion ownership |
| [MapObject.cpp](../../src/nomad/collections/MapObject.cpp) | 59, 106, 165 | Step 1B: temporary and insertion ownership |
| [Compiler.cpp](../../src/nomad/compiler/Compiler.cpp) | 2290 | Step 1B: initial-value ownership |
| [Expression.cpp](../../src/nomad/compiler/Expression.cpp) | 351, 1287, 1766, 2121, 2150 | Step 1B: owned expression/getter results |
| [LoopStatement.cpp](../../src/nomad/compiler/LoopStatement.cpp) | 145 | Step 1B: loop-default ownership |
| [Decompiler.cpp](../../src/nomad/debug/Decompiler.cpp) | 506 | Keep: nonstandard exception to diagnostic text |
| [Game.cpp](../../src/nomad/game/Game.cpp) | 47, 138, 149 | Keep: diagnostic text, worker exception capture, combined failure handling |
| [Interpreter.cpp](../../src/nomad/interpreter/Interpreter.cpp) | 306; 384, 407, 424, 488; 707 | Steps 1B, 3B, 4 respectively |
| [InterpreterObjectReceiver.cpp](../../src/nomad/interpreter/InterpreterObjectReceiver.cpp) | 73 | Step 1B: owned getter result |
| [LogModule.cpp](../../src/nomad/log/LogModule.cpp) | 277 | Step 1B: logger reference ownership |
| [Logger.cpp](../../src/nomad/log/Logger.cpp) | 193 | Step 2: delivery-state restoration |
| [ScriptBindingObject.cpp](../../src/nomad/modules/ScriptBindingObject.cpp) | 65; 144 | Step 1B: object ownership; Step 1A pilot: string ownership |
| [OsModule.cpp](../../src/nomad/os/OsModule.cpp) | 67, 83, 143 | Step 1B: snapshot/string ownership |
| [ProjectInitializer.cpp](../../src/nomad/project/ProjectInitializer.cpp) | 425 | Step 5A: filesystem rollback |
| [ProjectPackager.cpp](../../src/nomad/project/ProjectPackager.cpp) | 856 | Step 5B: publication/emergency cleanup |
| [CompositeType.cpp](../../src/nomad/script/CompositeType.cpp) | 291 | Step 4: layout-state rollback |
| [FormatString.cpp](../../src/nomad/script/FormatString.cpp) | 88; 169 | Step 3B: call-state restoration; Step 1B: dynamic getter ownership |
| [IteratorObject.cpp](../../src/nomad/script/IteratorObject.cpp) | 26 | Step 4: partial-current rollback |
| [OpCode.cpp](../../src/nomad/script/OpCode.cpp) | 70; 267, 280, 300, 314 | Step 3C: indexer arguments; Step 4: construction rollback |
| [StructType.cpp](../../src/nomad/script/StructType.cpp) | 83, 109 | Step 4: typed destination rollback |
| [VirtualMachine.cpp](../../src/nomad/script/VirtualMachine.cpp) | 299, 406, 494; 534, 560, 641, 660, 690, 830; 715; 881; 1073 | Steps 3B, 3A, 2, 3C, 4 respectively |
| [ModuleHostImpl.cpp](../../src/nomad/system/ModuleHostImpl.cpp) | 325 | Step 2: temporary callback reset |
| [ObjectConstructionTest.cpp](../../tests/ObjectConstructionTest.cpp) | 93 | Step 3C: owned argument block |
| [ValueCleanupContractTest.cpp](../../tests/ValueCleanupContractTest.cpp) | 59 | Keep: unexpected nonstandard exception to process diagnostic |
| [VirtualMachineStorageTest.cpp](../../tests/VirtualMachineStorageTest.cpp) | 632, 668, 681, 712 | Step 2: CRT allocation-hook restoration |
