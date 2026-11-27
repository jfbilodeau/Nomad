# Script runtime improvement plan

This plan records the review of `Runtime`, `Interpreter`, `Compiler`, `VirtualMachine`, and `InterpreterContext`. It holds the approved decisions, the staged work, and every finding with its priority, evidence, verification status, and target step. Update the status as each step lands.

**Current scope:** Remaining step 8 performance work is deferred at the user's
request: "Let's skip the performance stuff for now." This includes the approved
but unimplemented lexical-grammar prototype and remaining measurements.
Completed changes, decisions, and benchmark evidence stay intact. Step 8 is
not marked complete; resuming performance work requires explicit agreement.

**Active non-performance scope:** Event declaration ownership is implemented
on TypeManager, including EventIds and EventTypes. Runtime retains its public
facade and namespace/publication responsibilities. The development build and
all 13 existing CTests pass. Next: review hub declaration ownership separately;
singleton-hub language decisions and performance work remain out of scope.

**Status:** Decisions D1–D5 are approved. Steps 1 through 7 are implemented or resolved by review. Remaining Step 8 performance work is deferred: the approved free-function name index is implemented and measured against the checked Debug/Release baseline. It is retained as-is after review; method/constructor resolution now collects candidates from existing owner catalogs and is measured without another index. R2's string interning/name-lookup baselines are recorded; string indexing is deferred after confirming current production name lookups are compiler-side. V4 is implemented and measured: callable IDs live in the existing script-frame header and script-call targets use IDs; no VM cache or parallel index was added. R10's qualification cleanup, R11's shared hub/member conflict predicate, R14's registration-boundary work, and R16's navigation cleanup are complete. R17 is complete: owned metadata has const-correct views, and the existing mutable-context facade is explicitly retained and documented. R18's type-view/category cleanup and approved fixed-type borrowed views are implemented; the function getter remains dynamic. R19 is complete: parser argument dispatch shares the catalog-owned parameter classifier through Runtime. R20 is complete: context-variable rejection uses the existing shared lookup and its approved diagnostic origin. R22 is resolved with approval: native signature validation reuses the mutable native lookup, while compiler-placeholder RTTI is intentionally retained. R24's reviewed style cleanup is implemented; the concurrent native-getter rename was preserved with an approved const-overload delegation fix. R25 is complete with an approved optional native-member lookup; its count-conversion claim is a false positive. R28 is complete: count/script metadata and free-function lookup names are aligned, with specialized existence and source-category queries. V14 is complete: remaining VM parameter/local names are normalized; the current function-variable accessors are already symmetric. K5 is resolved by review with approval: existing shared instruction/argument-cleanup helpers and modest specialized emitter duplication are retained. K6 shares reserved-name checks and removes redundant classification from definition lookup, preserving the two distinct resolution contracts with approval. The approved registration-boundary revision is implemented: TypeManager type/family creation is catalog-only with opaque names; Runtime coordinates public namespace publication and enum execution registrations. Stored-property collision validation uses the declaring type's catalog. Global member-template compatibility and public parser lookup belong to Runtime; TypeManager stores local family signatures and owns parameter-shape and predicate-type metadata queries. Class rejection remains non-logging. R3, R4's publication guards, the native free/hub registration split, and class/struct execution-registration orchestration are implemented. R5 is resolved by ownership-contract review without a code change. R6's checked lookups are implemented under the approved failure contracts, with explicit unchecked constant accessors preserving compiler-trusted VM reads. R7's compiler permission checks and checked host callback contract are implemented; trusted VM dispatch remains unchecked. R8's obsolete documentation guards are removed and registration documentation is retained. R12 is resolved by review: source identifier syntax is already compiler-owned. R23's string-setter cleanup and measured registration-move optimization are complete. The extended opt-in benchmark builds and passes checked active-frame and no-query call workloads in Windows Debug and Release. Six selected Windows Debug CTests pass after V4; the existing 13 entries remain. Four targeted Windows Debug CTests passed after owner-catalog candidate narrowing. The full 13-entry run passed after the event declaration ownership move. Callable/type ID exhaustion and allocation-failure ordering were source-reviewed, not fault-injected. C4 retains the approved name-based host conveniences alongside resolved-ID storage APIs. V5's checked storage query is retained after Windows Debug measurements. The declaration seal was removed. TypeManager no longer calls Runtime directly and now owns event declarations and IDs; hub declaration ownership and the singleton-hub redesign remain separate work.

**I11:** Conditional owned-array field-setter scratch is implemented and validated with unchanged Type semantics. Debug requests drop by one; Release timings are mixed, including slower wide-field samples.

**R30:** The approved stable-definition pointer lookup is implemented and validated. Borrowed metadata survives registration growth/reentry; checked direct-ID lookup makes no Debug allocation requests. Explicit snapshots remain ordinary copies, measured separately in Debug and Release. Two selected Debug CTests and checked benchmarks pass.

**R23:** The approved local-registration move is implemented and validated. Per-callback target copies change from two to one for lvalue inputs and one to zero for moved inputs. Large captures and long metadata each save two Debug requests per registration; timings remain mixed. The selected language CTest, unchanged checked Debug/Release benchmarks, and strict documentation build pass.

**R18:** The approved ten fixed-type borrowed views are implemented and validated. Repeated fixed getters make zero Debug allocation requests; Release medians round to 0.001 us and are too small for a precise speedup ratio. Catalog ownership and dynamic function lookup are unchanged. Two selected Debug CTests, checked Debug/Release benchmarks, and strict documentation validation pass.

**R26:** The approved name-bucket duplicate checks, failure-consistent native publication, and single mutable-property fetch are implemented and validated. Failure/retry coverage and both selected Debug CTests pass; checked Debug/Release comparisons retain mixed validation/small-catalog timings. No new index or validation-snapshot change was added.

**R27:** The approved shared hub-variable owner resolver is implemented and validated. Successful context queries now use one discovery and match membership-query Debug allocation counts; fallback precedence and public APIs remain unchanged. Two selected Debug CTests, checked Debug/Release comparisons, and strict documentation validation pass.

**R13:** Targeted rebuilding remains deferred with approval. The separate exception-rollback fix is implemented and validated: a propagating rebuild exception removes the attempted hub and preserves retry, while logged NomadException rejection and full refreshing remain unchanged. Two selected Debug CTests, checked Debug/Release successful-path benchmarks, and strict docs validation pass.

**C2:** The approved vector-scratch rollback is implemented and validated. The optimization-specific allocation test is removed; existing storage/lifecycle coverage and all benchmark evidence remain. Restored Debug update requests are one for empty/integer/string and two for the pair. The selected remaining CTest and checked Debug/Release benchmarks pass; ownership, retirement, other scratch consumers, and public APIs are unchanged. The scratch experiment closes without a speedup claim. Further C2 storage redesign remains unapproved; next reviewable scope is I5/Q2's dynamic-format cost breakdown before a caching decision.

**I5/Q2:** The dynamic-format baseline and generic per-thread fixed lexical-grammar design are recorded. The approved prototype is unimplemented and deferred with the remaining performance sweep. No token/AST cache or Compiler reuse is approved. Keep the baseline and design for a possible explicitly agreed resumption; no production code is changed by this deferral.

**Validation update:** Before C2, the user reported completing a full rebuild and running all tests. This is user-reported validation, not another assistant-verified full-suite run.

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
- The native free/hub registration boundary is implemented. Ordinary `registerNativeFunction` registrations never become hub members. Both callback and typed `registerHubNativeFunction` registrations publish only valid IDs to the explicitly supplied hub, and keep that ownership when later hub declarations rebuild member indexes.
- Class/struct execution-registration orchestration is implemented on `Runtime`: native/script methods and constructors, native class properties, indexers, and iterable factories. The former `TypeManager` entry points and their private callable builders were moved, not retained as forwarding aliases.
- Verified with `RunCtest_CMakeTools` after the removal: the build passes and all 13 CTest entries pass. The 26 failures the seal caused (`nomad_test_bindings` 4, `nomad_test_language` 8, `nomad_test_remaining` 14) are gone. Only seal-specific assertions were removed from the tests.
- Member descriptors still hold their own ownership. Descriptor ownership remains with the declaring type.
- Stored-property declarations, property ID allocation, and type/property catalogs remain on `TypeManager`. Declaring types still own their member descriptors; `Runtime` allocates and catalogs callable IDs and indexes native members. TypeManager has no direct Runtime calls or Runtime.hpp source dependency. It retains Runtime identity and callback parameters for descriptor construction and now also owns event declarations and EventIds. Hub declaration ownership and the singleton-hub redesign remain separate work; this does not claim that full architectural migration is complete.
- Both approved registration-boundary stages are implemented. Class rejection is non-logging, and type/family names are opaque catalog strings, including empty/internal names. TypeManager preserves descriptor/ID integrity and nominal uniqueness; callback/event metadata retain non-nominal display-name behavior. Runtime's public declaration entry points coordinate namespace policy and publication. Source syntax remains compiler-owned, and actual construction/allocation exceptions propagate.
- Compiler registration, built-in collections, game/audio/log APIs, and existing contract tests now use the Runtime member entry points. Registration keeps the existing validation, diagnostics, callback signatures, owner categories, and layout behavior; it does not execute callbacks or initializers.
- Event declaration ownership is implemented and validated below. Review the remaining hub declaration and execution responsibilities in separately approved steps. Do not revive the superseded name-check-only refactoring.
- Registration-before-compile setup, Game API setup, and fresh runtimes for language-server analysis batches existed only to satisfy the seal and are dropped. Whether the Interpreter or VM should require successful compilation is an open question (Q4).
- The earlier blanket V5 `static_cast` and ScriptObject field-table proposals were not implemented. The checked query preserves receiver validation and per-instance layouts; registration ownership changes do not authorize changing those contracts.

#### Approved event declaration ownership move

- The user selected `events_first` as the first non-performance ownership
  step. At review, Runtime owned the stable event-definition vector and allocated
  EventIds; TypeManager already owned EventType objects. Consolidate event
  declaration storage, IDs, catalog creation, and metadata queries on
  TypeManager before considering hub/singleton changes.
- Move the existing `vector<unique_ptr<EventDefinition>>` catalog out of
  Runtime. Keep individually owned definitions so borrowed pointers remain
  stable through registration growth and reentry until the owning Runtime
  is destroyed. Do not replace ordinary detached snapshots or EventType's
  independent const definition copy with shared/borrowed mutable state.
- TypeManager supplies catalog-only event creation and the current name/ID
  lookup and first/next enumeration behavior. It allocates EventIds and
  associated EventType declarations using its existing type-registration
  machinery. Names remain opaque catalog strings; do not call Runtime
  namespace policy or publication from TypeManager.
- Runtime keeps `registerEvent` and the existing public event query/enumeration
  facade. Registration still performs namespace and duplicate checks,
  reports the existing errors, delegates catalog creation, and publishes the
  event's typed HubMember only after a valid registration. These are retained
  host APIs, not obsolete compatibility shims.
- Preserve 0-based contiguous EventIds, ordering, duplicate/rejection
  behavior, checked missing-definition results, parameter/name/doc ownership,
  non-nominal EventType display names, and existing Type identity rules.
  No new event-parameter restrictions, syntax, dispatch/callback behavior,
  initialization requirement, or declaration seal is approved.
- Retain the current invalid-type rejection cleanup and propagating
  exception/publication behavior. This step does not introduce a new
  whole-registration transaction, undo Runtime side effects, or silently
  strengthen failure guarantees. Discuss any necessary ordering/failure
  deviation before implementing it.
- Keep Runtime-to-TypeManager dependency direction. TypeManager may pass its
  borrowed Runtime identity to an EventType constructor, as existing catalog
  creation does, but must not call Runtime registration/publication APIs.
  Use narrow public catalog methods rather than friendship.
- Hub storage/member maps and current qualified-name behavior remain on
  Runtime in this step. Do not add HubType/singleton instances, aliases,
  receiver semantics, parent-child hub relationships, new VM instructions,
  indexes, or other performance changes. The performance deferral remains
  in force; no lookup benchmark run is part of this ownership implementation.
- Add focused catalog/publication/ID-order and stable-definition coverage
  in existing tests, keeping R30's growth/reentry and detached-copy behavior.
  Runtime/TypeManager layout changes require dependent consumers to be
  rebuilt and validated. Run CTest directly through the workspace integration,
  without a separate pre-test build, and validate the existing full suite
  before closing this ownership step. Update host/catalog documentation.
- This design review changed only the plan. The implementation and validation
  are recorded below. Review hub declaration ownership separately afterward;
  settle Q5-Q7 before any singleton language changes.

#### Event declaration ownership implementation and validation

- TypeManager owns the individually allocated EventDefinition catalog and
  allocates EventIds. `createEvent` creates the definition and associated
  EventType through existing catalog registration. Name/ID queries and
  first/next enumeration now belong to this catalog. No reverse
  TypeManager-to-Runtime registration/publication calls or friendship were added.
- Runtime retains its public event API, namespace/duplicate checks and errors,
  and typed HubMember publication. Its queries delegate to TypeManager and
  preserve the existing results and enumeration, including catalog-only events.
  EventType still owns its detached const definition copy; borrowed definitions
  and parameter addresses remain stable through growth and reentry.
- During implementation, review identified that rebuilding all catalog events
  would incorrectly adopt TypeManager-only declarations into hubs. The user
  explicitly approved `completed_catalog_only`: Runtime records completed
  public EventIds separately for namespace rebuilding. Bookkeeping capacity
  is reserved before creation; publication intent is recorded after catalog
  and EventType success, before hub insertion.
- This approval intentionally changes one legacy exception edge: a definition
  retained by an EventType construction exception stays queryable but is not
  silently published on a later hub rebuild. A completed public declaration
  whose hub insertion throws retains publication intent and can be adopted
  on rebuilding. No whole-registration transaction or new rollback framework
  is introduced. Invalid EventType registration still removes the attempted
  definition; ordinary allocation/construction exceptions propagate.
- Catalog-only names remain opaque, including empty/internal strings and names
  colliding with hub members. Their creation does not publish namespace members,
  even when hubs are added later. Public registration still rejects names
  already in the catalog and uses the existing namespace checks and logging.
  No new parameter restrictions, ID format, dispatch behavior, declaration seal,
  or initialization requirement is introduced.
- Two focused TypeManager tests cover opaque catalog names and metadata/type
  identity, input ownership, duplicate rejection, mixed public/catalog-only
  contiguous IDs and enumeration, stable pointers, and late parent/child hub
  rebuilding without private-event adoption. Existing R30 detached-copy and
  actual reentrant-registration coverage remain intact.
- A focused existing-file Debug failure test replays eligible allocation
  failures with fresh fixtures. It verifies both retained incomplete
  declarations remaining unpublished and completed declarations retaining
  publication through later rebuilding. It reuses the existing hook and
  excludes Debug proxy-sized requests, including same-sized real allocations.
  This is bounded failure coverage, not all-allocation or cross-platform
  injection. No performance benchmark or production fault API was added.
- All 13 existing Windows Debug CTest entries pass, run directly through the
  workspace integration without a separate pre-test build. Compiler exports,
  LSP, module/runtime, distribution/package integrations, staged scripts,
  binding/language/remaining/audio tests, cleanup contracts, and packaged
  graphical smoke are green. The explicit `nomad-development` workspace build
  also passes after testing, covering the updated tools, modules, runtime and
  tests against the changed Runtime/TypeManager layouts.
- Embedding documentation now describes catalog-only event creation, shared
  query identity, stable metadata, and the approved exception/publication
  contract. No manual gameplay, Release suite, concurrent-registration
  guarantee, or performance improvement is claimed.
- This completes the bounded events-first implementation. Performance remains
  deferred, and hub storage/member maps, qualified lookup, singleton instances
  and VM behavior are unchanged. Next reviewable scope: review hub declaration
  ownership separately, then obtain decisions on Q5-Q7 before any singleton-hub
  language changes.

#### Planned hub types and singleton instances

Status: planned direction, not implemented. Settle Q5–Q7 before implementing language changes. The current native free/hub registration boundary remains in effect until migration.

- Introduce a specialized `HubType` derived from `ClassType`, reusing `CompositeType` member metadata and ordinary reference-object behavior rather than maintaining a parallel hub member/storage system.
- `TypeManager` owns hub type declarations and type/property ID allocation. Declaring types retain ownership of their member descriptors. `Runtime` allocates and catalogs callable IDs and retains one ordinary `ScriptObject` instance per hub per runtime, not a process-wide singleton.
- Represent writable hub variables as stored or native properties, and native/script hub functions as ordinary methods. Hub-registration APIs become thin entry points into shared member registration; module/free-function registration remains separate.
- Reuse ordinary object lifetime, property/method resolution, and existing VM instructions. Do not introduce a separate hub storage category, hub-only dispatch instructions, or a second member-resolution system.
- Constants, enum/type declarations, events, and generic type families need declaration metadata and lookup support; `CompositeType` does not currently represent these categories. Design that shared lookup explicitly rather than treating declarations as artificial instance fields.
- Resolve field layouts and singleton allocation timing while preserving existing typed defaults and hub initializer ordering. Account for open hubs and existing instances without reintroducing a global post-compilation declaration seal.
- Register members against their actual owning type. Once dependent callers and lookups are migrated, remove obsolete hub-specific member maps, variable storage, and prefix-based membership rebuilding rather than retaining a parallel model.
- The shared class/struct execution-registration foundation is complete. Deliver the remaining hub redesign in separately approved, reviewable sub-steps: settle semantics; add hub descriptors and singleton lifetime; migrate native/source registration and compiler/interpreter/tooling access; then remove superseded machinery. No singleton-hub implementation has started, and Q5–Q7 remain unresolved.
- Add focused regressions for singleton identity and shared state, properties and methods, ownership, initialization, and the agreed receiver/value/hierarchy behavior. Validate affected consumers and run all CTest entries before completing each implementation sub-step.

### Step 5: Interpreter correctness and simplification (complete)

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
- I10 is resolved by review: `MemberAssignmentStatement::onResolve`, `Compiler::resolveStructField`/`resolveObjectField`, and `StorageLocation::emitAssignment` require exact type-descriptor identity, matching `validateStoredPropertyAssignment` for Interpreter property updates. Callable argument resolution instead uses `acceptsArgument` in both the Compiler and Interpreter. This is a distinct argument-acceptance policy, not an assignment conversion rule. No implicit conversions, relaxed assignments, or new compatibility helper were added.
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
- I13 API ambiguity is resolved by the user's removal decision, not a rename: removed both test-only functional `Interpreter::setProperty` overloads and their private `setStoredProperty` helper. Production borrowed-storage setters and receiver/context wrappers are unchanged. No compatibility shim or replacement-value API was added.
- Migrated useful assignment, snapshot lifetime, exact-type/path validation, zero-sized field, and copy-failure coverage to explicit `InterpreterStorage` ownership plus the existing in-place setters. Removed redundant functional-setter assertions from the property-read test; production getter coverage remains.
- I13 removal validation: all 13 CTest entries pass on `windows-debug`, including migrated ownership/copy-failure tests, borrowed and empty struct mutation, property selection/type errors, and module integration. No functional setter or private helper remains in the engine; mutating production call sites are unchanged.
- I13 validation-order policy was approved separately: both in-place setters now check receiver-storage width before resolving a property selector, matching the existing direct-ID setter. Existing error text and valid mutation/ownership behavior are unchanged. Only the diagnostic precedence changes when both the storage span and selector are invalid.
- The new regression first reproduced nine wrong-precedence cases for empty, undersized, and oversized receiver spans combined with invalid path selectors. It also checks direct-ID selectors, a valid path, and that rejected calls leave all supplied slots unchanged.
- I13 storage-first validation: all 13 CTest entries pass on `windows-debug`. Both retained setters now report the existing receiver-width error consistently when storage and selector are invalid, while existing valid assignment, selector/type-error, empty-struct, ownership, and copy-failure coverage remains green.
- I15 is resolved by the user's approved borrowed-facade contract: `Interpreter` holds a reference to the caller's mutable VM, not owned execution state. Constness applies to the facade, not transitively to that VM. Execution through `const Interpreter&` and the mutable reference returned by `getVirtualMachine() const` are intentional; contexts and receiver wrappers retain their existing const Interpreter references.
- Documented that contract and the existing VM/interpreter/context lifetime requirements. No signatures, execution behavior, `mutable` members, casts, or extra getters changed. This does not decide the separate C1 context-storage-accessor contract.
- I15 verification is documentation/comment-only: existing `InterpreterTest.cpp` assertions and borrowing/bound-VM tests explicitly cover const facade access, independent contexts, and calls reaching the supplied VM. No C++ rebuild or test rerun was needed; the last full 13-entry run remains I13 validation above.
- C1 is implemented internally without public API changes. Variable and parameter read accessors now resolve through the existing const VM storage overloads or `InterpreterStorage::getStorage()`. Writable accessors continue using mutable storage APIs. Removed the shared private const resolvers that returned writable spans and the receiver's redundant private forwarding resolver; receiver storage resolution remains a non-const operation.
- The small read/write validation blocks remain specialized rather than introducing mutability flags, casts, templates, or new public helpers. Frame validation, invalid/missing ID handling, slot addresses, zero-sized storage, ownership, and diagnostics are unchanged. The approved I15 borrowed mutable-VM facade contract remains separate and unchanged.
- C1 validation: the build and all 13 CTest entries pass on `windows-debug`. Compilation checks the internal const-span paths; existing tests preserve read/write alias identity, live parameter/local/receiver access, missing and empty storage, stale-frame errors, nested evaluation lifetimes, and reference ownership. No new behavior or private-signature-only test was introduced.
- C3's approved naming cleanup is implemented: renamed `InterpreterContext::clearError` to `clearDiagnostics` and `setError` to `addError`, updating engine/test callers and embedding documentation without aliases. Clearing still removes both errors and warnings; adding an error still appends without replacing earlier diagnostics.
- Retained `reportInputError` for explicitly named source input and `addDiagnostic` for full diagnostic metadata; these are distinct host contracts, not dead forwarding APIs. Retained the deleted `const Interpreter&&` constructors: they reject both const and non-const temporary interpreters and prevent contexts from borrowing a destroyed facade. Existing temporary-rejection checks remain unchanged.
- No diagnostic ordering, source attribution, result/loop-control state, disposable-formatting policy, or constructor lifetime behavior changed.
- C3 validation: the build and all 13 CTest entries pass on `windows-debug`. Existing append/first-error ordering, warning/error clearing, explicit-source diagnostics, reset/formatting, and temporary-interpreter rejection coverage remains green. No old diagnostic method names remain in engine, test, app, module, template, or sample C++ code.
- C4 is resolved by review, retaining the step-2 convenience decisions. Name-based reads support host/console bindings and parameter names; ID-based variable and parameter storage APIs serve already-resolved AST references. Scalar name reads also enforce the scalar-versus-struct contract. These are distinct entry points, not duplicate compatibility shims.
- `setVariable(name, type, storage)` is used by interpreted assignment and debugger/console editing, and can declare or retype context-owned variables while preserving the stricter frame-variable/parameter rules. Replacing it with raw ID writes would change declaration, ownership, and binding behavior. Resolved AST reads already use variable IDs rather than performing repeated name lookup.
- Kept the explicitly approved `setVariableProperty`, `setReceiverProperty`, `getVariableStorage(name)`, and `hasWarning` conveniences. No additional accessor removal, cache, index, lookup benchmark, or binding-semantics change was introduced. Lookup cost alone does not justify removing the host-facing API.
- C4 verification is source/contracts review only. Existing context-growth/rebinding, scalar/struct restriction, parameter/local access, missing/empty storage, snapshot, and retired-storage tests cover the retained contracts. Only documentation changed, so no C++ rebuild or test rerun was needed; the last full 13-entry run remains C3 validation above. Step 5 is complete.
- Next stage: step 6, Runtime correctness and dead code, starting with R3 string-lookup validation. The separate registration ownership split remains incomplete; completing step 5 does not claim that architectural work is done.

### Step 6: Runtime correctness and dead code (complete)

- R3's failure policy was revised by user decision: `getString` and `getStringByName` return `nullptr` without logging for invalid/unregistered IDs or absent text. The caller decides whether to log. No empty-string fallback or implicit registration is permitted.
- R3 is implemented: guard the string table index before conversion/access; name lookup delegates to that checked accessor, including for missing text. Successful lookups remain borrowed, identity-stable, and reference-count neutral; registered empty text remains a valid non-null result.
- The new language-registry regression first reproduced the unchecked index as a Debug CRT vector-bounds failure. Its scoped test-process settings keep Windows assertion reporting non-interactive without hiding test failures. Coverage checks negative IDs, an ID outside the table, the first unregistered index, missing text, absence of lookup logging, unchanged interning/reference counts, and registered empty text. No opcode or string-registration policy was changed.
- R3's initial implementation passed the build and all 13 CTest entries on `windows-debug`. The regression now requires null results without logging, no implicit registration, unchanged valid-string identity/reference counts, and non-null registered empty-string lookup.
- R3 logging-policy follow-up: the build and targeted `nomad_test_language` entry pass on `windows-debug`, including the no-logging regression. No full-suite rerun was performed for this follow-up.
- R4 review led to the approved native free/hub registration split. Ordinary native registration now validates and catalogs module/free functions without publishing hub membership; qualified names do not imply membership. The explicit hub API uses the supplied owner and local name, including dotted local names, and publishes only valid callable IDs.
- Native membership survives nested-hub registration without adopting ordinary functions or moving explicitly owned functions. Script-function and non-callable membership behavior is unchanged. Game, audio, and music registrations and hub-specific fixtures now use the explicit API; game initialization checks registration results.
- Focused regressions first reproduced automatic adoption and owner migration. Coverage now checks callback and typed registration before/after hub declarations, explicit overload ownership across rebuilding, rejected duplicate registration without catalog/member changes, and compiled script calls.
- Validation: the build and all 13 CTest entries pass on `windows-debug`, including module, package, audio, and graphical smoke tests. No manual gameplay validation was performed. Callable-capacity exhaustion was not exercised; R4's script invalid-ID publication path remains open.
- The class/struct execution-registration move is implemented: Runtime now exposes the native/script method and constructor APIs, native properties, indexers, and iterable factories. TypeManager retains type/stored-field declarations and property IDs; all dependent engine, compiler, module, and test callers are migrated without compatibility shims.
- Validation of that move: the build, focused language/runtime groups, and all 13 CTest entries pass on `windows-debug`, including consuming module/package builds, tooling, audio, and graphical smoke tests. Existing regressions cover shared callable IDs, declaring-type descriptor ownership and lifetime, rejected registrations without publication, constructor factory result types, struct storage ABIs, and unchanged finalized layouts. Native-property coverage also checks identity in the TypeManager property catalog. No manual gameplay validation was performed.
- R4's remaining script-publication guard is implemented. `registerScriptFunction` publishes hub membership only when callable registration returns a valid ID; `registerHubScriptFunction` uses that same path. Existing failed-candidate cleanup is unchanged, and successful script membership remains name-based. No new logging, capacity controls, or test-only hooks were added.
- R4 validation: the build, focused language/runtime groups, and all 13 CTest entries pass on `windows-debug`. Existing tests cover successful publication, invalid hub IDs, conflicting declarations, duplicate/native callable rejection, and unchanged catalogs after those rejections. Source review confirms that catalog exhaustion returns an invalid ID, destroys the rejected candidate, and now skips hub publication. The exhaustion branch itself was not runtime-tested; no new test is presented as reproducing that branch. Allocation-exception rollback is outside this fix.
- R5 is resolved by review, with no C++ change. The iterator `current` getter copies/retains into empty local storage using `Type::assignValue`, then transfers that owned value into the caller's empty output with raw assignment. `RuntimeValue` is trivially copyable and has a trivial destructor, so the local does not release the transferred reference or leave an additional retained reference behind. The caller releases its owned result through the value type; the iterator separately releases its current value on advancement or destruction. Passing an already owned output would violate the existing getter contract, not establish a leak in this transfer.
- R5 validation: existing `nomad_test_language` and `nomad_test_remaining` entries pass on `windows-debug`, including iterator advancement failure/retry ownership and native-property/compiled-loop coverage. The getter-transfer conclusion is based on source and contract review; no new regression or behavior change was introduced. No separate build or full-suite rerun was needed for this documentation-only disposition; the last full 13-entry run remains R4's validation above.
- R6's first review traced instruction, dynamic-variable, context-name, and constant accessors. Instruction registry access serves compiler emission, verification, and diagnostics rather than VM instruction dispatch. Constant name/type lookup delegates to unchecked `VariableMap` accessors, while constant-value lookup already bounds-checks but silently leaves the output unchanged on failure. Constant reads and dynamic-variable callback invocation are shared with VM execution and remain unchanged in this sub-step.
- R6's first fix is implemented: `getInstructionFn` returns `nullptr` without logging for invalid or unregistered IDs. This honors the existing null-result handling in `Compiler::addOpCode` and `setOpCode`, preserving their unknown-opcode diagnostics. Valid callback mappings are unchanged; no checks were added to VM execution.
- The new instruction-lookup regression first reproduced a Debug CRT vector-bounds assertion. It now covers negative IDs, an out-of-range ID, the first unregistered index, unchanged valid callback lookup, and rejected compiler emission without appending an instruction. The existing scoped non-interactive bounds-reporting setup is shared by the string and instruction regressions through a test fixture.
- R6 first-fix validation: the build, focused `nomad_test_language` entry, and all 13 CTest entries pass on `windows-debug`, including compiler, tooling, module/package, audio, and graphical smoke tests. No manual gameplay validation was performed. Other metadata accessors and their failure policies have not changed.
- R6 metadata policy was explicitly approved: invalid or unregistered IDs throw for names/reference-returning metadata, return `nullptr` for pointer queries, and return `false` for capability predicates. None of these queries log; the caller decides how to report failures. This approval covers instruction metadata, dynamic-variable metadata/capabilities, and context names, not constant reads or VM callback invocation.
- That metadata group is implemented with existing bounds helpers and unchanged public signatures. `getInstructionName`, `getInstructionOperands`, `getInstructionStackEffect`, `getDynamicVariableName`, and `getContextName` throw `NomadException` for invalid IDs. `getDynamicVariableType` returns null, and `canGetDynamicVariable`/`canSetDynamicVariable` return false. Valid empty names, empty operand lists, stored stack effects, and read-only/write-only permissions retain their existing behavior.
- The new metadata regression first reproduced a Debug CRT vector-bounds assertion. It now checks negative IDs, an out-of-range ID, and the first unregistered index for each catalog; verifies valid edge cases; and captures every log level to require no lookup logging. Existing compiler, script, interpreter, and format-string coverage remains unchanged.
- R6 metadata validation: the build, focused `nomad_test_language` entry, and all 13 CTest entries pass on `windows-debug`, including consuming modules/packages, tooling, audio, and graphical smoke tests. Constant reads, dynamic-variable callback invocation, and VM execution code are unchanged. No manual gameplay validation was performed.
- R6 constant-access policy was explicitly approved: host name/value reads throw `NomadException` for invalid or unregistered IDs, type queries return `nullptr`, and none log. A failed value read leaves its output unchanged. Successful reads remain borrowed and reference-count neutral.
- That constant group is implemented without changing existing host signatures. Two inline accessors, `getUncheckedConstantValue` and `getUncheckedConstantType`, require compiler-resolved IDs and retain the same borrowed lookup semantics without validation. Constant-load opcodes and the VM's compiled-format variable lookup use them; the load opcodes still copy into owned destinations. No new instructions, storage categories, or runtime mode flags were introduced.
- The new host regression first reproduced both the silent failed value read and a Debug CRT bounds assertion in name lookup. It now covers negative, out-of-range, and first-unregistered IDs, unchanged failed-read output, valid empty names, borrowed string identity/reference counts, and no logging at any level.
- New VM execution coverage invokes the actual result, intermediate, and stack constant-load instructions, verifies one retained reference per owned destination, and verifies cleanup back to the baseline. The compiler-resolved variable lookup is separately checked to remain borrowed and reference-count neutral.
- R6 constant validation: the build, focused language/runtime groups, and all 13 CTest entries pass on `windows-debug`, including compiled scripts, interpreter/format access, modules/packages, tooling, audio, and graphical smoke tests. No manual gameplay validation was performed. R6 is complete for checked lookups; direct dynamic-variable callback invocation retains its existing valid-ID precondition and is reviewed separately under R7 for missing callbacks.
- R7's first review found missing compiler validation: dynamic-variable read checks were limited to non-Script modes, direct compiled assignments omitted setter checks, and simple format-identifier compilation accepted dynamic variables without getters. The first reviewable fix targets those resolution boundaries rather than adding repeated checks to VM dispatch.
- That compiler group is implemented. Dynamic reads require a getter in every compiler mode. Assignment setter validation is shared before the script/interpreter mode split, preserving the existing read-only diagnostic. Simple format-identifier resolution checks readability both when validating and when populating a format descriptor. Runtime callback APIs and VM execution are unchanged; no new APIs or instructions were added.
- Focused regressions first reproduced 11 failures across unsupported compiled reads, plain/expression format placeholders, assignments, and format-helper validation. They now pass and also verify that supported read-only reads and write-only assignments work in scripts and the Interpreter without invoking callbacks during compilation.
- R7 compiler validation: the build, focused `nomad_test_language` entry, and all 13 CTest entries pass on `windows-debug`, including analysis/tooling and module/package consumers, audio, and graphical smoke tests. No manual gameplay validation was performed. Direct host calls still require registered IDs and the corresponding callback; their error contract has not been changed or claimed fixed.
- R7's direct host contract was explicitly approved and is implemented: invalid or unregistered IDs and absent callbacks throw `NomadException` without logging. Rejected reads leave output unchanged before invocation. Exceptions from an actual callback propagate unchanged; its output writes and other side effects are not rolled back.
- Two inline methods, `getUncheckedDynamicVariableValue` and `setUncheckedDynamicVariable`, serve compiler-resolved VM dispatch without ID or permission checks. Dynamic-variable opcodes and compiled-format variable lookup use them. Getter output remains owned; setters borrow their input and copy what they retain. No new instructions or runtime mode flags were added.
- `setStringDynamicVariable` now uses the existing `RuntimeValueGuard` to release its temporary on both return and throw. Setter-retained values keep their independent ownership, including when a setter retains and then throws. This covers the tightly coupled dynamic-string portion of R23; context-string cleanup and registration-copy optimization remain separate.
- The new host regressions first reproduced `std::bad_function_call`, an extra retained string reference, and three leaked temporary strings. They now cover invalid IDs, missing accessors, unchanged rejected outputs, no logging at any level, unchanged actual callback exceptions and side effects, and retained/non-retained string cleanup on success and failure.
- VM execution coverage invokes the actual result/intermediate/stack dynamic-load instructions and the setter instruction, checking owned results, borrowed setter inputs, retained copies, and cleanup. Compiler-resolved format-variable lookup is also covered. The build, focused language/runtime groups, and all 13 CTest entries pass on `windows-debug`, including modules/packages, analysis/tooling, audio, and graphical smoke tests. No manual gameplay validation was performed.
- R7 is complete. R21's silent constant-read failure was already fixed by the approved R6 checked-host/unchecked-VM split; its table entry is reconciled below without another code change.
- R8 review confirmed that documentation fields and arguments are unconditional in `Documentation.hpp`, while the repository does not define `NOMAD_FUNCTION_DOC`. Its only source/test uses were two stale registration branches and a guarded keyword assertion. Keyword documentation is consumed by language-server completion through `getKeywords`.
- R8 is implemented: remove those obsolete guards and retain supplied keyword and dynamic-variable documentation unconditionally. Re-registering a keyword preserves its ID and original documentation. Dynamic-variable documentation remains private registration metadata; no new accessor or generator output was added. Existing documentation macros, public signatures, execution behavior, and registration-copy behavior are unchanged.
- The existing keyword documentation assertion was made unconditional and first reproduced an empty description. It now verifies the original description after duplicate registration and catalog growth. No new test-only API or documentation build option was introduced.
- R8 validation: the build, focused `nomad_test_language` entry, and all 13 CTest entries pass on `windows-debug`, including language-server/tooling, module/package consumers, audio, and graphical smoke tests. Source/test searches find no remaining `NOMAD_FUNCTION_DOC` uses. No manual gameplay validation was performed.
- R12 is resolved by review without a C++ change. Current host hub registration accepts keyword, numeric, spaced, and hyphenated names; it does not enforce source identifier characters. Its remaining dotted-name loop rejects only empty namespace segments. Nonempty names, valid modes, namespace conflicts, and open/closed redeclaration rules remain intentional host registration contracts, not source-token validation.
- The compiler's `parseHubHeader` requires `Tokenizer::nextIdentifier` and end-of-line before registering the declaration. Existing regressions distinguish unrestricted host names from rejected numeric or malformed source identifiers, retain accepted keyword names, and cover empty namespace segments and conflict rejection without partial registration. The embedding guide already describes this boundary, so no additional API or documentation contract change was needed.
- R12 validation: existing `nomad_test_language` passes on `windows-debug`, including `native_hub_names_are_not_restricted_to_script_identifiers`, `script_hub_names_require_identifier_tokens`, and the namespace-conflict/structure cases. This disposition changes only the plan; no separate build, new regression, or full-suite rerun was needed. The last full 13-entry run remains R8's validation above.
- R23's remaining context-string cleanup is implemented. `setStringContextVariableValue` uses the existing `RuntimeValueGuard`, matching the dynamic-string setter, so its temporary is released on both return and throw. Context setters still borrow input and copy what they retain. Setter exceptions propagate unchanged, and retained values and other context writes are not rolled back. Existing invalid-handle diagnostics and public signatures are unchanged.
- The new context regression first reproduced five failed assertions: leaked temporary strings/objects when the setter throws before or after retention, and an extra reference after retention. It now verifies the original exception, unchanged empty storage before retention, the retained string and its single context-owned reference after retention, and final string/object counts returning to baseline.
- R23 correctness validation: the build, focused language/runtime groups, and all 13 CTest entries pass on `windows-debug`, including existing successful context-string assignment, consuming modules/packages, tooling, audio, and graphical smoke tests. No manual gameplay validation was performed. The registration-copy optimization remains deferred to the measured step-8 sweep.
- Step 6 is complete. Step 7's progress and next reviewable scope are recorded below. Before implementing singleton hubs, settle receiver, first-class value, and hierarchy semantics in Q5–Q7. Remaining declaration/namespace dependency cleanup stays separate.
- Invalid IDs and publication: R3, R4, R6, R21.
- Null-safe dynamic variables: R7 complete with checked host calls and compiler-trusted VM dispatch.
- Ownership: R5 resolved by review under the existing empty-output getter contract.
- Exception-safe string setters: R23 correctness complete; registration-copy optimization remains in step 8.
- Dead `NOMAD_FUNCTION_DOC` blocks: R8 complete; registration documentation is retained without a feature flag.
- Identifier syntax validation: R12 resolved by review; the compiler already owns source-token validation.

### Step 7: redundancy and naming (complete)

Default: no behavior change. Consolidate duplicated helpers and align names and style with the repository rules. Any observable diagnostic or API change needs explicit review.

- R10 is implemented: hub constant, enum, and callback-based native-function registration reuse the existing `qualifyHubMemberName` path. Native functions still check local conflicts and publish to the explicitly supplied hub only after successful callable registration; qualified names do not choose a different owner. Valid local names and all registration results are unchanged.
- R10's diagnostic trade-off was explicitly approved before implementation. Invalid hub IDs now use the same `Cannot register hub member '<name>': invalid hub` diagnostic as the existing helper-based APIs, replacing the three type-specific variants. Owner validation remains before definition validation; non-owner diagnostics are unchanged. No new helper overloads, mode flags, or registration abstractions were added.
- The focused regression checks invalid, negative, and out-of-range owner IDs across all three wrappers, including malformed member definitions, requiring one error per rejection with the approved text. Existing arbitrary-name, overload/conflict, and nested-hub ownership coverage remains green.
- R10 validation: the build, focused language/runtime groups, and all 13 CTest entries pass on `windows-debug`, including modules/packages, tooling, audio, and graphical smoke tests. Public embedding documentation describes the common invalid-owner diagnostic. No manual gameplay validation was performed.
- R11 review confirmed that the broad conflict chains encode different policies. Hubs reserve catalog/family names and context prefixes; hub variables require an existing owner and pending initialization; event/dynamic-variable registrations retain their existing catalog coexistence outside hubs. Type publication preserves callback/event exceptions and its identity/namespace rechecks. These paths are intentionally kept specialized rather than merged into a generalized validator.
- R11's genuinely shared part is consolidated in the existing `canRegisterHubMember` predicate: reject a reserved hub name or an existing incompatible hub member. Hub-name reservation precedes the existing internal-name bypass. Registration and type-availability callers no longer repeat the same hub-name check; broader checks, overload allowances, arbitrary host names, and publication validation remain unchanged.
- The tightly coupled Runtime portion of R14 was explicitly approved: the shared predicate does not log, and `canRegisterTypeName` is a silent availability query. Registration callers keep their current category-specific errors without a second generic member-conflict log. Type-publication failures still throw their existing exceptions without predicate-side logging. No mode flags, helper overloads, or public signature changes were added.
- The regression first reproduced logging from an availability query and duplicate registration errors. It now verifies silent queries, reserved hub names containing `@`, the retained generated-name bypass, one unchanged caller diagnostic for each of six registration paths, and exception-only reporting from rejected type publication.
- R11/Runtime-R14 validation: the build, focused language/runtime groups, and all 13 CTest entries pass on `windows-debug`, including outside-hub catalog coexistence, event/callback metadata, module/package consumers, tooling, audio, and graphical smoke tests. Public documentation now states that type availability does not log. No manual gameplay validation was performed.
- R14's initial class-registration reporting cleanup was explicitly approved and implemented with a boolean-plus-error-text predicate and caller logging. It removed duplicate diagnostics for constructor-time conflicts. The later no-logging class-registration revision below supersedes that reporting contract.
- Both validation passes remain: preflight before reservation/construction, then identity, name availability, and mode checks before publication in their original order. Rejected descriptors are destroyed, unused reserved IDs remain unavailable, and declarations made during construction are not rolled back. Public registration signatures and ownership are unchanged; no validation flags, result framework, or new registration APIs were added.
- Focused tests cover the four preflight failure categories and reentrant type/hub name collisions. The latter first reproduced two duplicate-log failures, then verified one actual error, retained constructor-created declarations, and rejected-descriptor cleanup. Existing successful creation, nested type reservations, constructor exceptions, and namespace validation coverage remains green.
- R14 validation: the build, focused language/runtime groups, and all 13 CTest entries pass on `windows-debug`, including consumers of the templated class-registration API, modules/packages, tooling, audio, and graphical smoke tests. No manual gameplay validation was performed.
- The user subsequently approved invalid-ID rejection as class registration's only reporting path, leaving logging/diagnostics to the compiler, interpreter, or host. Duplicate nominal names remain rejected for catalog integrity, and construction/allocation exceptions still propagate unchanged.
- First reviewable stage complete: `registerClass` and `publishClass` no longer log, and their unused private error-text plumbing and header logging dependency are removed. Preflight/publication checks, empty-name and hub-reservation policy, modes, nominal uniqueness, public signatures, IDs, and descriptor cleanup remain unchanged in this stage. Other TypeManager registration methods retain their existing contracts.
- Updated preflight and reentrant-conflict tests first reproduced three unwanted-log failures. They now verify rejection without logging while retaining the existing checks, constructor-created declarations, and rejected-descriptor cleanup. Existing constructor-exception and successful-registration coverage also passes. The build, targeted runtime group, and all 13 CTest entries pass on `windows-debug`; no manual gameplay validation was performed.
- The next coordinated boundary stage was approved and is implemented. TypeManager class/struct/raw/enum/opaque-ID and family registration are catalog-only. Names are opaque, including empty/internal strings; enum value, mode, argument, descriptor/ID, and nominal-integrity validation remain. `registerType` returns a `TypeId` with non-logging invalid-ID rejection instead of throwing for invalid descriptors; the redundant `catalogType` entry point is removed. Construction/allocation exceptions are not caught or converted to invalid IDs.
- Runtime now provides public declaration entry points and coordinates type/family namespace publication and enum constants/operators. Compiler, collection, game/module, and directly dependent test callers are migrated. Catalog-only types/families are not implicitly adopted during hub rebuilding: Runtime tracks only explicitly published declarations. Existing public qualified-name membership behavior is otherwise retained; no singleton-hub semantics were introduced.
- Public family registration reuses the existing factory/initializer callback contract: reject namespace conflicts before catalog registration, then publish an accepted specialization before its initializer runs. The manager retains factories, declarations, specialization caches, and IDs. Exceptions from actual callbacks propagate; declarations made by callbacks are not rolled back.
- Initial catalog-only regressions reproduced nine failures involving opaque-name rejection, automatic membership, and enum execution publication. Coverage now includes all empty-name creation paths, nominal/ID rejection, no adoption before/after hub rebuilding, source namespace conflicts and descriptor cleanup, and public family publication before initialization. Existing source diagnostics and callback interning remain green.
- Event descriptors exposed another non-nominal display-name case during validation. Their existing `Type::asEvent` query is now implemented by `EventType`; distinct event declarations retain the shared `event` display name without being treated as duplicate nominal types or nominal hub members. Existing event catalog-growth coverage passes; no new event syntax or IDs were introduced.
- Validation: the build, focused language/runtime groups, and all 13 CTest entries pass on `windows-debug`, including module/SDK packaging, compiler/tooling, audio, and graphical smoke tests. API return shapes and publication semantics changed, so consuming C++ modules must be rebuilt and public declaration callers must use Runtime entry points. ID-space exhaustion was not runtime-tested, and no general allocation-rollback guarantee was added. No manual gameplay validation was performed.
- The naming/publication stage is complete. The next review found that stored-property name collisions are already validated by the declaring `CompositeType` catalog, whereas member-template signatures enforce a separate global compatibility policy. These are handled as separate reviewable sub-steps.
- Stored-property validation is implemented without Runtime back-calls: remove the redundant native-member index lookup and rely on the owner catalog's existing native/script method and property collision checks. Matching stored-field declarations still reuse their IDs; owner/type/ID/layout validation and catalog ownership are unchanged. No new validation APIs, flags, or VM checks were added.
- The diagnostic trade-off was explicitly approved: native-method name collisions now use the existing generic property-duplicate diagnostic, matching script-method collisions, instead of the separate named TypeManager error. The existing collision regression first reproduced that diagnostic difference, then verified one error per rejection and unchanged next-property IDs. It also covers a type-owned native method that has not been indexed in Runtime.
- Validation: the build, focused language/runtime groups, and all 13 CTest entries pass on `windows-debug`, including compiler/property registration, module/package consumers, tooling, audio, and graphical smoke tests. No manual gameplay validation was performed.
- The member-template boundary was explicitly approved and is implemented. `Runtime::registerMemberTemplate` retains the template-definition API and global parser-compatibility checks. TypeManager accepts a prepared `MemberSignature` and validates only local family/metadata integrity. Public callers are migrated; no new instructions, registration flags, or generalized validation framework were introduced.
- Runtime's parser lookup considers only explicitly public template signatures. Catalog-only families can store incompatible same-name signatures without changing global parsing. Existing precedence is preserved: the earliest registered family supplies a public signature regardless of template-registration order, and public templates precede native-member fallback.
- Family-specific catalog lookup returns snapshots; Runtime indexes family identity and declaration order rather than retaining pointers into movable family records. The initial pointer-based index failed the family-growth regression and collection tests; using family identity corrected that lifetime defect. The obsolete one-name catalog lookup and old template-definition catalog signature are removed without forwarding aliases.
- Boundary regressions first reproduced three failures involving implicit global publication and cross-family rejection. Coverage now verifies catalog-only isolation, family-specific signatures, invalid local metadata without publication, public precedence after catalog growth, and the existing public arity/parser-dependent type diagnostics.
- Validation: the build, focused language/runtime groups, and all 13 CTest entries pass on `windows-debug`, including collection/OS consumers, compiler/tooling, module/SDK packaging, audio, and graphical smoke tests. TypeManager's template API changed; C++ consumers must use Runtime for public templates or supply a prepared catalog signature. No manual gameplay validation or new allocation-rollback guarantee was added.
- The parameter-classification boundary was explicitly approved and is implemented. `TypeManager::getParameterShape` is a read-only catalog query; `Runtime::getParameterShape` delegates its existing facade, and constructor inference calls the catalog directly. Canonical source/event type identity and callback precedence remain unchanged. Null generic parameters have ordinary value shape. This query does not register types, validate names, or impose naming policy.
- No parser restructuring or new VM instructions were added. Existing tests already cover omitted `$file`, `$function`, and `$line` constructor arguments, rejected incompatible/foreign argument types, event/callback layouts, and public member signature compatibility; no new test was needed for this behavior-preserving relocation.
- Validation: the build, focused language/runtime groups, and all 13 CTest entries pass on `windows-debug`, including compiler/tooling, module/SDK consumers, audio, and graphical smoke tests. No manual gameplay validation was performed. Source inspection confirms that predicate-type lookup is the only remaining direct Runtime call in TypeManager.
- Predicate lookup now uses the catalog-local boolean descriptor and existing callback interning. Runtime-owned catalogs preserve the same parameterless boolean callback identity. The unused Runtime.hpp source include is removed; TypeManager no longer invokes Runtime methods directly.
- The missing-entry contract was explicitly approved: a separately constructed TypeManager without a boolean entry throws `NomadException` without logging or creating a callback. It does not borrow a return type from Runtime or register a boolean type implicitly. Callback construction and allocation exceptions still propagate unchanged.
- The regression first reproduced four failures involving missing-entry acceptance, catalog mutation, foreign-catalog return metadata, and callback identity. It now verifies rejection without mutation/logging, local boolean identity, and cached callback reuse. Existing runtime predicate and callable/parser tests remain green.
- Validation: the build, focused language/runtime groups, and all 13 CTest entries pass on `windows-debug`, including compiler/tooling, module/SDK consumers, audio, and graphical smoke tests. No manual gameplay validation was performed. Direct TypeManager-to-Runtime call cleanup is complete; remaining declaration ownership is not.
- R16 review confirmed that the former function-only source-line and start-address APIs were already removed. The remaining function-only instruction lookup was used only by `VirtualMachine::getCurrentScript`, which itself had test-only callers. Removing both APIs was explicitly approved rather than retaining a second navigation scan or a forwarding alias.
- R16 is implemented: remove `Runtime::getFunctionAtInstructionIndex` and `VirtualMachine::getCurrentScript`. Existing catalog/frame tests now use general script-callable navigation and `getCurrentCallable`; function-only category checks use the existing `getScriptFunction(id)` view. Function-only ID lookups and enumeration remain unchanged.
- General navigation behavior is unchanged: inclusive source lines select the innermost matching callable, instruction ranges exclude the end address, and start-address lookup requires an exact match. No new resolver, range policy, cast, or VM execution check was introduced.
- Validation: the build, focused language/runtime groups, and all 13 CTest entries pass on `windows-debug`, including callable catalog growth, compiled method/constructor/callback navigation, frame-backed interpretation, module/SDK consumers, tooling, audio, and graphical smoke tests. No code references to the removed APIs remain. No new test was needed for this removal because the existing coverage was migrated to production APIs. No manual gameplay validation was performed.
- R17's owned-metadata group was explicitly approved and is implemented. Mutable Runtime format lookup returns `FormatString*`, while the const overload returns `const FormatString*`. Function enumeration has mutable and const overloads accepting `std::vector<ScriptFunction*>` and `std::vector<const ScriptFunction*>`. Mutable compilation access remains available; const callers cannot retrieve writable metadata through these APIs.
- The const documentation reader uses the new const enumeration. Exact compile-time API checks verify the format return type and that mutable function enumeration cannot be invoked on a const Runtime. Existing tests also verify shared descriptor identity, failed format lookup, catalog growth, vector replacement, and exclusion of native functions, methods, and constructors.
- No lookup results, registration, execution, or metadata ownership changed. No const casts, new accessor framework, or declaration seal was introduced. Runtime's variable-context value/tracking APIs and the approved const-callable Interpreter facade over a mutable borrowed VM are unchanged in this sub-step.
- Validation: the build, focused language/runtime groups, and all 13 CTest entries pass on `windows-debug`, including formatting, documentation generation, compiler/tooling, module/SDK consumers, audio, and graphical smoke tests. No manual gameplay validation was performed.
- R17's remaining context contract was explicitly reviewed and retained. Contexts combine metadata and live values, and can forward reads/writes to scene/entity state. `getVariableContext` intentionally returns a borrowed mutable pointer through const Runtime; value and read/write-tracking operations remain const-callable. Const Runtime does not freeze context contents or metadata. This shallow-const facade is distinct from the owned format/function metadata views.
- No signatures, context implementations, diagnostics, ownership, or execution behavior changed. Header and public embedding documentation describe the retained contract without new views, casts, snapshots, or a declaration seal. Existing const declarations and dispatch establish callability; no new test was added for unchanged behavior.
- R17 context validation: existing `nomad_test_language` passes on `windows-debug`, including context identity/catalog growth, read/write tracking, live script/interpreter/format access, and string-setter lifetime coverage. Only comments and documentation changed; no separate C++ build or full-suite rerun was needed. The last full 13-entry run remains the owned-metadata validation above. No manual gameplay validation was performed.
- R17 is complete.
- R18's type-view/category cleanup was explicitly approved and is implemented. `getStringType()` returns `const Type*`, matching the other convenience getters, without a concrete-descriptor downcast. The built-in iterator-family initializer uses the existing `asClassType()` query. Name-based lookups, catalog identity, ownership, script behavior, and initialization order are unchanged; no caching, new category API, or VM checks were added.
- The existing string getter regression checks the exact base-pointer return contract and descriptor identity. Class-specific test consumers use `asClassType()` while retaining their foreign-owner and mismatched-owner rejection cases. C++ consumers that relied on the old subtype return must use an appropriate category query and rebuild. Public embedding documentation describes the borrowed base views.
- Validation: the build, focused language/runtime groups, and all 13 CTest entries pass on `windows-debug`, including iterator behavior, string lifetime, compiler/tooling, module/SDK consumers, audio, and graphical smoke tests. No manual gameplay validation or performance measurements were performed.
- R19's remaining parser-classification cleanup is implemented. `parseCallableArguments` uses `Runtime::getParameterShape`, which delegates to the catalog-owned classifier, for source, event, callback, and ordinary/generic value dispatch. Hidden source arguments still consume no tokens; overload omission, argument construction, callback/predicate precedence, and diagnostics are unchanged.
- No public APIs, type registration, parser syntax, or VM instructions changed. Existing language coverage verifies hidden source arguments, ordinary and generic values, overload arity, callbacks, and event dispatch/handler syntax. No new tests were added for this behavior-preserving reuse.
- R19 validation: the workspace build and existing `nomad_test_language` and `nomad_test_tooling` CTest groups pass on `windows-debug`. No full-suite rerun, manual gameplay validation, or performance measurements were performed; the last full 13-entry run remains R18's validation above.
- R20 is implemented: the existing private `getContextVariable` lookup owns the identical invalid-ID diagnostic used by its six accessor callers. Each caller retains its specialized return and context dispatch. No helper overloads, flags, public API changes, new validation, or VM instructions were added.
- The diagnostic-origin change was explicitly approved: invalid-ID errors now identify the shared lookup rather than each accessor. Error text, severity, one error per failed call, empty/null metadata results, unchanged read output, context state, borrowed-value ownership, and propagated context exceptions are retained.
- The focused regression first reproduced 20 diagnostic-origin mismatches against the old implementation. It now verifies shared provenance and exactly one error for each of the six accessors with invalid, negative, unregistered, and maximal handles, while preserving values and read/write tracking through const Runtime. Public embedding documentation records the rejection contract.
- R20 validation: the workspace build and existing `nomad_test_language` and `nomad_test_remaining` CTest groups pass on `windows-debug`, including context identity/catalog growth, script/interpreter/format access, and string-setter lifetime/exception coverage. No full-suite rerun, manual gameplay validation, or performance measurements were performed; the last full 13-entry run remains R18's validation above.
- R22's native-function cleanup is implemented. `validateNativeFunctionSignatures` uses the mutable native lookup with each registered callable ID instead of a separate RTTI cast. That lookup is now the non-const `getNativeFunction(id)` overload after the concurrent rename integrated below. Script functions remain excluded; pending-signature checks, overload validation, completion, descriptor identity, and diagnostics are unchanged.
- The remaining `DeferredType` RTTI was explicitly reviewed and retained. It identifies compiler-owned placeholders, not a runtime storage category, and the same distinction is used by compiler target collection and native callable/property resolution. The user approved keeping that existing mechanism rather than adding a new Type category API or expanding this change into deferred resolution.
- R22 validation: the workspace build and existing `nomad_test_language` CTest group pass on `windows-debug`, including unresolved/completed native signatures, wrong-source IDs, catalog growth, foreign placeholders, generated script/native cycles, and overload conflicts. No new tests were needed for this behavior-preserving lookup reuse. No full-suite rerun, manual gameplay validation, or performance measurements were performed; the last full 13-entry run remains R18's validation above.
- R24's remaining concrete style findings are implemented manually: camelCase opcode lookup naming, K&R constant-getter braces, reference/pointer spacing, one-per-line registration parameters/arguments, and native-registration block/return spacing. Trailing whitespace is removed from Runtime.cpp. Removed function-only navigation APIs and obsolete pointer-spacing/unbraced-if findings were not reintroduced or replaced with unrelated cleanup; no clang-format was used.
- The initial token check detected a concurrent `getMutableNativeFunction` to `getNativeFunction` rename outside the formatting work. Its new non-const getter called itself in its guard. The user approved preserving the rename and fixing that recursion by explicitly querying the const overload with `std::as_const`. Compiler target and test migrations were preserved; native mutable/const descriptor identity coverage and public embedding documentation were aligned.
- A lexical comparison against the captured pre-format worktree preserves all 21,251 original C++ tokens except the three local opcode-name occurrences, the preserved native-getter rename, and the approved const-overload delegation. Concrete style checks find no old opcode local name, old reference/pointer spacing, or trailing whitespace. No lookup ordering, registration policy, lifecycle, or VM instructions changed.
- R24 validation: the workspace build and existing `nomad_test_language`, `nomad_test_remaining`, and `nomad_module_runtime_integration` CTests pass on `windows-debug`. The module test initially timed out and then passed unchanged on its focused retry in 30.84 seconds; the timeout cause was not established. No full-suite rerun, manual gameplay validation, or performance measurements were performed; the last full 13-entry run remains R18's validation above.
- R25's result-shape revision was explicitly approved and is implemented. `getNativeMemberId(ownerType, name)` returns `std::optional<MemberReference>` and uses `std::nullopt` when the name is absent or no native member matches the owner. Successful results retain the same property/method IDs and first-matching-owner order; this query still does not select an overload or log lookup misses. MemberReference's variant, registry entries, enumeration, and overload vectors are unchanged.
- Compiler, parser, interpreter, registration predicates, and directly dependent C++ test callers explicitly handle or unwrap the optional result. Stored class/struct field fallback, script-method precedence, unknown-member diagnostics, and borrowed metadata ownership are retained. Existing exact API and metadata-copy tests verify the new return shape, absent names, null/wrong owners, rejected publication, valid ID identity, and no callback copies. Public embedding documentation describes the new shape; C++ consumers must migrate lookups and rebuild.
- R25's count-conversion claim is a false positive: `NomadIndex` is an alias of `std::size_t`, so vector sizes already have the declared return type. No casts, count representation, or capacity policy changes were made.
- R25 validation: the workspace build and existing `nomad_test_language`, `nomad_test_remaining`, `nomad_test_tooling`, `nomad_module_runtime_integration`, `nomad_package_integration`, and `nomad_distribution_integration` CTests pass on `windows-debug`. No full-suite rerun, manual gameplay validation, or performance measurements were performed; the last full 13-entry run remains R18's validation above.
- An approved R25 follow-up adds `CompositeType::getMethod(NomadStringView)` alongside property-name lookup. It returns a borrowed `const Method*` or nullptr, including script and unindexed native methods, with first-declaration order for overloaded names. Parser/expression method-existence checks, initializer lookup, and property collision validation reuse the owner query instead of repeating method enumeration.
- Follow-up validation: the workspace build and existing `nomad_test_language` and `nomad_test_remaining` CTests pass on `windows-debug`. Focused coverage verifies owner-catalog lookup, native/script methods, view slices, first-declaration identity after growth, initializer identity, property/constructor exclusion, and no callback execution. No full-suite rerun, manual gameplay validation, or performance measurements were performed.
- R28's first naming group was explicitly approved and is implemented. `getFunctionTableSize` is renamed to `getCallableCount`, counting all registered functions, methods, and constructors. The script-only `getFunctionCount`, `getFunctions`, and `getFunctionName` APIs are renamed to `getScriptFunctionCount`, `getScriptFunctions`, and `getScriptFunctionName`. Return types, category filters, ID identity, catalog order, vector replacement, borrowed mutable/const metadata views, and optional name failures are unchanged.
- Direct compiler, LSP, game/debugger, documentation, and test callers are migrated without compatibility aliases. Existing catalog/category/growth and const-enumeration tests exercise the new names; no new tests were needed for this naming-only change. Public embedding documentation describes the counts and script-only views. C++ consumers must use the new names and rebuild; script APIs and behavior are unchanged.
- R28 metadata naming validation: the workspace build and existing `nomad_test_language`, `nomad_test_remaining`, `nomad_test_tooling`, `nomad_lsp_process`, `nomad_module_runtime_integration`, `nomad_package_integration`, and `nomad_distribution_integration` CTests pass on `windows-debug`. No full-suite rerun, manual gameplay validation, or performance measurements were performed; the last full 13-entry run remains R18's validation above.
- R28's remaining function-query scope was explicitly approved and is implemented. The old Runtime `getCallableId(name)` is replaced by `getFunctionId(name)` for the first native/script free-function overload. The old script-only `getFunctionId(name)` is renamed to `getScriptFunctionId(name)`, alongside the retained native-only `getNativeFunctionId(name)`. `functionExists(name)` serves presence-only callers; `getFunctionIds(name, ids)` replaces the overload-list name, and `getFunctionOverloadCount(name)` names its count consistently.
- Name queries retain free-function category filtering, first-match registration order, unresolved-declaration visibility, lookup silence, and overload-vector replacement. Methods and constructors do not become global functions, even when their names match. Compiler/interpreter consumers that need signatures retain ID/descriptor lookup; namespace checks use the existence predicate. The any-category API remains `getCallable(id)`; no broad callable-by-name lookup was added.
- Direct Runtime callers are migrated without forwarding aliases, including script-only Game dispatch. Unrelated Game and AST getter names remain unchanged. Focused coverage verifies source-neutral versus native/script-only IDs, existence misses and member/constructor exclusion, first-overload identity, complete ordered overload lists, and coexistence with same-named methods in either registration order. Public embedding documentation describes the specialized queries; C++ consumers must migrate and rebuild.
- R28 function-query validation: the workspace build and existing `nomad_test_language`, `nomad_test_remaining`, `nomad_test_tooling`, `nomad_lsp_process`, `nomad_module_runtime_integration`, `nomad_package_integration`, and `nomad_distribution_integration` CTests pass on `windows-debug`. No full-suite rerun, manual gameplay validation, or performance measurements were performed; the last full 13-entry run remains R18's validation above.
- V14's remaining naming cleanup is implemented. The constructor parameter is `stackSize`; scalar operand readers use `integerValue` and `floatValue`; native-method dispatch uses `methodId`, and property access uses `propertyId`. The old accessor-asymmetry finding no longer applies: `getFunctionVariableValue` and `setFunctionVariableValue` already form a matching pair, so those APIs were retained.
- Only parameter/local identifiers changed in `VirtualMachine.hpp/cpp`. Function signatures, defaults, frame/storage operations, reference ownership, diagnostics, instruction dispatch, and compiler-trusted validation remain unchanged. No call-site migration, new tests, VM checks, or instructions were needed.
- V14 validation: the workspace build and existing `nomad_test_language` and `nomad_test_remaining` CTests pass on `windows-debug`, including scalar operands, native method/property calls, frame restoration, storage, and function-variable access. No full-suite rerun, manual gameplay validation, or performance measurements were performed; the last full 13-entry run remains R18's validation above.
- K5 is resolved by source/contract review with explicit approval. Existing `addFunctionCallInstruction` and `addMethodCallInstruction` share source selection where callers need it; `addScriptCallTarget` shares script link operands, and `addFreeArguments` shares argument/receiver cleanup. Scalar source-specialized wrappers retain their direct opcode/operand emission rather than adding descriptor lookups and another source branch solely to eliminate a few lines.
- Native function/method calls carry IDs; script calls link their start addresses and reserved-stack-slot counts. Object construction and struct/object destination calls retain their distinct receiver, hidden-storage, and cleanup rules. Modest duplication across those specialized emitters is intentional; no generalized call emitter, category flags, new helpers, opcodes, APIs, or VM checks were introduced.
- K5 validation was source review only. No C++ code, emitted bytecode, diagnostics, ownership, or execution behavior changed, so no build or test rerun was needed. The last targeted validation remains V14's build and language/runtime groups above; no new gameplay or performance validation was performed.
- K6's bounded cleanup was explicitly approved and is implemented. Both identifier APIs use a private keyword/statement query. `getIdentifierDefinition` no longer invokes a full classification pass before searching the same catalogs; successful exact context lookup explicitly records the ContextVariable category.
- The two broader paths remain intentionally specialized. Classification recognizes context prefixes without resolving a member; definition lookup retains its source-name checks and exact context-handle interning. Their existing precedence for coexisting catalog names is unchanged. Lexical receiver/capture handling remains before global lookup. No public API, lookup result, diagnostics, syntax restrictions, catalog publication, or VM behavior changed.
- K6 validation: the workspace build and existing `nomad_test_language` and `nomad_test_tooling` CTests pass on `windows-debug`, including context-prefix versus exact lookup, lexical receivers in callbacks/formats, source functions, constants, event/dynamic arguments, and compiler diagnostics. No new tests were needed for this behavior-preserving cleanup. No full-suite rerun, manual gameplay validation, or performance measurements were performed; the last full 13-entry run remains R18's validation above.
- Step 7 is complete. Next reviewable scope: step 8's lookup/registration performance baselines before selecting an indexed-lookup change. R26/R27, R23's remaining optimization, and R18 caching stay in that measured sweep; separately unresolved singleton-hub semantics remain pending.

### Step 8: performance sweep (remaining work deferred)

The user requested skipping performance work for now. Keep completed changes
and all measurements; do not start the approved grammar prototype or additional
benchmarking/optimization until this scope is explicitly resumed.
Event declaration ownership is the separately approved active non-performance
scope above. Hub declaration ownership and singleton-hub design remain
unfinished; their implementation is not authorized by the performance deferral.

If resumed, measure each change before and after on the same workload.

- Lookups: R1, R2, R13, R15, R26, R27 (name and interning indexes).
- Hot paths: V4 (current callable without scanning), V6 (native property accessors without copying), V16 (no allocation per host call), I11 (no scratch vector per write), R30 (no event copies), R23 (move instead of copy), R18 (cache well-known type IDs).
- Storage: C2.
- Format templates: I5, subject to Q2.

#### Callable-name lookup and registration baseline

- The user approved a small reusable benchmark instead of a session-only harness.
  [RuntimeLookupBenchmark.cpp](../../tests/RuntimeLookupBenchmark.cpp) builds as
  `nomad-lookup-benchmark`, linked only to `Nomad::Language`. It is excluded from
  normal builds and is not registered with CTest. The generated `ALL_BUILD` and
  `nomad-development` dependencies exclude it; the existing 13 tests are unchanged.
- Protocol: five fresh Runtime samples per catalog size, adding 100, 1,000, or
  5,000 uniquely named native free functions, each with one integer parameter.
  The initial catalog contains 14 callables. Registration timing excludes Runtime
  construction and prepared names/parameters. IDs must remain contiguous and
  catalog counts must match; registration/lookup must not invoke callbacks.
- Warm lookup timing checks the first, middle, last, and missing name. Each sample
  performs 25,000 first-name lookups and `2,000,000 / added-function-count` lookups
  for each other position: 20,000, 2,000, or 400. Names and expected IDs are
  prepared outside the timed loop; warmup IDs and timed checksums are verified.
- Measured on 2026-11-22 with Windows x64/MSVC, using Debug and Release in the
  existing `out/build/windows-debug` multi-configuration build. Generated project
  settings confirm disabled optimization/debug CRT for Debug and `/O2`/release CRT
  for Release in both the benchmark and language library. Both builds and checked
  benchmark executions pass. The build guide describes rerunning the same target.
- Values below are five-sample medians with `[min,max]` ranges. Lookup times are
  microseconds per call; registration times are total milliseconds. These are
  synthetic warm-name measurements on a shared machine, not whole-compilation,
  gameplay, or cross-platform performance results.

| Configuration | Added functions | Registration (ms) | First (us) | Middle (us) | Last (us) | Missing (us) |
| --- | --- | --- | --- | --- | --- | --- |
| Debug | 100 | 1.228 [0.890,2.084] | 0.222 [0.208,0.274] | 1.318 [1.235,1.443] | 2.311 [2.194,2.412] | 2.172 [2.043,2.282] |
| Debug | 1,000 | 38.525 [32.928,42.564] | 0.223 [0.198,0.277] | 11.016 [9.699,11.880] | 20.215 [20.117,22.581] | 19.972 [18.999,23.025] |
| Debug | 5,000 | 743.244 [711.783,812.132] | 0.246 [0.191,0.308] | 54.124 [49.567,61.258] | 113.387 [99.375,125.757] | 117.715 [97.594,123.617] |
| Release | 100 | 0.125 [0.119,0.158] | 0.034 [0.030,0.048] | 0.402 [0.377,0.428] | 0.766 [0.696,0.881] | 0.618 [0.504,0.763] |
| Release | 1,000 | 8.170 [7.764,10.445] | 0.035 [0.032,0.052] | 3.628 [3.372,4.236] | 7.470 [7.179,8.751] | 4.992 [4.916,7.034] |
| Release | 5,000 | 211.632 [175.185,218.708] | 0.048 [0.041,0.063] | 19.739 [14.537,23.781] | 36.604 [29.302,42.547] | 32.916 [24.765,40.445] |

- Increasing additions from 1,000 to 5,000 increases Release last-hit lookup cost
  about 4.9 times and registration time about 25.9 times. This supports prioritizing
  R1's free-function name index and measuring its effect on R15's registration cost.
  No production optimization, timing threshold, or unit-test timing assertion was
  introduced; no CTest rerun or manual gameplay validation was needed for this
  standalone benchmark addition.
#### Indexed free-function names: first R1/R15 scope

- The user approved using a private owned-name to ordered-CallableId index for
  both free-function lookups and same-name registration/overload checks. It
  follows the existing native-member index pattern. Function IDs, registration
  order, native/script filtering, pending-declaration visibility, diagnostics,
  and hub publication/ownership remain unchanged. Signatures are read live from
  descriptors, not cached in the index. Methods and constructors remain excluded.
- `getFunctionId`, `getFunctionIds`, `getFunctionOverloadCount`, and
  `resolveFunction` use the indexed bucket. Native registration and overload
  validation inspect only same-named functions rather than scanning unrelated
  callables. Registration stays open; no declaration seal or new VM checks were
  introduced. Method/constructor resolution is unchanged in this scope.
- Function publication prepares owner/table capacity before changing the index
  and keeps the candidate locally owned until index allocation succeeds.
  The final ownership/table commits cannot allocate. The former single-caller
  `catalogCallable` helper is folded into this publication path. Rejections do
  not publish name entries or consume callable IDs; actual allocation exceptions
  propagate. This ordering was source-reviewed, not tested by allocator fault
  injection or ID-space exhaustion.
- Existing catalog regressions now cover missing names queried before later
  registration, rejected callbacks/duplicates without ID or overload pollution,
  ordered overload appends after unrelated catalog growth, and script identity.
  Existing native/script/deferred signatures, same-named owner members, hub
  ownership, and source diagnostics remain covered.
- Validation: the indexed language library and the unchanged
  `nomad-lookup-benchmark` build and pass in Windows Debug and Release.
  `nomad_test_language`, `nomad_test_remaining`, `nomad_test_tooling`,
  `nomad_lsp_process`, `nomad_module_runtime_integration`,
  `nomad_package_integration`, and `nomad_distribution_integration` pass on
  `windows-debug`. Runtime's private layout changed, so C++ hosts/modules must
  rebuild. No public signature changed.
- The same 2026-11-22 workload, configuration flags, catalog sizes, warmup,
  query counts, and five-sample protocol were used. Indexed values below are
  medians with `[min,max]` ranges; units match the baseline above.

| Configuration | Added functions | Registration (ms) | First (us) | Middle (us) | Last (us) | Missing (us) |
| --- | --- | --- | --- | --- | --- | --- |
| Debug | 100 | 1.198 [0.849,1.538] | 0.263 [0.224,0.356] | 0.213 [0.199,0.309] | 0.243 [0.209,0.274] | 0.227 [0.200,0.262] |
| Debug | 1,000 | 10.879 [9.320,11.228] | 0.258 [0.242,0.338] | 0.212 [0.187,0.275] | 0.213 [0.195,0.252] | 0.162 [0.155,0.179] |
| Debug | 5,000 | 61.419 [53.225,68.867] | 0.282 [0.213,0.358] | 0.195 [0.187,0.492] | 0.215 [0.192,0.435] | 0.267 [0.155,0.365] |
| Release | 100 | 0.053 [0.044,0.080] | 0.024 [0.022,0.032] | 0.022 [0.020,0.030] | 0.022 [0.020,0.028] | 0.015 [0.014,0.017] |
| Release | 1,000 | 0.648 [0.567,0.703] | 0.030 [0.029,0.034] | 0.023 [0.022,0.028] | 0.023 [0.022,0.023] | 0.015 [0.015,0.016] |
| Release | 5,000 | 3.655 [3.417,4.498] | 0.022 [0.021,0.044] | 0.021 [0.020,0.036] | 0.021 [0.020,0.037] | 0.014 [0.014,0.024] |

- At 5,000 additions, Release last-hit lookup changes from 36.604 to 0.021 us,
  and native registration from 211.632 to 3.655 ms. Debug last-hit lookup changes
  from 113.387 to 0.215 us and registration from 743.244 to 61.419 ms. Lookup
  position/catalog size no longer produces the previous linear late-hit cost.
- Debug first-name medians are about 0.03-0.04 us higher than baseline, with
  overlapping observed ranges. This is not a claim that every case improved.
  The index adds storage whose memory cost was not measured. These remain
  shared-machine microbenchmarks, not whole-startup, compilation, gameplay, or
  cross-platform measurements. No full-suite rerun or manual gameplay validation
  was performed.
#### Owner-scoped method and constructor resolution baseline

- The function-name index is retained as-is after the user's review. The existing
  opt-in benchmark now appends an independent owner-resolution workload; its
  original free-function workload is unchanged. No engine API, production
  behavior, new index, or CTest entry was introduced.
- Every fresh Runtime has one native method with one integer parameter and two
  native constructor overloads (zero parameters and one integer parameter) on a
  fixed owner. A second registered owner has no members. Only unrelated native
  free functions vary: 0, 100, 1,000, and 5,000, producing total callable counts
  of 17, 117, 1,017, and 5,017. Population/setup is outside the timed section.
- Hit cases resolve the integer-parameter method or constructor. Miss cases use
  the empty owner, including a method name that exists on the other owner. Each
  case checks status, selected ID, candidate registration order, and argument
  types for 32 warmup queries, then verifies timed status/ID/candidate-count
  checksums. No callbacks or constructors execute.
- Five samples per size use `2,000,000 / max(unrelated-count, 100)` measured
  queries per case: 20,000, 20,000, 2,000, and 400. Timings include complete
  resolution, result allocation, overload matching, and result destruction.
  They are not directly equivalent to the raw ID lookup timings above.
- Debug and Release builds and checked executions pass on Windows x64/MSVC,
  using the same existing multi-configuration build and optimization settings.
  Values below are median `[min,max]` microseconds per resolution.

| Configuration | Unrelated functions | Method hit (us) | Method miss (us) | Constructor hit (us) | Constructor miss (us) |
| --- | --- | --- | --- | --- | --- |
| Debug | 0 | 3.365 [3.300,3.437] | 1.771 [1.702,1.779] | 3.617 [3.538,3.678] | 1.500 [1.401,1.564] |
| Debug | 100 | 5.026 [4.758,5.147] | 3.256 [2.986,3.301] | 3.622 [3.471,4.813] | 1.651 [1.626,2.208] |
| Debug | 1,000 | 20.796 [19.762,23.530] | 17.313 [16.527,18.796] | 7.567 [5.736,8.046] | 4.027 [3.512,4.494] |
| Debug | 5,000 | 86.032 [81.814,97.873] | 78.046 [72.133,92.900] | 15.818 [10.628,18.727] | 11.441 [10.922,15.190] |
| Release | 0 | 0.206 [0.180,0.225] | 0.082 [0.072,0.092] | 0.214 [0.199,0.232] | 0.068 [0.065,0.076] |
| Release | 100 | 0.289 [0.278,0.301] | 0.182 [0.179,0.193] | 0.278 [0.265,0.287] | 0.132 [0.129,0.133] |
| Release | 1,000 | 1.462 [1.417,1.505] | 1.339 [1.286,1.879] | 0.880 [0.839,1.142] | 0.729 [0.707,0.758] |
| Release | 5,000 | 6.305 [6.046,6.664] | 6.690 [6.118,10.859] | 3.437 [3.363,5.458] | 3.408 [3.325,3.522] |

- The owner member counts do not change, yet cost rises with unrelated callable
  count. This supports narrowing candidate collection through the existing
  composite-owned method/constructor catalogs, without adding a parallel index.
  It does not establish representative compilation/startup benefit or justify a
  new index. These are synthetic warm measurements on a shared machine; memory,
  whole-project compilation, large owner overload sets, gameplay, and other
  platforms remain unmeasured.
- No CTest rerun was needed for the standalone benchmark extension; discovery
  still lists the existing 13 entries. The build guide describes the added cases.
#### Owner-catalog candidate collection

- Method and constructor resolution now collects candidates through the existing
  `CompositeType::getMethods()` and `getConstructors()` snapshots instead of
  scanning unrelated callables. No new index, cache, public API, or function-index
  change was introduced. The shared overload-selection path remains unchanged.
- Owner catalogs can contain declarations not published in Runtime. Candidates
  must match the requested owner and their exact descriptor in Runtime's callable
  table, not merely a numeric ID. This retains foreign/unpublished declaration
  exclusion, native/script visibility, live signatures, candidate order, argument
  metadata, and failure results.
- A focused regression checks unpublished owner-local methods/constructors and
  foreign descriptors whose IDs collide with accepted callables. Existing
  constructor cases also verify ordered candidate IDs. The language,
  Runtime/catalog, tooling, and module-runtime CTests pass on `windows-debug`.
  Public embedding documentation clarifies the unchanged visibility contract.
- Debug and Release benchmark builds and checked runs pass with identical source
  and workloads. The executable's language DLL was hash-checked against the
  current build before each run. The owner section still uses one native method,
  two constructors, an empty owner, five samples, and the query counts above.
  Values below are median `[min,max]` microseconds per complete resolution.

| Configuration | Unrelated functions | Method hit (us) | Method miss (us) | Constructor hit (us) | Constructor miss (us) |
| --- | --- | --- | --- | --- | --- |
| Debug | 0 | 3.906 [3.875,4.402] | 1.813 [1.690,2.063] | 4.543 [4.089,4.715] | 1.737 [1.643,1.830] |
| Debug | 100 | 4.501 [4.006,8.454] | 1.912 [1.827,2.219] | 4.514 [4.344,5.156] | 1.838 [1.746,2.042] |
| Debug | 1,000 | 4.845 [3.907,6.467] | 2.184 [1.996,2.839] | 5.103 [3.646,5.365] | 1.649 [1.520,1.830] |
| Debug | 5,000 | 3.223 [3.116,6.196] | 1.531 [1.424,1.907] | 4.312 [3.469,5.595] | 1.772 [1.393,2.129] |
| Release | 0 | 0.209 [0.187,0.236] | 0.043 [0.038,0.074] | 0.279 [0.222,0.351] | 0.042 [0.038,0.066] |
| Release | 100 | 0.202 [0.174,0.326] | 0.043 [0.041,0.046] | 0.240 [0.215,0.276] | 0.043 [0.041,0.067] |
| Release | 1,000 | 0.183 [0.162,0.295] | 0.041 [0.037,0.067] | 0.216 [0.200,0.591] | 0.042 [0.037,0.105] |
| Release | 5,000 | 0.183 [0.163,0.289] | 0.043 [0.037,0.069] | 0.224 [0.196,0.400] | 0.043 [0.037,0.077] |

- At 5,000 unrelated functions, Release method hits change from 6.305 to 0.183 us
  and constructor hits from 3.437 to 0.224 us. Costs no longer rise with the
  unrelated callable count in this workload.
- Existing owner snapshots and identity checks add some small-catalog overhead:
  with no unrelated functions, Release constructor medians change from 0.214 to
  0.279 us, while method medians remain near 0.21 us; Debug hits also have higher
  medians in the smallest catalog. This is not a claim of improvement for every
  case. No additional borrowed-view API was introduced to optimize that overhead.
- These remain shared-machine synthetic measurements, not representative
  compilation/startup or gameplay measurements. Large owner overload sets,
  allocation/memory costs, and other platforms remain unmeasured. No full-suite
  rerun or manual gameplay validation was performed.
#### R2 string interning and name-lookup baseline

- The existing opt-in benchmark appends a string-only workload; the function and
  owner workloads are unchanged. Runtime storage, registration/lookup APIs, and
  compiler-only static-string lifetime rules are unchanged. No new index,
  timing assertion, or CTest entry was introduced.
- Five fresh Runtime samples register 100, 1,000, or 5,000 unique prepared texts
  (`benchmark.string.` followed by six digits). An empty-text primer is registered
  outside timing, producing total string counts of 101, 1,001, and 5,001. Unique
  registration timing includes string-object allocation and interning, but
  excludes Runtime construction, text preparation, and the primer.
- Lookups measure `getStringId` for first, middle, last, and absent text.
  Deduplication measures repeated `registerString` calls for the last interned
  text. Each sample uses 32 warmup calls, then 25,000 first-name lookups and
  `2,000,000 / unique-count` other lookups/deduplication calls: 20,000, 2,000, or
  400. Timed checksums verify the returned IDs.
- Checks verify contiguous IDs/text, stable interned object identity/reference
  counts, empty-text reuse, absent-name lookup, and the first unregistered ID.
  Debug live-string counts return to their initial value after Runtime cleanup;
  Release retains the identity/refcount checks but does not track live counts.
- Windows x64/MSVC Debug and Release benchmark builds and checked executions pass.
  DLL freshness was hash-checked before execution. Debug's first run had wide
  sample ranges, so the same configuration was repeated once without edits; both
  five-sample runs are retained below rather than selecting the fastest one.
  Values are median `[min,max]`; registration is total milliseconds and the
  remaining columns are microseconds per call.

| Configuration/run | Unique strings | Registration (ms) | Duplicate last (us) | First (us) | Middle (us) | Last (us) | Missing (us) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Debug 1 | 100 | 0.680 [0.514,0.705] | 17.630 [14.675,74.221] | 0.279 [0.167,2.441] | 9.360 [8.770,15.569] | 20.017 [17.517,30.682] | 18.874 [16.688,29.373] |
| Debug 1 | 1,000 | 88.597 [86.199,160.402] | 172.694 [147.923,434.245] | 0.552 [0.298,0.665] | 90.952 [75.979,146.007] | 174.802 [163.857,319.642] | 174.835 [132.137,309.908] |
| Debug 1 | 5,000 | 2723.241 [2288.885,4095.013] | 1186.531 [834.481,3225.640] | 0.322 [0.280,0.478] | 468.252 [432.937,1015.845] | 889.546 [822.824,5097.066] | 1177.648 [838.755,2612.044] |
| Debug 2 | 100 | 0.862 [0.695,1.179] | 17.954 [16.065,23.187] | 0.315 [0.281,1.141] | 8.449 [7.993,8.673] | 16.965 [15.853,17.841] | 16.404 [15.992,17.530] |
| Debug 2 | 1,000 | 87.937 [85.594,89.754] | 169.664 [158.245,198.133] | 0.322 [0.302,0.398] | 85.633 [79.504,89.748] | 164.507 [160.823,192.874] | 173.425 [166.179,217.252] |
| Debug 2 | 5,000 | 2139.084 [2110.546,2405.924] | 857.849 [830.056,946.166] | 0.296 [0.255,0.476] | 460.469 [410.125,843.759] | 919.499 [858.916,2439.262] | 858.294 [790.663,982.396] |
| Release 1 | 100 | 0.038 [0.032,0.047] | 0.569 [0.539,0.688] | 0.009 [0.008,0.009] | 0.351 [0.276,0.485] | 0.674 [0.560,0.900] | 0.410 [0.328,0.684] |
| Release 1 | 1,000 | 2.716 [2.599,3.295] | 5.521 [5.325,6.315] | 0.009 [0.008,0.014] | 4.177 [3.302,5.085] | 6.527 [5.375,7.102] | 3.401 [3.186,4.119] |
| Release 1 | 5,000 | 59.622 [55.546,62.495] | 23.728 [20.839,28.368] | 0.009 [0.008,0.009] | 11.359 [10.609,12.362] | 24.491 [21.384,33.175] | 18.012 [17.090,22.439] |

- Release unique registration rises from 2.716 to 59.622 ms at 1,000 to 5,000
  strings; last-name lookup rises from 6.527 to 24.491 us and repeated-last
  registration from 5.521 to 23.728 us. First-name lookups remain about 0.009 us.
  These distinguish late/missing lookup costs from already-cheap early hits.
- These are synthetic, warm, fixed-length text workloads on a shared machine.
  Actual compiler string counts, text-length distributions, memory overhead,
  whole-project compilation/startup, and other platforms remain unmeasured.
  The measurements do not by themselves justify a parallel index for every use.
  The benchmark/build guide and checked executable are persistent; CTest
  discovery still lists the unchanged 13 entries.
- String-index design was reviewed, including a compact sorted-ID array and
  borrowed immutable-text hash keys. Current in-repository production calls to
  `getStringId` come through compiler-side `registerString`; the
  `getStringByName` convenience path has only test/benchmark consumers. Compiled
  string opcodes use `getString(StringId)` directly, without text scans.
- The user approved deferring string indexing and prioritizing an execution-time
  lookup baseline. Linear string interning is retained without a new index,
  borrowed-key lifetime contract, or allocation-publication change. Revisit it
  only with representative compilation evidence; other R2 catalogs remain pending.

#### V4 active-callable lookup baseline

- The opt-in benchmark now appends a checked `VirtualMachine::getCurrentCallable`
  workload. This query currently scans Runtime's callable table by instruction
  range and is used during execution by local-storage access, closure creation,
  frame-backed interpretation, and formatting. No Runtime or VM behavior,
  execution-state field, cache, or index changed in this measurement step.
- Each fresh Runtime registers a native probe and 0, 100, 1,000, or 5,000
  unrelated native functions, then compiles a parameterless caller and callee.
  The probe runs in the caller, the nested callee, and the restored caller.
  Total callable counts are 18, 118, 1,018, and 5,018. Script initialization is
  performed through Runtime before execution; compiler-verified bytecode runs
  through the normal VM APIs, without fabricated frame or instruction state.
- Five samples per size perform 32 warmup pointer checks and
  `2,000,000 / max(unrelated-count, 100)` timed queries at each of the three
  probe sites: 20,000, 20,000, 2,000, and 400. Every timed query must return the
  exact expected descriptor. Compilation, initialization, native probe entry,
  and frame transitions are outside the timing interval.
- Debug and Release builds and checked executions pass on Windows x64/MSVC.
  The benchmark's language DLL was hash-checked against the current library.
  Earlier benchmark sections still pass unchanged. CTest discovery retains
  its 13 entries; no CTest rerun was needed for this standalone extension.
  Values below are five-sample median `[min,max]` microseconds per checked query.

| Configuration | Unrelated functions | Caller (us) | Nested callee (us) | Restored caller (us) |
| --- | --- | --- | --- | --- |
| Debug | 0 | 0.137 [0.053,0.285] | 0.043 [0.042,0.419] | 0.088 [0.049,0.325] |
| Debug | 100 | 0.252 [0.227,0.873] | 0.354 [0.174,0.433] | 0.349 [0.271,0.557] |
| Debug | 1,000 | 3.382 [1.326,8.035] | 2.260 [1.593,2.792] | 2.066 [1.435,4.134] |
| Debug | 5,000 | 9.319 [6.707,11.697] | 9.809 [8.184,16.258] | 12.147 [10.173,18.354] |
| Release | 0 | 0.046 [0.024,0.077] | 0.041 [0.023,0.059] | 0.034 [0.024,0.054] |
| Release | 100 | 0.176 [0.155,0.211] | 0.156 [0.146,0.158] | 0.159 [0.151,0.201] |
| Release | 1,000 | 1.282 [1.224,1.667] | 1.398 [1.224,3.659] | 1.401 [1.220,2.007] |
| Release | 5,000 | 6.970 [5.971,9.050] | 7.445 [6.633,9.560] | 7.022 [6.638,8.040] |

- Unrelated callable growth increases current-callable lookup cost in real
  active frames. These remain repeated warm-lookup microbenchmarks on a shared
  machine, not representative whole-call, closure, formatting, or gameplay
  measurements. The runs contain scheduling variability; all ranges are retained.
  No whole-project performance, memory, or cross-platform result is claimed.
- The frame-local design review below is complete with approval. The benchmark
  remains the unchanged pre-implementation baseline.

#### V4 approved frame-header design

- Source review confirmed that script calls previously encoded a start address and
  reserved local-slot count. The shared compiler call-target helper already
  knew the target `CallableId`; linking converted it into an address.
  Runtime's existing `getScriptCallable(CallableId)` resolves through the
  canonical callable table in constant time.
- The user approved storing the callee's `CallableId` in the existing script-frame
  header. Grow the header from four to five slots, keeping the saved receiver
  last so script-receiver access remains unchanged. The current callable is
  resolved from that header, not from a new VM member, parallel catalog, or
  pointer/range cache.
- Ordinary returns discard the callee-ID slot and restore the existing caller
  local-frame index. That index selects the enclosing frame's own ID. Native
  calls already preserve the local-frame index, and `ExecutionState` already
  captures/restores it; no new snapshot field or independent restoration state
  is needed. IDs do not own or retain callable descriptors.
- Reuse the existing specialized script-function and script-method opcodes,
  keeping two operands: `CallableId` followed by reserved local-slot count.
  Only the target operand changes meaning; no new opcode or instruction width
  is needed. Update compiler linking, operand metadata/dumping, direct call
  consumers, and frame-slot/transient-stack accounting together. Preserve
  compiler-owned target/compiled-body validation and existing diagnostics.
- The approved behavioral distinction is explicit: `getCurrentCallable()`
  identifies the active script frame, or null without one. Arbitrary changes
  to the instruction pointer alone no longer select another callable.
  Runtime's start-address and instruction-range navigation APIs retain their
  existing live-range, first-match, and boundary behavior.
- Keep registration and callable metadata open. Resolve frame IDs through the
  existing Runtime table without a declaration seal, range-invalidation
  mechanism, additional ownership, or compiler-specific instruction.
- The design review was source-only. Implementation and measured validation
  are recorded below.

#### V4 implementation and validation

- The approved five-slot header stores the callee ID immediately before the
  borrowed receiver. `getCurrentCallable()` reads it through the local-frame
  index and reuses Runtime's constant-time ID lookup. Return and raw-state
  restoration select the caller header without an extra VM/snapshot field.
  Native frames retain the enclosing local-frame index.
- Script-call linking now writes `CallableId` and reserved slot count. The
  existing callable-ID operand formatter handles function/method targets.
  C++ frame-entry APIs take already-resolved `ScriptCallableBase` references,
  avoiding a redundant lookup when the host already has the descriptor.
  Existing compiler target/compiled-body diagnostics remain unchanged.
- The shared frame-slot constant and return decoder account for five slots.
  Compiler peak-storage metadata still measures the caller's own operands and
  temporaries; nested frame/callee capacity is checked when entering that frame.
  No new transient-stack policy or early-rejection behavior was introduced.
  Exact-capacity tests now cover the additional slot, including prepared scalar
  rejection/success at six/seven total slots.
- The focused regression first failed seven assertions against instruction-based
  identity. It now verifies no-frame lookup, cross-callable jumps, native entry,
  mutable compiled ranges, nested return, closure execution, exception unwinding,
  and raw-state restoration. Runtime's instruction-range tests remain unchanged.
- All six selected Windows Debug CTests pass: `nomad_test_language`,
  `nomad_test_remaining`, `nomad_test_tooling`, `nomad_test_bindings`,
  `nomad_compiler_exports`, and `nomad_module_runtime_integration`.
  Existing constructor/initializer, receiver, capture, storage, allocation-free
  bookkeeping, and consuming-module regressions pass. One old scalar boundary
  expectation was corrected for the approved extra header slot; consuming
  project scripts were not changed.
- Debug and Release benchmark builds and checked runs pass with language DLL
  hashes matched to the current build. The earlier benchmark sections still pass.
  Fresh pre-change measurements were captured before editing VM/dispatch code.
  Active lookup uses the unchanged five-sample, three-probe workload and query
  counts above. Values are median `[min,max]` microseconds per checked query.

| Build | Phase | Unrelated functions | Caller (us) | Nested callee (us) | Restored caller (us) |
| --- | --- | --- | --- | --- | --- |
| Debug | Before | 0 | 0.059 [0.043,0.110] | 0.041 [0.039,0.096] | 0.070 [0.049,0.090] |
| Debug | After | 0 | 0.018 [0.016,0.041] | 0.018 [0.016,0.021] | 0.017 [0.015,0.027] |
| Debug | Before | 100 | 0.262 [0.217,0.331] | 0.313 [0.210,0.344] | 0.258 [0.212,0.430] |
| Debug | After | 100 | 0.032 [0.022,0.048] | 0.021 [0.016,0.048] | 0.022 [0.016,0.047] |
| Debug | Before | 1,000 | 3.379 [2.439,4.957] | 2.313 [1.415,3.644] | 2.153 [1.646,3.847] |
| Debug | After | 1,000 | 0.021 [0.018,0.056] | 0.021 [0.016,0.043] | 0.021 [0.016,0.042] |
| Debug | Before | 5,000 | 13.253 [10.365,24.787] | 14.296 [9.884,20.044] | 14.328 [11.286,18.942] |
| Debug | After | 5,000 | 0.033 [0.018,0.058] | 0.032 [0.017,0.053] | 0.031 [0.016,0.053] |
| Release | Before | 0 | 0.035 [0.025,0.063] | 0.025 [0.022,0.072] | 0.037 [0.024,0.060] |
| Release | After | 0 | 0.002 [0.002,0.002] | 0.002 [0.002,0.002] | 0.002 [0.002,0.002] |
| Release | Before | 100 | 0.165 [0.156,0.215] | 0.167 [0.159,0.255] | 0.222 [0.166,0.270] |
| Release | After | 100 | 0.002 [0.002,0.003] | 0.002 [0.002,0.006] | 0.002 [0.002,0.004] |
| Release | Before | 1,000 | 1.428 [1.308,1.861] | 1.420 [1.285,1.641] | 1.520 [1.203,1.735] |
| Release | After | 1,000 | 0.002 [0.002,0.004] | 0.002 [0.002,0.004] | 0.002 [0.002,0.004] |
| Release | Before | 5,000 | 11.119 [8.593,12.167] | 7.304 [6.158,11.053] | 7.789 [6.484,15.863] |
| Release | After | 5,000 | 0.002 [0.002,0.003] | 0.002 [0.002,0.003] | 0.002 [0.002,0.003] |

- A separate checked call benchmark was added before the VM change: five fresh
  Runtimes per size, 32 warmup invocations, then 50,000 invocations of a prepared
  scalar caller that calls one compiled leaf and returns 42. Timing includes
  two script frames, execution, return, and a result checksum, with no timed
  current-callable queries. Setup, initialization, and full state-restoration
  checks are outside timing. Totals are 17, 117, 1,017, and 5,017 callables.
  This measures complete call overhead, not isolated frame-entry instructions.

| Build | Unrelated functions | Before nested call (us) | After nested call (us) |
| --- | --- | --- | --- |
| Debug | 0 | 0.589 [0.422,0.674] | 0.539 [0.446,0.659] |
| Debug | 100 | 0.478 [0.445,0.559] | 0.516 [0.497,0.657] |
| Debug | 1,000 | 0.443 [0.404,0.645] | 0.582 [0.479,0.679] |
| Debug | 5,000 | 0.668 [0.501,0.746] | 0.626 [0.585,0.665] |
| Release | 0 | 0.093 [0.072,0.140] | 0.071 [0.063,0.106] |
| Release | 100 | 0.073 [0.062,0.084] | 0.074 [0.064,0.085] |
| Release | 1,000 | 0.068 [0.061,0.081] | 0.084 [0.073,0.100] |
| Release | 5,000 | 0.061 [0.057,0.088] | 0.078 [0.071,0.125] |

- Query cost no longer grows with unrelated registrations. No-query calls have
  extra header/ID-dispatch work: the Release medians at 1,000 and 5,000 additions
  rise by 0.016 and 0.017 us per complete two-frame invocation. Other medians
  and overlapping ranges show scheduling variation; not every workload is
  claimed faster. The extra header slot also reduces maximum nesting capacity
  for a fixed-size stack.
- These are warm microbenchmarks on a shared machine. Very cheap post-change
  queries use short timing batches and rounded nanosecond-level values, so those
  figures are approximate. No timing threshold, whole-game speedup, heap-memory
  estimate, allocator fault injection, or cross-platform result is claimed.
  The full 13-entry suite was not rerun, and no manual gameplay validation was done.
- V4 is complete. The following V6 baseline measures native-property metadata
  copying before proposing a borrowed-access change. Other step-8 catalogs,
  registration-copy work, and singleton-hub decisions remain separate.

#### V6 native-property copy/access baseline

- The opt-in benchmark appends a native-property workload without changing
  Runtime, NativeProperty, VM behavior, or public signatures. The existing
  `getNativeProperty(PropertyId)` borrows a descriptor; `getNativePropertyInfo`
  returns a snapshot copying the property name, documentation, and both
  callbacks. Current VM getter and setter access each obtain that full snapshot.
- Three controlled cases register one resolved integer property on a fresh
  owner. `small_capture` uses 16-byte getter/setter objects and five-byte name
  and documentation. `large_capture` keeps that text but captures 32 integers
  plus a benchmark-state reference in each 264-byte callback object.
  `long_metadata` keeps the small callbacks with a 128-byte name and
  256-byte documentation. Callback object sizes are reported, not inferred
  from a presumed standard-library inline-storage threshold.
- Five fresh Runtimes per case perform 32 warmups and 50,000 timed operations
  for each metric: borrowed descriptor lookup, copied metadata retrieval,
  complete VM getter access, and complete VM setter access. No accessor is
  invoked during either metadata-only metric. Getter results are 42; setter
  inputs vary over 64 integers and every resulting side effect is checked.
  Both callback shapes perform the same scalar operation and observe the
  bound receiver while counting visits.
- Warmups verify full metadata contents, owner/value type and IDs, callback
  presence, and absence of method-backed IDs. Timed checks cover descriptor
  identity, metadata checksums, getter checksums, and individual writes.
  Setup, callback/receiver lifetime management, and final state checks are
  outside timing. Final checks verify exact callback counts, borrowed receiver
  identity/reference count, all execution-state fields, result/intermediate
  registers, no script frame, lookup misses, and receiver-object cleanup.
- Windows Debug and Release builds and all checked executions pass. Bundled
  language DLL hashes match the current configuration's build. All earlier
  benchmark sections still pass. The existing 13 CTest entries are unchanged
  and were not rerun for this standalone benchmark-only extension.
- Release was repeated once unchanged because the long-metadata measurements
  varied substantially, including a metadata-only median above complete getter
  access in the first run. Both runs and all ranges are retained, rather than
  selecting the faster samples. Values are five-sample median `[min,max]`
  microseconds per checked operation.

| Build/run | Case | Descriptor (us) | Metadata copy (us) | VM getter (us) | VM setter (us) |
| --- | --- | --- | --- | --- | --- |
| Debug 1 | Small capture | 0.014 [0.013,0.039] | 2.472 [1.991,2.577] | 2.913 [2.874,3.204] | 3.221 [2.464,3.488] |
| Debug 1 | Large capture | 0.023 [0.012,0.029] | 2.895 [2.729,3.205] | 3.630 [2.976,3.758] | 3.754 [3.090,4.207] |
| Debug 1 | Long metadata | 0.016 [0.013,0.036] | 3.462 [3.189,4.176] | 3.633 [3.485,3.817] | 3.648 [3.403,3.941] |
| Release 1 | Small capture | 0.001 [0.001,0.001] | 0.036 [0.031,0.044] | 0.070 [0.059,0.085] | 0.064 [0.062,0.080] |
| Release 1 | Large capture | 0.001 [0.001,0.002] | 0.095 [0.089,0.107] | 0.123 [0.109,0.180] | 0.125 [0.109,0.183] |
| Release 1 | Long metadata | 0.003 [0.001,0.005] | 0.146 [0.094,0.205] | 0.137 [0.126,0.199] | 0.182 [0.127,0.243] |
| Release 2 | Small capture | 0.001 [0.001,0.004] | 0.039 [0.032,0.041] | 0.058 [0.054,0.066] | 0.067 [0.061,0.083] |
| Release 2 | Large capture | 0.002 [0.001,0.003] | 0.128 [0.084,0.140] | 0.129 [0.116,0.158] | 0.132 [0.111,0.166] |
| Release 2 | Long metadata | 0.001 [0.001,0.002] | 0.101 [0.089,0.131] | 0.143 [0.117,0.158] | 0.132 [0.116,0.198] |

- Metadata retrieval is measurably more expensive than descriptor lookup in
  every case. Release copy medians are 0.036/0.039 us for small captures,
  0.095/0.128 us for large captures, and 0.146/0.101 us for long metadata.
  These identify callback and text copying as worthwhile execution-path
  design candidates, not a measured speedup from an unimplemented alternative.
- These are synthetic warm scalar workloads on a shared machine. Timings include
  their respective result checks, and full access also includes receiver
  observation/callback counters. Descriptor timings are rounded at nanosecond
  scale. No allocation counts, heap-memory estimates, timing thresholds,
  whole-game speedup, or cross-platform results are claimed.
  Reference-valued properties, unresolved value types, method-backed properties,
  exception paths, and stateful value-captured callbacks are not timed.
- The descriptor-execution design review below is complete with approval.
  These checked measurements remain the pre-implementation baseline.

#### V6 approved descriptor-execution design

- The user approved invoking each callback owned by the registered
  NativeProperty descriptor instead of copying the full metadata snapshot for
  execution. Introduce narrow read-only capability/method-ID queries and
  specialized getter/setter invocation methods. Keep raw callbacks private;
  no friend, cache, index, duplicated metadata record, or new opcode is needed.
- VM dispatch retains its existing checks and receiver-restoration path.
  Preserve error precedence: invalid/method-backed properties and missing
  accessors are rejected before binding the receiver; a missing receiver is
  rejected before unresolved-type invocation. NativeProperty retains the
  deferred-type guard at callback invocation. Existing callback exceptions
  propagate without replacing diagnostics or rolling back their side effects.
- The approved semantic change is explicit: mutable value-captured state belongs
  to each registered getter or setter and persists across invocations, including
  throws. All receivers and VMs using that property invoke the same callback
  instance; this is not per-object or per-VM storage. Getter and setter state
  are not automatically shared with each other. Reentrant calls use that same
  instance, and callback state synchronization remains the host's responsibility,
  as with other native callables.
- Executing the registered callback also removes execution-time callback-copy
  constructors and their possible allocation failures or copy side effects.
  Registration and explicit snapshot retrieval keep their existing copying
  contracts. State restoration still restores borrowed VM receivers, not state
  mutated inside a callback.
- Property descriptors are separately allocated and owned by the declaring
  composite's `unique_ptr` collection, so catalog growth does not move them.
  The Runtime/owner must outlive invocation, including reentry. Use this existing
  lifetime contract without retaining extra ownership, exposing raw callback
  references, or sealing open registration.
- Preserve `NativeProperty::getInfo()` and
  `Runtime::getNativePropertyInfo()` for metadata consumers. Resolved snapshots
  still copy their callbacks and text; their explicit callback invocation remains
  snapshot-based. Existing unresolved snapshots retain descriptor-backed guarded
  callbacks that can be invoked after successful type completion. Do not change
  that deferred-resolution contract while optimizing VM execution.
- Adjacent interpreter dispatch currently copies full snapshots merely to inspect
  method-backed accessor IDs. Replace those execution-time checks with the
  descriptor's narrow queries, including the corresponding expression-evaluation
  checks, while preserving method routing and read-only/write-only diagnostics.
  Leave compiler resolution, language-server, documentation, and other metadata
  consumers on their existing APIs unless directly required by this change.
- The design review was source-only. Its implementation, focused regressions,
  and measured validation are recorded below. Snapshot-copy timings remain a
  separate control, not a target for this execution-path change.

#### V6 descriptor execution implementation and measurements

- NativeProperty now exposes `getGetterMethodId`, `getSetterMethodId`,
  `hasGetterCallback`, and `hasSetterCallback` without constructing metadata.
  Its const `getValue`/`setValue` methods invoke the stored raw callback through
  the existing deferred-type guard. Raw callback objects remain private.
  Callers supply receiver binding and value ownership.
- VM getter/setter dispatch uses the registered descriptor while retaining
  its method-backed/accessor checks and receiver-restoration helper. Adjacent
  InterpreterObjectReceiver routing and two expression-evaluation capability
  checks use method IDs directly. Compiler resolution, language-server,
  documentation generation, Runtime lookup signatures, and opcodes are unchanged.
- Resolved metadata snapshots still copy callbacks and text. Existing unresolved
  snapshot wrappers reuse the guarded invocation methods and retain their
  descriptor-backed behavior after successful completion. No new friend,
  callback-reference API, cache/index, duplicate metadata storage, declaration
  seal, or reference-ownership policy was introduced.
- Two focused existing-file regressions first reproduced 13 failed assertions
  under copied-per-access execution. They now verify persistent mutable captures
  across receivers, VMs and object interpretation; independent explicit snapshot
  state; same-instance reentry while 64 new properties are registered; unchanged
  descriptor identity; state retained after a callback throws; and complete
  borrowed receiver/execution-state restoration. The deferred-property regression
  additionally checks that missing-receiver errors precede unresolved invocation.
- All three selected Windows Debug CTests pass: `nomad_test_language`,
  `nomad_test_remaining`, and `nomad_compiler_exports`. Existing method-backed,
  read-only/write-only, deferred snapshot, reference-result/setter ownership,
  and native receiver regressions remain green.
- Debug and Release benchmark builds and checked runs pass, with bundled
  language DLL hashes matched to their current builds. The benchmark source and
  property protocol are unchanged: five samples, 32 warmups, 50,000 operations,
  and the same three capture/text cases. All preceding benchmark sections pass.
  One unchanged Release repeat characterizes noisy access ranges; both runs
  are retained. Values are median `[min,max]` microseconds per checked operation.

| Build/run | Case | Descriptor (us) | Metadata copy (us) | VM getter (us) | VM setter (us) |
| --- | --- | --- | --- | --- | --- |
| Debug 1 | Small capture | 0.014 [0.013,0.017] | 1.961 [1.761,2.106] | 0.293 [0.244,0.344] | 0.325 [0.253,0.351] |
| Debug 1 | Large capture | 0.016 [0.014,0.029] | 2.245 [2.201,2.908] | 0.256 [0.236,0.326] | 0.275 [0.262,0.292] |
| Debug 1 | Long metadata | 0.016 [0.015,0.033] | 2.326 [2.194,2.569] | 0.273 [0.260,0.298] | 0.248 [0.235,0.310] |
| Release 1 | Small capture | 0.001 [0.001,0.004] | 0.037 [0.035,0.050] | 0.033 [0.032,0.040] | 0.039 [0.035,0.047] |
| Release 1 | Large capture | 0.001 [0.001,0.002] | 0.110 [0.099,0.126] | 0.051 [0.029,0.084] | 0.042 [0.029,0.052] |
| Release 1 | Long metadata | 0.001 [0.001,0.003] | 0.112 [0.082,0.132] | 0.031 [0.026,0.072] | 0.036 [0.030,0.066] |
| Release 2 | Small capture | 0.001 [0.001,0.002] | 0.034 [0.031,0.042] | 0.031 [0.029,0.052] | 0.035 [0.028,0.048] |
| Release 2 | Large capture | 0.001 [0.001,0.002] | 0.098 [0.079,0.141] | 0.049 [0.029,0.063] | 0.047 [0.035,0.081] |
| Release 2 | Long metadata | 0.001 [0.001,0.001] | 0.086 [0.080,0.106] | 0.036 [0.029,0.039] | 0.036 [0.032,0.051] |

- Against baseline Release run 1, getter/setter medians change from
  0.070/0.064 to 0.033/0.039 us for small captures, 0.123/0.125 to
  0.051/0.042 us for large captures, and 0.137/0.182 to 0.031/0.036 us
  for long metadata. The large-capture repeat changes baseline
  0.129/0.132 to 0.049/0.047 us. Debug getter medians change from
  2.913/3.630/3.633 to 0.293/0.256/0.273 us across the three cases.
- Explicit snapshot retrieval still pays its copying cost; its timing variation
  is not attributed to an optimized metadata API. Full-access measurements
  include result checks, receiver observation and callback counters. These
  remain warm scalar microbenchmarks on a shared machine, not whole-interpreter,
  whole-game, or cross-platform performance evidence. No allocation-count or
  heap-memory claim, timing threshold, full 13-entry rerun, or manual gameplay
  validation is claimed.
- V6 is complete. V16's host-call/capture consumers and allocation/latency
  baselines are recorded below; no argument-storage change has been made.

#### V16 host-call allocation and latency baseline

- Production `executeFunction(CallableId, arguments)` reaches the raw
  ScriptFunction runner, and Runtime initialization calls that runner directly.
  Production game/entity/scene/event callbacks use Closure objects through
  `executeFunction(Closure*, arguments)`, which reaches the Closure runner and
  InterpreterCallFrame. The explicit third raw capture-vector parameter has
  a test consumer in InterpreterTest; no production caller supplying that
  parameter was found in the repository. It was not removed in this step.
- The raw runner creates a local argument-type vector and reserves parameter
  count for cleanup. The Closure path validates explicit/capture widths, then
  prepares an InterpreterCallFrame with its own argument metadata. These are
  distinct paths; prepared execution is a separate control, not a replacement
  for owned argument preparation or host validation.
- The existing opt-in benchmark now appends nine checked cases, with five fresh
  Runtimes, 32 warmup calls, and 50,000 timed calls per case. Compilation,
  initialization, argument vectors, and source-created scalar/string closure
  preparation are outside timing. Cases return integer 42 and time the full
  invocation plus result checksum:
  - `prepared_empty`: prepared ScriptFunction execution, without host arguments.
  - `raw_empty`, `raw_integer`, and `raw_four_integers`: raw script calls with
    zero, one, or four explicit integers.
  - `raw_two_strings`: two prepared string arguments, returning their total
    length without creating a result string.
  - `raw_capture_vector`: one explicit integer plus borrowed scalar/string
    captures from the prepared Closure, passed through the raw vector API.
  - `closure_empty`, `closure_integer`, and `closure_captured`: Closure runner
    calls without captures, with one integer, or with one integer plus owned
    scalar/string captures. Raw and Closure capture cases execute the same body.
- An untimed pass separately counts allocation/reallocation requests over
  1,000 calls using the existing MSVC Debug CRT-hook approach. A direct
  single-allocation CRT control verifies the probe; the previous hook is restored
  on return or throw and no hook is installed during latency timing.
  Counts are current-thread requests, not bytes or peak/live heap usage.
  Release and non-MSVC builds explicitly report counts as unavailable.
- Checks verify results, the enclosing instruction/stack/parameter/local/running
  state and borrowed receiver, a preexisting stack marker, argument/receiver
  references, capture references, no active script frame after return, and final
  receiver/argument/capture object cleanup. Raw APIs do not promise to preserve
  result/intermediate registers, so no additional register contract is imposed.
- Debug and Release builds and checked executions pass with bundled language
  DLL hashes matched to the current builds. All preceding benchmark sections
  still pass. The first Debug run revealed unexpectedly large Closure costs;
  after adding the subpath probes below, the unchanged timed workloads were
  run again. Both Debug runs are retained. The existing 13 CTest entries remain
  unchanged and were not rerun for this standalone benchmark extension.
  Values are five-sample median `[min,max]` microseconds per invocation.

| Case | Debug 1 (us) | Debug 2 (us) | Release (us) | Debug requests/call |
| --- | --- | --- | --- | --- |
| Prepared empty | 0.422 [0.404,0.464] | 0.416 [0.402,0.459] | 0.056 [0.053,0.063] | 0 |
| Raw empty | 1.520 [1.463,1.652] | 1.464 [1.450,1.552] | 0.077 [0.074,0.098] | 2 |
| Raw integer | 2.383 [2.267,2.421] | 2.250 [2.191,2.439] | 0.147 [0.129,0.153] | 3 |
| Raw four integers | 3.154 [3.045,3.210] | 3.104 [3.047,3.131] | 0.265 [0.260,0.291] | 3 |
| Raw two strings | 9.422 [9.160,9.548] | 9.111 [8.950,9.249] | 0.738 [0.663,0.763] | 7 |
| Raw capture vector | 5.440 [5.369,5.984] | 5.274 [5.164,5.416] | 0.446 [0.397,0.465] | 4 |
| Closure empty | 2.074 [2.004,2.128] | 2.003 [1.908,2.125] | 0.155 [0.139,0.193] | 2 |
| Closure integer | 62.618 [61.860,62.714] | 63.299 [62.724,64.329] | 1.316 [1.225,1.333] | 93 |
| Closure captured | 187.167 [184.759,190.941] | 187.432 [187.245,188.982] | 3.786 [3.613,3.913] | 275 |

- Each Debug allocation value above has identical minimum/median/maximum across
  all five samples in both runs. These include Debug STL instrumentation,
  default empty capture-vector temporaries, validation, body execution, and
  cleanup; they must not be interpreted as Release allocation counts or as
  allocations exclusively from the argument-type reserve.
- Four untimed Debug subpath probes also use five samples of 1,000 operations:

| Probe | Allocation requests/operation, median [min,max] |
| --- | --- |
| Integer parameter classification | 45 [45,45] |
| Integer callable storage layout | 45 [45,45] |
| Integer Closure-frame preparation, without execution | 48 [48,48] |
| Raw empty call with an explicitly prepared empty capture vector | 1 [1,1] |

- Source tracing explains the repeated work: `Parameter::getStackValueCount`
  calls `Runtime::getParameterShape`, which delegates to TypeManager. For an
  ordinary integer, classification performs five built-in name lookups;
  `getTypeId` scans descriptors and obtains type names by value.
  The Closure runner checks parameter width, then frame preparation calculates
  call layout through the same classifier again. For the scalar case, the
  measured 45 classifier requests plus 48 frame-preparation requests account
  for the complete 93-request call. Allocation call stacks were not captured.
- In the measured Release build, the scalar Closure call is 1.316 us versus
  0.147 us for raw integer invocation; the shared captured body costs 3.786 us
  through Closure versus 0.446 us through raw capture vectors. These compare
  complete paths, including different validation/ownership preparation, not
  isolated vector or classifier instruction timings.
- Only benchmark code, its private linkage to the existing Boost scope-exit
  dependency, the build guide, and this plan changed. VM/Interpreter/TypeManager
  behavior, APIs, guards, ownership, and registration remain unchanged.
  These are warm microbenchmarks on a shared Windows/MSVC machine, not a
  whole-game result. No Release allocation count, timing threshold, memory-byte
  estimate, exception/fault-injection benchmark, or cross-platform result is
  claimed.
- The parameter-width design review below is complete with approval. These
  checked measurements remain the pre-implementation baseline. Raw argument
  bookkeeping changes stay separate.

#### V16 approved specialized parameter-width design

- This records the initial optional-width design. The later fixed-contribution
  revision below supersedes its return contract while retaining the measured
  allocation-free separation from parser classification.
- The user selected a specialized per-type width query instead of caching five
  canonical parser descriptors in TypeManager. Only event-callback and
  event-dispatch parameters have special stack widths; source arguments,
  ordinary callback values, structs, and other values use their value size.
  Execution layout does not need the parser's complete parameter classification.
- The approved virtual type-metadata query is
  `Type::getParameterStackValueCount()`, returning
  `std::optional<NomadIndex>`. Its default returns the live `getSize()` result.
  EventCallbackType supplies two slots for its canonical registered descriptor;
  EventDispatchType reports no fixed width for its canonical descriptor.
  Keep the specialized behavior in those two existing final classes, rather
  than adding a type-category enum, generalized flags, or compiler special cases.
- `Parameter::getStackValueCount()` retains the unresolved-null check and its
  existing error. It delegates fixed-width lookup to the type and, for an absent
  width, raises the unchanged call-site-dependent event-dispatch error including
  the parameter name. Callable offset, storage-size, and layout calculations
  consume that existing Parameter API without dropping their overflow checks
  or host-input validation.
- Preserve canonical identity, not merely matching display names, dynamic type,
  or numeric IDs. The two event-type overrides can check publication through the
  owning Runtime's existing constant-time ID lookup and exact pointer identity.
  Runtime constructs the canonical final event descriptors before public use;
  unregistered copies and colliding IDs must retain their former ordinary-width
  behavior. A registered descriptor from another Runtime is interpreted through
  its own owning Runtime, as the existing Parameter path already does.
- TypeManager's parser-oriented `getParameterShape()` and Runtime's forwarding
  API remain unchanged, including source/event/callback precedence, null and
  foreign-descriptor behavior, and standalone catalog behavior. This is not a
  claimed optimization of every classifier consumer: the direct integer-shape
  benchmark is intentionally an unchanged control.
- The query reads live size/registration metadata on each use. No cached
  callable layout, per-type shape field, name index, parallel descriptor cache,
  declaration seal, extra ownership, or registration-order requirement is added.
  TypeManager still does not call Runtime; any event-descriptor identity check
  belongs to the event type implementation. No public registration, VM, or
  interpreter signature needs removal.
- The design review was source-only. Implementation and checked measurements
  are recorded below; cleanup vectors remain unchanged.

#### V16 live-width implementation and validation

- Type now exposes `getParameterStackValueCount()` with the approved optional
  live-width contract. Only EventCallbackType and EventDispatchType override it,
  using the owning Runtime's ID lookup and exact descriptor identity.
  Parameter delegates to the query while retaining its original unresolved-type
  and named dynamic-width exceptions. Callable layout/overflow checks, host
  Closure validation, parser classification, and VM behavior remain unchanged.
- A focused existing-file regression covers both Runtime owners, unpublished
  event descriptors with colliding IDs, reserved holes, invalid IDs, and event
  descriptors in a separate TypeManager catalog. Existing width checks now
  verify the exact missing/dynamic-width messages. Existing empty/struct,
  ordinary/callback/source/event, layout-overflow, owned argument/capture,
  Closure validation, reentry, and catalog-growth coverage remains green.
- Four selected Windows Debug CTests pass: `nomad_test_language`,
  `nomad_test_remaining`, `nomad_compiler_exports`, and
  `nomad_module_runtime_integration`. The changed virtual Type interface and
  dependent consumers were rebuilt through the workspace testing integration.
  Public embedding documentation describes fixed zero versus absent width,
  event identity behavior, and the C++ rebuild requirement.
- The unchanged benchmark builds and passes checked Debug/Release runs with
  matching language DLL hashes, including every preceding section. The same
  nine host cases retain five samples, 32 warmups, 50,000 timing calls, and a
  separate 1,000-call Debug allocation pass. Release was repeated once unchanged
  because untouched controls also exhibited timing drift; both runs are retained.
  Values are median `[min,max]` microseconds per invocation.

| Case | After Debug (us) | After Release 1 (us) | After Release 2 (us) | Debug requests/call |
| --- | --- | --- | --- | --- |
| Prepared empty | 0.438 [0.398,0.462] | 0.033 [0.030,0.070] | 0.032 [0.028,0.044] | 0 |
| Raw empty | 1.494 [1.386,1.547] | 0.058 [0.040,0.098] | 0.063 [0.042,0.128] | 2 |
| Raw integer | 2.189 [2.096,2.275] | 0.092 [0.083,0.128] | 0.130 [0.074,0.178] | 3 |
| Raw four integers | 2.976 [2.943,3.108] | 0.161 [0.139,0.200] | 0.244 [0.133,0.272] | 3 |
| Raw two strings | 8.893 [8.514,9.365] | 0.424 [0.350,0.483] | 0.434 [0.383,0.564] | 7 |
| Raw capture vector | 4.986 [4.703,5.080] | 0.252 [0.223,0.302] | 0.242 [0.221,0.369] | 4 |
| Closure empty | 2.011 [1.911,2.093] | 0.119 [0.089,0.124] | 0.086 [0.078,0.158] | 2 |
| Closure integer | 3.042 [2.950,3.388] | 0.157 [0.126,0.245] | 0.142 [0.126,0.267] | 3 |
| Closure captured | 7.310 [6.905,7.590] | 0.393 [0.336,0.451] | 0.427 [0.299,0.618] | 5 |

- Debug allocation values have identical minimum/median/maximum across all
  five samples. The untimed subpath probes show the intended separation:

| Probe | Before Debug requests/operation | After median [min,max] |
| --- | --- | --- |
| Integer parser parameter classification | 45 | 45 [45,45] |
| Integer callable storage layout | 45 | 0 [0,0] |
| Integer Closure-frame preparation | 48 | 3 [3,3] |
| Raw empty call with explicit empty capture vector | 1 | 1 [1,1] |

- Against Debug baseline run 2, scalar Closure calls change from 63.299 to
  3.042 us and from 93 to 3 allocation requests; captured Closure calls change
  from 187.432 to 7.310 us and from 275 to 5 requests. The unchanged classifier
  still makes 45 requests, while integer storage-layout lookup makes zero.
  No new cached metadata, index, shape field, category enum, declaration seal,
  or TypeManager-to-Runtime call was introduced.
- Release scalar Closure medians change from 1.316 to 0.157/0.142 us; captured
  Closure medians change from 3.786 to 0.393/0.427 us. These are observations
  from the same checked workload, not isolated virtual-call timings.
  Untouched raw/prepared controls also vary between runs, so their changes are
  not attributed to this implementation. Release allocations are unmeasured;
  Debug counts include STL instrumentation and are not Release heap estimates.
- This completes the measured width-query sub-step, not all V16 bookkeeping
  work. No full 13-entry suite rerun, manual gameplay, allocation fault injection,
  timing threshold, or cross-platform result is claimed.
- Next reviewable scope: review the remaining raw argument-type cleanup vector,
  default empty capture-vector construction, and Closure-frame argument metadata
  separately. Preserve partial-copy cleanup, borrowed enclosing state,
  explicit/capture ordering, and result ownership before choosing a storage
  change. The recorded post-width measurements provide the new baseline;
  do not optimize those vectors automatically in this step.

#### V16 fixed contributions and concrete call-site widths

- The user approved simplifying `Type::getParameterStackValueCount()` to return
  a plain `NomadIndex`. It reports one parameter value's fixed contribution,
  including zero for empty storage. Event callbacks retain their canonical
  two-slot override; event dispatch uses the default one event-ID slot.
  The concrete call site counts expanded payload arguments separately.
- InterpreterCallFrame now totals actual typed argument storage and capture
  spans before calling the existing `getCallStorageLayout(argumentSlotCount)`
  overload. Compiler ArgumentList already counts expanded argument nodes and
  now checks total-slot overflow. No new opcode, layout cache, argument buffer,
  or ownership discriminator was added.
- Signature-only Parameter, callable total-size/layout, and later-offset queries
  still reject event-dispatch widths they cannot determine. A nonallocating
  concrete event-type and exact-publication check retains those diagnostics
  without parser name scans or another public type-category API. Raw host calls
  validate complete scalar/reference storage and reject unexpanded dispatch
  or event-callback pairs before executing a body. Their existing partial-copy
  cleanup and enclosing-state restoration remain unchanged.
- A new raw-host regression first reproduced six failures under the old
  acceptance path: both expanded event kinds ran despite incomplete inputs.
  It now verifies rejection before execution, unchanged result/state/stack
  marker, and release of a reference argument copied earlier in reverse order.
  Additional focused coverage counts event ID plus struct, empty, and scalar
  payloads before hidden result-pointer placement and rejects max-plus-one
  call-site overflow. Event identity and signature-only diagnostic tests remain.
- Four selected Windows Debug CTests pass: `nomad_test_language`,
  `nomad_test_remaining`, `nomad_compiler_exports`, and
  `nomad_module_runtime_integration`. Existing typed/Closure capture, empty and
  struct argument, event parsing, ownership, partial-copy, reentry, and module
  consumers pass after rebuilding the changed C++ interface.
- The unchanged benchmark builds and passes checked Debug and Release runs
  with bundled library hashes matched to the current configuration, including
  all preceding workloads. Five-sample median `[min,max]` timings below use
  the existing host protocol; allocation counts remain Debug-only.

| Case | Debug (us) | Release (us) | Debug requests/call |
| --- | --- | --- | --- |
| Prepared empty | 0.270 [0.234,1.241] | 0.035 [0.034,0.037] | 0 |
| Raw empty | 0.961 [0.863,3.242] | 0.047 [0.044,0.059] | 2 |
| Raw integer | 1.491 [1.322,3.415] | 0.107 [0.105,0.119] | 3 |
| Raw four integers | 1.998 [1.898,3.495] | 0.245 [0.228,0.290] | 3 |
| Raw two strings | 6.707 [6.353,28.430] | 0.459 [0.435,0.504] | 7 |
| Raw capture vector | 3.801 [3.501,8.818] | 0.312 [0.307,0.339] | 4 |
| Closure empty | 1.317 [1.245,4.880] | 0.103 [0.088,0.136] | 2 |
| Closure integer | 1.835 [1.717,4.316] | 0.149 [0.143,0.164] | 3 |
| Closure captured | 5.877 [4.819,8.126] | 0.427 [0.388,0.466] | 5 |

- Debug counts are unchanged from the optional-width sub-step: integer
  signature layout makes zero allocation requests; integer Closure-frame
  preparation makes three; the parser classifier control remains at 45; and
  raw empty calls with explicit empty captures make one. Each count has identical
  minimum/median/maximum across five samples.
- Release scalar Closure timing remains within the preceding 0.142/0.157 us
  measurements, and captured Closure timing is 0.427 us. Debug samples show
  substantial scheduling variability, including untouched controls. This is an
  API/contract simplification with preserved allocation gains, not a claimed new
  speedup. No Release allocation count, full-suite rerun, manual gameplay,
  cross-platform result, or timing threshold is claimed.
- This follow-up is complete. The raw-runner bookkeeping design below is
  approved; partial-copy cleanup and raw/typed ownership contracts remain intact.

#### V16 approved raw-runner bookkeeping design

- The user approved optimizing only the raw ScriptFunction runner next.
  Retain the per-invocation cleanup records rather than reconstructing them
  after execution: they record the exact types and partial-copy progress for
  the stack values owned by that invocation.
- Replace the local argument-type `std::vector` with
  `boost::container::small_vector<const Type*, 4>`. Four inline pointer entries
  cover every measured raw argument/capture count. Larger calls retain heap
  fallback; no argument limit or silent fallback is added.
  The inline payload is four pointers, with container bookkeeping in addition;
  exact native-stack/frame overhead remains unmeasured.
- Boost is already the pinned dependency and C++20 has no equivalent standard
  small vector. Declare Container explicitly in the existing Boost selection
  and target linkage, without a new dependency manager or version change.
  Keep this local snapshot independent for every invocation and nested reentry,
  not a VM member, shared buffer, cache, index, or global mutable state.
- Change only the optional raw capture parameter from a vector reference to
  `std::span<const RuntimeValue>` with an empty default. The explicit argument
  vector stays unchanged. The span borrows capture storage for the synchronous
  call; captures are still copied/retained onto the VM stack under their types.
  Existing vector-based capture callers can supply a view, while capture
  initializer-list callers must prepare a vector or array. Update the known
  test caller and document the C++ signature/rebuild requirement without a shim.
- Preserve argument-count and expanded-input validation, reverse capture/argument
  copy order, stack preflight, saved enclosing state, result ownership, and
  caller-owned input lifetime. Reserve fallback capacity before copying values.
  Record the cleanup type before `copyValue`, retaining cleanup of a cleared
  destination if that copy throws. Resource side effects are not rolled back.
- The measured Debug raw empty/one-integer/four-integer paths make 2/3/3
  allocation requests. The raw empty call with an explicitly prepared capture
  vector makes one request, identifying default-vector construction separately.
  These are Debug CRT requests, not measured Release allocation counts; no
  speedup or total allocation elimination is claimed before implementation.
- Closure InterpreterCallFrame argument metadata and result storage remain
  unchanged. Their sizes, aggregate/capture support, snapshots, and allocation
  costs require a separate review after this bounded raw-runner change.
- The design review was source-only. Its bounded implementation and checked
  measurements are recorded below.

#### V16 raw inline cleanup implementation and measurements

- Raw ScriptFunction invocation now keeps cleanup type records in a local
  `small_vector<const Type*, 4>` and borrows optional captures through a span.
  Explicit argument vectors, reverse capture/argument copying, reserve-before-copy,
  record-before-copy, and normal/exception cleanup remain intact. The count
  validation uses guarded subtraction to avoid overflowing a sum of input sizes.
  No VM member, shared pool, cache, index, or argument cap was introduced.
- Container is explicit in the existing pinned Boost component selection and
  public language linkage, matching its use in the VM header. The known capture
  initializer-list caller now prepares an array. Vector capture callers remain
  compatible through span views; C++ consumers must rebuild for the signature
  change. Closure-frame argument and result containers are unchanged.
- Before production edits, the benchmark added one eight-integer workload to
  measure heap fallback, retaining the existing ten-case timing/check protocol.
  An untimed size report was added afterward without changing timed operations.
- The allocation regression first reproduced four failures at zero, one, four,
  and eight arguments. It now verifies zero allocation requests for small raw
  calls and one per eight-argument fallback call. A second focused case retains
  reference arguments plus captures through nested fallback calls, copy failure
  in the outer and inner invocation, and body failure. It verifies exact reference
  counts, stack markers, caller state, and borrowed receiver restoration.
- Five selected Windows Debug CTests pass: `nomad_test_language`,
  `nomad_test_remaining`, `nomad_compiler_exports`,
  `nomad_module_runtime_integration`, and `nomad_package_integration`.
  Existing expanded-input rejection, ownership, typed/Closure, and reentry tests
  remain green. Package consumers were rebuilt and executed with the dependency
  and C++ signature changes.
- Checked Debug and Release benchmark builds/runs pass before and after, with
  bundled language DLL hashes matched to their current builds. All preceding
  workloads pass. Values are five-sample median `[min,max]` microseconds per
  invocation. The separate Debug allocation pass uses 1,000 calls per sample.

| Case | Debug before (us) | Debug after (us) | Release before (us) | Release after (us) | Debug requests before/after |
| --- | --- | --- | --- | --- | --- |
| Prepared empty | 0.402 [0.280,0.421] | 0.264 [0.247,0.347] | 0.031 [0.029,0.045] | 0.049 [0.029,0.066] | 0 / 0 |
| Raw empty | 1.341 [0.848,2.984] | 0.559 [0.492,0.574] | 0.048 [0.046,0.060] | 0.046 [0.041,0.087] | 2 / 0 |
| Raw integer | 2.026 [1.281,3.506] | 0.677 [0.621,0.826] | 0.107 [0.098,0.115] | 0.076 [0.070,0.094] | 3 / 0 |
| Raw four integers | 2.512 [2.183,8.320] | 1.328 [1.272,1.350] | 0.223 [0.213,0.225] | 0.193 [0.185,0.243] | 3 / 0 |
| Raw two strings | 15.319 [6.916,37.525] | 4.910 [4.801,5.226] | 0.468 [0.418,0.549] | 0.413 [0.356,0.587] | 7 / 4 |
| Raw capture vector | 5.041 [3.886,7.685] | 3.139 [2.832,4.571] | 0.302 [0.299,0.335] | 0.273 [0.252,0.296] | 4 / 2 |
| Closure empty | 1.423 [1.234,2.501] | 1.265 [1.191,1.599] | 0.084 [0.082,0.100] | 0.083 [0.079,0.095] | 2 / 2 |
| Closure integer | 2.222 [1.774,4.554] | 2.035 [1.687,2.135] | 0.149 [0.137,0.176] | 0.143 [0.135,0.149] | 3 / 3 |
| Closure captured | 5.397 [4.756,11.688] | 4.698 [4.340,4.983] | 0.393 [0.379,0.452] | 0.385 [0.355,0.417] | 5 / 5 |
| Raw eight integers | 4.325 [3.566,10.686] | 2.570 [2.404,2.890] | 0.407 [0.394,0.480] | 0.406 [0.379,0.426] | 3 / 1 |

- Each Debug request count has identical minimum/median/maximum across all five
  samples. Raw empty calls with explicitly prepared empty captures change from
  one request to zero. Parser classification, integer signature layout, and
  scalar Closure-frame preparation remain 45, zero, and three requests.
  The legacy `raw_capture_vector` label remains because input storage is still
  prepared as a vector; invocation now borrows it through a span.
- The cleanup container's measured object size is 56 bytes in both configurations,
  versus 32-byte Debug and 24-byte Release vectors: 24/32 additional bytes in
  that local object. The capture span is 16 bytes. These are object sizes,
  not complete compiler-generated native-stack frame measurements; VM stack
  layout and script-frame slot count are unchanged.
- Release raw integer and four-integer medians change from 0.107/0.223 to
  0.076/0.193 us; the eight-argument fallback remains about 0.407/0.406 us.
  Untouched controls and the shared-machine Debug runs vary, so no gain is
  attributed to Closure or prepared execution. Reference/string bodies still
  incur other allocations; this is not a claim that every raw call is allocation
  free. Release allocation counts, full-suite rerun, allocation fault injection,
  manual gameplay, and cross-platform performance remain unmeasured.
- The raw-only sub-step is complete. The Closure-frame storage review below
  approves only a fixed-size owned result array; argument metadata remains
  separate.

#### V16 approved Closure-frame result-buffer design

- Source review confirms InterpreterCallFrame sizes its result vector once,
  before publishing hidden result/constructor receiver pointers. It never grows
  that buffer afterward. Scalar/reference results use the VM result register;
  empty struct and void results expose no storage.
- The user selected `result-buffer-only`: replace the result vector with
  `std::unique_ptr<RuntimeValue[]>`. Allocate and clear exactly the required
  slots for a nonempty struct result, using `std::make_unique<RuntimeValue[]>`.
  Leave the pointer empty for scalar, reference, void, and empty struct results.
  InterpreterContext already uses this owned-array mechanism for typed storage.
- Result addresses must remain stable from publication through frame destruction.
  Keep the existing noncopyable/nonmovable frame, borrowed `getResult()` API,
  hidden-pointer placement, and independent owned snapshots returned by
  `getResultValue()`. Do not reuse the VM argument stack, share result buffers,
  move ownership into RuntimeValue, or change raw/typed result semantics.
- Array ownership releases only the allocation. Preserve explicit Type-driven
  cleanup of completed or partial struct results before deleting the array,
  argument cleanup, and restoration of saved result/intermediate registers.
  Keep constructor-failure and nested-frame lifetime behavior unchanged.
  Buffer allocation must still occur before pointers are published or VM
  argument storage/registers are modified.
- Leave `m_arguments` and its type/offset/size snapshots as `std::vector`.
  These records are three machine words each; four inline records would add
  96 bytes of payload on x64, unlike the raw runner's four pointer entries.
  Existing measurements do not establish an optimal inline capacity for this
  larger record. No argument-metadata capacity or native-stack budget is
  approved in this result-buffer-only step.
- The existing Debug Closure empty/integer/captured paths make 2/3/5 allocation
  requests, and scalar Closure-frame preparation makes three. Those are complete
  measured paths, not counts attributed solely to result storage. Release
  allocation counts remain unavailable. This design does not eliminate the
  allocation required by a nonempty aggregate result, nor does it claim a
  Release speedup before measurement.
- No new dependency, result-size cache, inline result threshold, ownership flag,
  declaration seal, or public signature change is needed. The frame's C++ object
  layout changes, so dependent consumers must rebuild. Raw-runner containers and
  the fixed-contribution/call-site width contracts remain unchanged.
- The design review was source-only. The result-buffer implementation and
  checked measurements are recorded below; argument metadata remains unchanged.

#### V16 owned result-array implementation and validation

- InterpreterCallFrame now owns a `std::unique_ptr<RuntimeValue[]>` instead of
  a result vector. It allocates cleared storage only for nonempty struct results,
  before VM argument storage/register mutation or hidden-pointer publication.
  Scalar/reference results still use the VM result register; empty and void
  results remain null. `getResult()` keeps its writable borrowed-pointer contract.
- Explicit Type-driven cleanup still releases completed or partial struct
  results before the array allocation is deleted. Saved registers, owned
  snapshots, constructor receiver pointers, argument cleanup, and nonmovable
  nested-frame lifetimes are unchanged. No argument-metadata container,
  raw-runner, call-layout, opcode, ownership flag, cache, or dependency changed.
- A six-shape allocation regression first reproduced six old result-vector
  overhead failures. It now verifies one Debug request per no-buffer frame
  and two per nonempty aggregate frame, retaining the unchanged argument-vector
  request. A wide owned-result regression checks published pointers across
  nesting, partially populated string results during exception unwinding,
  snapshots surviving both frames, complete state/register restoration, and
  final object cleanup.
- Four selected Windows Debug CTests pass after rebuilding frame consumers:
  `nomad_test_language`, `nomad_test_remaining`, `nomad_compiler_exports`, and
  `nomad_module_runtime_integration`. Existing scalar/reference/empty results,
  struct constructors, partial-copy failures, captures, and reentry remain green.
- Before changing production storage, the opt-in benchmark added direct
  parameterless frame-result preparation for void, integer, string, empty struct,
  two-slot struct, and eight-slot struct results. Five fresh Runtimes per case
  use 32 warmups, 50,000 timed calls, and a separate 1,000-call Debug CRT probe.
  Timing includes preparation, cleared-storage checks, writes, destruction,
  and a saved-result checksum; no script body or owned snapshot copy is timed.
- The benchmark verifies hidden pointers, cleared complete storage, repeated
  result-pointer identity, restored execution state/registers/stack marker,
  and final object counts. Debug/Release builds and checked before/after runs
  pass with current bundled-library hashes, including all earlier workloads.
  Release was repeated once unchanged because timings were mixed; both after
  runs and all ranges are retained. Values are median `[min,max]` microseconds.

| Result | Debug before (us) | Debug after (us) | Debug requests before/after |
| --- | --- | --- | --- |
| Void | 1.110 [0.935,1.462] | 0.631 [0.605,0.834] | 2 / 1 |
| Integer | 0.995 [0.927,1.403] | 0.705 [0.576,0.898] | 2 / 1 |
| String | 1.006 [0.863,1.176] | 0.686 [0.577,0.881] | 2 / 1 |
| Empty struct | 1.148 [0.925,1.301] | 0.820 [0.707,0.853] | 2 / 1 |
| Two-slot struct | 1.629 [1.571,1.903] | 1.306 [1.261,1.854] | 3 / 2 |
| Eight-slot struct | 1.953 [1.720,2.325] | 1.223 [1.131,1.511] | 3 / 2 |

| Result | Release before (us) | Release after 1 (us) | Release after 2 (us) |
| --- | --- | --- | --- |
| Void | 0.038 [0.034,0.047] | 0.038 [0.034,0.039] | 0.047 [0.036,0.059] |
| Integer | 0.047 [0.041,0.058] | 0.054 [0.050,0.075] | 0.058 [0.041,0.157] |
| String | 0.046 [0.044,0.052] | 0.058 [0.043,0.090] | 0.060 [0.045,0.077] |
| Empty struct | 0.056 [0.052,0.067] | 0.058 [0.051,0.067] | 0.070 [0.049,0.083] |
| Two-slot struct | 0.115 [0.114,0.145] | 0.108 [0.096,0.119] | 0.117 [0.107,0.128] |
| Eight-slot struct | 0.149 [0.136,0.169] | 0.142 [0.136,0.151] | 0.160 [0.147,0.183] |

- Each Debug allocation value has identical minimum/median/maximum across five
  samples. Complete Closure empty/integer/captured calls change from 2/3/5
  to 1/2/4 requests; scalar Closure-frame preparation changes from three to two.
  Parser classification remains 45, integer signature layout remains zero,
  and raw-call counts are unchanged.

| Closure call | Debug before (us) | Debug after (us) | Release before (us) | Release after 1 (us) | Release after 2 (us) |
| --- | --- | --- | --- | --- | --- |
| Empty | 1.788 [1.177,2.193] | 1.277 [1.169,1.431] | 0.100 [0.089,0.128] | 0.112 [0.093,0.182] | 0.089 [0.079,0.121] |
| Integer | 2.413 [1.950,2.723] | 2.125 [1.745,2.170] | 0.170 [0.155,0.179] | 0.170 [0.151,0.257] | 0.163 [0.144,0.169] |
| Captured | 5.877 [5.681,7.230] | 5.052 [4.857,5.566] | 0.448 [0.444,0.478] | 0.542 [0.389,0.674] | 0.470 [0.406,0.534] |

- The frame object's measured size changes from 120 to 96 bytes in Debug and
  from 104 to 88 in Release. The array owner is eight bytes versus the prior
  32/24-byte result vector. These are object sizes, not full native-stack frames.
  Nonempty struct storage still requires an allocation; this step removes
  unused container state and its measured Debug request, not aggregate storage.
- Release timings show both increases and decreases, including integer direct
  frame medians rising from 0.047 to 0.054/0.058 us. No universal Release speedup
  or Release allocation reduction is claimed. These remain checked warm
  microbenchmarks on a shared machine, without timing thresholds, full-suite
  rerun, manual gameplay, allocator fault injection, or cross-platform results.
- The result-buffer-only step is complete. The separate argument snapshot
  design below is approved; the raw inline-four capacity was not carried over.

#### V16 approved inline-one argument snapshot design

- The user selected `inline-one-with-fallback`: replace InterpreterCallFrame's
  argument metadata vector with
  `boost::container::small_vector<ArgumentStorage, 1>`. Keep each invocation's
  type/offset/size cleanup snapshots, existing copy/release algorithm, and
  independent nested-frame ownership.
- One is a targeted budget for the measured zero/one-record paths, not a proven
  optimal capacity or a claim about production argument-count distributions.
  Each record has three machine words, or 24 bytes of payload on the measured
  x64 platform. Measure the complete frame-object cost and latency before
  accepting the implementation; do not equate payload with full native-stack
  usage or extrapolate the raw runner's four-pointer budget.
- Capacity counts logical explicit arguments plus captures, including zero-sized
  values. A multi-slot struct argument still uses one metadata record; its value
  remains in the existing VM argument storage. Larger record lists retain heap
  fallback with no argument/capture limit. Do not drop empty-value records or
  change concrete call-site slot counts as part of this storage change.
- Reserve the callable's record capacity before mutating VM argument storage
  or registers, as today. Record type/offset/size before calling `Type::copyValue`,
  including the cleared destination of a failed copy, and advance the value-slot
  offset only after success. Preserve cleanup order, complete struct/reference
  release, hidden pointers, saved registers, and restoration during exceptions
  and reentry. Do not reconstruct cleanup types from live metadata afterward.
- The existing container already tracks initialized records, avoiding a separate
  copied-record counter. A fixed owned metadata array was considered but not
  selected: it retains a heap allocation for every nonempty list and needs
  explicit initialized-record progress for partial-copy cleanup.
- Keep the owned result array and raw-runner bookkeeping unchanged. Reuse the
  existing public Boost.Container dependency; no new dependency, result buffer,
  shared pool, cache/index, ownership flag, declaration seal, or public signature
  change is needed. The C++ frame object layout changes, so consumers must rebuild.
- Current Debug Closure empty/integer/captured paths make 1/2/4 allocation
  requests, and scalar Closure-frame preparation makes two. The captured case
  has three records and therefore exercises fallback under the selected budget.
  These baseline counts include other work and Debug instrumentation; measure
  the new paths rather than promising every Closure call will be allocation-free.
  Release allocation counts remain unavailable.
- The design review was source-only. Implementation and checked measurements
  are recorded below.

#### V16 inline-one argument snapshot implementation and validation

- The only production change is InterpreterCallFrame's argument-record container:
  `small_vector<ArgumentStorage, 1>` replaces the vector. Copy, reserve, and
  release algorithms are unchanged, including recording type/offset/size before
  copying a value. The owned result array, raw-runner storage, concrete call-site
  widths, public signatures, and existing Boost dependency remain unchanged.
- A focused allocation regression covers zero/one/two logical arguments with
  integer, empty-struct, and two-slot struct values. It proves one multi-slot
  struct stays inline, while two zero-slot values still spill. Updated
  result-frame allocation expectations and this boundary test first reproduced
  15 old allocation failures; all now pass. Existing scalar/aggregate/reference
  capture partial-copy failures, fallback cleanup, reentry, and stable result
  pointer/snapshot tests remain green.
- Four selected Windows Debug CTests pass after rebuilding frame consumers:
  `nomad_test_language`, `nomad_test_remaining`, `nomad_compiler_exports`, and
  `nomad_module_runtime_integration`. No argument count, initialization, or
  ownership restriction was added.
- The benchmark added two/eight-integer Closure cases before production edits,
  capturing fresh Debug/Release spill baselines. Existing host/frame workloads
  and checks remain unchanged afterward: five samples, 32 warmups, 50,000
  timing calls, and separate 1,000-call Debug allocation passes. Builds and
  checked executions pass with current library hashes, including all earlier
  workloads. One unchanged Release repeat characterizes mixed timing samples;
  all median `[min,max]` microsecond ranges are retained.

| Closure case | Debug before (us) | Debug after (us) | Debug requests before/after |
| --- | --- | --- | --- |
| Empty | 1.411 [1.211,1.626] | 0.793 [0.770,0.919] | 1 / 0 |
| Integer | 2.272 [2.089,2.935] | 1.041 [0.996,1.234] | 2 / 0 |
| Captured | 6.125 [5.992,6.774] | 5.082 [4.662,5.670] | 4 / 3 |
| Two integers | 2.577 [2.267,3.281] | 2.181 [1.893,2.624] | 2 / 1 |
| Eight integers | 4.579 [4.198,5.311] | 3.759 [3.634,4.242] | 2 / 1 |

| Closure case | Release before (us) | Release after 1 (us) | Release after 2 (us) |
| --- | --- | --- | --- |
| Empty | 0.101 [0.087,0.129] | 0.105 [0.082,0.144] | 0.109 [0.079,0.149] |
| Integer | 0.174 [0.168,0.219] | 0.129 [0.115,0.166] | 0.141 [0.106,0.198] |
| Captured | 0.512 [0.425,0.647] | 0.493 [0.465,0.567] | 0.476 [0.406,0.606] |
| Two integers | 0.208 [0.199,0.241] | 0.245 [0.213,0.265] | 0.211 [0.199,0.285] |
| Eight integers | 0.595 [0.526,0.668] | 0.549 [0.540,0.700] | 0.584 [0.474,0.706] |

- Debug allocation counts have identical minimum/median/maximum across all
  samples. Scalar Closure-frame preparation drops from two requests to zero.
  Parameter classification remains 45, integer signature layout remains zero,
  and unrelated raw-call allocation controls are unchanged. Captured and
  multi-argument bodies still allocate elsewhere or spill; no universal
  allocation-free host-call claim is made.

| Direct result frame | Debug before (us) | Debug after (us) | Debug requests before/after |
| --- | --- | --- | --- |
| Void | 0.713 [0.645,0.998] | 0.414 [0.301,0.548] | 1 / 0 |
| Integer | 0.682 [0.631,0.919] | 0.342 [0.334,0.474] | 1 / 0 |
| String | 0.806 [0.648,0.894] | 0.367 [0.314,0.490] | 1 / 0 |
| Empty struct | 0.810 [0.747,0.939] | 0.393 [0.364,0.507] | 1 / 0 |
| Two-slot struct | 1.412 [1.127,1.629] | 0.877 [0.726,1.000] | 2 / 1 |
| Eight-slot struct | 1.512 [1.358,1.679] | 1.015 [0.787,1.034] | 2 / 1 |

| Direct result frame | Release before (us) | Release after 1 (us) | Release after 2 (us) |
| --- | --- | --- | --- |
| Void | 0.037 [0.036,0.044] | 0.048 [0.040,0.061] | 0.061 [0.034,0.061] |
| Integer | 0.063 [0.042,0.083] | 0.055 [0.044,0.085] | 0.058 [0.042,0.060] |
| String | 0.055 [0.043,0.074] | 0.053 [0.039,0.078] | 0.050 [0.042,0.060] |
| Empty struct | 0.070 [0.048,0.119] | 0.063 [0.049,0.065] | 0.055 [0.050,0.069] |
| Two-slot struct | 0.121 [0.115,0.150] | 0.115 [0.099,0.124] | 0.098 [0.095,0.142] |
| Eight-slot struct | 0.181 [0.158,0.196] | 0.168 [0.140,0.175] | 0.145 [0.124,0.148] |

- The frame object grows from 96 to 112 bytes in Debug and from 88 to 112 in
  Release, a 16/24-byte object-size cost. These are not compiler-generated native
  stack-frame measurements. One is a measured-path budget, not a tuned optimum;
  production record-count distributions and other platforms remain unprofiled.
- Release scalar Closure medians improve from 0.174 to 0.129/0.141 us, but
  empty calls and two-record spills have mixed results, including 0.208 to
  0.245/0.211 us for two integers. Untouched controls also vary. No universal
  latency improvement, Release allocation count, timing threshold, full-suite
  rerun, manual gameplay, or allocator fault injection is claimed.
- This completes the approved V16 host-bookkeeping scope with larger-call heap
  fallback intentionally retained. I11's per-field assignment scratch baselines
  are recorded below; other step-8 findings remain separate.

#### I11 assignment scratch baseline

- This step extends only the opt-in benchmark and related documentation.
  Interpreter/Type assignment code, public signatures, receiver validation,
  selector resolution, scratch ownership, and exception behavior are unchanged.
- Five value shapes use zero, one, one, two, and eight slots: empty struct,
  integer, string reference, integer/string pair, and seven integers plus a
  string reference. Their required scratch sizes are zero/zero/zero/two/eight.
  Each fresh Runtime registers nested stored-property owners with sentinel
  fields immediately before and after the selected value at both levels.
- Five samples per case perform 32 warmups and 50,000 timed operations for:
  scratch-vector construction/destruction; Type assignment with scratch prepared
  outside timing; the public property-ID setter; and the public nested-path
  setter. Input snapshots, source values, receiver initialization, and reusable
  scratch allocation are outside timing. Scalar/empty prepared assignment passes
  null scratch and uses Type's existing local-storage path.
- Prepared and public assignments alternate two distinct values. Each operation
  checks scalar contents or reference identity plus four receiver bounds markers.
  After timing, true source/destination aliasing through Type assignment and a
  public self-snapshot assignment are verified. Final checks cover cleared
  prepared scratch, exact source/destination reference counts, complete enclosing
  VM state/registers/stack marker, and receiver/source object cleanup.
- Allocation/reallocation requests use the existing separate 1,000-operation
  MSVC Debug CRT pass, not the timing loop. Release counts remain unavailable.
  Debug/Release builds and checked runs pass with current bundled-library
  hashes, including all preceding benchmark sections.
- Existing `nomad_test_remaining` passes on Windows Debug, including
  `failed_copying_before_commit_preserves_the_destination`, borrowed setter
  validation, shared references, and aliasing regressions. No new production
  behavior or unit-test implementation was introduced by this measurement step.
  Values below are five-sample median `[min,max]` microseconds per operation.

| Case | Debug construction (us) | Debug prepared (us) | Debug ID setter (us) | Debug nested setter (us) | Debug requests construction/prepared/ID/path |
| --- | --- | --- | --- | --- | --- |
| Empty | 0.394 [0.349,0.474] | 0.087 [0.064,0.133] | 1.492 [1.490,1.698] | 3.248 [3.126,4.100] | 1 / 0 / 1 / 1 |
| Integer | 0.396 [0.361,0.478] | 0.208 [0.128,0.227] | 1.750 [1.496,2.112] | 3.490 [3.187,4.291] | 1 / 0 / 1 / 1 |
| String | 0.377 [0.314,0.440] | 0.197 [0.168,0.261] | 1.841 [1.486,2.467] | 4.032 [3.782,4.134] | 1 / 0 / 1 / 1 |
| Pair | 0.700 [0.641,0.800] | 0.337 [0.292,0.430] | 2.283 [1.847,2.453] | 4.376 [3.384,4.451] | 2 / 0 / 2 / 2 |
| Wide | 0.826 [0.714,0.838] | 0.582 [0.533,0.685] | 2.732 [2.638,2.904] | 4.704 [4.479,4.877] | 2 / 0 / 2 / 2 |

| Case | Release construction (us) | Release prepared (us) | Release ID setter (us) | Release nested setter (us) |
| --- | --- | --- | --- | --- |
| Empty | 0.010 [0.009,0.013] | 0.026 [0.018,0.048] | 0.064 [0.050,0.086] | 0.114 [0.091,0.150] |
| Integer | 0.010 [0.009,0.020] | 0.035 [0.032,0.048] | 0.063 [0.055,0.083] | 0.118 [0.105,0.140] |
| String | 0.010 [0.009,0.016] | 0.038 [0.031,0.063] | 0.068 [0.061,0.108] | 0.115 [0.100,0.173] |
| Pair | 0.045 [0.035,0.064] | 0.078 [0.060,0.101] | 0.159 [0.142,0.180] | 0.195 [0.185,0.242] |
| Wide | 0.047 [0.045,0.070] | 0.153 [0.130,0.204] | 0.245 [0.225,0.258] | 0.312 [0.299,0.374] |

- Debug counts have identical minimum/median/maximum across all five samples.
  Prepared assignment makes zero requests in these fixtures. Public ID/path
  setters match scratch-construction requests: one for empty/scalar/reference
  cases and two for multi-slot cases. Empty-container Debug instrumentation is
  distinct from allocation of storage for multi-slot scratch.
- Public setter timings include selection and validation, while prepared Type
  assignment does not. All metrics include their stated checks and the saved
  result checksum; construction does not copy or inspect the selected field.
  These timings must not be subtracted as an exact decomposition of allocator
  versus selector/copy cost. Very cheap Release empty-vector work can also be
  optimized away. No Release allocation count or optimization speedup is claimed.
- These are synthetic warm workloads on a shared Windows/MSVC machine, not
  representative field-size distributions, whole-script/gameplay performance,
  cross-platform results, or an allocator-failure benchmark. No timing threshold
  or full-suite rerun is claimed.
- The conditional owned-array scratch design below is approved. These checked
  measurements remain the pre-implementation baseline.

#### I11 approved conditional owned-array scratch design

- The user selected `conditional-owned-array`: replace only the local scratch
  vector in the shared `assignStoredProperty` path with
  `std::unique_ptr<RuntimeValue[]>`. Read `getAssignmentScratchSize()` and allocate
  cleared slots with `std::make_unique<RuntimeValue[]>` only when that count is
  nonzero. Pass the owned array's pointer to the existing `Type::assignValue`.
- Empty, scalar, and reference fields require zero external scratch slots.
  Leave the pointer null and use Type's existing zero/single-slot behavior.
  Multi-slot fields still receive a fresh, disjoint, fully cleared array for
  each synchronous assignment. There is no inline threshold or size limit,
  shared scratch buffer, cache, pool, extra ownership flag, or new API.
- This reuses the owned-array mechanism already present in InterpreterContext
  parameter and frame-variable writes. Do not extract a generalized scratch
  abstraction or change other scratch consumers, argument/result containers,
  Type metadata, VM behavior, or public signatures in this bounded step.
- Preserve the public setters' receiver-size validation, selector resolution,
  and exact-type validation before allocating scratch. Allocation still precedes
  assignment; allocation failures propagate without modifying the destination.
  Do not bypass assignments or value evaluation merely because storage is empty.
- Leave `Type::assignValue` unchanged: copy into scratch before releasing the
  destination, transfer completed slots, and clear scratch afterward.
  `copyValue` failures retain their existing contract of releasing resources
  and clearing scratch. Array destruction releases only the allocation; it does
  not replace Type-driven value cleanup or add rollback of callback side effects.
  Preserve source/destination aliasing, independent snapshots, shared reference
  identity, and the unchanged-destination guarantee on copy failure.
- The intended optimization removes unused vector state and its measured Debug
  request. It does not remove multi-slot scratch allocation. Current public
  ID/path setters make one Debug request for empty/scalar/reference values and
  two for multi-slot values; verify actual post-change counts rather than
  treating these as Release allocation evidence.
- The owned pointer adds no inline payload budget or dependency. Exact latency
  effects remain unmeasured, and no Release speedup is promised. Keep the
  benchmark's vector-construction and prepared-assignment controls unchanged;
  compare the public setter metrics directly against the recorded baseline,
  without subtracting unlike timing loops.
- The design review was source-only. Implementation and checked results are
  recorded below; other scratch consumers remain unchanged.

#### I11 conditional owned-array implementation and validation

- The shared stored-field setter now reads the required scratch count and
  conditionally allocates a cleared `RuntimeValue[]` through `make_unique`.
  Zero external scratch passes null into Type's existing local-storage path.
  Multi-slot fields still allocate their full scratch on each assignment.
  Only this private helper changed in production; existing includes suffice.
- Type assignment, validation order, selector resolution, signatures, and other
  scratch consumers are unchanged. No inline threshold, reused buffer, cache,
  pool, ownership discriminator, generalized helper, or dependency was added.
  Copy-before-release, aliasing, and failure cleanup remain Type-owned.
- One focused existing-file allocation regression covers integer, reference,
  empty, pair, and wide fields through both property-ID and nested-path setters.
  It first reproduced ten old allocation failures. It now verifies zero Debug
  requests for zero-scratch fields and one per multi-slot write, with complete
  value checks, four bounds markers, shared reference counts, and unchanged
  VM result/intermediate registers and stack marker.
- Both selected Windows Debug CTests pass: `nomad_test_remaining` and
  `nomad_test_language`. Existing every-copy-stage failure-before-commit,
  alias/self-assignment, shared-reference, and receiver/storage/type validation
  regressions remain green. Allocation-failure ordering was source-reviewed,
  not allocator-fault-injected.
- The unchanged opt-in benchmark builds and passes checked Debug/Release runs,
  with bundled language DLL hashes matched to the current builds. Every earlier
  benchmark section also passes. The five-shape assignment workload retains five
  samples, 32 warmups, 50,000 timing operations, and a separate 1,000-operation
  Debug CRT allocation pass. Value/reference/alias/bounds/state checks remain
  unchanged. Release was repeated once without edits because timings were mixed;
  all median `[min,max]` microsecond ranges are retained.

| Case | Debug construction (us) | Debug prepared (us) | Debug ID setter (us) | Debug nested setter (us) | Debug requests construction/prepared/ID/path |
| --- | --- | --- | --- | --- | --- |
| Empty | 0.378 [0.365,0.496] | 0.113 [0.060,0.136] | 1.086 [0.941,1.305] | 2.797 [2.597,3.144] | 1 / 0 / 0 / 0 |
| Integer | 0.420 [0.368,0.550] | 0.216 [0.133,0.243] | 1.197 [1.038,1.435] | 3.094 [2.725,3.574] | 1 / 0 / 0 / 0 |
| String | 0.472 [0.355,0.556] | 0.287 [0.260,0.319] | 1.214 [1.072,1.569] | 3.233 [2.791,3.803] | 1 / 0 / 0 / 0 |
| Pair | 0.838 [0.554,0.900] | 0.388 [0.246,0.495] | 1.725 [1.600,2.294] | 3.604 [3.474,3.973] | 2 / 0 / 1 / 1 |
| Wide | 0.773 [0.637,0.953] | 0.534 [0.463,0.743] | 2.121 [1.620,2.435] | 3.894 [3.562,4.249] | 2 / 0 / 1 / 1 |

| Release run | Case | Construction (us) | Prepared (us) | ID setter (us) | Nested setter (us) |
| --- | --- | --- | --- | --- | --- |
| 1 | Empty | 0.010 [0.009,0.031] | 0.019 [0.019,0.043] | 0.066 [0.057,0.087] | 0.144 [0.108,0.150] |
| 1 | Integer | 0.011 [0.010,0.015] | 0.034 [0.026,0.039] | 0.118 [0.058,0.134] | 0.120 [0.112,0.143] |
| 1 | String | 0.010 [0.009,0.013] | 0.035 [0.032,0.038] | 0.072 [0.063,0.087] | 0.115 [0.109,0.133] |
| 1 | Pair | 0.044 [0.034,0.049] | 0.067 [0.062,0.076] | 0.152 [0.133,0.225] | 0.194 [0.172,0.251] |
| 1 | Wide | 0.072 [0.047,0.080] | 0.166 [0.139,0.187] | 0.287 [0.235,0.309] | 0.417 [0.332,0.469] |
| 2 | Empty | 0.009 [0.009,0.012] | 0.020 [0.019,0.020] | 0.061 [0.057,0.065] | 0.102 [0.101,0.112] |
| 2 | Integer | 0.009 [0.009,0.011] | 0.031 [0.027,0.035] | 0.069 [0.061,0.110] | 0.109 [0.096,0.130] |
| 2 | String | 0.010 [0.009,0.029] | 0.049 [0.036,0.139] | 0.068 [0.065,0.144] | 0.124 [0.108,0.152] |
| 2 | Pair | 0.039 [0.035,0.060] | 0.073 [0.057,0.088] | 0.157 [0.135,0.212] | 0.214 [0.177,0.220] |
| 2 | Wide | 0.045 [0.042,0.049] | 0.160 [0.158,0.250] | 0.287 [0.248,0.340] | 0.387 [0.271,0.411] |

- Each Debug allocation value has identical minimum/median/maximum across all
  five samples. Both public selectors change from one request to zero for
  empty/scalar/reference values and from two to one for multi-slot fields.
  The vector-construction control remains at one/two requests and prepared
  assignment remains at zero; neither control was optimized in this step.
- Debug ID-setter medians change from 1.492/1.750/1.841/2.283/2.732 to
  1.086/1.197/1.214/1.725/2.121 us across the five cases. Release results do not
  establish a general latency improvement. In particular, wide-field ID setters
  rise from 0.245 to 0.287/0.287 us, and nested wide-field setters rise from
  0.312 to 0.417/0.387 us. Integer ID-setter samples vary from baseline 0.063
  to 0.118/0.069 us. These increases and the mixed controls are retained rather
  than discarded; no Release speedup or allocation reduction is claimed.
- Timings include their respective checks on a shared Windows/MSVC machine.
  Controls have different work and are not an exact subtractive decomposition.
  Nonempty multi-slot scratch still allocates; no production field-size profile,
  full native-stack measurement, full-suite rerun, manual gameplay,
  cross-platform result, or timing threshold is claimed.
- This completes the approved bounded I11 storage change. R30 below records the
  follow-up event-definition metadata-copy measurements. Do not extend the
  scratch change to other consumers or add more storage strategies automatically.

#### R30 event-definition snapshot-copy baseline

- Extended the existing opt-in benchmark only. No Runtime API, event storage,
  compiler/game consumer, declaration ownership, or dispatch behavior changed.
  The current definition query scans a vector by ID and returns an optional
  detached copy containing the event name, parameter vector/text, and documentation.
- Four shapes cover empty, one short parameter, eight short parameters, and eight
  long-text parameters. Short event names are 13 bytes; short parameter names
  and type names are 6 and 3 bytes. The long shape uses 128-byte event names,
  64-byte parameter names, and a real registered 96-byte type name.
  Documentation is empty because public event registration does not accept it.
- Each shape uses 0/100/1,000 unrelated events registered before the target,
  giving totals of 1/101/1,001. Each combination has five fresh Runtime samples,
  32 warmups, and 20,000 timed operations per metric. A separate 1,000-operation
  MSVC Debug CRT pass counts allocation/reallocation requests per operation.
  Registration and fixture preparation are outside these measurements.
- The snapshot and prepared-copy metrics include the same metadata checksum.
  Presence measures the existing snapshot-returning query's `has_value()`;
  missing measures an absent-ID query. No borrowed Runtime lookup exists as a
  control. These operations have different work and compiler optimization
  opportunities, so their timings must not be subtracted as an exact breakdown.
- Checks validate full IDs, names, ordered parameter IDs/text, empty
  documentation, invalid/missing IDs, and terminal iteration. A modified detached
  snapshot must not affect registered metadata. After timing, appending 64
  events must preserve both a retained snapshot and fresh metadata lookup.
  Snapshot use after Runtime destruction is not tested.
- Both Windows Debug and Release builds and checked executions pass, including
  every earlier benchmark workload. Bundled language DLL hashes match the
  corresponding builds. The existing `nomad_test_language` Debug CTest also
  passes, including event ID, iteration, parameter, and catalog-growth coverage.
  Tables give median `[min,max]` microseconds per operation across five samples.

| Debug case | Unrelated events | Snapshot (us) | Prepared copy (us) | Presence (us) | Missing (us) |
| --- | --- | --- | --- | --- | --- |
| Empty | 0 | 1.501 [1.384,2.187] | 1.425 [0.948,1.720] | 1.695 [1.212,1.941] | 0.065 [0.031,0.105] |
| Empty | 100 | 1.729 [1.565,2.200] | 1.321 [1.039,1.381] | 1.864 [1.363,1.997] | 0.325 [0.168,0.376] |
| Empty | 1,000 | 4.009 [3.394,4.507] | 1.256 [1.014,1.885] | 3.801 [3.456,4.066] | 2.549 [2.362,2.902] |
| One short | 0 | 3.027 [2.896,3.331] | 2.424 [2.011,2.669] | 2.664 [2.186,3.004] | 0.029 [0.027,0.079] |
| One short | 100 | 3.316 [2.673,3.524] | 2.578 [2.472,3.318] | 3.123 [2.719,3.332] | 0.226 [0.163,0.436] |
| One short | 1,000 | 5.270 [4.744,5.582] | 2.703 [2.248,2.982] | 5.130 [4.344,5.277] | 2.191 [1.921,2.731] |
| Eight short | 0 | 8.975 [8.365,10.455] | 8.564 [7.872,10.807] | 8.907 [7.795,9.817] | 0.031 [0.028,0.058] |
| Eight short | 100 | 8.549 [7.705,11.007] | 8.505 [7.336,8.813] | 8.881 [8.238,9.167] | 0.194 [0.161,0.280] |
| Eight short | 1,000 | 11.277 [10.231,12.479] | 8.528 [8.173,9.754] | 11.217 [10.401,12.358] | 2.327 [2.139,2.529] |
| Eight long | 0 | 13.838 [12.980,15.494] | 13.804 [12.982,14.371] | 13.356 [12.512,14.496] | 0.046 [0.034,0.055] |
| Eight long | 100 | 14.375 [12.958,16.011] | 13.604 [12.236,14.964] | 13.954 [12.945,14.770] | 0.273 [0.187,0.308] |
| Eight long | 1,000 | 17.371 [14.635,18.208] | 14.458 [12.047,15.090] | 15.965 [15.371,16.445] | 2.497 [2.085,2.808] |

| Release case | Unrelated events | Snapshot (us) | Prepared copy (us) | Presence (us) | Missing (us) |
| --- | --- | --- | --- | --- | --- |
| Empty | 0 | 0.010 [0.009,0.011] | 0.009 [0.008,0.010] | 0.010 [0.010,0.011] | 0.001 [0.001,0.002] |
| Empty | 100 | 0.054 [0.044,0.069] | 0.014 [0.008,0.016] | 0.051 [0.042,0.143] | 0.042 [0.038,0.065] |
| Empty | 1,000 | 0.464 [0.383,0.787] | 0.012 [0.008,0.013] | 0.592 [0.540,0.764] | 0.622 [0.329,0.748] |
| One short | 0 | 0.076 [0.072,0.108] | 0.081 [0.071,0.114] | 0.088 [0.071,0.098] | 0.003 [0.002,0.003] |
| One short | 100 | 0.117 [0.070,0.203] | 0.071 [0.039,0.079] | 0.110 [0.072,0.171] | 0.042 [0.033,0.062] |
| One short | 1,000 | 0.762 [0.650,0.798] | 0.070 [0.070,0.073] | 0.710 [0.575,0.783] | 0.473 [0.314,0.753] |
| Eight short | 0 | 0.163 [0.142,0.246] | 0.127 [0.115,0.188] | 0.155 [0.125,0.238] | 0.002 [0.001,0.004] |
| Eight short | 100 | 0.180 [0.158,0.297] | 0.113 [0.094,0.139] | 0.174 [0.129,0.236] | 0.038 [0.036,0.069] |
| Eight short | 1,000 | 0.777 [0.514,0.932] | 0.121 [0.099,0.130] | 0.587 [0.490,0.862] | 0.470 [0.450,0.581] |
| Eight long | 0 | 1.063 [0.803,1.328] | 1.175 [0.849,1.435] | 1.113 [0.809,1.290] | 0.002 [0.001,0.003] |
| Eight long | 100 | 1.093 [0.873,1.279] | 0.972 [0.782,1.162] | 1.071 [0.711,1.430] | 0.042 [0.042,0.056] |
| Eight long | 1,000 | 1.667 [1.371,1.905] | 1.019 [0.751,1.208] | 1.710 [1.421,1.927] | 0.428 [0.388,0.540] |

- Snapshot, prepared-copy, and presence queries each make 3/6/20/37 Debug
  allocation requests for empty/one-short/eight-short/eight-long definitions.
  Their minima, medians, and maxima are identical across all samples and
  catalog sizes. Missing queries make zero requests throughout. These counts
  include Debug-library instrumentation; Release counts are unavailable, not
  zero, and no byte-volume or Release allocation estimate is inferred.
- Both copying and scanning warrant separate consideration. With no unrelated
  events, Release snapshots cost 0.163/1.063 us for eight short/long parameters;
  at 1,000 unrelated events they cost 0.777/1.667 us. Empty-event snapshots also
  grow from 0.010 to 0.464 us as the catalog grows, while their prepared copies
  remain 0.009/0.014/0.012 us. Presence-only callers still pay for complete
  metadata copies. No optimized path or speedup has been measured.
- Source review found nine production definition-query sites: two parser
  signature/argument consumers, three dispatch-parameter loops, three name-only
  adapters, and one presence-only check in Game dispatch. The parameter loops
  can cross host-defined value-copy operations, so registration/reentry and
  reference stability matter, not merely the immediate lookup expression.
- Current vector growth can invalidate borrowed element references. `EventType`
  independently owns a definition copy; its construction is not R29's alleged
  dangling-pointer bug. The existing snapshots remain detached and survive
  registration growth. Do not replace them with long-lived vector-element
  pointers without settling the ownership/lifetime contract first.
- These are checked microbenchmarks on a shared Windows/MSVC machine, with
  visible timing variation. No production event-frequency profile, full-suite
  rerun, manual gameplay, cross-platform result, or timing threshold is claimed.
  Documentation-heavy definitions are not represented by these public fixtures.
- The measurement step did not authorize production changes. The follow-up
  storage/lifetime review and its approved implementation scope are below.

#### R30 approved stable-definition access contract

- The user selected a vector of uniquely owned definitions over a deque.
  Replace the value-vector with `std::vector<std::unique_ptr<EventDefinition>>`,
  following the existing descriptor-catalog pattern. Vector growth moves
  owners, not definitions. This adds one descriptor allocation per event at
  registration; no registration-time or memory-use improvement is claimed.
- Under D5, replace the optional-copy result of `getEventDefinition(EventId)`
  with `const EventDefinition*`. Successful results borrow read-only metadata
  until the owning Runtime is destroyed, including across subsequent and
  reentrant event registrations. This does not introduce concurrent
  registration/thread-safety guarantees.
- Use the existing ID-domain/range guard and direct vector indexing. Event IDs
  are assigned from the vector size; current registration only appends or
  removes the newly rejected tail. Invalid or unregistered IDs return nullptr
  without logging. Failed/duplicate registration must not invalidate earlier
  successful borrows or alter their definitions.
- Presence-only and name-only consumers use the same checked pointer lookup;
  no separate existence/name APIs or additional index are needed. Keep current
  caller diagnostics and queued event-name ownership unchanged. Parameter
  consumers may retain the borrowed declaration across host-defined copying
  that reenters registration because the definition and its metadata stay put.
- Callers needing a detached snapshot explicitly copy the pointed-to
  `EventDefinition`; do not retain the old optional-copy query as a shim or add
  a separate snapshot API. Such copies remain independently mutable.
  `EventType`'s owned definition and the parser's owned dispatch-argument
  declaration remain copies; removing them is outside this scope.
- Keep current event/hub publication and registration-failure policies. Do not
  move declaration ownership between Runtime and TypeManager in this change.
  No shared ownership, registration freeze, seal, cache, new opcode, or
  dispatch/queue redesign is approved.
- The next implementation step updates the storage/query and directly
  dependent compiler/game/test/benchmark call sites together. Add focused
  regressions for borrowed address/metadata stability through catalog growth
  and reentrant registration from a host value-copy operation, misses,
  rejected registration, and explicit detached-copy independence.
- Update the embedding contract when the API changes. Validate the relevant
  existing CTests directly through the workspace integration, without a
  separate pre-test build. Rebuild and execute the checked opt-in benchmark
  in Debug and Release with current bundled DLLs.
- Preserve a comparable explicit snapshot lookup-and-copy metric and the
  prepared-copy control; add a separately labeled borrowed-definition metric.
  Presence/missing metrics retain their observable checks. Compare against the
  recorded baseline, report allocation requests and full timing ranges, and
  do not call reduced-work borrowed access an equal-work snapshot speedup.
  Stop after this bounded implementation/validation step for review.

#### R30 stable-definition implementation and validation

- Implemented the approved vector of uniquely owned definitions and checked
  direct-ID `const EventDefinition*` lookup. Invalid/unregistered IDs return
  nullptr without logging. Existing name scans, IDs, hub publication, and
  registration-failure policies remain unchanged. No parallel index, cache,
  registration freeze, new opcode, or ownership transfer was introduced.
- Compiler/game consumers now borrow definitions. Presence checks use the same
  query without materializing metadata; name adapters still copy names into
  their existing owned registrations/queue payloads. Parser dispatch arguments
  and `EventType` retain their independent definition copies.
- Focused regressions first reproduced four old snapshot-identity failures,
  then passed with the implementation. Existing catalog-growth coverage now
  checks definition/name/parameter address stability after duplicate/reserved
  registration rejection, 64 additional events, and hub rebuilding. New cases
  check detached-copy mutation and survival after Runtime destruction, and
  borrowed parameter iteration through 128 reentrant registrations made by a
  host-defined class copy before the remaining integer payload is copied.
- Both selected Windows Debug CTests pass: `nomad_test_language` and
  `nomad_test_remaining`. This includes the dependent parser and game/scene
  coverage. No new CTest entry was added. Construction/allocation exceptions
  and registration-failure ordering were source-reviewed, not fault-injected.
- The opt-in benchmark builds and passes in Debug and Release with bundled
  language DLL hashes matched to the corresponding builds. All earlier
  benchmark sections also pass. Release was repeated once without edits to
  retain visible timing variability; both runs are recorded below.
- Fixtures, five-sample protocol, 32 warmups, 20,000 timed operations, and
  separate 1,000-operation Debug allocation probes are unchanged. The snapshot
  metric now explicitly copies the checked query result, preserving detached
  snapshot work for comparison. The prepared-copy control is unchanged.
  Borrowed lookup includes the same metadata checksum but deliberately omits
  copying. Presence/missing retain their observable checks. Values are median
  `[min,max]` microseconds per operation.

| Debug case | Unrelated events | Explicit snapshot (us) | Prepared copy (us) | Presence (us) | Missing (us) | Borrowed (us) |
| --- | --- | --- | --- | --- | --- | --- |
| Empty | 0 | 1.169 [1.062,1.265] | 1.243 [0.919,1.556] | 0.016 [0.013,0.024] | 0.009 [0.008,0.037] | 0.025 [0.024,0.030] |
| Empty | 100 | 1.055 [0.974,1.267] | 1.101 [1.068,1.199] | 0.014 [0.013,0.018] | 0.009 [0.009,0.010] | 0.026 [0.023,0.036] |
| Empty | 1,000 | 1.183 [1.042,1.442] | 1.204 [0.928,1.302] | 0.015 [0.013,0.028] | 0.010 [0.009,0.020] | 0.025 [0.022,0.054] |
| One short | 0 | 2.622 [2.342,2.745] | 2.574 [2.255,3.005] | 0.014 [0.013,0.030] | 0.011 [0.009,0.023] | 0.032 [0.026,0.044] |
| One short | 100 | 2.396 [2.028,2.850] | 2.459 [1.987,2.464] | 0.014 [0.013,0.014] | 0.008 [0.007,0.010] | 0.028 [0.027,0.037] |
| One short | 1,000 | 2.256 [1.942,2.485] | 2.200 [2.094,2.268] | 0.015 [0.013,0.044] | 0.010 [0.009,0.018] | 0.053 [0.029,0.076] |
| Eight short | 0 | 7.932 [7.767,8.464] | 7.712 [7.496,8.303] | 0.013 [0.013,0.036] | 0.009 [0.008,0.009] | 0.061 [0.055,0.093] |
| Eight short | 100 | 8.057 [7.623,8.420] | 7.786 [7.253,8.925] | 0.029 [0.012,0.035] | 0.019 [0.008,0.024] | 0.061 [0.055,0.101] |
| Eight short | 1,000 | 7.671 [7.543,8.267] | 7.451 [7.215,9.013] | 0.015 [0.013,0.023] | 0.009 [0.009,0.013] | 0.087 [0.050,0.114] |
| Eight long | 0 | 14.588 [12.741,14.753] | 13.225 [12.792,14.349] | 0.014 [0.013,0.032] | 0.009 [0.009,0.012] | 0.056 [0.048,0.095] |
| Eight long | 100 | 12.659 [12.483,14.247] | 14.406 [12.599,15.625] | 0.017 [0.015,0.026] | 0.010 [0.009,0.016] | 0.062 [0.055,0.074] |
| Eight long | 1,000 | 13.814 [13.380,15.283] | 14.192 [13.122,14.659] | 0.019 [0.013,0.033] | 0.010 [0.009,0.015] | 0.057 [0.048,0.062] |

| Release run | Case | Unrelated events | Explicit snapshot (us) | Prepared copy (us) | Presence (us) | Missing (us) | Borrowed (us) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | Empty | 0 | 0.011 [0.011,0.012] | 0.009 [0.008,0.011] | 0.001 [0.001,0.001] | 0.001 [0.001,0.001] | 0.002 [0.002,0.002] |
| 1 | Empty | 100 | 0.012 [0.009,0.014] | 0.010 [0.010,0.011] | 0.001 [0.001,0.002] | 0.001 [0.001,0.002] | 0.002 [0.002,0.004] |
| 1 | Empty | 1,000 | 0.011 [0.009,0.025] | 0.009 [0.009,0.014] | 0.001 [0.001,0.002] | 0.001 [0.001,0.002] | 0.002 [0.002,0.003] |
| 1 | One short | 0 | 0.044 [0.037,0.078] | 0.044 [0.039,0.086] | 0.001 [0.001,0.002] | 0.001 [0.001,0.002] | 0.002 [0.002,0.004] |
| 1 | One short | 100 | 0.053 [0.038,0.091] | 0.044 [0.040,0.074] | 0.001 [0.001,0.002] | 0.001 [0.001,0.002] | 0.002 [0.002,0.004] |
| 1 | One short | 1,000 | 0.072 [0.039,0.086] | 0.072 [0.038,0.084] | 0.002 [0.001,0.003] | 0.002 [0.001,0.010] | 0.004 [0.002,0.005] |
| 1 | Eight short | 0 | 0.149 [0.105,0.218] | 0.142 [0.128,0.219] | 0.001 [0.001,0.001] | 0.001 [0.001,0.001] | 0.008 [0.005,0.011] |
| 1 | Eight short | 100 | 0.129 [0.101,0.158] | 0.115 [0.111,0.177] | 0.001 [0.001,0.001] | 0.001 [0.001,0.002] | 0.005 [0.005,0.006] |
| 1 | Eight short | 1,000 | 0.127 [0.108,0.156] | 0.116 [0.109,0.134] | 0.001 [0.001,0.002] | 0.002 [0.001,0.003] | 0.009 [0.005,0.009] |
| 1 | Eight long | 0 | 1.169 [0.750,1.650] | 1.378 [0.750,1.570] | 0.001 [0.001,0.002] | 0.001 [0.001,0.002] | 0.007 [0.005,0.015] |
| 1 | Eight long | 100 | 0.910 [0.702,1.007] | 0.962 [0.646,1.047] | 0.001 [0.001,0.002] | 0.001 [0.001,0.002] | 0.006 [0.005,0.011] |
| 1 | Eight long | 1,000 | 0.767 [0.661,1.236] | 0.913 [0.643,1.010] | 0.001 [0.001,0.002] | 0.001 [0.001,0.002] | 0.005 [0.005,0.017] |
| 2 | Empty | 0 | 0.012 [0.010,0.038] | 0.009 [0.008,0.023] | 0.001 [0.001,0.002] | 0.001 [0.001,0.003] | 0.002 [0.002,0.005] |
| 2 | Empty | 100 | 0.028 [0.009,0.033] | 0.010 [0.009,0.025] | 0.001 [0.001,0.002] | 0.001 [0.001,0.003] | 0.002 [0.002,0.005] |
| 2 | Empty | 1,000 | 0.010 [0.009,0.017] | 0.010 [0.009,0.031] | 0.001 [0.001,0.003] | 0.001 [0.001,0.002] | 0.002 [0.002,0.004] |
| 2 | One short | 0 | 0.047 [0.042,0.060] | 0.043 [0.038,0.053] | 0.001 [0.001,0.001] | 0.001 [0.001,0.001] | 0.002 [0.002,0.003] |
| 2 | One short | 100 | 0.040 [0.038,0.057] | 0.049 [0.038,0.100] | 0.001 [0.001,0.002] | 0.001 [0.001,0.005] | 0.002 [0.002,0.005] |
| 2 | One short | 1,000 | 0.044 [0.039,0.052] | 0.043 [0.037,0.069] | 0.001 [0.001,0.001] | 0.001 [0.001,0.001] | 0.002 [0.002,0.003] |
| 2 | Eight short | 0 | 0.115 [0.100,0.217] | 0.112 [0.098,0.172] | 0.001 [0.001,0.001] | 0.001 [0.001,0.001] | 0.005 [0.005,0.005] |
| 2 | Eight short | 100 | 0.128 [0.118,0.143] | 0.127 [0.107,0.199] | 0.001 [0.001,0.002] | 0.001 [0.001,0.002] | 0.006 [0.005,0.014] |
| 2 | Eight short | 1,000 | 0.134 [0.106,0.136] | 0.136 [0.105,0.219] | 0.001 [0.001,0.002] | 0.001 [0.001,0.001] | 0.006 [0.005,0.008] |
| 2 | Eight long | 0 | 1.041 [0.835,1.335] | 1.138 [0.792,1.422] | 0.001 [0.001,0.001] | 0.001 [0.001,0.001] | 0.005 [0.005,0.011] |
| 2 | Eight long | 100 | 0.931 [0.638,1.421] | 0.970 [0.689,1.605] | 0.001 [0.001,0.001] | 0.001 [0.001,0.001] | 0.005 [0.005,0.006] |
| 2 | Eight long | 1,000 | 1.212 [0.754,1.392] | 1.008 [0.879,1.096] | 0.001 [0.001,0.001] | 0.001 [0.001,0.001] | 0.005 [0.005,0.012] |

- Every borrowed/presence/missing Debug measurement makes zero allocation
  requests. Explicit snapshots and prepared copies still make 3/6/20/37
  requests for empty/one-short/eight-short/eight-long definitions, with identical
  minimum/median/maximum counts across all samples and catalog sizes. Presence
  therefore removes the old implicit 3/6/20/37 requests; intentionally owned
  copies do not. Release counts remain unavailable.
- Direct indexing removes catalog-size scaling without removing explicit
  metadata-copy work. At 1,000 unrelated events, Release explicit snapshots
  change from 0.464/0.777/1.667 us to 0.011/0.127/0.767 us in run 1 and
  0.010/0.134/1.212 us in run 2 for empty/eight-short/eight-long definitions.
  Borrowed checksum queries for those cases are 0.002/0.009/0.005 us and
  0.002/0.006/0.005 us; these are reduced-work operations, not equal-work
  snapshot speedups.
- The measurements do not establish a universal snapshot latency improvement:
  with no unrelated events, long-text snapshots change from 1.063 us to
  1.169/1.041 us, while their unchanged prepared-copy control changes from
  1.175 us to 1.378/1.138 us. All ranges and both Release runs are retained.
  Presence/missing timings near 0.001 us are close to measurement resolution;
  no precise speedup ratio or timing threshold is inferred.
- Registration now allocates one separate owned descriptor per event as
  approved. Registration latency, allocation byte volume, production event
  frequency, concurrent registration, and documentation-heavy definitions were
  not measured. These are shared-machine Windows/MSVC microbenchmarks, not
  cross-platform or gameplay results. The full 13-entry suite was not rerun.
- This completes the bounded R30 implementation. Next reviewable scope:
  measure R23's remaining dynamic-variable registration-copy cost before
  choosing a copy-to-move edit. R18 caching and R26/R27 lookup work remain in
  the measured sweep; do not implement another optimization automatically.

#### R23 dynamic-variable registration-copy baseline

- Extended the existing opt-in benchmark and its target-only support files.
  No production API, registration policy, storage, callable lifetime, or callback
  execution behavior changed. Runtime.cpp was not edited by this scope.
  The benchmark remains excluded from default builds and CTest;
  no dependency or CTest entry was added.
- Source review confirms that `registerDynamicVariable` takes callbacks by
  value, moves them into a local registration, then copies that registration
  into the vector. The final copy includes both callable targets and owned
  name/documentation strings. Existing checked host invocation and compiler
  permission contracts are outside this measurement-only change.
- Three fixtures use integer read/write callbacks and a single opened `bench`
  hub. Unique qualified names and documentation are prepared outside timing.
  Large callbacks capture a 128-integer array plus the state pointer; post-run
  checks read the complete array. Actual capture sizes are reported, not
  inferred from an allocation count.

| Case | Name bytes | Documentation bytes | Setter/getter capture bytes |
| --- | --- | --- | --- |
| Small capture | 11 | 9 | 8 / 8 |
| Large capture | 11 | 9 | 1,032 / 1,032 |
| Long metadata | 128 | 512 | 8 / 8 |

- Each fixture registers batches of 100 or 1,000 variables. Five fresh samples
  are collected per metric, with 32 separate warmup registrations before fresh
  measurement instances. Runtime/VM/hub construction, callback/input preparation,
  output-buffer allocation, post-run validation, and destruction of retained
  entries are outside timing. Internal temporary destruction remains part of
  the public registration call.
- Lvalue and rvalue metrics pass prebuilt `std::function` inputs by copy or
  move respectively. The callback-copy control copies pairs into presized
  buffers and retains them until validation. It does not construct metadata,
  grow the catalog, or publish hub members. These different workloads and
  ownership-transfer costs must not be subtracted as an exact decomposition.
- Separate fresh-runtime MSVC Debug allocation passes repeat the same batch
  operations. Counts include Debug-library instrumentation and catalog/hash
  growth, so requests per registration can be fractional. Release counts are
  unavailable, not zero. No allocation byte-volume estimate is inferred.
- Checks cover contiguous IDs, full names/types/capabilities, first/middle/last
  name lookup and hub publication, initial/terminal iteration, no callback
  execution during registration/copying, captured values, complete read/write
  behavior, invocation counts, and forwarded VM identity. Documentation is
  supplied but not read back by this harness; its retention was source-reviewed.
- Debug and Release target builds and complete checked benchmark executions
  pass with matching current bundled language DLL hashes. Every earlier
  benchmark section also passes. The selected Windows Debug
  `nomad_test_language` CTest passes directly, before the separate benchmark
  build. Release was repeated once without edits because timings varied
  substantially; neither run is discarded.
- Tables report median `[min,max]` microseconds per registration or copied
  callback pair. Debug request minima/medians/maxima are identical across all
  five samples in each case.

| Debug case | Registrations | Lvalue registration (us) | Rvalue registration (us) | Prepared callback copy (us) | Lvalue requests | Rvalue requests | Copy requests |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Small capture | 100 | 11.652 [8.187,21.441] | 9.087 [6.915,18.974] | 0.250 [0.236,0.619] | 17.830 | 17.830 | 0.000 |
| Small capture | 1,000 | 11.067 [7.740,17.894] | 9.576 [7.436,13.739] | 0.272 [0.212,0.696] | 16.295 | 16.295 | 0.000 |
| Large capture | 100 | 9.125 [7.725,12.104] | 7.936 [7.111,30.738] | 1.070 [0.800,1.521] | 21.830 | 19.830 | 2.000 |
| Large capture | 1,000 | 12.419 [10.835,15.558] | 9.471 [8.718,17.759] | 1.537 [0.949,2.988] | 20.295 | 18.295 | 2.000 |
| Long metadata | 100 | 18.258 [10.475,21.930] | 15.103 [12.496,28.077] | 0.249 [0.240,0.406] | 26.830 | 26.830 | 0.000 |
| Long metadata | 1,000 | 15.200 [13.307,19.532] | 15.523 [14.956,24.420] | 0.388 [0.223,0.407] | 25.295 | 25.295 | 0.000 |

| Release run | Case | Registrations | Lvalue registration (us) | Rvalue registration (us) | Prepared callback copy (us) |
| --- | --- | --- | --- | --- | --- |
| 1 | Small capture | 100 | 0.376 [0.335,0.464] | 0.418 [0.358,0.421] | 0.024 [0.021,0.026] |
| 1 | Small capture | 1,000 | 0.411 [0.329,0.839] | 0.433 [0.374,0.860] | 0.026 [0.020,0.050] |
| 1 | Large capture | 100 | 0.694 [0.441,1.559] | 0.476 [0.458,2.028] | 0.213 [0.133,0.490] |
| 1 | Large capture | 1,000 | 1.222 [0.982,3.308] | 0.532 [0.486,0.535] | 0.948 [0.566,1.007] |
| 1 | Long metadata | 100 | 1.015 [0.828,1.050] | 0.897 [0.855,1.952] | 0.022 [0.020,0.025] |
| 1 | Long metadata | 1,000 | 1.141 [1.056,2.019] | 1.778 [1.062,2.757] | 0.033 [0.021,0.064] |
| 2 | Small capture | 100 | 0.672 [0.361,0.958] | 0.411 [0.404,0.783] | 0.039 [0.023,0.044] |
| 2 | Small capture | 1,000 | 0.471 [0.349,4.146] | 0.582 [0.352,5.496] | 0.025 [0.020,0.034] |
| 2 | Large capture | 100 | 1.105 [0.765,1.734] | 0.511 [0.481,0.672] | 0.369 [0.342,0.510] |
| 2 | Large capture | 1,000 | 3.142 [0.549,4.301] | 0.824 [0.520,2.521] | 0.751 [0.638,1.578] |
| 2 | Long metadata | 100 | 0.849 [0.842,1.486] | 1.455 [0.844,4.512] | 0.022 [0.021,0.037] |
| 2 | Long metadata | 1,000 | 1.748 [1.061,3.655] | 3.879 [1.105,14.759] | 0.021 [0.020,0.036] |

- A separate five-sample constructor probe uses an eight-byte instrumented
  callable target. Input preparation is excluded and both callbacks are
  checked after publication. This target's nontrivial copy/move constructors
  can change `std::function` storage eligibility, so it is not substituted into
  the timing/allocation cases. Values below are target constructions per
  registration, identical in Debug and both Release runs, including ranges.

| Input | Registrations | Setter copies | Getter copies | Setter moves | Getter moves |
| --- | --- | --- | --- | --- | --- |
| Lvalue | 100 | 2.000 [2.000,2.000] | 2.000 [2.000,2.000] | 3.840 [3.840,3.840] | 3.840 [3.840,3.840] |
| Rvalue | 100 | 1.000 [1.000,1.000] | 1.000 [1.000,1.000] | 4.840 [4.840,4.840] | 4.840 [4.840,4.840] |
| Lvalue | 1,000 | 2.000 [2.000,2.000] | 2.000 [2.000,2.000] | 3.137 [3.137,3.137] | 3.137 [3.137,3.137] |
| Rvalue | 1,000 | 1.000 [1.000,1.000] | 1.000 [1.000,1.000] | 4.137 [4.137,4.137] | 4.137 [4.137,4.137] |

- Both entry modes retain one copy of each target at the local-registration
  append. Lvalues additionally copy targets into the by-value API arguments.
  Move counts include parameter/local transfers and catalog growth; they are
  standard-library-specific, not a portable target-move guarantee.
- At 1,000 registrations, the large-capture Debug lvalue/rvalue batches make
  20.295/18.295 requests per registration, while copying a prepared pair makes
  two. Small captures make 16.295 requests in either entry mode and zero for
  the pair control; long metadata makes 25.295 in either entry mode. This
  confirms copy-related work but does not measure a proposed append fix.
- Release timing does not justify a universal lvalue-to-rvalue speedup claim.
  Large-capture 1,000-entry medians are 1.222/0.532 us in run 1 and
  3.142/0.824 us in run 2, whereas long-metadata medians are 1.141/1.778 us
  and 1.748/3.879 us. The 14.759-us rvalue maximum is retained.
  No optimized production path was executed, no controls are subtracted,
  and no precise speedup ratio or timing threshold is inferred.
- These are shared-machine Windows/MSVC microbenchmarks, not a production
  registration-frequency profile or whole-startup/gameplay/cross-platform
  result. Read-only/write-only cases, copy/allocation failure injection,
  reentrant registration, allocation byte volume, and the full 13-entry suite
  are not covered by this baseline.
- The follow-up review approved the bounded copy-to-move contract below.
  The baseline itself did not change production registration.

#### R23 approved local-registration move contract

- The user approved changing only the prepared registration's append to
  `m_dynamicVariables.push_back(std::move(definition))`. The local entry is not
  read afterward. Retain its construction and the existing publication sequence;
  do not replace them with a new registration abstraction.
- Public setter/getter arguments remain by value. Passing lvalue wrappers still
  copies them into the API; moved inputs transfer their wrappers. Runtime owns
  the registered callbacks and metadata. Names/documentation still copy from
  caller inputs into the prepared entry; the type pointer remains borrowed.
  IDs, capability queries, name resolution, and hub publication are unchanged.
- Removing the final entry copy intentionally changes observable custom
  callable copy/move-constructor side effects and eliminates that copy's
  possible exceptions. This is approved, not a compatibility guarantee for
  deliberate constructor side effects. The existing probe should show one
  target copy per getter/setter for lvalues and zero for moved inputs.
  Do not assert portable target-move counts or promise a universal allocation
  or latency reduction.
- Callback execution exceptions and their side effects retain their current
  propagation contract. Keep existing rejection diagnostics and construction,
  allocation, and publication-failure policies; do not add rollback guards,
  shared storage, a seal, an index, or changes to another registration path.
- Reuse the existing constructor probe in focused registry tests for lvalue
  and moved callback inputs, including successful invocation after registration
  returns. Add its existing support files to the unit-test target as needed.
  Retain existing coverage for metadata, capabilities, invalid IDs, hub
  conflicts, and throwing callbacks rather than duplicating it or introducing
  another probe framework.
- Update the public embedding description of callback ownership when the change
  lands, without exposing vector internals or constructor-count measurements as
  host API guarantees.
- The next continuation implements and validates this scope. Run the relevant
  existing language CTest directly without a separate pre-test build, then
  build/run the unchanged checked benchmark in Debug and Release with current
  bundled DLLs. Compare constructor counts, Debug requests, and full timing
  ranges with the baseline; preserve noisy/slower samples and unavailable
  Release allocation counts. Stop after that bounded implementation step for
  review rather than starting another optimization.

#### R23 local-registration move implementation and validation

- The only production edit moves the prepared dynamic-variable registration
  into its existing vector. Construction, public by-value arguments, names,
  documentation, borrowed type pointer, IDs, capabilities, and hub publication
  remain unchanged. No registration abstraction, cache, index, rollback
  guard, or other registration path was added or changed.
- As approved, the extra callable-target copy and its possible exceptions are
  removed. Custom constructor side effects can differ; callback execution
  exceptions and their side effects still propagate under the existing
  contract. No construction/allocation-failure injection is claimed.
- Two focused registry tests reuse the existing constructor probe and its
  matching support files. They first reproduced four failures with the old
  append, then passed after the move. They check one target copy per callback
  for lvalues, zero for moved inputs, no execution during registration, and
  successful getter/setter invocation after caller wrappers leave scope.
  Portable target-move counts are not asserted.
- The selected Windows Debug `nomad_test_language` CTest passes directly,
  without a separate pre-test build. Existing invalid-ID, capability, hub
  conflict, and throwing-callback coverage remains green. No CTest entry was
  added. The embedding guide now describes callback-wrapper and captured-state
  ownership without exposing internal storage or copy counts as API guarantees.
- The unchanged opt-in benchmark builds and passes every checked section in
  Debug and Release. Bundled language DLL hashes match the corresponding
  builds. Release was repeated once without edits to retain timing variability;
  both runs are recorded. Fixture sizes, 100/1,000-entry batches, five fresh
  samples, 32 separate warmups, preparation exclusions, and separate fresh
  Debug allocation passes remain as in the baseline above.
- Values below are median `[min,max]` microseconds per registration or copied
  callback pair. Debug request counts have identical minimum/median/maximum
  across the five samples in each case.

| Debug case | Registrations | Lvalue registration (us) | Rvalue registration (us) | Prepared callback copy (us) | Lvalue requests | Rvalue requests | Copy requests |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Small capture | 100 | 6.750 [6.547,18.552] | 9.433 [7.900,15.881] | 0.235 [0.233,0.241] | 17.830 | 17.830 | 0.000 |
| Small capture | 1,000 | 8.797 [6.625,36.261] | 7.892 [7.039,9.419] | 0.221 [0.210,0.366] | 16.295 | 16.295 | 0.000 |
| Large capture | 100 | 9.604 [7.681,13.861] | 7.391 [6.562,9.945] | 0.795 [0.560,1.186] | 19.830 | 17.830 | 2.000 |
| Large capture | 1,000 | 8.751 [8.082,21.471] | 8.555 [7.585,10.297] | 1.816 [0.883,3.183] | 18.295 | 16.295 | 2.000 |
| Long metadata | 100 | 20.378 [9.123,23.192] | 9.665 [9.360,36.320] | 0.368 [0.229,0.494] | 24.830 | 24.830 | 0.000 |
| Long metadata | 1,000 | 32.435 [21.657,44.272] | 31.983 [14.939,65.029] | 0.220 [0.203,0.258] | 23.295 | 23.295 | 0.000 |

| Release run | Case | Registrations | Lvalue registration (us) | Rvalue registration (us) | Prepared callback copy (us) |
| --- | --- | --- | --- | --- | --- |
| 1 | Small capture | 100 | 0.399 [0.342,0.526] | 0.394 [0.230,0.440] | 0.024 [0.022,0.026] |
| 1 | Small capture | 1,000 | 0.332 [0.306,0.433] | 0.357 [0.343,0.584] | 0.021 [0.020,0.032] |
| 1 | Large capture | 100 | 0.649 [0.440,1.168] | 0.448 [0.304,1.770] | 0.345 [0.248,0.488] |
| 1 | Large capture | 1,000 | 3.646 [1.141,5.709] | 0.358 [0.320,1.122] | 0.695 [0.612,0.925] |
| 1 | Long metadata | 100 | 0.971 [0.782,1.038] | 0.857 [0.779,6.367] | 0.021 [0.021,0.303] |
| 1 | Long metadata | 1,000 | 1.003 [0.764,3.293] | 1.829 [1.111,3.302] | 0.021 [0.020,0.043] |
| 2 | Small capture | 100 | 0.374 [0.308,0.403] | 0.366 [0.361,0.644] | 0.021 [0.020,0.092] |
| 2 | Small capture | 1,000 | 0.407 [0.308,2.365] | 0.350 [0.299,3.273] | 0.020 [0.019,0.021] |
| 2 | Large capture | 100 | 0.722 [0.661,0.776] | 0.997 [0.388,11.926] | 0.325 [0.300,0.338] |
| 2 | Large capture | 1,000 | 1.394 [0.495,7.066] | 1.218 [0.237,1.623] | 0.747 [0.685,0.845] |
| 2 | Long metadata | 100 | 0.966 [0.780,11.046] | 0.996 [0.790,2.845] | 0.030 [0.021,0.040] |
| 2 | Long metadata | 1,000 | 1.165 [0.976,10.735] | 2.121 [1.042,5.314] | 0.021 [0.020,0.021] |

- Separate instrumented target-constructor counts below are per registration,
  identical in Debug and both Release runs, including their ranges.
  Input preparation is excluded. Target moves include catalog growth and are
  MSVC-specific; these counts do not describe the storage/allocation behavior
  of the non-instrumented timing fixtures.

| Input | Registrations | Setter copies | Getter copies | Setter moves | Getter moves |
| --- | --- | --- | --- | --- | --- |
| Lvalue | 100 | 1.000 [1.000,1.000] | 1.000 [1.000,1.000] | 4.840 [4.840,4.840] | 4.840 [4.840,4.840] |
| Rvalue | 100 | 0.000 [0.000,0.000] | 0.000 [0.000,0.000] | 5.840 [5.840,5.840] | 5.840 [5.840,5.840] |
| Lvalue | 1,000 | 1.000 [1.000,1.000] | 1.000 [1.000,1.000] | 4.137 [4.137,4.137] | 4.137 [4.137,4.137] |
| Rvalue | 1,000 | 0.000 [0.000,0.000] | 0.000 [0.000,0.000] | 5.137 [5.137,5.137] | 5.137 [5.137,5.137] |

- Each setter/getter target loses exactly one measured copy: lvalue inputs
  change from two to one and moved inputs from one to zero. Observed target
  moves increase by one per callback in this standard library.
- Large-capture Debug requests fall by two for both entry modes at each batch
  size: 21.830/19.830 to 19.830/17.830 at 100 and 20.295/18.295 to
  18.295/16.295 at 1,000 for lvalue/rvalue inputs. Long metadata also loses
  two requests: 26.830 to 24.830 at 100 and 25.295 to 23.295 at 1,000.
  Small-capture requests remain 17.830/16.295 at 100/1,000, and callback-copy
  control counts remain zero/two/zero by shape. Release counts remain
  unavailable; no byte-volume or Release allocation estimate is inferred.
- Timing does not establish a general speedup. At 1,000 entries the Debug
  long-metadata lvalue/rvalue medians increase from 15.200/15.523 us to
  32.435/31.983 us. Release large-capture lvalues change from
  1.222/3.142 us in the two baseline runs to 3.646/1.394 us; moved-input
  medians change from 0.532/0.824 us to 0.358/1.218 us. Unchanged controls
  also vary. All slower/noisy samples and both runs are retained rather than
  selecting favorable results or subtracting unlike controls.
- SDK-backed documentation generation and `mkdocs build --strict` pass after
  the embedding update. The full 13-entry suite, production startup profile,
  manual gameplay, cross-platform behavior, and allocator fault injection were
  not run. No timing threshold or universal latency claim is made.
- This completes the approved bounded R23 change. Next reviewable scope:
  measure R18's repeated well-known-type lookups before deciding whether a
  cache is warranted. R26/R27 and other remaining step-8 work stay separate;
  do not implement another optimization automatically.

#### R18 well-known-type lookup baseline

- Extended the existing opt-in benchmark only. No Runtime/TypeManager API,
  storage, registration, parser classification, or ownership behavior changed.
  No dependency, CMake target, or CTest entry was added.
- All eleven Runtime convenience getters currently delegate to name lookup.
  TypeManager scans registered descriptors, obtains names by value, and then
  performs a checked ID lookup. The ten constructor-registered descriptors
  occupy a fixed prefix, so appending unrelated types does not add scan work
  for those getters.
- `getFunctionType()` is different: a fresh Runtime has no descriptor named
  `callback`. Callback registration creates one lazily; later callback
  signatures can share that display name. The getter returns the first
  matching name, not a canonical signature. Its only current production call
  site is documentation generation, so synthetic late-callback costs are not
  evidence of a gameplay hot path.
- Each added-count uses five fresh Runtime samples. In each sample the ten
  fixed getters are measured first, followed by function-type absence and
  then a first callback registered after the unrelated classes. The initial
  catalog has 13 slots. Adding 0/100/1,000 uniquely named classes gives
  13/113/1,013 slots before the callback and 14/114/1,014 afterward.
  Unrelated class names are 18 bytes; callback scan costs depend on that
  fixture, not solely on descriptor count.
- Fixed getter cases use 25,000 timed queries. Function cases use integer
  `clamp(500000 / measuredTypes, 500, 25000)`: 25,000 operations at zero
  additions, 4,424/4,385 for missing/late callbacks at 100 additions, and
  500 for both at 1,000 additions. All operations have 32 warmups. Separate
  MSVC Debug allocation passes use `min(1000, iterations)` operations.
- Convenience getters use direct typed lambdas, not a type-erased callable or
  member-pointer dispatcher. Name strings and IDs are prepared outside timing.
  Timed loops include pointer comparison/counting. Runtime construction,
  registration, initial full metadata checks, and post-growth checks are
  excluded. Queries must not change catalog size.
- Checks validate descriptor ownership, full names/IDs, name/ID/convenience
  agreement, and every timed/probed result. Post-measurement registration of a
  different callback signature and another class must preserve the first
  callback match and all ten original built-in pointers.
- Prepared-ID controls bypass name discovery. The missing-function case uses
  `TypeId::INVALID` as a known-absence control, not a resolved descriptor.
  These controls are not an implemented pointer cache, an equal-work
  replacement for discovery, or an exact subtractive timing decomposition.
- Both Windows Debug and Release targets build and all checked benchmark
  sections pass with bundled language DLL hashes matched to the current
  builds. The selected `nomad_test_language` Debug CTest also passes directly,
  without a separate pre-test build.
- Tables retain median `[min,max]` microseconds per query from all five
  samples. Debug request minima/medians/maxima are identical in each case.

| Debug case | Added types | Convenience (us) | Prepared name (us) | Prepared ID (us) | Requests convenience/name/ID |
| --- | --- | --- | --- | --- | --- |
| void | 0 | 0.631 [0.506,0.714] | 0.628 [0.500,0.633] | 0.059 [0.035,0.072] | 1 / 1 / 0 |
| boolean | 0 | 2.326 [2.111,2.561] | 2.268 [2.087,3.107] | 0.038 [0.034,0.066] | 5 / 5 / 0 |
| integer | 0 | 1.777 [1.542,2.336] | 1.979 [1.693,2.428] | 0.038 [0.033,0.050] | 4 / 4 / 0 |
| float | 0 | 1.686 [1.236,1.870] | 1.816 [1.453,1.876] | 0.041 [0.033,0.085] | 3 / 3 / 0 |
| string | 0 | 3.082 [2.562,3.756] | 2.747 [2.627,3.404] | 0.050 [0.033,0.084] | 6 / 6 / 0 |
| file_name | 0 | 3.567 [2.765,3.761] | 3.328 [3.037,3.512] | 0.054 [0.050,0.079] | 7 / 7 / 0 |
| function_name | 0 | 4.295 [3.552,4.485] | 3.617 [3.473,4.086] | 0.047 [0.039,0.077] | 8 / 8 / 0 |
| line_number | 0 | 4.464 [3.924,4.666] | 4.323 [4.185,5.981] | 0.037 [0.033,0.070] | 9 / 9 / 0 |
| event_callback | 0 | 5.334 [4.402,5.442] | 5.037 [4.839,6.000] | 0.042 [0.033,0.099] | 10 / 10 / 0 |
| event_dispatch | 0 | 5.568 [5.185,5.831] | 5.479 [5.192,5.817] | 0.046 [0.033,0.097] | 11 / 11 / 0 |
| function_missing | 0 | 6.094 [5.872,7.143] | 6.336 [6.194,6.828] | 0.040 [0.028,0.063] | 13 / 13 / 0 |
| function_late | 0 | 6.860 [6.573,7.410] | 6.760 [5.918,6.841] | 0.065 [0.044,0.069] | 14 / 14 / 0 |
| void | 100 | 0.532 [0.426,0.608] | 0.490 [0.445,0.652] | 0.049 [0.034,0.056] | 1 / 1 / 0 |
| boolean | 100 | 2.298 [2.184,2.413] | 2.474 [1.914,2.758] | 0.034 [0.033,0.055] | 5 / 5 / 0 |
| integer | 100 | 1.934 [1.679,2.768] | 1.827 [1.693,2.536] | 0.045 [0.034,0.049] | 4 / 4 / 0 |
| float | 100 | 1.618 [1.301,1.734] | 1.562 [1.468,3.915] | 0.036 [0.033,0.056] | 3 / 3 / 0 |
| string | 100 | 3.098 [2.374,3.798] | 2.640 [2.444,3.116] | 0.038 [0.034,0.058] | 6 / 6 / 0 |
| file_name | 100 | 3.278 [2.553,3.386] | 2.988 [2.689,3.611] | 0.054 [0.039,0.070] | 7 / 7 / 0 |
| function_name | 100 | 3.650 [3.023,4.489] | 3.910 [3.505,4.080] | 0.035 [0.034,0.087] | 8 / 8 / 0 |
| line_number | 100 | 4.514 [4.179,4.876] | 4.177 [3.647,4.633] | 0.046 [0.035,0.071] | 9 / 9 / 0 |
| event_callback | 100 | 4.546 [4.016,4.972] | 4.891 [3.713,5.578] | 0.043 [0.035,0.063] | 10 / 10 / 0 |
| event_dispatch | 100 | 5.407 [4.137,6.049] | 5.312 [4.677,5.550] | 0.045 [0.035,0.077] | 11 / 11 / 0 |
| function_missing | 100 | 79.137 [65.996,87.605] | 79.983 [72.018,86.409] | 0.031 [0.028,0.060] | 213 / 213 / 0 |
| function_late | 100 | 81.979 [71.612,90.389] | 75.959 [69.768,87.386] | 0.035 [0.034,0.123] | 214 / 214 / 0 |
| void | 1,000 | 0.519 [0.411,0.675] | 0.538 [0.479,0.736] | 0.041 [0.035,0.065] | 1 / 1 / 0 |
| boolean | 1,000 | 2.090 [1.980,2.816] | 2.886 [2.202,3.251] | 0.043 [0.041,0.072] | 5 / 5 / 0 |
| integer | 1,000 | 1.664 [1.558,2.641] | 1.938 [1.578,2.445] | 0.046 [0.035,0.064] | 4 / 4 / 0 |
| float | 1,000 | 1.352 [1.212,2.000] | 1.262 [1.186,1.652] | 0.053 [0.035,0.063] | 3 / 3 / 0 |
| string | 1,000 | 3.364 [2.352,23.809] | 2.767 [2.202,9.440] | 0.051 [0.033,0.058] | 6 / 6 / 0 |
| file_name | 1,000 | 3.473 [2.584,4.252] | 4.181 [2.764,8.044] | 0.044 [0.032,0.055] | 7 / 7 / 0 |
| function_name | 1,000 | 4.233 [3.204,4.850] | 3.787 [3.275,4.500] | 0.045 [0.034,0.054] | 8 / 8 / 0 |
| line_number | 1,000 | 4.409 [3.287,10.851] | 5.408 [3.379,49.910] | 0.038 [0.033,0.063] | 9 / 9 / 0 |
| event_callback | 1,000 | 5.937 [3.832,21.400] | 5.065 [3.729,6.954] | 0.048 [0.035,0.074] | 10 / 10 / 0 |
| event_dispatch | 1,000 | 4.794 [4.307,27.060] | 5.228 [4.124,33.496] | 0.062 [0.037,0.082] | 11 / 11 / 0 |
| function_missing | 1,000 | 724.440 [573.083,1615.497] | 752.607 [630.958,1256.393] | 0.030 [0.028,0.098] | 2,013 / 2,013 / 0 |
| function_late | 1,000 | 760.589 [621.224,890.365] | 654.519 [596.359,764.416] | 0.037 [0.034,0.055] | 2,014 / 2,014 / 0 |

| Release case | Added types | Convenience (us) | Prepared name (us) | Prepared ID (us) |
| --- | --- | --- | --- | --- |
| void | 0 | 0.016 [0.011,0.020] | 0.021 [0.010,0.030] | 0.003 [0.002,0.005] |
| boolean | 0 | 0.042 [0.036,0.052] | 0.049 [0.027,0.062] | 0.003 [0.002,0.004] |
| integer | 0 | 0.039 [0.020,0.046] | 0.030 [0.028,0.040] | 0.002 [0.002,0.003] |
| float | 0 | 0.023 [0.019,0.048] | 0.027 [0.019,0.035] | 0.002 [0.002,0.004] |
| string | 0 | 0.047 [0.028,0.057] | 0.052 [0.031,0.062] | 0.002 [0.002,0.005] |
| file_name | 0 | 0.067 [0.040,0.097] | 0.047 [0.038,0.056] | 0.002 [0.002,0.003] |
| function_name | 0 | 0.046 [0.034,0.070] | 0.056 [0.054,0.105] | 0.003 [0.002,0.004] |
| line_number | 0 | 0.084 [0.060,0.087] | 0.075 [0.044,0.115] | 0.002 [0.002,0.003] |
| event_callback | 0 | 0.075 [0.048,0.100] | 0.069 [0.053,0.105] | 0.003 [0.002,0.004] |
| event_dispatch | 0 | 0.090 [0.078,0.092] | 0.096 [0.077,0.100] | 0.003 [0.002,0.004] |
| function_missing | 0 | 0.083 [0.051,0.151] | 0.081 [0.074,0.117] | 0.003 [0.001,0.003] |
| function_late | 0 | 0.088 [0.060,0.125] | 0.093 [0.067,0.152] | 0.003 [0.002,0.004] |
| void | 100 | 0.018 [0.015,0.056] | 0.015 [0.010,0.031] | 0.003 [0.002,0.008] |
| boolean | 100 | 0.041 [0.026,0.104] | 0.044 [0.029,0.089] | 0.002 [0.002,0.007] |
| integer | 100 | 0.028 [0.020,0.037] | 0.033 [0.021,0.047] | 0.002 [0.002,0.004] |
| float | 100 | 0.029 [0.018,0.058] | 0.027 [0.019,0.051] | 0.002 [0.002,0.004] |
| string | 100 | 0.044 [0.036,0.068] | 0.043 [0.029,0.052] | 0.003 [0.002,0.004] |
| file_name | 100 | 0.059 [0.036,0.092] | 0.048 [0.037,0.077] | 0.002 [0.002,0.004] |
| function_name | 100 | 0.048 [0.042,0.075] | 0.054 [0.048,0.065] | 0.003 [0.002,0.008] |
| line_number | 100 | 0.068 [0.052,0.085] | 0.069 [0.051,0.086] | 0.003 [0.002,0.003] |
| event_callback | 100 | 0.068 [0.050,0.074] | 0.070 [0.050,0.095] | 0.003 [0.002,0.008] |
| event_dispatch | 100 | 0.078 [0.059,0.109] | 0.097 [0.081,0.142] | 0.003 [0.002,0.004] |
| function_missing | 100 | 5.658 [4.631,7.173] | 5.383 [3.504,7.572] | 0.003 [0.001,0.004] |
| function_late | 100 | 4.865 [3.513,7.481] | 5.834 [3.584,7.611] | 0.002 [0.002,0.003] |
| void | 1,000 | 0.011 [0.010,0.051] | 0.012 [0.010,0.015] | 0.002 [0.002,0.003] |
| boolean | 1,000 | 0.044 [0.026,0.050] | 0.036 [0.033,0.055] | 0.003 [0.002,0.003] |
| integer | 1,000 | 0.023 [0.022,0.033] | 0.033 [0.022,0.037] | 0.002 [0.002,0.003] |
| float | 1,000 | 0.022 [0.020,0.027] | 0.040 [0.023,0.068] | 0.003 [0.002,0.004] |
| string | 1,000 | 0.038 [0.031,0.044] | 0.044 [0.029,0.077] | 0.002 [0.002,0.004] |
| file_name | 1,000 | 0.050 [0.036,0.074] | 0.056 [0.047,0.100] | 0.002 [0.002,0.003] |
| function_name | 1,000 | 0.058 [0.034,0.070] | 0.043 [0.037,0.076] | 0.003 [0.002,0.011] |
| line_number | 1,000 | 0.072 [0.062,0.115] | 0.076 [0.062,0.097] | 0.003 [0.002,0.003] |
| event_callback | 1,000 | 0.067 [0.047,0.105] | 0.090 [0.066,0.169] | 0.003 [0.002,0.003] |
| event_dispatch | 1,000 | 0.084 [0.062,0.113] | 0.077 [0.056,0.100] | 0.002 [0.002,0.007] |
| function_missing | 1,000 | 46.101 [37.297,68.599] | 47.146 [41.449,79.214] | 0.003 [0.001,0.004] |
| function_late | 1,000 | 45.548 [40.938,54.355] | 48.524 [46.450,69.505] | 0.002 [0.002,0.003] |

- In case order, fixed getters/names make 1/5/4/3/6/7/8/9/10/11 Debug requests
  at every catalog size; prepared IDs make zero. Function-name absence makes
  13/213/2,013 requests and late lookup makes 14/214/2,014. These include
  Debug STL overhead and copies of the 18-byte unrelated names. Release
  request counts are unavailable, not inferred from Debug or from timings.
- Fixed-prefix Release convenience costs remain in the tens of nanoseconds:
  integer medians are 0.039/0.028/0.023 us and event-dispatch medians are
  0.090/0.078/0.084 us at 0/100/1,000 additions. Existing prepared-ID
  controls are about 0.002-0.003 us. No pointer-cache implementation,
  initialization cost, memory trade-off, or cache speedup was measured.
- The absent/late function lookup has different scaling: Release medians
  grow from 0.083/0.088 us at zero additions to 46.101/45.548 us at 1,000.
  A cache must not freeze initial absence or substitute the most recently
  registered callback for the first name match. Multiple callback signatures
  are checked; arbitrary host-defined mutable display names and nominal name
  collisions are not exercised by these fixtures.
- TypeManager's parameter classifier also performs its own name lookups.
  Caching Runtime wrappers alone would not automatically optimize that
  separate path. No expansion of that classifier or its policies is approved.
- All ranges, including Debug outliers, are retained. This is a shared-machine
  Windows/MSVC lookup microbenchmark, not a production getter-frequency,
  compilation/startup, or gameplay profile. No cross-platform result,
  full-suite rerun, allocator-failure injection, timing threshold, or universal
  latency claim is made.
- The follow-up review approved the Runtime-held borrowed-pointer contract
  below. This baseline did not implement a cache or measure its added setup,
  object-size, or lookup effects.

#### R18 approved fixed-type borrowed views

- The user selected ten named, non-owning `const Type*` fields in Runtime,
  rather than cached TypeIds, broader TypeManager getter APIs, or deferral.
  TypeManager remains the sole descriptor owner and ID allocator. Runtime
  borrows existing descriptors; it does not gain a parallel type catalog.
- The ten existing getters cover void, boolean, integer, float, string,
  source-file name, source-function name, source-line number, event callback,
  and event dispatch types. Keep their names, const-callability, and
  `const Type*` return views unchanged. Use per-runtime fields, not static or
  shared storage, hard-coded IDs, a keyed index, or a new type-category enum.
- Bind the views through existing catalog queries once, immediately after the
  successful built-in registration batch and before iterator/range,
  instruction, operator, and native registration setup. The built-in
  constructor/publication path was source-reviewed: it does not need these
  Runtime convenience getters before that point.
- Construction must not succeed with an unbound fixed view. Preserve existing
  registration-failure diagnostics and exception propagation; do not silently
  substitute a type, fall back to lazy lookup, or swallow allocation failures.
  Getters then return their bound fields without per-call lookup, mutation,
  validation, or new exceptions.
- The descriptors remain owned by the same runtime's type catalog until its
  destruction. The existing owned-descriptor storage and registration rules
  keep their addresses stable through growth; no new registration restriction,
  seal, freeze, erase policy, shared ownership, or cleanup discriminator is
  needed.
- Leave `getFunctionType()` unchanged and name-based. Preserve initial absence
  and selection of the first matching `callback` display name; do not bind
  a fixed callback signature, cache initial null, or cache a first non-null
  result under a new lifetime/name-stability assumption.
- Generic name/ID queries, TypeManager's parameter classifier and predicate
  lookup, namespace publication, and registration behavior remain unchanged.
  Do not add catalog convenience getters, an initialization API on TypeManager,
  or a TypeManager-to-Runtime call to extend this optimization.
- The accepted field payload is ten pointers: 80 bytes on x64. This is not a
  measured total Runtime object-size increase, construction cost, startup
  improvement, or production-frequency result. Record those limits rather
  than inferring an aggregate speedup from the lookup controls.
- The next continuation implements the fields, constructor binding, and ten
  getter bodies together. Add focused existing-file regressions for per-runtime
  identity, name/ID agreement through later registration, and zero MSVC Debug
  allocation requests on repeated fixed-getter calls. Reuse existing allocation
  probe patterns, not a new framework. Keep the existing benchmark's callback
  absence/late-signature checks as coverage of the excluded dynamic getter.
- Describe observable borrowed lifetime and the separate function-lookup
  contract in public embedding documentation as needed; keep cache internals
  in this plan rather than the host API guide.
- Run the relevant language and remaining Debug CTests directly through the
  workspace integration without a separate pre-test build. Build and execute
  the unchanged checked lookup workloads in Debug and Release with current
  bundled DLLs; compare fixed-getter requests and full latency ranges against
  the baseline while retaining name/ID and dynamic-function controls.
  Preserve noisy/slower samples and unavailable Release allocation counts.
  Stop after that bounded implementation/validation step for review.

#### R18 fixed-view implementation and validation

- Implemented ten named, per-Runtime borrowed `const Type*` fields. Existing
  catalog queries bind them once after the successful built-in registration
  batch, before iterator/range/core setup. A missing binding fails construction
  explicitly; allocation exceptions propagate. The ten existing getters now
  return their fields without lookup, mutation, or per-call checks.
- TypeManager remains the sole descriptor owner and ID allocator. Its code,
  generic name/ID queries, parameter classifier, predicate lookup, namespace
  publication, and registration behavior are unchanged. No catalog API,
  enum, keyed index, seal, static cache, fallback path, or shared ownership
  was added.
- `getFunctionType()` is unchanged and name-based. The checked benchmark still
  observes initial absence, later first-callback selection, and retention of
  that first match after another signature and class registration.
- Focused language coverage checks all ten views against names, IDs, and
  owners in two distinct runtimes, then repeats the checks after 64 classes,
  hub rebuilding, and callback registration. A separate case reuses the
  existing VM-storage allocation hook rather than adding a probe framework.
  Its 100 calls per getter first reproduced ten old failures:
  100/500/400/300/600/700/800/900/1,000/1,100 requests. It now checks zero
  requests and all 100 correct results for each getter.
- Both selected Windows Debug CTests pass: `nomad_test_language` and
  `nomad_test_remaining`. No CTest entry was added. Constructor failure
  propagation was source-reviewed, not allocator-fault-injected.
- The unchanged opt-in benchmark builds and passes every checked section in
  Debug and Release with bundled language DLL hashes matched to the current
  builds. The Debug CLI/compiler tools were also built for SDK-backed
  documentation generation; strict MkDocs validation passes.
- Workloads, fixture metadata, five-sample protocol, preparation exclusions,
  32 warmups, 25,000 fixed queries, scan-budgeted function queries, and separate
  Debug allocation probes are unchanged from the baseline above. Tables
  retain median `[min,max]` microseconds per query and all controls.
  Debug request minima/medians/maxima are identical in each case.

| Debug case | Added types | Convenience (us) | Prepared name (us) | Prepared ID (us) | Requests convenience/name/ID |
| --- | --- | --- | --- | --- | --- |
| void | 0 | 0.003 [0.002,0.005] | 0.636 [0.476,0.764] | 0.038 [0.036,0.089] | 0 / 1 / 0 |
| boolean | 0 | 0.002 [0.002,0.004] | 2.436 [2.134,2.656] | 0.056 [0.042,0.086] | 0 / 5 / 0 |
| integer | 0 | 0.002 [0.002,0.003] | 1.909 [1.741,2.403] | 0.043 [0.035,0.099] | 0 / 4 / 0 |
| float | 0 | 0.002 [0.002,0.004] | 1.538 [1.391,1.749] | 0.041 [0.033,0.064] | 0 / 3 / 0 |
| string | 0 | 0.002 [0.002,0.003] | 2.835 [2.728,3.303] | 0.037 [0.035,0.080] | 0 / 6 / 0 |
| file_name | 0 | 0.002 [0.002,0.003] | 3.327 [3.105,3.598] | 0.100 [0.035,0.111] | 0 / 7 / 0 |
| function_name | 0 | 0.004 [0.002,0.005] | 3.648 [3.285,4.322] | 0.046 [0.035,0.131] | 0 / 8 / 0 |
| line_number | 0 | 0.004 [0.002,0.006] | 4.655 [3.965,4.875] | 0.045 [0.034,0.099] | 0 / 9 / 0 |
| event_callback | 0 | 0.002 [0.002,0.003] | 5.067 [4.311,5.207] | 0.040 [0.033,0.078] | 0 / 10 / 0 |
| event_dispatch | 0 | 0.003 [0.002,0.005] | 5.584 [4.541,8.034] | 0.052 [0.035,0.100] | 0 / 11 / 0 |
| function_missing | 0 | 6.494 [6.363,6.943] | 5.906 [5.591,6.601] | 0.047 [0.029,0.131] | 13 / 13 / 0 |
| function_late | 0 | 6.792 [5.988,7.444] | 6.510 [5.877,8.267] | 0.046 [0.035,0.085] | 14 / 14 / 0 |
| void | 100 | 0.002 [0.002,0.002] | 0.492 [0.413,0.742] | 0.035 [0.033,0.067] | 0 / 1 / 0 |
| boolean | 100 | 0.002 [0.002,0.003] | 2.470 [2.316,2.771] | 0.056 [0.038,0.085] | 0 / 5 / 0 |
| integer | 100 | 0.006 [0.002,0.014] | 2.016 [1.787,2.567] | 0.035 [0.033,0.050] | 0 / 4 / 0 |
| float | 100 | 0.002 [0.002,0.003] | 1.544 [1.290,1.694] | 0.048 [0.033,0.069] | 0 / 3 / 0 |
| string | 100 | 0.003 [0.002,0.004] | 3.075 [2.457,3.274] | 0.042 [0.033,0.045] | 0 / 6 / 0 |
| file_name | 100 | 0.002 [0.002,0.003] | 3.410 [3.222,4.058] | 0.041 [0.034,0.103] | 0 / 7 / 0 |
| function_name | 100 | 0.002 [0.002,0.004] | 4.144 [3.584,4.473] | 0.058 [0.034,0.079] | 0 / 8 / 0 |
| line_number | 100 | 0.002 [0.002,0.004] | 4.066 [3.848,5.189] | 0.037 [0.034,0.049] | 0 / 9 / 0 |
| event_callback | 100 | 0.002 [0.002,0.003] | 4.942 [4.217,5.197] | 0.038 [0.034,0.059] | 0 / 10 / 0 |
| event_dispatch | 100 | 0.002 [0.002,0.004] | 5.927 [5.448,6.808] | 0.034 [0.033,0.081] | 0 / 11 / 0 |
| function_missing | 100 | 86.939 [80.027,91.993] | 90.491 [79.703,97.337] | 0.042 [0.027,0.070] | 213 / 213 / 0 |
| function_late | 100 | 89.581 [83.000,114.396] | 89.140 [73.599,91.094] | 0.042 [0.033,0.057] | 214 / 214 / 0 |
| void | 1,000 | 0.004 [0.002,0.013] | 0.530 [0.435,0.840] | 0.039 [0.036,0.055] | 0 / 1 / 0 |
| boolean | 1,000 | 0.002 [0.002,0.006] | 2.559 [2.331,3.142] | 0.044 [0.035,0.089] | 0 / 5 / 0 |
| integer | 1,000 | 0.002 [0.002,0.005] | 2.068 [1.855,2.978] | 0.054 [0.036,0.082] | 0 / 4 / 0 |
| float | 1,000 | 0.002 [0.002,0.003] | 1.727 [1.349,1.836] | 0.034 [0.032,0.085] | 0 / 3 / 0 |
| string | 1,000 | 0.002 [0.002,0.002] | 3.020 [2.817,3.221] | 0.035 [0.034,0.063] | 0 / 6 / 0 |
| file_name | 1,000 | 0.002 [0.002,0.002] | 3.650 [3.061,4.052] | 0.044 [0.034,0.126] | 0 / 7 / 0 |
| function_name | 1,000 | 0.002 [0.002,0.005] | 4.457 [3.170,5.041] | 0.052 [0.034,0.075] | 0 / 8 / 0 |
| line_number | 1,000 | 0.002 [0.002,0.003] | 4.645 [4.442,4.842] | 0.037 [0.033,0.082] | 0 / 9 / 0 |
| event_callback | 1,000 | 0.002 [0.002,0.002] | 5.267 [4.674,5.685] | 0.039 [0.034,0.098] | 0 / 10 / 0 |
| event_dispatch | 1,000 | 0.002 [0.002,0.009] | 5.414 [5.294,6.975] | 0.045 [0.038,0.085] | 0 / 11 / 0 |
| function_missing | 1,000 | 797.636 [771.821,875.104] | 787.499 [760.522,884.773] | 0.045 [0.029,0.105] | 2,013 / 2,013 / 0 |
| function_late | 1,000 | 841.535 [824.110,947.569] | 908.867 [876.234,923.061] | 0.041 [0.035,0.073] | 2,014 / 2,014 / 0 |

| Release case | Added types | Convenience (us) | Prepared name (us) | Prepared ID (us) |
| --- | --- | --- | --- | --- |
| void | 0 | 0.001 [0.000,0.001] | 0.012 [0.010,0.013] | 0.002 [0.002,0.003] |
| boolean | 0 | 0.001 [0.000,0.001] | 0.033 [0.029,0.042] | 0.002 [0.002,0.002] |
| integer | 0 | 0.001 [0.000,0.001] | 0.028 [0.023,0.035] | 0.002 [0.002,0.003] |
| float | 0 | 0.001 [0.001,0.002] | 0.024 [0.021,0.025] | 0.002 [0.002,0.002] |
| string | 0 | 0.001 [0.001,0.001] | 0.035 [0.032,0.043] | 0.002 [0.002,0.003] |
| file_name | 0 | 0.001 [0.000,0.001] | 0.046 [0.043,0.053] | 0.002 [0.002,0.003] |
| function_name | 0 | 0.001 [0.001,0.002] | 0.051 [0.046,0.055] | 0.002 [0.002,0.005] |
| line_number | 0 | 0.001 [0.001,0.002] | 0.065 [0.051,0.083] | 0.002 [0.002,0.003] |
| event_callback | 0 | 0.001 [0.000,0.002] | 0.064 [0.053,0.067] | 0.002 [0.002,0.003] |
| event_dispatch | 0 | 0.001 [0.001,0.002] | 0.068 [0.060,0.084] | 0.002 [0.002,0.002] |
| function_missing | 0 | 0.089 [0.065,0.110] | 0.072 [0.055,0.118] | 0.001 [0.001,0.003] |
| function_late | 0 | 0.076 [0.065,0.111] | 0.070 [0.059,0.082] | 0.002 [0.002,0.002] |
| void | 100 | 0.001 [0.001,0.001] | 0.015 [0.012,0.033] | 0.003 [0.002,0.006] |
| boolean | 100 | 0.001 [0.001,0.008] | 0.048 [0.034,0.068] | 0.003 [0.002,0.005] |
| integer | 100 | 0.001 [0.001,0.002] | 0.033 [0.025,0.041] | 0.002 [0.002,0.003] |
| float | 100 | 0.001 [0.000,0.001] | 0.028 [0.023,0.052] | 0.003 [0.002,0.004] |
| string | 100 | 0.001 [0.001,0.002] | 0.043 [0.034,0.070] | 0.002 [0.002,0.004] |
| file_name | 100 | 0.001 [0.000,0.001] | 0.053 [0.044,0.079] | 0.002 [0.002,0.004] |
| function_name | 100 | 0.001 [0.000,0.001] | 0.067 [0.054,0.077] | 0.003 [0.002,0.003] |
| line_number | 100 | 0.001 [0.001,0.001] | 0.086 [0.051,0.118] | 0.003 [0.002,0.019] |
| event_callback | 100 | 0.001 [0.000,0.001] | 0.074 [0.058,0.082] | 0.002 [0.002,0.003] |
| event_dispatch | 100 | 0.001 [0.001,0.001] | 0.079 [0.063,0.084] | 0.002 [0.002,0.003] |
| function_missing | 100 | 4.057 [3.885,5.988] | 4.163 [3.995,4.219] | 0.002 [0.002,0.003] |
| function_late | 100 | 4.299 [3.426,4.957] | 3.605 [3.460,4.389] | 0.002 [0.002,0.003] |
| void | 1,000 | 0.001 [0.000,0.002] | 0.013 [0.010,0.023] | 0.002 [0.002,0.004] |
| boolean | 1,000 | 0.001 [0.000,0.001] | 0.039 [0.031,0.069] | 0.002 [0.002,0.003] |
| integer | 1,000 | 0.001 [0.000,0.003] | 0.031 [0.023,0.036] | 0.002 [0.002,0.003] |
| float | 1,000 | 0.001 [0.001,0.002] | 0.024 [0.020,0.030] | 0.002 [0.002,0.003] |
| string | 1,000 | 0.001 [0.001,0.002] | 0.043 [0.032,0.078] | 0.002 [0.002,0.003] |
| file_name | 1,000 | 0.001 [0.000,0.002] | 0.060 [0.050,0.084] | 0.003 [0.002,0.004] |
| function_name | 1,000 | 0.001 [0.000,0.002] | 0.054 [0.040,0.076] | 0.002 [0.002,0.003] |
| line_number | 1,000 | 0.001 [0.001,0.002] | 0.068 [0.052,0.083] | 0.002 [0.002,0.003] |
| event_callback | 1,000 | 0.001 [0.001,0.002] | 0.074 [0.065,0.130] | 0.002 [0.002,0.008] |
| event_dispatch | 1,000 | 0.001 [0.000,0.002] | 0.075 [0.057,0.088] | 0.003 [0.002,0.003] |
| function_missing | 1,000 | 52.843 [46.093,78.269] | 51.355 [40.914,54.851] | 0.003 [0.003,0.005] |
| function_late | 1,000 | 44.868 [42.408,51.189] | 52.265 [40.969,59.218] | 0.002 [0.002,0.005] |

- Every fixed-getter Debug case now makes zero requests, down from
  1/5/4/3/6/7/8/9/10/11 by case. Prepared-name counts retain those original
  values, prepared-ID counts remain zero, and dynamic missing/late function
  counts remain 13/213/2,013 and 14/214/2,014. Release allocation counts remain
  unavailable, not inferred from Debug.
- Fixed Release getter medians change from the baseline range
  0.011-0.090 us to values rounded to 0.001 us. Some minima round to
  0.000 us; these are not zero-duration results. The operations are near the
  report's precision/measurement limits, so no precise ratio or application
  speedup is inferred. At 1,000 additions, Debug integer/event-dispatch
  medians change from 1.664/4.794 us to 0.002/0.002 us.
- All controls and slower samples are retained. The unchanged missing-function
  Release median at 1,000 additions rises from 46.101 to 52.843 us, while
  late-function lookup changes from 45.548 to 44.868 us. Prepared-name/ID
  controls also vary; no benefit is attributed to their incidental timing
  changes and no unlike controls are subtracted.
- The ten new pointer fields have 80 bytes of payload on x64. Total Runtime
  object-size change, constructor/setup cost, production frequency, and startup
  effects were not measured. These are shared-machine Windows/MSVC lookup
  measurements, not cross-platform, whole-compilation, or gameplay results.
  The full 13-entry CTest suite was not rerun.
- This completes the approved bounded R18 implementation. Next reviewable
  scope: verify and measure R26's native-member cataloging/validation lookups
  before choosing another optimization. Do not extend these views to the
  dynamic function getter, TypeManager's classifier, or other consumers
  automatically.

#### R26 native-member cataloging and validation baseline

- Extended the existing opt-in benchmark only. No production API, descriptor
  ownership, registration policy, index, validation rule, or callback behavior
  changed. No CMake, dependency, or CTest entry was added.
- Source review confirms that both catalog helpers search the mixed
  `m_nativeMembers` vector for duplicate IDs before publication. Property
  cataloging additionally checks the owner's named property; method cataloging
  obtains a copied owner-method list. The existing `m_nativeMembersByName`
  index is already used for signatures and owner/name lookup.
- `validateNativePropertyTypes()` first copies all property pointers through
  `TypeManager::getProperties()`. For each incomplete native property,
  `getMutableNativeProperty()` performs a const native lookup followed by a
  mutable ID lookup before completion. These are indexed reads, not another
  full catalog scan. Completion itself still checks registered type/property
  identity and value-type validity.
- Cataloging is reached from native property/method registration; the central
  property-validation pass is reached during compiler deferred-type resolution.
  These measurements do not establish an execution-time/gameplay bottleneck.

##### Catalog publication protocol and results

- Existing low-level public `TypeManager::registerProperty()` and
  `Runtime::registerMethod()` prepare already-owned, valid descriptors without
  native-member publication. No benchmark-only production entry point is
  needed. Timed loops then call the existing catalog helpers directly.
- Each case has five fresh Runtime samples and a batch of 64 target owners,
  each with one property or one parameterless integer-returning method.
  All targets share an eight-byte property name or seven-byte method name.
  Before measurement, 0/100/1,000 unrelated owners each publish one property
  and one method with distinct eight-byte non-target names. The initial native
  catalog has 14 members, giving 14/214/2,014 entries before each target batch.
  The target name has no indexed entries initially and 64 afterward.
- Thirty-two separate owner/member pairs warm both paths before measurements.
  Runtime/owner/descriptor creation, callback/string preparation, metadata
  checks, and destruction are outside timing. Catalog guards, signature
  checks, growth, and both flat/name index updates are inside timing.
  Separate fresh-runtime MSVC Debug passes count requests over the same
  64 publications; fractional counts are batch averages.
- Checks validate the unchanged catalog prefix, exact appended IDs/order,
  overload counts, owner/name lookup, complete owned metadata, one-member
  owner catalogs, no callback execution during preparation/publication, and
  retained callback state/results afterward. No duplicate/invalid call is
  timed; existing rejection regressions remain in the selected CTests.
- Values are median `[min,max]` microseconds per cataloged member. Request
  minima/medians/maxima are identical across each case's five Debug probes.

| Configuration | Kind | Unrelated owner pairs | Catalog before | Catalog (us/member) | Debug requests/member |
| --- | --- | --- | --- | --- | --- |
| Debug | Property | 0 | 14 | 3.853 [3.433,5.006] | 4.266 |
| Debug | Method | 0 | 14 | 4.033 [3.902,15.691] | 5.266 |
| Debug | Property | 100 | 214 | 6.680 [6.545,7.944] | 4.203 |
| Debug | Method | 100 | 214 | 9.639 [6.789,17.084] | 5.203 |
| Debug | Property | 1,000 | 2,014 | 56.141 [43.183,95.248] | 4.203 |
| Debug | Method | 1,000 | 2,014 | 40.973 [31.316,105.272] | 5.203 |
| Release | Property | 0 | 14 | 0.116 [0.106,0.138] | Unavailable |
| Release | Method | 0 | 14 | 0.152 [0.147,0.248] | Unavailable |
| Release | Property | 100 | 214 | 0.222 [0.198,0.233] | Unavailable |
| Release | Method | 100 | 214 | 0.247 [0.212,0.358] | Unavailable |
| Release | Property | 1,000 | 2,014 | 1.722 [1.084,1.847] | Unavailable |
| Release | Method | 1,000 | 2,014 | 0.898 [0.828,1.105] | Unavailable |

- With one-member owner catalogs held fixed, Release property/method medians
  rise from 0.116/0.152 us to 1.722/0.898 us at 1,000 unrelated pairs.
  This supports reviewing the global duplicate scan rather than adding an
  index for owner-local lookup without evidence.
- The target name bucket grows to 64 entries while unrelated names differ.
  Very large same-name buckets, many members on one owner, complete public
  registration/startup latency, and allocation byte volume are not measured.
  Counts include index growth and Debug-library temporary storage; they do not
  isolate allocations due to the duplicate check.

##### Property-validation protocol and results

- Each fixture contains 100 or 1,000 native properties on one owner and
  0 or 1,000 added stored properties. The initial four properties are native
  and already resolved. Total properties therefore range from 104 to 2,004,
  with 104 or 1,004 native entries.
- Real compiler-owned `DeferredType` placeholders for `int` are used during
  property registration. Existing checked `resolveValueType()` calls bind the
  actual integer descriptor outside timing, leaving completion pending, as
  in the compiler's target-binding/central-completion sequence. The benchmark
  does not time type discovery, target binding, or whole compilation.
- The public validation pass is compared with a privately prepared pointer
  array of equal length: native entries borrow mutable descriptors and stored
  entries are null. The prepared loop performs readiness checks and the same
  `completeTypeResolution()` calls, but omits snapshot construction, virtual
  native-category queries, and checked mutable-descriptor retrieval.
  Its preparation is excluded. It is a reduced-work control, not an
  implemented optimization or an exact subtractive breakdown.
- Each operation has five fresh fixtures plus separate fresh Debug allocation
  fixtures. First completion is one mutating sweep with no same-state warmup.
  Once resolved, 32 warmup sweeps precede 1,000 timed/probed resolved sweeps.
  Times below are total microseconds per sweep, not per property.
- Checks validate catalog sizes and the original pointer prefix, the exact
  pending count and bound value type, no callback execution, stable descriptor
  identity through both stages, complete readiness after each measured stage,
  and first/last callback state/results afterward.

| Debug pending native | Added stored | Total properties | Public completion (us) | Prepared completion (us) | Public resolved (us) | Prepared resolved (us) |
| --- | --- | --- | --- | --- | --- | --- |
| 100 | 0 | 104 | 50.000 [48.700,68.600] | 53.800 [43.900,238.600] | 1.024 [0.898,1.573] | 0.155 [0.128,0.404] |
| 100 | 1,000 | 1,104 | 59.500 [55.100,167.100] | 114.800 [45.900,126.800] | 4.167 [3.453,5.518] | 0.946 [0.647,1.731] |
| 1,000 | 0 | 1,004 | 662.100 [478.100,930.400] | 485.700 [450.800,1372.900] | 5.844 [3.974,7.508] | 1.531 [1.274,2.664] |
| 1,000 | 1,000 | 2,004 | 1109.800 [494.700,1969.300] | 619.000 [446.500,1085.600] | 8.530 [8.059,10.782] | 2.298 [1.974,4.200] |

| Release pending native | Added stored | Total properties | Public completion (us) | Prepared completion (us) | Public resolved (us) | Prepared resolved (us) |
| --- | --- | --- | --- | --- | --- | --- |
| 100 | 0 | 104 | 5.200 [3.100,7.900] | 3.000 [2.400,4.200] | 0.424 [0.294,0.473] | 0.123 [0.083,0.226] |
| 100 | 1,000 | 1,104 | 7.000 [6.100,9.500] | 3.600 [3.400,4.100] | 2.000 [1.528,2.263] | 0.342 [0.330,0.404] |
| 1,000 | 0 | 1,004 | 37.300 [30.300,41.800] | 24.300 [23.300,30.500] | 2.149 [2.022,2.653] | 0.683 [0.669,0.864] |
| 1,000 | 1,000 | 2,004 | 37.400 [33.600,96.400] | 27.100 [25.000,30.900] | 3.325 [3.289,6.180] | 0.958 [0.940,1.115] |

- Debug first-completion requests are `pending + 2` for the public pass
  (102/1,002) and `pending` for the prepared pass (100/1,000), unchanged
  by stored-property count. Already-resolved public passes make two requests
  per sweep; the prepared loop makes zero. Every request value has identical
  minimum/median/maximum across samples. Release counts are unavailable.
- The two public-pass requests are consistent with its copied pointer vector.
  Completion still calls the unchanged `Type::isInternal()` query, which obtains
  the value-type name by value. Source review points to that path for the
  remaining per-property Debug requests; allocator call stacks were not
  captured. The redundant indexed reads themselves are not allocation costs.
- At 1,000 pending native properties, adding 1,000 stored properties changes
  Release public completion from 37.300 to 37.400 us and already-resolved
  passes from 2.149 to 3.325 us. First-completion samples are single sweeps;
  their ranges and the slower prepared Debug samples are retained.
- No production change or proposed speedup was measured. A prepared pointer
  array does not authorize replacing an owned snapshot with live catalog
  iterators; registration/reentry and lifetime safety require separate review.
  Do not attribute the full public/prepared difference to repeated ID lookup.

##### Validation and next scope

- Both Windows Debug and Release targets build and all checked benchmark
  sections pass with bundled language DLL hashes matched to the current
  builds. The selected Debug `nomad_test_language` and `nomad_test_remaining`
  CTests pass directly before the separate benchmark builds, including existing
  catalog rejection and deferred-property completion regressions.
- These are shared-machine Windows/MSVC microbenchmarks, not production
  catalog-frequency or compilation/startup/gameplay profiles. No cross-platform
  result, full-suite rerun, failure injection, thread/reentry validation,
  allocator call-stack profile, or timing threshold is claimed.
- The follow-up review approved the bounded duplicate/publication/lookup
  contract below. The baseline did not implement or measure that optimization.

#### R26 approved native-catalog publication contract

- The user selected existing-name-index duplicate guards with consistent
  publication and a single mutable-property fetch, rather than doing only the
  fetch simplification or deferring both changes.
- Native property and callable member names are owned, private strings exposed
  through nonvirtual const getters, with no rename API. Their constructors are
  the only current writers. Use the existing name bucket for duplicate checks;
  no new naming restriction, declaration seal, or second membership index is
  needed. A future rename facility would have to maintain the existing index.
- Match the full typed `MemberReference`, not its numeric ID or just the first
  member found for an owner/name. Property and callable IDs occupy separate
  spaces, and same-name buckets can contain multiple entries. The search
  remains linear in that bucket, not constant time for arbitrarily large
  same-name sets.
- Preserve the specialized property/method ownership and category checks,
  duplicate-before-signature validation order, rejection messages, and
  successful flat/per-name ordering. Read-only duplicate checks must not
  insert an empty name entry as a side effect of rejection.
- Source review found a necessary failure-contract issue: current code appends
  to the flat vector before updating the name index. If the second update
  allocates and throws, an ID can remain visible only in the flat catalog.
  Checking duplicates exclusively through the name index would then be unsafe.
  This partial-publication case was source-reviewed, not fault-injected during
  the design step.
- The approved implementation makes the two native-member publications
  logically all-or-none for a new ID. Use a narrow shared private append path:
  append to the name bucket first, then append to the flat catalog. If the
  latter throws, pop only the newly added name entry and rethrow unchanged.
  A readable try/catch is appropriate for this exception-only rollback; do not
  add a commit flag, scope-guard framework, or another membership cache.
- Existing memberships and order must survive every rejected or failed
  attempt. An empty private name bucket may remain after allocation failure;
  no new member ID may remain visible in either native index. Do not promise
  unchanged capacities, allocation counts, or a full Runtime transaction.
- This intentionally strengthens failed native-catalog publication. A
  descriptor may already belong to its type, property table, or callable table
  before cataloging begins; those earlier registrations are not rolled back.
  Keep that descriptor/ID intact so cataloging can be retried after failure.
  This is not atomicity for the whole high-level registration operation, and
  it introduces no concurrency guarantee.
- `getMutableNativeProperty()` fetches through the existing checked mutable
  property query once, validates the native category, and returns the same
  owned descriptor. Invalid/unregistered IDs and stored properties still
  return nullptr without logging. Do not expand mutable access or change
  property ownership, resolution, or callback contracts.
- Keep `validateNativePropertyTypes()`'s owned pointer snapshot and all
  completion/type-identity checks unchanged. The prepared-pointer benchmark
  remains a reduced-work control, not approval for a live catalog view or for
  removing value-type checks. The remaining name-copy allocation path is
  outside this scope.
- The next continuation implements these changes and focused regressions
  together. Reuse existing same-name, invalid, duplicate, ordering, and
  deferred-property tests; cover non-first same-name entries and typed-ID
  distinctions where missing. Add allocation-failure/retry coverage for both
  catalog kinds, including failures between the two publications and growth
  cases, using the existing MSVC Debug CRT probe pattern rather than new
  production hooks or a fault-injection framework.
- Failure tests must restore the prior hook before assertions or an exception
  escapes the probe,
  verify both public catalog views retain their prior memberships/order,
  preserve the already-owned descriptor/ID, retry successfully once, and
  reject a subsequent duplicate without adding another entry. Reproduce the
  old partial-publication gap before applying the fix when feasible.
- Run the existing language/remaining Debug CTests directly through the
  workspace integration without a separate pre-test build. Build and execute
  the unchanged checked Debug/Release benchmarks with current bundled DLLs;
  compare full catalog/validation ranges and allocation requests to the
  baseline, retaining noisy/slower samples and unavailable Release counts.
  Update public documentation only for observable catalog failure/retry
  contracts, not private index mechanics.
- Stop after this bounded implementation/validation step for review. No new
  public API, TypeManager dependency change, mutable catalog range, owner-method
  snapshot removal, or other optimization is authorized automatically.

#### R26 indexed duplicate guards and consistent publication

- Implemented typed duplicate searches in the existing member-name buckets.
  Property and method ownership/category checks, duplicate-before-signature
  validation, rejection messages, and successful flat/per-name order are
  retained. There is no new index or lookup API. Bucket search remains linear
  in the number of members sharing that name.
- Both catalog helpers use one private append path. It appends the name
  membership first, then the flat membership; if the latter throws, it pops
  only the new name membership and rethrows. Existing members/order survive.
  An empty private name bucket may remain. Earlier descriptor/ID registration
  is not rolled back; failed native publication can be retried for that ID.
- `getMutableNativeProperty()` now obtains the checked mutable property once,
  verifies its native category, and returns it. Invalid/unregistered IDs and
  stored properties still return nullptr without logging. TypeManager,
  `validateNativePropertyTypes()`'s snapshot, and completion/type-validity
  checks are unchanged.
- A focused native-member regression publishes same-name members on three
  owners, deliberately giving a property and a method equal numeric IDs.
  It verifies the full typed identities, non-first duplicate rejection, owner
  lookup, and unchanged flat/name order after rejection.
- The Debug allocation-failure case prepares batches of 16 already-owned
  properties or methods, with both a new name bucket and four seeded entries.
  A successful control counts eligible allocations; fresh fixtures then reject
  each eligible request in turn, covering publication growth and rollback.
  Checks verify the successful prefix in both public catalog views, no new
  failed membership, unchanged owned descriptors/IDs, successful retry exactly
  once, subsequent duplicate rejection, and retained callback behavior.
  The prior hook is restored before rejection propagates and before assertions.
- The initial all-request probe hit termination during MSVC Debug STL
  bookkeeping, followed by recursive CRT/Boost error reporting and a stack
  overflow. Temporary local stack tracing was removed after diagnosis.
  The final fault probe excludes requests the size of `std::_Container_proxy`
  because Debug proxies can allocate inside noexcept container constructors.
  Same-sized real buffers are also excluded: those requests are not
  fault-tested. Normal allocation counting is unchanged. With this boundary,
  the old implementation reproduced a flat-only native membership on the
  first eligible name-publication failure; the fixed case passes all retries.
- Both selected Windows Debug CTests pass: `nomad_test_language` and
  `nomad_test_remaining`, including existing invalid/category/deferred-property
  coverage. No CTest entry, production fault hook, or new probe framework was
  added. The embedding guide describes only observable publication/retry and
  ownership behavior.
- The unchanged opt-in benchmark builds and passes every checked section in
  Debug and Release, with bundled language DLL hashes matched before each
  execution. Release was repeated once without workload or implementation
  changes; both runs are retained. The existing five-sample, 64-publication
  catalog batches and property-validation fixtures/protocol remain as above.
  Values below are median `[min,max]` microseconds.

| Configuration/run | Kind | Unrelated owner pairs | Catalog (us/member) | Debug requests/member |
| --- | --- | --- | --- | --- |
| Debug 1 | Property | 0 | 9.522 [3.987,12.448] | 4.266 |
| Debug 1 | Method | 0 | 4.531 [3.773,5.406] | 5.266 |
| Debug 1 | Property | 100 | 4.323 [4.270,8.548] | 4.203 |
| Debug 1 | Method | 100 | 4.472 [4.078,7.539] | 5.203 |
| Debug 1 | Property | 1,000 | 7.853 [4.469,11.697] | 4.203 |
| Debug 1 | Method | 1,000 | 4.319 [4.233,14.125] | 5.203 |
| Release 1 | Property | 0 | 0.113 [0.111,0.123] | Unavailable |
| Release 1 | Method | 0 | 0.150 [0.139,0.352] | Unavailable |
| Release 1 | Property | 100 | 0.159 [0.105,0.258] | Unavailable |
| Release 1 | Method | 100 | 0.208 [0.141,0.256] | Unavailable |
| Release 1 | Property | 1,000 | 0.220 [0.191,0.317] | Unavailable |
| Release 1 | Method | 1,000 | 0.263 [0.230,0.311] | Unavailable |
| Release 2 | Property | 0 | 0.269 [0.189,0.312] | Unavailable |
| Release 2 | Method | 0 | 0.314 [0.194,1.389] | Unavailable |
| Release 2 | Property | 100 | 0.248 [0.192,0.308] | Unavailable |
| Release 2 | Method | 100 | 0.244 [0.153,0.859] | Unavailable |
| Release 2 | Property | 1,000 | 0.281 [0.205,1.366] | Unavailable |
| Release 2 | Method | 1,000 | 0.244 [0.216,0.733] | Unavailable |

| Debug pending native | Added stored | Total properties | Public completion (us) | Prepared completion (us) | Public resolved (us) | Prepared resolved (us) |
| --- | --- | --- | --- | --- | --- | --- |
| 100 | 0 | 104 | 49.900 [47.200,187.900] | 46.000 [44.100,47.400] | 0.881 [0.865,3.619] | 0.136 [0.127,0.193] |
| 100 | 1,000 | 1,104 | 107.400 [54.400,131.000] | 79.600 [46.600,278.700] | 5.071 [3.517,8.408] | 0.745 [0.599,2.278] |
| 1,000 | 0 | 1,004 | 1231.300 [459.600,1482.300] | 566.300 [444.900,762.500] | 6.664 [5.146,7.841] | 1.704 [1.438,2.821] |
| 1,000 | 1,000 | 2,004 | 522.300 [452.400,743.600] | 876.200 [492.000,1559.600] | 8.496 [5.279,12.878] | 3.301 [2.327,4.630] |

| Release run | Pending native | Added stored | Total properties | Public completion (us) | Prepared completion (us) | Public resolved (us) | Prepared resolved (us) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 100 | 0 | 104 | 3.600 [3.300,9.500] | 2.900 [2.600,7.700] | 0.451 [0.243,0.812] | 0.083 [0.075,0.295] |
| 1 | 100 | 1,000 | 1,104 | 6.400 [5.800,18.800] | 3.800 [3.400,3.900] | 1.714 [1.533,3.528] | 0.339 [0.329,0.371] |
| 1 | 1,000 | 0 | 1,004 | 33.300 [30.400,51.700] | 28.000 [26.500,82.000] | 2.305 [1.918,4.162] | 0.847 [0.682,1.212] |
| 1 | 1,000 | 1,000 | 2,004 | 35.600 [33.300,66.900] | 35.000 [26.600,55.000] | 3.549 [3.162,10.367] | 1.884 [0.903,2.696] |
| 2 | 100 | 0 | 104 | 5.100 [4.500,42.300] | 4.000 [3.400,6.500] | 0.391 [0.303,0.897] | 0.111 [0.093,0.297] |
| 2 | 100 | 1,000 | 1,104 | 7.700 [6.100,11.000] | 3.500 [3.200,10.900] | 1.869 [1.544,3.031] | 0.670 [0.322,0.756] |
| 2 | 1,000 | 0 | 1,004 | 31.800 [30.800,39.500] | 40.000 [24.300,71.400] | 2.775 [1.847,3.364] | 0.936 [0.640,2.377] |
| 2 | 1,000 | 1,000 | 2,004 | 63.400 [37.600,102.400] | 42.400 [25.400,63.200] | 5.652 [3.845,7.983] | 1.433 [0.925,2.892] |

- Debug allocation requests are unchanged from the baseline in every
  catalog/validation case. Cataloging retains the reported batch-average
  counts; public/prepared first completion remains `pending + 2` / `pending`
  and resolved validation remains two/zero requests per sweep. Every Debug
  request minimum/median/maximum is identical across its five samples.
  Release counts remain unavailable.
- At 1,000 unrelated owner pairs, Release property cataloging changes from
  1.722 to 0.220/0.281 us per member and method cataloging from 0.898 to
  0.263/0.244 us. The unrelated-catalog scan is removed, not the same-name
  bucket scan, owner-method snapshot, signature checks, or publication work.
- Small cases are not uniformly faster. With no unrelated owners, Debug
  property/method medians change from 3.853/4.033 to 9.522/4.531 us;
  Release changes from 0.116/0.152 to 0.113/0.150 in run 1 and
  0.269/0.314 us in run 2. All ranges and both Release runs are retained.
- The single mutable fetch does not establish a general validation speedup.
  At 1,000 pending native plus 1,000 stored properties, Release public
  completion changes from 37.400 to 35.600/63.400 us per sweep; resolved
  validation changes from 3.325 to 3.549/5.652 us. Debug completion with
  1,000 pending and no stored properties rises from 662.100 to 1231.300 us.
  The prepared control is unchanged, also varies, and is not subtracted as
  an exact lookup-cost decomposition.
- SDK-backed generation and strict MkDocs validation pass. These are
  shared-machine Windows/MSVC microbenchmarks, not whole-startup, compilation,
  gameplay, or cross-platform results. The full 13-entry suite was not rerun.
  No all-allocation fault guarantee, concurrency/reentry guarantee,
  allocation-byte measurement, large same-name/owner-local profile, timing
  threshold, or universal speedup claim is made.
- This completes the approved bounded R26 implementation. Next reviewable
  scope: verify and measure R27's repeated hub lookup during context-prefix
  resolution before selecting a change. Do not remove the validation snapshot
  or add another catalog optimization automatically.

#### R27 context-prefix resolution baseline

- Extended only the existing opt-in benchmark and its documentation. No
  production query, registration, storage, ownership, naming, or precedence
  behavior changed. No CMake, dependency, or CTest entry was added.
- Source review confirms that `getVariableContextIdByPrefix()` first calls
  `isHubVariable()`. On a positive result, it repeats
  `getHubId(getContainingHub(variableName))` to obtain the hub's context.
  `getContainingHub()` itself walks dotted prefixes, using linear hub-name
  lookup at each candidate. On a negative membership result, only the first
  discovery is performed before the context-prefix scan.
- Hub variables take priority over host-context prefixes. If the nearest
  containing hub has no variable member with the supplied local name, the
  query falls back to the first matching registered context prefix, not the
  longest prefix. A constant member is not a hub variable; an unknown member
  can still match a registered host prefix.
- The reference trace found eleven production call sites: seven compiler/AST
  resolution or interpreter-evaluation uses and four Runtime registration or
  context-handle paths. No direct raw-VM caller was found. These lookup
  measurements are not a compiled-gameplay or whole-compilation profile.

##### Protocol and correctness checks

- Each of two text shapes uses five fresh Runtime samples at 0/100/1,000
  unrelated hubs and the same number of unrelated host contexts. An early hub
  is registered first, the unrelated hubs follow, and the target late hub is
  last. Measured totals are 2/102/1,002 hubs and 6/106/1,006 contexts.
  The context count includes the existing global context, two hub contexts,
  and three host fallback/overlap fixtures.
- Short hub names are `first` and `last`; long names contain 96 repeated
  characters. The dotted local name is `branch.leaf`, whose intermediate
  prefix is not a registered hub. Host and missing-name fixtures are disjoint
  from hub names. Full variable-name lengths are:

| Case | Short bytes | Long bytes | Expected context ID |
| --- | --- | --- | --- |
| hub_early | 11 | 102 | 1 |
| hub_late | 10 | 102 | 2 |
| hub_dotted | 16 | 108 | 2 |
| host_prefix | 16 | 102 | Unrelated contexts + 3 |
| missing | 12 | 102 | INVALID |

- Each metric has 32 warmups followed by integer
  `clamp(2000000 / (noiseCount + 1), 500, 25000)` timed queries:
  25,000/19,801/1,998 at the three noise counts. Separate MSVC Debug allocation
  passes use 1,000 operations. Runtime/name/context preparation, registrations,
  full precedence checks, and post-measurement growth are outside timing.
- The `prefix` metric uses the current public context resolver; `hub_member`
  uses the existing boolean `isHubVariable()` predicate. Timed checksums verify
  exact context IDs or expected membership results. These operations have
  different work and outputs: the predicate is a reduced-work control, not an
  implemented single-discovery resolver or an exact subtractive cost breakdown.
- Untimed checks exercise actual hub-variable priority despite a matching host
  prefix, constant/unknown-member fallback, first-prefix selection despite a
  later longer matching prefix, empty/unqualified misses, existing variable
  handles, and nearest-containing-hub discovery for a dotted local name.
  Hub/host variable counts must remain unchanged. Adding a hub and another
  host context afterward must preserve all five measured query results.
- Both Windows Debug and Release targets build and every checked benchmark
  section passes with bundled language DLL hashes matched before execution.
  The selected `nomad_test_language` Debug CTest passes directly, before the
  separate benchmark build. Tables retain median `[min,max]` microseconds per
  query across five samples.

| Debug text | Case | Unrelated hubs/contexts | Prefix (us) | Hub membership (us) | Requests prefix/member |
| --- | --- | --- | --- | --- | --- |
| short | hub_early | 0 | 2.616 [2.412,2.817] | 1.570 [1.436,1.818] | 5 / 3 |
| short | hub_late | 0 | 2.591 [2.274,2.872] | 1.627 [1.385,1.791] | 5 / 3 |
| short | hub_dotted | 0 | 3.371 [3.030,3.796] | 2.015 [1.889,2.193] | 7 / 4 |
| short | host_prefix | 0 | 1.285 [1.187,1.418] | 1.154 [1.029,1.254] | 3 / 3 |
| short | missing | 0 | 1.209 [1.039,1.253] | 0.900 [0.869,1.180] | 2 / 2 |
| short | hub_early | 100 | 2.455 [2.207,2.944] | 1.522 [1.348,1.828] | 5 / 3 |
| short | hub_late | 100 | 10.332 [10.010,11.376] | 5.674 [5.024,6.016] | 5 / 3 |
| short | hub_dotted | 100 | 16.722 [16.360,17.281] | 8.938 [7.694,9.452] | 7 / 4 |
| short | host_prefix | 100 | 9.224 [7.912,9.921] | 5.243 [4.973,6.781] | 3 / 3 |
| short | missing | 100 | 6.437 [5.680,7.756] | 5.205 [4.672,6.290] | 2 / 2 |
| short | hub_early | 1,000 | 2.598 [1.948,3.350] | 1.431 [1.178,1.696] | 5 / 3 |
| short | hub_late | 1,000 | 87.756 [76.948,92.348] | 42.008 [38.178,44.206] | 5 / 3 |
| short | hub_dotted | 1,000 | 130.424 [125.272,134.851] | 63.922 [60.928,66.758] | 7 / 4 |
| short | host_prefix | 1,000 | 73.609 [69.482,82.288] | 41.340 [37.828,48.148] | 3 / 3 |
| short | missing | 1,000 | 57.325 [52.321,63.554] | 40.946 [35.385,43.217] | 2 / 2 |
| long | hub_early | 0 | 2.801 [2.762,4.105] | 1.762 [1.656,2.248] | 7 / 4 |
| long | hub_late | 0 | 2.951 [2.847,3.610] | 1.757 [1.706,1.866] | 7 / 4 |
| long | hub_dotted | 0 | 3.168 [3.012,3.507] | 1.922 [1.864,2.070] | 7 / 4 |
| long | host_prefix | 0 | 1.348 [1.285,1.708] | 1.147 [1.094,1.335] | 3 / 3 |
| long | missing | 0 | 1.426 [1.368,1.852] | 1.257 [1.044,1.482] | 3 / 3 |
| long | hub_early | 100 | 2.947 [2.662,3.293] | 1.718 [1.583,1.995] | 7 / 4 |
| long | hub_late | 100 | 11.493 [11.141,12.248] | 6.550 [6.101,7.852] | 7 / 4 |
| long | hub_dotted | 100 | 15.935 [15.581,16.719] | 8.516 [7.563,16.571] | 7 / 4 |
| long | host_prefix | 100 | 8.732 [7.766,9.972] | 5.092 [5.025,6.082] | 3 / 3 |
| long | missing | 100 | 8.529 [8.134,9.420] | 4.913 [4.804,6.264] | 3 / 3 |
| long | hub_early | 1,000 | 3.762 [2.924,4.478] | 1.853 [1.418,2.322] | 7 / 4 |
| long | hub_late | 1,000 | 87.045 [79.431,94.824] | 41.809 [40.379,51.062] | 7 / 4 |
| long | hub_dotted | 1,000 | 131.856 [125.129,135.989] | 64.271 [61.121,71.013] | 7 / 4 |
| long | host_prefix | 1,000 | 75.133 [70.499,77.461] | 41.461 [40.211,46.094] | 3 / 3 |
| long | missing | 1,000 | 73.239 [72.363,80.556] | 41.538 [37.736,46.976] | 3 / 3 |

| Release text | Case | Unrelated hubs/contexts | Prefix (us) | Hub membership (us) |
| --- | --- | --- | --- | --- |
| short | hub_early | 0 | 0.069 [0.061,0.074] | 0.038 [0.035,0.047] |
| short | hub_late | 0 | 0.065 [0.065,0.092] | 0.045 [0.041,0.120] |
| short | hub_dotted | 0 | 0.154 [0.145,0.247] | 0.083 [0.071,0.146] |
| short | host_prefix | 0 | 0.083 [0.074,0.118] | 0.073 [0.046,0.091] |
| short | missing | 0 | 0.062 [0.055,0.073] | 0.027 [0.023,0.029] |
| short | hub_early | 100 | 0.065 [0.059,0.121] | 0.041 [0.037,0.112] |
| short | hub_late | 100 | 0.698 [0.503,1.525] | 0.349 [0.255,0.379] |
| short | hub_dotted | 100 | 0.958 [0.799,1.610] | 0.463 [0.448,0.534] |
| short | host_prefix | 100 | 0.716 [0.679,0.807] | 0.288 [0.269,0.309] |
| short | missing | 100 | 0.399 [0.363,0.480] | 0.299 [0.222,0.315] |
| short | hub_early | 1,000 | 0.064 [0.059,0.081] | 0.038 [0.035,0.051] |
| short | hub_late | 1,000 | 6.657 [5.346,8.578] | 3.642 [2.307,4.932] |
| short | hub_dotted | 1,000 | 10.682 [8.775,13.717] | 5.482 [4.663,6.457] |
| short | host_prefix | 1,000 | 8.063 [6.195,8.588] | 3.412 [3.056,4.519] |
| short | missing | 1,000 | 3.944 [3.524,4.907] | 2.941 [2.201,3.648] |
| long | hub_early | 0 | 0.156 [0.134,0.183] | 0.089 [0.064,0.115] |
| long | hub_late | 0 | 0.134 [0.130,0.173] | 0.071 [0.065,0.086] |
| long | hub_dotted | 0 | 0.169 [0.148,0.207] | 0.087 [0.074,0.099] |
| long | host_prefix | 0 | 0.089 [0.074,0.162] | 0.062 [0.053,0.081] |
| long | missing | 0 | 0.112 [0.079,0.134] | 0.060 [0.055,0.104] |
| long | hub_early | 100 | 0.119 [0.109,0.184] | 0.086 [0.066,0.093] |
| long | hub_late | 100 | 0.794 [0.603,2.429] | 0.354 [0.302,0.404] |
| long | hub_dotted | 100 | 0.992 [0.924,1.178] | 0.523 [0.440,0.616] |
| long | host_prefix | 100 | 0.728 [0.678,0.822] | 0.309 [0.302,0.367] |
| long | missing | 100 | 0.762 [0.665,4.310] | 0.312 [0.268,1.370] |
| long | hub_early | 1,000 | 0.115 [0.103,0.140] | 0.064 [0.058,0.713] |
| long | hub_late | 1,000 | 5.749 [5.303,6.322] | 2.987 [2.510,3.387] |
| long | hub_dotted | 1,000 | 9.005 [7.443,12.516] | 3.984 [3.763,10.581] |
| long | host_prefix | 1,000 | 6.427 [6.407,8.271] | 3.306 [2.206,5.399] |
| long | missing | 1,000 | 6.975 [6.065,7.891] | 3.150 [2.663,5.708] |

- Debug allocation counts are invariant across all five samples and catalog
  sizes: short direct hub queries make five/three requests for prefix/member,
  short dotted queries seven/four, short host fallback three/three, and short
  misses two/two. Long hub cases make seven/four and long fallback/misses
  three/three. Counts include Debug-library instrumentation; Release counts
  are unavailable, not inferred from Debug or timings.
- Short late-hub prefix medians grow from 0.065 to 6.657 us in Release at
  0 to 1,000 noise entries, and dotted-member medians from 0.154 to 10.682 us.
  The early-hub case remains 0.069/0.065/0.064 us. Long late/dotted cases
  reach 5.749/9.005 us. These distinguish early matches from repeated late
  discovery; no optimized resolver or speedup has been measured.
- Fallback/miss measurements include both containing-hub discovery and the
  context scan. Hubs and contexts grow together, so those costs are not
  independently isolated. The membership control omits the fallback scan
  and does not return a context ID; its difference is not an exact allocator
  or discovery-cost decomposition.
- Full ranges, including outliers, are retained. These are shared-machine
  Windows/MSVC lookup measurements, not a production frequency, whole-startup,
  compilation, or gameplay result. Deep registered-hub hierarchies,
  large same-hub member sets, read/write execution, concurrency/reentry, failure
  injection, other platforms, and the full 13-entry suite were not measured.
  No timing threshold or universal latency claim is made.
- The follow-up review approved the shared internal resolver contract below.
  This baseline did not implement or measure that optimized path.

#### R27 approved shared hub-variable owner resolver

- The user approved a private `getHubVariableOwner(name)` helper on Runtime.
  It returns a borrowed `const Hub*` only when the existing nearest containing
  hub lookup finds an exact local member holding `VariableId`. Missing hubs,
  unknown members, and non-variable members return nullptr without logging.
- Move the existing membership resolution into that helper. Keep
  `getContainingHub()`, `getHubId()`, and their dotted-name walk unchanged;
  this change removes the second discovery from the successful context-prefix
  path, not every internal name scan or name copy.
- `isHubVariable()` tests whether the helper returns a hub.
  `getVariableContextIdByPrefix()` uses that same result to read the hub's
  existing context ID, instead of calling the public predicate and repeating
  discovery. The fallback remains the same registration-ordered prefix scan.
- Preserve actual hub-variable priority over matching host prefixes, the
  nearest-hub/exact-local-member checks, and the distinction between variable
  members and constants/functions/types/events. Unknown or non-variable
  members may still fall back to a host context. Do not switch to longest-prefix
  matching, invent prefix normalization, or add language restrictions.
- The helper borrows an existing Runtime-owned hub only during lookup. It adds
  no persistent pointer cache, index, shared ownership, public API, friendship,
  initialization requirement, or concurrency/reentry guarantee.
  Context/member IDs, storage, registration, initializer ordering, and all
  existing diagnostics and failure results remain unchanged.
- As approved, eliminating the repeated name copies also removes their
  allocation-failure opportunities on a positive match. Other allocation and
  callback exceptions keep their existing propagation contracts; no new
  noexcept promise, guard, fallback, or transaction is introduced.
- The next continuation adds focused existing-file regressions for dotted
  membership, hub-versus-host priority, unknown/non-variable fallback,
  overlapping host-prefix order, and stability through later registration.
  Reuse existing handle/storage/registration coverage instead of duplicating
  unrelated tests.
- Add a focused MSVC Debug allocation regression using the existing CRT probe
  pattern: repeated successful context resolution should require no more
  requests than the existing membership predicate for the same prepared name,
  while returning the correct context ID. Cover short, long, and dotted names;
  do not claim zero allocations or hard-code a portable STL allocation count.
  Reproduce the old repeated-discovery allocation cost before applying the
  production fix when feasible.
- Update public embedding documentation for observable membership precedence,
  registration-order fallback, and absence results as needed. Keep helper
  mechanics and measurement details in this plan, not the host API guide.
- Run the existing language and remaining Debug CTests directly through the
  workspace integration without a separate pre-test build. Build and execute
  the unchanged checked Debug/Release benchmarks with current bundled DLLs.
  Compare full context-prefix/member ranges and Debug requests against the
  baseline, retaining fallback/missing controls, noisy/slower samples, and
  unavailable Release allocation counts.
- Stop after the bounded implementation/validation step for review. No broader
  hub registration/rebuilding change, cache, index, TypeManager ownership
  migration, or other optimization is authorized automatically.

#### R27 shared owner resolution implementation and validation

- Implemented one private Runtime helper returning the existing containing hub
  only for an exact local `VariableId` member. The public membership predicate
  tests its result; context-prefix resolution reads that same hub's context ID
  without repeating discovery. No persistent owner cache, index, public API,
  friendship, storage change, or registration rule was added.
- The nearest-containing-hub walk and generic hub-ID lookup are unchanged.
  Constants and missing/non-variable members still fall back to the first
  registered matching context prefix. Dotted local names and actual hub
  variables keep priority over matching host prefixes. Remaining name copies
  and their exception behavior are retained; removing repeated copies does
  not introduce a noexcept or concurrency guarantee.
- A focused language regression covers parent/child hubs, dotted local
  membership, hub-variable priority despite broad/narrow host prefixes,
  constant/unknown-member fallback, first-prefix rather than longest-prefix
  matching, empty/unqualified/missing names, existing handles, and unchanged
  host storage. It repeats after 64 later hub/context registrations and hub
  member rebuilding.
- A separate regression reuses the existing MSVC Debug CRT hook for 100
  successful queries per operation with short/long direct/dotted names.
  Before the implementation it reproduced four failures: context resolution
  made 500 versus 300 requests for short direct names and 700 versus 400 for
  the other shapes. It now verifies correct context IDs with no more requests
  than membership lookup, without asserting a portable STL count or zero
  allocations.
- Both selected Windows Debug CTests pass: `nomad_test_language` and
  `nomad_test_remaining`, run directly without a separate pre-test build.
  Existing registration, initializer, handle/storage, and catalog-failure
  regressions remain green. No CTest entry or probe framework was added.
- The unchanged opt-in benchmark builds and passes every checked section in
  Debug and Release, with bundled language DLL hashes matched before each
  run. The bounded Debug run outlasted its initial wait but completed
  successfully without a restart or overlapping build. Release was repeated
  once without edits to retain timing variability; both runs are recorded.
- The baseline's five-sample, short/long, 0/100/1,000 paired hub/context
  fixtures and query counts are unchanged. Timings below retain median
  `[min,max]` microseconds per query for context resolution and the existing
  boolean membership control. Request minima/medians/maxima are identical
  in all Debug samples.

| Debug text | Case | Unrelated hubs/contexts | Prefix (us) | Hub membership (us) | Requests prefix/member |
| --- | --- | --- | --- | --- | --- |
| short | hub_early | 0 | 1.702 [1.469,1.865] | 1.528 [1.444,1.828] | 3 / 3 |
| short | hub_late | 0 | 1.531 [1.460,1.853] | 1.835 [1.563,2.104] | 3 / 3 |
| short | hub_dotted | 0 | 2.037 [1.716,2.522] | 1.960 [1.790,2.168] | 4 / 4 |
| short | host_prefix | 0 | 1.487 [1.258,1.723] | 1.358 [1.190,1.511] | 3 / 3 |
| short | missing | 0 | 1.178 [1.088,1.343] | 0.903 [0.881,1.166] | 2 / 2 |
| short | hub_early | 100 | 1.562 [1.466,2.026] | 1.576 [1.351,3.879] | 3 / 3 |
| short | hub_late | 100 | 5.864 [5.459,7.176] | 5.975 [5.525,6.539] | 3 / 3 |
| short | hub_dotted | 100 | 8.334 [7.962,10.833] | 8.051 [7.587,11.444] | 4 / 4 |
| short | host_prefix | 100 | 10.139 [8.681,11.214] | 5.943 [5.015,11.750] | 3 / 3 |
| short | missing | 100 | 7.953 [6.041,10.695] | 4.797 [4.327,5.375] | 2 / 2 |
| short | hub_early | 1,000 | 1.528 [1.263,2.717] | 1.751 [1.478,3.902] | 3 / 3 |
| short | hub_late | 1,000 | 42.894 [38.804,63.138] | 42.980 [36.734,49.957] | 3 / 3 |
| short | hub_dotted | 1,000 | 61.574 [61.050,82.512] | 69.866 [61.101,79.492] | 4 / 4 |
| short | host_prefix | 1,000 | 70.692 [69.720,78.229] | 48.610 [41.338,64.879] | 3 / 3 |
| short | missing | 1,000 | 57.375 [53.475,59.408] | 40.410 [34.851,43.521] | 2 / 2 |
| long | hub_early | 0 | 2.074 [1.714,3.411] | 1.827 [1.728,2.050] | 4 / 4 |
| long | hub_late | 0 | 2.007 [1.844,4.063] | 1.981 [1.863,4.840] | 4 / 4 |
| long | hub_dotted | 0 | 1.948 [1.804,2.156] | 2.005 [1.932,2.289] | 4 / 4 |
| long | host_prefix | 0 | 1.427 [1.247,4.300] | 1.202 [1.097,1.435] | 3 / 3 |
| long | missing | 0 | 1.505 [1.363,1.653] | 1.338 [1.103,1.496] | 3 / 3 |
| long | hub_early | 100 | 2.086 [1.724,3.321] | 2.159 [1.723,3.111] | 4 / 4 |
| long | hub_late | 100 | 5.887 [5.396,7.140] | 5.968 [5.613,6.072] | 4 / 4 |
| long | hub_dotted | 100 | 8.137 [7.613,8.356] | 8.111 [7.637,8.753] | 4 / 4 |
| long | host_prefix | 100 | 8.407 [7.301,17.865] | 5.260 [4.604,6.060] | 3 / 3 |
| long | missing | 100 | 8.427 [7.702,10.847] | 4.852 [4.561,7.490] | 3 / 3 |
| long | hub_early | 1,000 | 1.750 [1.534,2.985] | 1.889 [1.479,2.910] | 4 / 4 |
| long | hub_late | 1,000 | 42.341 [41.600,44.770] | 43.592 [37.632,44.053] | 4 / 4 |
| long | hub_dotted | 1,000 | 60.726 [54.596,62.257] | 59.797 [54.386,67.053] | 4 / 4 |
| long | host_prefix | 1,000 | 74.414 [66.589,78.864] | 42.441 [36.086,62.024] | 3 / 3 |
| long | missing | 1,000 | 79.217 [70.623,100.672] | 44.934 [38.777,55.399] | 3 / 3 |

| Release run | Text | Case | Unrelated hubs/contexts | Prefix (us) | Hub membership (us) |
| --- | --- | --- | --- | --- | --- |
| 1 | short | hub_early | 0 | 0.040 [0.036,0.078] | 0.046 [0.036,0.049] |
| 1 | short | hub_late | 0 | 0.073 [0.050,0.122] | 0.043 [0.042,0.098] |
| 1 | short | hub_dotted | 0 | 0.081 [0.074,0.109] | 0.093 [0.076,0.136] |
| 1 | short | host_prefix | 0 | 0.098 [0.089,0.140] | 0.050 [0.048,0.094] |
| 1 | short | missing | 0 | 0.064 [0.048,0.106] | 0.026 [0.022,0.038] |
| 1 | short | hub_early | 100 | 0.049 [0.037,0.071] | 0.042 [0.039,0.075] |
| 1 | short | hub_late | 100 | 0.337 [0.287,0.422] | 0.310 [0.243,0.705] |
| 1 | short | hub_dotted | 100 | 0.542 [0.491,0.582] | 0.632 [0.552,0.729] |
| 1 | short | host_prefix | 100 | 0.836 [0.673,1.092] | 0.314 [0.298,0.439] |
| 1 | short | missing | 100 | 0.551 [0.416,0.890] | 0.355 [0.246,0.440] |
| 1 | short | hub_early | 1,000 | 0.036 [0.034,0.040] | 0.035 [0.034,0.038] |
| 1 | short | hub_late | 1,000 | 2.733 [2.264,2.911] | 3.790 [2.965,4.271] |
| 1 | short | hub_dotted | 1,000 | 5.041 [3.832,5.373] | 4.446 [3.800,5.367] |
| 1 | short | host_prefix | 1,000 | 6.929 [6.550,8.370] | 2.777 [2.319,3.693] |
| 1 | short | missing | 1,000 | 4.054 [3.808,4.415] | 2.888 [2.418,2.974] |
| 1 | long | hub_early | 0 | 0.151 [0.092,0.365] | 0.143 [0.073,0.256] |
| 1 | long | hub_late | 0 | 0.102 [0.066,0.201] | 0.137 [0.077,0.218] |
| 1 | long | hub_dotted | 0 | 0.123 [0.079,0.261] | 0.132 [0.110,0.833] |
| 1 | long | host_prefix | 0 | 0.152 [0.079,0.167] | 0.177 [0.056,0.342] |
| 1 | long | missing | 0 | 0.197 [0.108,0.422] | 0.074 [0.055,0.178] |
| 1 | long | hub_early | 100 | 0.091 [0.061,0.142] | 0.109 [0.070,0.116] |
| 1 | long | hub_late | 100 | 0.417 [0.319,0.920] | 0.372 [0.303,0.544] |
| 1 | long | hub_dotted | 100 | 0.558 [0.444,0.806] | 0.671 [0.621,0.914] |
| 1 | long | host_prefix | 100 | 0.811 [0.789,1.088] | 0.499 [0.358,0.647] |
| 1 | long | missing | 100 | 1.169 [0.923,1.287] | 0.470 [0.280,0.568] |
| 1 | long | hub_early | 1,000 | 0.105 [0.062,0.123] | 0.104 [0.061,0.139] |
| 1 | long | hub_late | 1,000 | 3.826 [2.666,5.217] | 3.091 [2.612,12.447] |
| 1 | long | hub_dotted | 1,000 | 5.957 [4.206,11.605] | 5.306 [4.061,11.382] |
| 1 | long | host_prefix | 1,000 | 9.935 [8.130,12.234] | 3.715 [3.199,4.253] |
| 1 | long | missing | 1,000 | 9.190 [7.742,11.779] | 3.527 [2.572,6.262] |
| 2 | short | hub_early | 0 | 0.036 [0.035,0.055] | 0.045 [0.034,0.054] |
| 2 | short | hub_late | 0 | 0.042 [0.037,0.091] | 0.056 [0.041,0.077] |
| 2 | short | hub_dotted | 0 | 0.115 [0.074,0.159] | 0.085 [0.080,0.110] |
| 2 | short | host_prefix | 0 | 0.081 [0.076,0.091] | 0.061 [0.055,0.122] |
| 2 | short | missing | 0 | 0.056 [0.049,0.139] | 0.025 [0.023,0.042] |
| 2 | short | hub_early | 100 | 0.045 [0.041,0.051] | 0.043 [0.036,0.115] |
| 2 | short | hub_late | 100 | 0.281 [0.251,0.349] | 0.298 [0.262,0.376] |
| 2 | short | hub_dotted | 100 | 0.464 [0.396,0.635] | 0.446 [0.390,0.509] |
| 2 | short | host_prefix | 100 | 0.750 [0.637,0.802] | 0.271 [0.267,0.309] |
| 2 | short | missing | 100 | 0.448 [0.351,0.610] | 0.312 [0.222,0.384] |
| 2 | short | hub_early | 1,000 | 0.035 [0.034,0.179] | 0.036 [0.034,0.680] |
| 2 | short | hub_late | 1,000 | 3.230 [2.397,4.999] | 3.120 [2.522,4.738] |
| 2 | short | hub_dotted | 1,000 | 4.675 [3.686,5.479] | 4.442 [4.074,5.771] |
| 2 | short | host_prefix | 1,000 | 7.802 [6.028,9.329] | 2.887 [2.758,3.323] |
| 2 | short | missing | 1,000 | 4.001 [3.079,4.903] | 3.298 [2.139,5.006] |
| 2 | long | hub_early | 0 | 0.077 [0.062,0.188] | 0.115 [0.081,0.139] |
| 2 | long | hub_late | 0 | 0.122 [0.069,0.152] | 0.101 [0.069,0.123] |
| 2 | long | hub_dotted | 0 | 0.088 [0.085,0.162] | 0.127 [0.085,0.162] |
| 2 | long | host_prefix | 0 | 0.081 [0.072,0.138] | 0.053 [0.050,0.063] |
| 2 | long | missing | 0 | 0.089 [0.079,0.133] | 0.063 [0.051,0.065] |
| 2 | long | hub_early | 100 | 0.065 [0.060,0.089] | 0.071 [0.064,0.328] |
| 2 | long | hub_late | 100 | 0.400 [0.291,0.507] | 0.393 [0.280,0.570] |
| 2 | long | hub_dotted | 100 | 0.523 [0.455,0.619] | 0.488 [0.445,0.639] |
| 2 | long | host_prefix | 100 | 0.777 [0.613,0.853] | 0.276 [0.273,0.436] |
| 2 | long | missing | 100 | 0.731 [0.680,1.156] | 0.291 [0.249,0.307] |
| 2 | long | hub_early | 1,000 | 0.065 [0.060,0.123] | 0.061 [0.058,0.154] |
| 2 | long | hub_late | 1,000 | 2.923 [2.456,3.769] | 2.713 [2.596,3.582] |
| 2 | long | hub_dotted | 1,000 | 3.576 [3.391,4.341] | 3.654 [3.429,5.969] |
| 2 | long | host_prefix | 1,000 | 8.141 [6.039,8.912] | 2.875 [2.357,3.165] |
| 2 | long | missing | 1,000 | 7.479 [6.396,8.527] | 3.326 [2.501,3.826] |

- Successful prefix queries now match the membership control's Debug request
  counts at all catalog sizes: three for short direct names and four for
  short dotted/long names. These change from five/seven requests respectively,
  saving two/three requests per positive query, not making lookup allocation-
  free. Predicate and fallback/missing counts retain their baseline values.
  Release allocation counts remain unavailable.
- At 1,000 paired unrelated entries, short late/dotted prefix Release medians
  change from 6.657/10.682 us to 2.733/5.041 us in run 1 and
  3.230/4.675 us in run 2. Long late/dotted queries change from
  5.749/9.005 us to 3.826/5.957 us and 2.923/3.576 us.
  The remaining hub scans still grow with the catalog; this is not constant-
  time name resolution or a hub-index implementation.
- Results do not show every case improving. With no noise, short late-hub
  prefix changes from 0.065 us to 0.073/0.042 us. Long host fallback at
  1,000 entries changes from 6.427 us to 9.935/8.141 us, and long misses
  from 6.975 us to 9.190/7.479 us. The unchanged predicate also varies.
  All slower samples, ranges, and both Release runs are retained; no unlike
  controls are subtracted as an exact discovery/allocator decomposition.
- SDK-backed documentation generation and strict MkDocs validation pass after
  the public precedence description. These are shared-machine Windows/MSVC
  microbenchmarks, not startup/compilation frequency or gameplay results.
  No full 13-entry CTest rerun, cross-platform check, allocator failure
  injection, concurrency/reentry guarantee, timing threshold, or universal
  speedup is claimed.
- This completes the approved bounded R27 implementation. Next reviewable
  scope: verify and measure R13's hub-registration/member-rebuilding costs
  before deciding whether another change is warranted. Keep the separately
  planned hub type/singleton redesign and its unresolved semantics out of
  that measurement step.

#### R13 hub-registration and member-rebuilding baseline

- Extended only the existing opt-in benchmark and its documentation. No
  production registration, ownership, member resolution, index, language
  semantics, or failure policy changed. No CMake, dependency, or CTest entry
  was added.
- The reference trace confirms the private rebuild is called only by new-hub
  registration. An existing opened hub returns its existing ID before
  rebuilding. New registration validates its name/conflicts, appends the hub,
  builds replacement member maps for every hub, commits those maps, and
  resolves the new ID. These public-path costs are measured together.
- The rebuild retains explicitly published native functions on their actual
  owner, then resolves variable/script-function/constant/type/event/dynamic/
  family memberships from names. Late child declarations can therefore
  redistribute eligible members even when their descriptors/IDs stay put.
  Ordinary qualified native free functions must not become hub members.
- The current source catches `NomadException` from rebuilding to reject the
  new hub; it does not provide an all-exception registration transaction.
  Failure/conflict behavior was source-reviewed and existing regressions were
  run, not fault-injected in this measurement step.

##### Fixtures and protocol

- Each case has five fresh Runtime samples with 8/32/128 existing opened
  owners, then a timed batch of eight new opened hubs. Initial owners are all
  created before optional fixture members are registered. Setup, prepared
  names, expected maps, output buffers, full correctness checks, and destruction
  are outside timing. Each reported new-hub value is the average over that
  growing batch, not the last insertion or an entire startup.
- Empty fixtures contain no members. Each mixed owner contains eight map
  entries: one root stored variable; one explicitly owned native-function
  group with two overloads; and six `child.*` entries for a script function,
  constant, class type, event, dynamic variable, and parameterized family.
  An additional ordinary qualified native function is deliberately not a
  hub member. Script functions remain uncompiled metadata in these fixtures;
  no script body is executed.
- The report's `source_members` counts the six name-resolved `child.*`
  declaration categories per owner, including declarations created through
  host APIs, not just source-compiled functions. `pinned_native_ids` counts the
  two explicit overload IDs in each native group. Initial member-map counts
  are 0 for empty fixtures and 64/256/1,024 for mixed fixtures.
- Unrelated additions have distinct 12-byte names. Child additions use
  18-byte names under the first eight existing owners. In mixed child cases
  exactly six entries move from each affected parent to its child; the
  parent's root variable and native overload group stay on the parent.
  Unrelated additions must leave all existing memberships unchanged.
- Reopening the last existing owner is a separate control: 32 warmups precede
  5,000 timed reopen calls. Separate fresh Debug fixtures use 1,000 reopen
  operations and the same eight new-hub additions for allocation probes.
  Thirty-two new empty-hub registrations in a separate Runtime warm the
  mutating path before the fixtures. There is no repeated same-ID warmup for
  a new hub.
- New registration includes collision checks, descriptor/context allocation,
  full rebuilding, name copies, and ID lookup; reopen bypasses most of that.
  The control cannot be subtracted as an exact rebuild/allocator breakdown
  or treated as an implemented optimization.
- Checks compare all typed member alternatives, exact IDs, per-group overload
  order, member counts, root variable handles and values, old hub pointer/name
  identity, contiguous new IDs, and repeat-open identity. They verify the
  first-eight child redistribution, unchanged pinned native membership, and
  absence of ordinary native free functions from all checked maps.
  Registration/rebuilding must invoke no callback or family factory.
  Selected native callbacks are checked afterward for retained state/results.
- Both Windows Debug and Release targets build and every checked benchmark
  section passes with bundled language DLL hashes matched before execution.
  The selected `nomad_test_language` Debug CTest passes directly, before the
  separate benchmark target build. Release was repeated once without changes
  to retain timing variability; both runs are recorded.
- Values below are median `[min,max]` microseconds per new hub or reopen call.
  Debug new-hub requests are eight-operation averages and include Debug-library
  instrumentation; all request minima/medians/maxima are identical across the
  five samples. Release counts are unavailable, not zero.

| Debug shape | Addition | Existing hubs | New hub (us) | Reopen (us) | Requests new/reopen |
| --- | --- | --- | --- | --- | --- |
| empty | unrelated | 8 | 58.575 [49.175,144.425] | 0.264 [0.228,0.664] | 140.250 / 0.000 |
| empty | child | 8 | 55.825 [49.638,156.800] | 0.259 [0.212,0.530] | 146.250 / 0.000 |
| mixed | unrelated | 8 | 478.812 [296.125,537.663] | 0.340 [0.230,0.423] | 636.250 / 0.000 |
| mixed | child | 8 | 387.700 [272.738,435.700] | 0.255 [0.207,0.362] | 642.250 / 0.000 |
| empty | unrelated | 32 | 131.675 [113.450,145.262] | 0.803 [0.784,0.927] | 236.000 / 0.000 |
| empty | child | 32 | 90.400 [86.037,124.287] | 0.789 [0.711,1.122] | 242.000 / 0.000 |
| mixed | unrelated | 32 | 1583.537 [1496.412,2037.025] | 0.852 [0.716,1.268] | 2220.000 / 0.000 |
| mixed | child | 32 | 1497.450 [1420.025,2530.363] | 0.973 [0.809,1.091] | 2226.000 / 0.000 |
| empty | unrelated | 128 | 252.100 [222.912,317.038] | 3.163 [2.645,4.808] | 620.000 / 0.000 |
| empty | child | 128 | 304.775 [245.900,528.862] | 3.481 [2.888,3.946] | 626.000 / 0.000 |
| mixed | unrelated | 128 | 10303.325 [9711.175,10725.800] | 3.041 [2.823,3.205] | 8556.000 / 0.000 |
| mixed | child | 128 | 12074.475 [10662.163,13581.112] | 3.175 [2.920,5.188] | 8562.000 / 0.000 |

| Release run | Shape | Addition | Existing hubs | New hub (us) | Reopen (us) |
| --- | --- | --- | --- | --- | --- |
| 1 | empty | unrelated | 8 | 2.925 [2.650,4.513] | 0.062 [0.055,0.097] |
| 1 | empty | child | 8 | 5.487 [3.962,8.113] | 0.071 [0.058,0.086] |
| 1 | mixed | unrelated | 8 | 17.125 [16.538,24.225] | 0.052 [0.047,0.060] |
| 1 | mixed | child | 8 | 18.712 [17.988,21.962] | 0.049 [0.045,0.054] |
| 1 | empty | unrelated | 32 | 5.763 [5.500,9.675] | 0.194 [0.187,0.245] |
| 1 | empty | child | 32 | 6.862 [5.875,55.700] | 0.251 [0.203,0.877] |
| 1 | mixed | unrelated | 32 | 144.312 [100.338,269.025] | 0.240 [0.159,0.347] |
| 1 | mixed | child | 32 | 133.762 [109.438,332.438] | 0.188 [0.186,0.209] |
| 1 | empty | unrelated | 128 | 31.238 [19.438,42.600] | 1.042 [0.714,1.363] |
| 1 | empty | child | 128 | 18.425 [15.825,31.600] | 0.740 [0.714,1.069] |
| 1 | mixed | unrelated | 128 | 1392.025 [1182.125,1678.638] | 0.855 [0.606,1.016] |
| 1 | mixed | child | 128 | 1341.737 [1027.575,1607.575] | 0.860 [0.638,0.955] |
| 2 | empty | unrelated | 8 | 2.500 [2.362,4.513] | 0.056 [0.054,0.099] |
| 2 | empty | child | 8 | 5.362 [3.013,28.163] | 0.089 [0.052,0.113] |
| 2 | mixed | unrelated | 8 | 21.100 [17.988,30.650] | 0.071 [0.054,0.100] |
| 2 | mixed | child | 8 | 24.100 [18.288,37.675] | 0.056 [0.046,0.076] |
| 2 | empty | unrelated | 32 | 5.975 [5.013,17.975] | 0.226 [0.205,0.275] |
| 2 | empty | child | 32 | 5.938 [5.638,6.700] | 0.216 [0.192,0.267] |
| 2 | mixed | unrelated | 32 | 120.487 [97.638,213.838] | 0.196 [0.172,0.305] |
| 2 | mixed | child | 32 | 116.800 [105.588,182.300] | 0.225 [0.171,0.309] |
| 2 | empty | unrelated | 128 | 16.512 [16.312,18.600] | 0.791 [0.728,0.932] |
| 2 | empty | child | 128 | 16.825 [15.925,17.700] | 0.753 [0.658,1.164] |
| 2 | mixed | unrelated | 128 | 1123.275 [1048.925,1559.025] | 0.664 [0.605,1.001] |
| 2 | mixed | child | 128 | 1356.275 [1058.987,1878.088] | 1.183 [0.699,1.402] |

- At 128 existing owners, mixed unrelated additions cost 1392.025/1123.275 us
  per new hub in the two Release runs, versus 31.238/16.512 us for empty
  fixtures. Mixed child additions cost 1341.737/1356.275 us. This supports
  reviewing repeated work on populated catalogs, including additions that do
  not alter existing memberships; no optimized registration path was measured.
- Debug mixed unrelated/child cases at 128 owners make 8,556/8,562 requests per
  new hub, versus 620/626 in empty fixtures. Reopening makes zero Debug
  requests but still performs hub-name lookup; it is not constant-time by
  implication. Request differences are not attributed to one internal helper
  without allocation call-stack profiling.
- Both Release runs and all ranges are retained, including the 55.700-us
  empty-child maximum at 32 owners in run 1 and 28.163-us maximum at eight
  owners in run 2. Unrelated/child cases also have different name lengths and
  redistribution work; no exact subtractive decomposition or universal
  latency claim is made.
- These are shared-machine Windows/MSVC synthetic registration measurements,
  not a production catalog/startup-frequency profile, a whole-build timing,
  gameplay validation, or a cross-platform result. Fixture preparation and
  interleaved real-world member/hub registration patterns are not measured.
  Failure injection, concurrent/reentrant registration, allocation byte volume,
  deep hierarchy/large-overload profiles, and the full 13-entry suite were not
  run. No timing threshold or ownership/exception-policy change is inferred.
- The follow-up review approved performance deferral and the separate bounded
  exception-rollback contract below. The baseline did not authorize targeted
  rebuilding or measure an optimized registration path.

#### R13 approved full-refresh retention and exception rollback

- The user selected deferring the performance optimization while approving
  the narrow exception-rollback correction, rather than deferring both or
  continuing targeted-rebuild design now.
- Retain full member rebuilding on new hub registration. Its scan rereads
  mutable hub contexts; `getVariableContext()` intentionally exposes the
  same context storage used by hubs, and hosts can register variables through
  that interface. Updating only a new hub and its nearest parent would need
  to account for edits that the current full refresh picks up elsewhere.
  This source-reviewed behavior was not isolated as a timed fixture.
- The measured populated-catalog cost warrants attention, but these synthetic
  registration measurements do not establish production startup impact.
  Avoid adding invalidation machinery or restrictions to the existing hub
  model solely from those timings. Revisit targeted rebuilding with a
  representative startup profile or during the separately planned hub
  type/singleton redesign; its language/ownership decisions remain unresolved.
- Add no cache, index, dirty flag, observer, new registration API, declaration
  seal, or new restriction on mutable metadata. Generic hub-name lookup,
  name-based adoption/redistribution, explicit native ownership and overload
  ordering, context handles/values, and successful reopening stay unchanged.
- Source review found that `registerHub()` appends the candidate before
  calling `rebuildHubMembers()`, but removes it only in the `NomadException`
  rejection path. Other exceptions from rebuilding, including allocation
  failures, currently propagate with the appended candidate still present.
  This was source-reviewed, not fault-injected in the design step.
- The next implementation adds exception-only rollback for a propagating
  non-Nomad rebuild exception, using the existing previous-count rollback
  pattern and rethrowing the original exception unchanged. Keep the current
  `NomadException` path's log message and `HubId::INVALID` result.
  Do not convert propagated exceptions into logged success-shaped results,
  add a commit flag/scope-guard framework, or extend the catch to earlier
  validation/descriptor construction with a different failure contract.
- Recoverable failure while preparing replacement maps must leave the
  attempted hub absent, existing hub descriptors/members and stored values
  intact, and the next available hub ID reusable for retry. Existing
  declarations remain registered; this does not undo caller-owned state or
  deliberate side effects of custom virtual metadata operations. It is not
  a whole-Runtime transaction or a new concurrency/reentry guarantee.
- Add focused existing-file failure/retry regressions, first reproducing the
  old appended-hub publication gap when feasible. Prefer the existing
  MSVC Debug CRT failure-probe pattern for allocation coverage, retaining its
  documented proxy-size exclusions rather than introducing production fault
  hooks or a new framework. Restore hooks before assertions or exceptions
  escape the probe.
- Verify the failed candidate's name/ID is absent, existing hub identities and
  typed member memberships/order are unchanged, earlier variable handles and
  values remain valid, and retry succeeds once with the expected ID. Include
  a late-child case so failed preparation cannot prematurely redistribute
  declarations or disturb pinned native functions. Retain existing logged
  conflict-rejection and repeat-open coverage.
- Preserve `rebuildHubMembers()`'s full temporary-map preparation and commit
  strategy. Do not remove snapshots, redesign publication, or combine a
  performance refactor with this correctness fix. Update public embedding
  documentation only for observable exception/rollback/retry behavior.
- Run the existing language/remaining Debug CTests directly through the
  workspace integration without a separate pre-test build. Rebuild and
  execute the unchanged checked Debug/Release benchmark workloads with current
  bundled DLLs to preserve the successful registration/adoption/ownership
  checks; do not claim a performance improvement from exception-only code.
  Record actual fault-probe coverage/exclusions and validation limits.
- Stop after that bounded implementation/validation step for review. The
  deferred R13 optimization and hub redesign are not automatically authorized
  by approval of the exception correction.

#### R13 exceptional-registration rollback implementation and validation

- Added only previous-count cleanup and rethrow for a non-Nomad exception
  escaping member rebuilding after the candidate hub was appended. The
  existing `NomadException` log and `HubId::INVALID` rejection are unchanged.
  Earlier validation/construction exceptions retain their existing handling.
  No success flag, guard framework, publication redesign, or performance
  optimization was introduced.
- Full temporary-map preparation and rebuilding remain unchanged. Existing
  declarations stay registered; this does not undo caller-owned state or
  deliberate side effects elsewhere. The performance deferral and the
  separately unresolved hub type/singleton redesign remain in effect.
- One focused existing-file Debug regression prepares two hubs, a root
  variable/value, a pinned two-overload native group, an ordinary nonmember
  native function, and child-qualified constant/type declarations. A successful
  control counts eligible allocations; fresh fixtures then reject each
  eligible request for unrelated and late-child registration.
- Before the fix, the regression reproduced an appended candidate remaining
  registered at the eleventh eligible allocation in the unrelated case.
  After the fix, every injected failure propagates as `std::bad_alloc` with
  the attempted name/ID absent, prior hub pointers and typed member maps/order
  intact, declarations retained, and the original variable handle/value
  preserved. Retry succeeds with the expected ID and reopening does not add
  another hub. Late-child success redistributes only the eligible declarations
  and retains explicit native ownership; failed preparation does not move them.
- The test reuses the existing MSVC Debug CRT probe. Hooks are restored before
  assertions and before the injected failure propagates. Proxy-sized requests
  remain excluded because Debug STL constructors can allocate in noexcept
  paths; same-sized real buffers are also not fault-tested. This is bounded
  allocation-failure coverage, not an all-allocation or all-exception-type
  injection guarantee. No production fault hook or new test framework was added.
- Both selected Windows Debug CTests pass: `nomad_test_language` and
  `nomad_test_remaining`, run directly without a separate pre-test build.
  Existing logged conflict rejection, late declaration/native ownership, and
  repeat-open tests remain green. No CTest entry was added.
- The unchanged opt-in benchmark builds and every checked section executes
  successfully in Debug and Release with current bundled language DLL hashes
  matched before execution. The twelve R13 empty/mixed unrelated/child cases
  retain hub/variable identities, exact memberships and overload order,
  stored values, and callback/factory checks.
- Successful-path Debug request counts are unchanged from the baseline,
  including zero for reopening. Release counts remain unavailable. Captured
  median `[min,max]` microseconds below are retained for run traceability only;
  exception-only code is not presented as a latency optimization, and no
  before/after speedup comparison or timing threshold is inferred.

| Configuration | Shape | Addition | Existing hubs | New hub (us) | Reopen (us) |
| --- | --- | --- | --- | --- | --- |
| Debug | empty | unrelated | 8 | 52.312 [47.850,58.475] | 0.274 [0.218,0.435] |
| Debug | empty | child | 8 | 59.138 [48.962,436.587] | 0.332 [0.221,0.432] |
| Debug | mixed | unrelated | 8 | 452.925 [371.613,557.862] | 0.392 [0.233,1.415] |
| Debug | mixed | child | 8 | 353.038 [299.225,829.250] | 0.339 [0.216,1.033] |
| Debug | empty | unrelated | 32 | 142.963 [95.700,155.713] | 0.833 [0.788,1.383] |
| Debug | empty | child | 32 | 140.137 [91.088,226.175] | 0.948 [0.671,1.224] |
| Debug | mixed | unrelated | 32 | 2375.275 [2003.625,2530.287] | 0.942 [0.764,1.401] |
| Debug | mixed | child | 32 | 2157.838 [2099.375,3172.000] | 0.968 [0.692,1.195] |
| Debug | empty | unrelated | 128 | 360.712 [234.700,500.012] | 3.440 [2.908,5.049] |
| Debug | empty | child | 128 | 303.850 [236.488,495.750] | 3.892 [3.537,4.399] |
| Debug | mixed | unrelated | 128 | 12152.788 [11431.638,13668.925] | 3.308 [3.214,4.228] |
| Debug | mixed | child | 128 | 13950.812 [13213.188,15291.625] | 3.936 [3.656,4.555] |
| Release | empty | unrelated | 8 | 3.750 [3.550,4.950] | 0.058 [0.058,0.162] |
| Release | empty | child | 8 | 4.162 [3.938,15.488] | 0.095 [0.072,0.127] |
| Release | mixed | unrelated | 8 | 33.763 [17.262,71.725] | 0.073 [0.048,0.194] |
| Release | mixed | child | 8 | 37.562 [26.262,219.525] | 0.058 [0.053,0.207] |
| Release | empty | unrelated | 32 | 8.975 [5.200,10.613] | 0.246 [0.185,0.409] |
| Release | empty | child | 32 | 7.037 [5.925,13.137] | 0.224 [0.184,0.233] |
| Release | mixed | unrelated | 32 | 161.137 [127.938,253.512] | 0.235 [0.191,0.240] |
| Release | mixed | child | 32 | 107.450 [101.763,162.162] | 0.223 [0.182,0.452] |
| Release | empty | unrelated | 128 | 33.175 [18.012,42.962] | 0.748 [0.657,1.290] |
| Release | empty | child | 128 | 30.375 [29.625,48.138] | 0.770 [0.680,1.017] |
| Release | mixed | unrelated | 128 | 1489.662 [1168.037,1792.338] | 0.950 [0.615,1.549] |
| Release | mixed | child | 128 | 1362.287 [1030.375,1966.362] | 0.917 [0.644,1.070] |

- SDK-backed documentation generation and strict MkDocs validation pass after
  the observable exception/retry contract update. No full 13-entry suite,
  manual gameplay, cross-platform fault test, concurrency/reentry guarantee,
  or whole-Runtime transaction is claimed.
- This completes the approved bounded correctness fix; R13's performance
  optimization stays deferred. Next reviewable scope: verify and measure C2's
  InterpreterContext variable allocations and retired-storage retention before
  proposing a storage change. Do not revive targeted hub rebuilding
  automatically.

#### C2 interpreter variable-allocation and retirement baseline

Verified production paths:

- Each owned local uses a separate `InterpreterStorage` owner and a vector of
  physical slots; nonempty values also need a value-buffer allocation. Empty
  structs are valid zero-slot bindings, not missing values.
- A same-type update reuses the existing owner and slot address, but constructs
  a vector for assignment scratch. The tested empty, integer, and string types
  require zero external scratch slots; the two-slot struct requires two.
  Type copying, assignment, and cleanup are unchanged.
- Type replacement prepares new storage and reserves `retired.size() + 1`
  before replacing the live owner. Clearing during evaluation instead appends
  the old owner with ordinary `push_back`. Retired owners are cleared only
  when the outermost evaluation exits; nested exit must not invalidate old
  borrowed views.

Bounded protocol:

- Extended only the existing opt-in benchmark; no production API, counter,
  container, scratch implementation, dependency, or CTest entry was added.
- Four shapes: empty struct, integer, string, and a two-slot integer/string
  struct. Creation batches contain 64 or 256 variables with prepared 14-byte
  names. Source values and their ownership are prepared outside measurement.
- `prepared_storage` creates retained owners in a presized output buffer;
  `new_binding` includes context name resolution, metadata publication, and
  table growth. The prepared control omits that work and cannot be subtracted
  as an exact cost decomposition.
- `same_type_update` starts with all bindings populated, performs 32 warmup
  assignments, then 4,096 updates. Sources alternate once per complete table
  pass, ending with the original source. Runtime, VM, context construction,
  source/name preparation, and output buffers are excluded. Creation has no
  warmups; retained-owner destruction is outside these timings.
- Every operation uses five fresh timing fixtures and separate fresh Debug
  allocation fixtures. Requests include Debug STL overhead, not heap byte
  volume; fractional counts are per-operation averages rounded to three
  decimals. Release allocation probes are unavailable and do not run their
  mutation callback.
- Checks cover exact type, physical size and contents, logical IDs/order,
  same-type address stability, string reference ownership, empty values,
  diagnostics/results, unchanged VM registers/stack, and final live-object
  counts.
- Retirement uses one binding and 1/16/64 mutations inside two nested public
  evaluation guards. Type changes alternate string/integer; clear/rebind
  uses distinct strings. A presized buffer captures the previous borrowed
  slot before each mutation. Per-mutation timing includes that lookup/capture;
  outermost-exit timing is the total cleanup for the batch, not per mutation.
- After inner exit, every old slot retains its contents and every prepared
  string has one context-owned reference in addition to its source owner.
  After outer exit, old strings have only their source reference and any
  current string retains its live binding reference. No private retired-vector
  introspection is used.

Windows x64 MSVC baseline, median `[min,max]` microseconds per operation:

| Configuration | Shape | Variables | Prepared storage (us) | New binding (us) | Same-type update (us) |
| --- | --- | --- | --- | --- | --- |
| Debug | empty | 64 | 0.361 [0.330,0.408] | 6.666 [4.303,9.911] | 1.360 [1.237,2.971] |
| Debug | empty | 256 | 0.309 [0.291,0.618] | 11.789 [9.846,16.003] | 4.205 [3.571,7.530] |
| Debug | integer | 64 | 0.577 [0.555,1.248] | 8.317 [5.327,12.853] | 1.728 [1.605,2.690] |
| Debug | integer | 256 | 0.560 [0.537,1.632] | 13.239 [11.621,18.955] | 4.216 [4.016,6.155] |
| Debug | string | 64 | 1.250 [0.573,1.808] | 4.691 [4.562,11.239] | 2.019 [1.454,2.587] |
| Debug | string | 256 | 1.011 [0.509,1.408] | 12.727 [12.705,27.740] | 5.026 [4.110,5.557] |
| Debug | pair | 64 | 0.644 [0.628,1.533] | 4.609 [4.491,7.223] | 2.375 [2.041,3.045] |
| Debug | pair | 256 | 0.649 [0.606,2.050] | 18.707 [11.177,33.673] | 5.544 [4.788,5.726] |
| Release | empty | 64 | 0.064 [0.031,0.089] | 0.856 [0.562,1.014] | 0.214 [0.200,0.288] |
| Release | empty | 256 | 0.023 [0.022,0.024] | 1.666 [1.564,1.695] | 0.630 [0.561,0.718] |
| Release | integer | 64 | 0.084 [0.053,0.100] | 0.691 [0.627,1.019] | 0.214 [0.193,0.267] |
| Release | integer | 256 | 0.049 [0.042,0.098] | 1.774 [1.514,3.380] | 0.771 [0.531,1.001] |
| Release | string | 64 | 0.138 [0.056,0.227] | 0.834 [0.598,1.022] | 0.257 [0.248,0.333] |
| Release | string | 256 | 0.068 [0.049,0.409] | 1.565 [1.543,2.709] | 0.654 [0.526,0.937] |
| Release | pair | 64 | 0.119 [0.089,0.181] | 0.647 [0.606,1.008] | 0.303 [0.255,0.360] |
| Release | pair | 256 | 0.086 [0.067,0.104] | 1.681 [1.591,2.778] | 0.719 [0.663,0.907] |

Debug allocation-request averages below are identical across all five
samples, so their minimum and maximum equal the shown median. Integer and
string rows have the same counts. Release counts are unavailable.

| Shape | Variables | Prepared storage | New binding | Same-type update |
| --- | --- | --- | --- | --- |
| empty | 64 | 2.000 | 6.344 | 1.000 |
| empty | 256 | 2.000 | 5.602 | 1.000 |
| integer/string | 64 | 3.000 | 7.344 | 1.000 |
| integer/string | 256 | 3.000 | 6.602 | 1.000 |
| pair | 64 | 3.000 | 7.344 | 2.000 |
| pair | 256 | 3.000 | 6.602 | 2.000 |

Retirement baseline, median `[min,max]` microseconds. Old-string counts are
the distinct source strings held by old slots before outermost exit, verified
by reference counts; all `changes` old physical slots are checked. Debug
mutation-request averages have equal median/min/max across five samples.
No allocation probe measures exit itself.

| Configuration | Mutation | Changes | Source strings | Held old strings | Mutation (us each) | Outermost exit (us total) | Requests each |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Debug | type_change | 1 | 1 | 1 | 4.100 [2.700,10.300] | 1.800 [1.300,4.300] | 4.000 |
| Debug | type_change | 16 | 9 | 8 | 2.556 [1.456,4.381] | 6.400 [5.900,8.500] | 4.000 |
| Debug | type_change | 64 | 33 | 32 | 2.642 [1.873,3.136] | 27.400 [20.500,51.000] | 4.000 |
| Debug | clear_rebind | 1 | 2 | 1 | 3.500 [3.400,4.900] | 1.900 [1.600,2.100] | 4.000 |
| Debug | clear_rebind | 16 | 17 | 16 | 1.300 [1.256,1.450] | 6.600 [5.800,6.900] | 3.500 |
| Debug | clear_rebind | 64 | 65 | 64 | 1.945 [1.233,4.559] | 28.500 [23.200,30.500] | 3.188 |
| Release | type_change | 1 | 1 | 1 | 0.700 [0.600,1.100] | 0.200 [0.200,0.300] | unavailable |
| Release | type_change | 16 | 9 | 8 | 0.400 [0.169,0.450] | 0.900 [0.500,1.200] | unavailable |
| Release | type_change | 64 | 33 | 32 | 0.202 [0.191,0.228] | 1.700 [1.500,1.700] | unavailable |
| Release | clear_rebind | 1 | 2 | 1 | 0.500 [0.300,17.200] | 0.100 [0.100,0.300] | unavailable |
| Release | clear_rebind | 16 | 17 | 16 | 0.156 [0.144,3.375] | 0.500 [0.400,1.600] | unavailable |
| Release | clear_rebind | 64 | 65 | 64 | 0.147 [0.128,0.200] | 1.900 [1.800,3.200] | unavailable |

Interpretation and validation:

- Same-type writes already preserve storage addresses; the measured remaining
  request cost is scratch construction, not another owner allocation. The
  prepared control distinguishes owning storage from binding work but does
  not isolate each metadata/name-lookup allocation.
- Type changes request retired capacity for each append; clear/rebind has
  different growth behavior. This baseline supports reviewing that cost, not
  removing lifetime retention or changing exception/publication ordering.
- Every old string reference is released at outermost exit and the current
  binding remains valid. Sources stay caller-owned, so exit does not measure
  last-reference string-payload destruction. Retired pointer-container
  capacity, peak retained heap bytes, wider values, frame-backed variables,
  and custom throwing-copy types are not measured.
- All captured timing ranges are retained, including the 17.200-us single
  clear/rebind sample. These are small, noisy host bookkeeping batches, not
  whole evaluation, native reentry, startup, gameplay, cross-platform, or
  before/after speedup evidence. No timing threshold is used.
- `nomad_test_remaining` passes through the workspace CTest integration,
  without a separate pre-test build. Existing actual native-reentry,
  exception-unwinding, failed-copy, stable-address, and empty-value regressions
  remain green. No additional unit test is needed for measurement alone.
- The extended benchmark builds and all checked sections pass in Debug and
  Release, with bundled language DLL hashes matched to the current builds
  before execution. The user's reported full rebuild/all-test run precedes
  this baseline; no assistant full-suite rerun or gameplay check is claimed.
- C2's measurement scope is complete. Next reviewable scope: review a bounded
  conditional assignment-scratch allocation or retired-capacity growth
  change, or defer further work. Any owner/buffer redesign must separately
  preserve borrowed addresses, nested lifetime, and Type failure contracts;
  no production optimization is authorized by this baseline.

#### C2 approved conditional owned-array assignment-scratch design

- The user selected `scratch_only`: replace only the temporary assignment
  vector in `InterpreterStorage::assign` with
  `std::unique_ptr<RuntimeValue[]>`. Read `getAssignmentScratchSize()` once
  and allocate cleared slots with `std::make_unique<RuntimeValue[]>` only
  when the reported count is nonzero. Pass its pointer to the existing
  `Type::assignValue`.
- Zero external scratch leaves the pointer null and uses Type's existing
  zero/single-slot behavior. Multi-slot values still receive a fresh, cleared,
  disjoint allocation on every assignment. Do not skip Type assignment for
  empty values, add an inline threshold, or cache/reuse scratch.
- Reuse the existing owned-array pattern used by the stored-field setter
  and InterpreterContext writes. Do not extract a generalized helper, change
  Type metadata or APIs, or extend this optimization to other consumers.
- Preserve exact source-size validation before scratch allocation. Storage
  owners and their physical value vectors remain unchanged, so same-type
  address stability and logical variable identity remain intact.
- Keep `Type::assignValue` unchanged: copy into scratch before releasing the
  destination, transfer completed slots, and clear scratch. Valid-metadata
  copy failures retain Type's resource-release/cleared-scratch contract and
  preserve the previous destination. Array destruction frees only memory;
  it does not replace Type-driven cleanup or roll back callback side effects.
- Required scratch allocation still precedes assignment and propagates
  failure without changing the destination. Removing the vector removes its
  bookkeeping and allocation failure points, not Type assignment calls or
  ownership semantics.
- InterpreterContext retirement, reserve-before-metadata-publication ordering,
  nested lifetime, clearing, and retained capacity stay unchanged. The fresh
  baseline does not measure capacity reuse across evaluations; retirement
  growth and owner/value-buffer redesign require a separate scope and approval.
- Expected Debug requests per same-type update are zero for the measured
  empty/integer/string values and one for the two-slot struct, rather than
  one/two. Verify those exact targets; multi-slot allocation remains and
  Release allocation counts are unavailable. No Release speedup is promised,
  particularly given I11's mixed timings with the same container change.
- Implementation adds focused allocation coverage using the existing Debug
  probe and preserves aliasing, reference ownership, failed-copy, stable
  address, and nested-reentry coverage. Run the selected remaining CTest
  directly without a separate pre-test build, then build and execute the
  unchanged checked Debug/Release benchmark. Compare same-type update metrics
  against C2's baseline; keep prepared-storage and retirement workloads
  unchanged, retain all ranges, and do not subtract unlike controls.
- This review changes only the plan, not production code. Next reviewable
  scope: implement this approved scratch-only design, record checked results,
  then stop before any further storage change.

#### C2 assignment-scratch implementation and checked comparisons

- Replaced only the local scratch vector in `InterpreterStorage::assign`
  with the approved conditional, fresh, cleared owned array. Exact source-size
  validation still precedes allocation; zero external scratch still calls
  `Type::assignValue` with null scratch. Type code and public APIs are unchanged.
- Storage owners, their value vectors, metadata/IDs, and all retirement code
  are unchanged. Array destruction releases memory only; Type retains copy,
  transfer, and cleanup responsibility and the existing copy-failure contract.
- One focused existing-probe Debug regression covers empty, integer, string,
  and two-slot integer/string bindings. Over 100 alternating updates it checks
  actual contents and address stability, then self-assignment and source
  references. Hooks are restored before assertions. Before the change it
  reproduced exactly four failures: three `100 != 0` request totals and one
  `200 != 100`. Afterward, all exact request targets and behavior checks pass.
  No new fault hook, rejection coverage, or CTest entry was added.
- `nomad_test_remaining` passes directly through the workspace CTest
  integration without a separate pre-test build. Existing failed-copy,
  empty-value, independent-snapshot, native-reentry, and nested-retirement
  coverage remains green. This is a selected test run, not a full-suite rerun.
- The unchanged opt-in benchmark builds and all checked sections execute
  successfully in Debug and Release. The bundled language DLL hashes match
  the current configuration before each execution. Release uses the existing
  multi-config build tree; no build preset or dependency change was made.
- Every measured same-type Debug update saves one request: empty/integer/string
  change from one to zero; the two-slot struct changes from two to one.
  Minimum and maximum equal the shown averages across all five samples.
  Prepared-owner/new-binding and retirement Debug counts match the baseline.
  Release allocation probes remain unavailable.

| Shape | Variables | Baseline update requests | Current update requests |
| --- | --- | --- | --- |
| empty/integer/string | 64/256 | 1.000 | 0.000 |
| pair | 64/256 | 2.000 | 1.000 |

Post-change timings below are median `[min,max]` microseconds per operation.
Run 2 is one unchanged Release repeat, without another build, retained because
run 1's update medians and several unchanged controls were higher than the
earlier baseline. No samples are discarded.

| Run | Configuration | Shape | Variables | Prepared storage (us) | New binding (us) | Same-type update (us) |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Debug | empty | 64 | 0.494 [0.466,0.656] | 6.733 [5.172,20.847] | 1.727 [1.173,2.141] |
| 1 | Debug | empty | 256 | 0.446 [0.407,1.150] | 13.691 [11.051,19.034] | 4.222 [3.775,4.769] |
| 1 | Debug | integer | 64 | 0.908 [0.866,1.609] | 8.700 [4.427,10.961] | 1.372 [1.055,1.702] |
| 1 | Debug | integer | 256 | 1.046 [0.742,1.577] | 14.326 [11.069,21.145] | 5.127 [4.452,6.733] |
| 1 | Debug | string | 64 | 1.039 [0.577,3.728] | 5.753 [5.652,9.403] | 2.164 [1.677,2.523] |
| 1 | Debug | string | 256 | 0.939 [0.879,1.559] | 14.140 [11.318,26.590] | 4.322 [3.751,5.633] |
| 1 | Debug | pair | 64 | 1.059 [1.008,3.705] | 6.130 [6.042,7.061] | 3.005 [2.448,16.768] |
| 1 | Debug | pair | 256 | 1.059 [0.977,1.095] | 12.052 [10.966,16.993] | 4.799 [4.737,5.399] |
| 1 | Release | empty | 64 | 0.083 [0.041,0.170] | 0.858 [0.789,2.286] | 0.292 [0.265,0.840] |
| 1 | Release | empty | 256 | 0.037 [0.025,0.043] | 2.972 [1.803,4.545] | 1.124 [0.949,2.719] |
| 1 | Release | integer | 64 | 0.123 [0.087,0.191] | 0.836 [0.791,1.269] | 0.436 [0.288,1.195] |
| 1 | Release | integer | 256 | 0.075 [0.051,0.108] | 3.072 [2.235,4.015] | 1.075 [0.604,1.564] |
| 1 | Release | string | 64 | 0.147 [0.073,0.208] | 0.838 [0.738,0.950] | 0.430 [0.292,0.830] |
| 1 | Release | string | 256 | 0.089 [0.050,0.098] | 2.675 [1.703,3.339] | 0.930 [0.767,1.353] |
| 1 | Release | pair | 64 | 0.094 [0.070,1.266] | 0.869 [0.717,1.056] | 0.396 [0.329,0.663] |
| 1 | Release | pair | 256 | 0.067 [0.062,0.102] | 1.779 [1.730,3.742] | 1.081 [0.747,1.901] |
| 2 | Release | empty | 64 | 0.070 [0.033,0.098] | 0.947 [0.798,1.158] | 0.344 [0.252,0.444] |
| 2 | Release | empty | 256 | 0.029 [0.022,0.066] | 1.792 [1.716,2.778] | 0.819 [0.679,1.232] |
| 2 | Release | integer | 64 | 0.078 [0.059,0.103] | 0.705 [0.683,0.812] | 0.362 [0.233,0.537] |
| 2 | Release | integer | 256 | 0.069 [0.043,0.212] | 2.758 [1.740,6.469] | 0.887 [0.675,1.060] |
| 2 | Release | string | 64 | 0.075 [0.066,0.155] | 0.714 [0.675,0.861] | 0.303 [0.236,0.457] |
| 2 | Release | string | 256 | 0.086 [0.049,0.098] | 2.487 [1.721,2.745] | 0.978 [0.700,1.274] |
| 2 | Release | pair | 64 | 0.133 [0.087,0.222] | 1.244 [0.720,2.234] | 0.480 [0.395,1.029] |
| 2 | Release | pair | 256 | 0.082 [0.063,0.129] | 1.816 [1.741,2.875] | 1.004 [0.859,2.316] |

Retirement timings are retained as unchanged-workload checks, not optimization
evidence. Old-string/source counts remain exactly those in the baseline, and
all old spans/references remain valid through nested exit and release at
outermost exit. Mutation timing includes borrowing the old slot; exit timing
is the total for the batch. No exit-allocation or retained-byte claim is made.

| Run | Configuration | Mutation | Changes | Mutation (us each) | Outermost exit (us total) |
| --- | --- | --- | --- | --- | --- |
| 1 | Debug | type_change | 1 | 5.000 [4.600,20.500] | 1.900 [1.800,6.300] |
| 1 | Debug | type_change | 16 | 2.544 [1.512,4.369] | 8.500 [6.100,10.400] |
| 1 | Debug | type_change | 64 | 3.044 [1.939,9.055] | 22.900 [21.200,30.100] |
| 1 | Debug | clear_rebind | 1 | 4.700 [3.200,5.200] | 2.000 [1.100,2.100] |
| 1 | Debug | clear_rebind | 16 | 1.319 [1.306,2.500] | 6.100 [5.900,7.100] |
| 1 | Debug | clear_rebind | 64 | 1.511 [1.156,1.830] | 25.900 [22.300,29.700] |
| 1 | Release | type_change | 1 | 0.500 [0.400,0.600] | 0.100 [0.100,0.200] |
| 1 | Release | type_change | 16 | 0.181 [0.163,0.206] | 0.500 [0.500,0.500] |
| 1 | Release | type_change | 64 | 0.252 [0.212,0.280] | 1.800 [1.700,2.200] |
| 1 | Release | clear_rebind | 1 | 0.700 [0.500,17.500] | 0.200 [0.100,0.200] |
| 1 | Release | clear_rebind | 16 | 0.175 [0.138,0.212] | 0.600 [0.500,0.800] |
| 1 | Release | clear_rebind | 64 | 0.136 [0.122,0.188] | 1.800 [1.700,3.000] |
| 2 | Release | type_change | 1 | 0.300 [0.300,0.700] | 0.100 [0.100,0.200] |
| 2 | Release | type_change | 16 | 0.181 [0.163,0.325] | 0.500 [0.500,0.900] |
| 2 | Release | type_change | 64 | 0.364 [0.214,0.889] | 2.000 [1.700,2.700] |
| 2 | Release | clear_rebind | 1 | 0.600 [0.200,6.300] | 0.100 [0.100,0.200] |
| 2 | Release | clear_rebind | 16 | 0.150 [0.138,0.163] | 0.500 [0.500,0.500] |
| 2 | Release | clear_rebind | 64 | 0.128 [0.120,0.134] | 1.800 [1.700,1.800] |

- Debug update medians are mixed: five of eight are higher than the baseline.
  Both Release runs have higher medians in all eight update cases. For example,
  integer/64 changes from 0.214 us to 0.436/0.362 us; pair/256 changes from
  0.719 us to 1.081/1.004 us. These results are not presented as a Release gain.
- Several unchanged control medians also moved. The earlier baseline and
  current executions are not a matched contemporaneous A/B comparison, so
  this does not establish the cause of the higher medians. Do not attribute
  the difference entirely to noise or claim a causal regression was isolated.
  A timing/retention decision is still appropriate before further performance
  work; no additional code strategy is automatically authorized.
- The proven result is one fewer Debug request per measured update, with
  unchanged checked behavior. Multi-slot scratch still allocates; no caller
  workload profile, peak-memory, startup, cross-platform, gameplay, or
  concurrency benefit is claimed. The benchmark and building-guide protocol
  are unchanged.
- This completes the approved implementation/verification scope. Next
  reviewable scope: review the observed Release timing trade-off and whether
  to obtain matched measurements before retaining or revising this choice.
  Do not proceed directly to retirement growth, owner/buffer redesign, or
  another optimization.

#### C2 approved matched Release timing comparison

- The timing review confirms higher update medians in all eight Release cases
  in both post-change runs. Unchanged new-binding controls also have higher
  medians in eight/seven cases, and prepared-storage controls in six/five.
  The measured Debug request reduction remains proven, but these earlier
  comparisons do not isolate the cause of the Release differences.
- The user selected `matched_release_ab` rather than retaining the cleanup
  without further timing investigation or approving an immediate rollback.
  This approves a measurement step, not another production optimization.
- Prepare isolated vector and conditional-array Release variants from the
  same current source snapshot and compiler configuration. The vector variant
  differs only in InterpreterStorage's temporary assignment-scratch
  implementation; validation, Type calls, ownership layout, and all other
  code remain the same. Do not temporarily revert the production source,
  add a permanent configuration/API, or change the active build preset.
- Use the actual InterpreterContext same-type update path in the existing
  checked benchmark, not a helper-only substitute presented as end-to-end
  evidence. Retain the empty/integer/string/pair shapes, 64/256 variables,
  prepared sources/names, 32 warmups, 4,096 alternating updates, five fresh
  samples, and unchanged controls/checks.
- Build and execute sequentially. Run two order-balanced pairs:
  vector then array, followed by array then vector. Keep variant outputs
  isolated, verify the bundled language DLL identity before each execution,
  and record compiler/configuration and source identity so the intended
  variant is demonstrably loaded.
- Retain each execution's median/min/max and all behavior checks separately.
  Do not discard slower samples, subtract unlike controls, or turn the
  unavailable Release allocation probe into measured allocation evidence.
  Order balancing reduces one comparison weakness; it does not guarantee
  elimination of shared-machine timing variation or establish significance.
- Leave production code, its existing regression, public APIs, storage
  ownership, and retirement unchanged during the experiment. No owner/buffer
  redesign, retirement-capacity change, new cache, or unrelated fix is in scope.
  Only exact experiment-owned temporary artifacts may be cleaned up.
- This step is source/evidence review and approval only; no C++ build, test,
  or benchmark rerun is performed here. Next reviewable scope: execute the
  approved matched comparison, record the results, then stop for the
  retain/revert decision before further performance work.

#### C2 matched Release comparison results

Isolation and identity:

- Copied the same current 649 tracked working files into two experiment-owned
  source snapshots, preserving the existing plan edit. The source commit is
  `bb629c24c1531f3fb48e839ee57fa51b60a3cceb`. Only the vector snapshot's
  temporary scratch implementation differs; both include groups, validation,
  Type calls, ownership layout, and all other source files are identical.
- Snapshot SHA-256:
  `fb25bfcd81006de2ad87469e8120c5a7272d22c175ef6f6c4ef5a11480f0700b`.
  Assignment source SHA-256 is
  `39164b472f93753737dc13a619fc1fdbc3f4dc80dc5d667e24ab4c32a6d59802`
  for vector and
  `9ae062aa18ef086f514a7837880923f1bad002180ac80620aae3562f3b2e42c7`
  for array. Both source trees and all production snapshot files were
  rechecked unchanged after execution, before recording these results.
- Configured separate binary trees with CMake 4.4.4, Visual Studio 17 2022
  x64, MSVC 19.44.35215.0/toolset 14.44.35207, and the same Release
  `/O2 /Ob2 /DNDEBUG` flags. Verified 101 matching cache settings and the
  full generated Release target definitions after normalizing variant roots:
  C++20, MaxSpeed optimization, and the shared CRT match.
- Reused ten existing pinned FetchContent source caches with explicit source
  overrides and separate dependency binary directories. Configuration and
  benchmark/language builds ran sequentially; no dependency installation,
  active preset change, production edit, or active-tree build was needed.
- Executed vector, array, array, vector without rebuilding between passes.
  Each executable retains all existing workloads before C2. The C2 shapes,
  64/256 variables, 14-byte prepared names, 32 update warmups, 4,096 alternating
  updates, five fresh samples, controls, and checks remain unchanged.
- Before every execution, the bundled DLL hash matches its isolated build.
  The running process's loaded module path and hash are also verified.
  Vector DLL SHA-256:
  `e8345d3393ab2297e8a2d2adef54fec4cf69a3ce17320a35fed3135b2f2af25a`.
  Array DLL SHA-256:
  `5216ff6352f7ed76ca76290d2dfc6e982e08cb79020a6e4857e2e86b8ed4c1e9`.
  Each variant uses the same executable/DLL on its repeat; processes do not
  overlap. All four exit successfully with empty stderr.

All storage results below are Release median `[min,max]` microseconds per
operation, retained individually in execution order. Allocation probes are
unavailable; this experiment adds no Release allocation evidence.

| Sequence | Variant | Shape | Variables | Prepared storage (us) | New binding (us) | Same-type update (us) |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | vector | empty | 64 | 0.027 [0.025,0.039] | 0.759 [0.536,0.800] | 0.198 [0.194,0.263] |
| 1 | vector | empty | 256 | 0.022 [0.021,0.037] | 1.592 [1.527,2.605] | 0.772 [0.521,0.871] |
| 1 | vector | integer | 64 | 0.066 [0.052,0.116] | 0.608 [0.580,0.641] | 0.223 [0.191,0.238] |
| 1 | vector | integer | 256 | 0.047 [0.044,0.059] | 1.843 [1.582,2.030] | 0.552 [0.526,0.590] |
| 1 | vector | string | 64 | 0.056 [0.053,0.070] | 0.620 [0.583,0.755] | 0.211 [0.196,0.258] |
| 1 | vector | string | 256 | 0.050 [0.048,0.098] | 1.590 [1.546,2.594] | 0.787 [0.561,0.901] |
| 1 | vector | pair | 64 | 0.098 [0.077,0.102] | 0.802 [0.648,0.903] | 0.323 [0.295,0.343] |
| 1 | vector | pair | 256 | 0.070 [0.064,0.103] | 1.759 [1.539,2.586] | 0.772 [0.600,0.857] |
| 2 | array | empty | 64 | 0.041 [0.025,0.044] | 0.772 [0.717,1.098] | 0.305 [0.251,0.511] |
| 2 | array | empty | 256 | 0.038 [0.032,0.083] | 2.504 [2.441,2.772] | 0.856 [0.832,1.475] |
| 2 | array | integer | 64 | 0.048 [0.047,0.064] | 0.680 [0.652,0.923] | 0.250 [0.235,0.307] |
| 2 | array | integer | 256 | 0.047 [0.044,0.076] | 1.845 [1.696,4.348] | 0.962 [0.609,1.533] |
| 2 | array | string | 64 | 0.058 [0.052,0.087] | 0.727 [0.673,0.834] | 0.259 [0.244,0.402] |
| 2 | array | string | 256 | 0.050 [0.047,0.134] | 1.988 [1.715,2.529] | 0.831 [0.612,1.093] |
| 2 | array | pair | 64 | 0.073 [0.069,0.075] | 0.741 [0.695,1.122] | 0.333 [0.291,0.627] |
| 2 | array | pair | 256 | 0.071 [0.070,0.940] | 1.861 [1.750,1.879] | 0.757 [0.722,0.980] |
| 3 | array | empty | 64 | 0.083 [0.047,0.161] | 0.800 [0.731,1.725] | 0.278 [0.265,0.484] |
| 3 | array | empty | 256 | 0.037 [0.025,0.042] | 2.404 [1.693,2.556] | 0.983 [0.688,1.594] |
| 3 | array | integer | 64 | 0.153 [0.087,0.222] | 1.509 [0.809,2.228] | 0.456 [0.260,0.846] |
| 3 | array | integer | 256 | 0.050 [0.043,0.107] | 1.858 [1.653,4.279] | 0.816 [0.630,0.986] |
| 3 | array | string | 64 | 0.128 [0.058,0.183] | 0.848 [0.675,0.861] | 0.270 [0.255,0.390] |
| 3 | array | string | 256 | 0.055 [0.048,0.084] | 1.868 [1.696,3.775] | 0.839 [0.632,0.882] |
| 3 | array | pair | 64 | 0.116 [0.102,0.205] | 1.231 [0.848,1.295] | 0.433 [0.374,0.578] |
| 3 | array | pair | 256 | 0.095 [0.070,0.168] | 2.476 [1.751,3.458] | 0.878 [0.817,1.186] |
| 4 | vector | empty | 64 | 0.041 [0.037,0.086] | 0.773 [0.750,1.144] | 0.263 [0.247,0.296] |
| 4 | vector | empty | 256 | 0.023 [0.021,0.114] | 1.548 [1.531,1.898] | 0.543 [0.517,0.882] |
| 4 | vector | integer | 64 | 0.081 [0.075,0.089] | 0.792 [0.780,0.833] | 0.290 [0.259,0.407] |
| 4 | vector | integer | 256 | 0.087 [0.041,0.107] | 2.462 [1.549,2.950] | 0.778 [0.525,1.512] |
| 4 | vector | string | 64 | 0.056 [0.048,0.092] | 0.808 [0.572,1.458] | 0.263 [0.193,0.579] |
| 4 | vector | string | 256 | 0.084 [0.052,0.094] | 1.778 [1.538,3.331] | 0.854 [0.536,0.973] |
| 4 | vector | pair | 64 | 0.103 [0.097,0.128] | 0.895 [0.720,2.952] | 0.356 [0.324,0.651] |
| 4 | vector | pair | 256 | 0.082 [0.066,0.124] | 1.607 [1.582,3.090] | 0.977 [0.723,1.093] |

Unchanged retirement controls, Release median `[min,max]` microseconds:

| Sequence | Variant | Mutation | Changes | Mutation (us each) | Outermost exit (us total) |
| --- | --- | --- | --- | --- | --- |
| 1 | vector | type_change | 1 | 0.400 [0.300,0.700] | 0.100 [0.100,0.200] |
| 1 | vector | type_change | 16 | 0.188 [0.175,0.194] | 0.500 [0.500,0.600] |
| 1 | vector | type_change | 64 | 0.216 [0.214,0.681] | 1.700 [1.600,2.100] |
| 1 | vector | clear_rebind | 1 | 0.500 [0.400,14.900] | 0.100 [0.100,0.100] |
| 1 | vector | clear_rebind | 16 | 0.138 [0.131,0.156] | 0.500 [0.400,0.600] |
| 1 | vector | clear_rebind | 64 | 0.170 [0.166,0.287] | 3.000 [2.900,3.200] |
| 2 | array | type_change | 1 | 0.300 [0.300,0.600] | 0.100 [0.100,0.200] |
| 2 | array | type_change | 16 | 0.219 [0.206,0.237] | 0.700 [0.700,0.900] |
| 2 | array | type_change | 64 | 0.273 [0.264,0.366] | 2.900 [2.800,3.400] |
| 2 | array | clear_rebind | 1 | 0.700 [0.500,26.900] | 0.300 [0.100,0.400] |
| 2 | array | clear_rebind | 16 | 0.194 [0.181,0.219] | 0.800 [0.800,0.800] |
| 2 | array | clear_rebind | 64 | 0.172 [0.163,0.189] | 2.900 [2.900,3.100] |
| 3 | array | type_change | 1 | 0.300 [0.200,0.500] | 0.100 [0.000,0.100] |
| 3 | array | type_change | 16 | 0.250 [0.188,0.281] | 0.600 [0.600,0.900] |
| 3 | array | type_change | 64 | 0.228 [0.212,0.261] | 1.700 [1.700,1.900] |
| 3 | array | clear_rebind | 1 | 0.500 [0.100,9.300] | 0.100 [0.000,0.200] |
| 3 | array | clear_rebind | 16 | 0.150 [0.131,0.156] | 0.500 [0.400,0.500] |
| 3 | array | clear_rebind | 64 | 0.131 [0.120,0.203] | 1.700 [1.700,1.900] |
| 4 | vector | type_change | 1 | 0.600 [0.300,0.900] | 0.200 [0.100,0.400] |
| 4 | vector | type_change | 16 | 0.225 [0.212,0.256] | 0.800 [0.800,0.900] |
| 4 | vector | type_change | 64 | 0.358 [0.273,0.525] | 3.100 [2.900,3.400] |
| 4 | vector | clear_rebind | 1 | 0.500 [0.300,9.600] | 0.100 [0.100,0.100] |
| 4 | vector | clear_rebind | 16 | 0.194 [0.181,0.200] | 0.800 [0.700,0.800] |
| 4 | vector | clear_rebind | 64 | 0.170 [0.164,0.205] | 3.000 [3.000,3.000] |

Interpretation and closeout:

- Pair 1 compares sequence 2 array with sequence 1 vector; pair 2 compares
  sequence 3 array with sequence 4 vector. Update medians are higher for
  array in seven/eight and six/eight cases. Pair/256 is lower for array in
  both pairs: 0.757 versus 0.772 us, and 0.878 versus 0.977 us. No consistent
  overall Release latency improvement is demonstrated.
- Unchanged new-binding controls have higher array medians in seven/eight
  cases in both pairs. Prepared-owner controls are mixed, and retirement
  timing also varies. These controls do not justify subtracting an exact
  scratch cost or dismissing update differences entirely as noise.
- The comparison fixes the stale-baseline and one-way ordering weaknesses.
  It still uses small batches on a shared machine, separate DLL images, and
  two execution pairs, not a statistically established performance result.
  Matching source/settings and checking loaded modules do not isolate the
  instruction-level cause of higher update timings.
- All checked workloads pass: actual values, logical IDs, borrowed-address
  stability, source ownership, unchanged VM state, nested old-slot/string
  retention, outermost reference release, and final live-object counts remain
  valid. Old-string/source counts exactly match the earlier baseline.
  No samples are discarded, no timing threshold is applied, and no
  last-reference payload destruction, retained-byte, or Release allocation
  claim is made.
- No production or permanent benchmark code changed, no new test was added,
  and no CTest/full-suite/gameplay/cross-platform run is claimed here. This
  experiment's validation is the isolated Release builds and all four
  complete checked benchmark executions. The earlier Debug allocation
  reduction remains separate evidence.
- This completes the approved measurement scope. Next reviewable scope:
  decide whether to retain the allocation cleanup despite the unproven
  Release benefit or revert it. No rollback, third implementation strategy,
  retirement growth, or owner/buffer redesign is authorized automatically.

#### C2 approved assignment-scratch rollback

- The user selected `revert_scratch_vector` after reviewing the matched
  Release results. The proven one-request Debug reduction does not establish
  a Release benefit, and most matched update medians were higher for array.
  Controls also varied, so restoring the earlier implementation is a
  conservative choice rather than a claim of an isolated causal regression.
- Restore only the temporary vector in `InterpreterStorage::assign`, using
  `getAssignmentScratchSize()` and passing null when scratch is empty.
  Keep exact source-size validation and the existing `Type::assignValue`
  call. Remove the array-only include if no longer needed; do not revert a
  whole commit or other consumers of the conditional-array pattern.
- Remove only the added
  `interpreter_same_type_updates_allocate_only_required_assignment_scratch`
  test, whose allocation targets belong to the abandoned optimization.
  Do not weaken its expected counts merely to make the vector pass or remove
  existing storage, aliasing, failed-copy, snapshot, or reentry coverage.
  Clean directly unnecessary includes without unrelated formatting.
- Restore the prior scratch-container allocation behavior, including its
  Debug bookkeeping requests and possible allocation failure points.
  Type copying, cleanup, source aliasing, stable storage addresses, variable
  IDs, owned value buffers, and all retirement/publication behavior stay
  unchanged. No new strategy, scratch reuse, cache, or lifetime restriction
  is approved.
- Keep the permanent benchmark workloads, building guide, and all baseline,
  array, and matched-comparison results. Run the selected remaining CTest
  directly without a separate pre-test build, then build and execute the
  unchanged checked Debug/Release benchmark. Verify the restored measured
  Debug update counts of one for empty/integer/string and two for the pair,
  while retaining all timing ranges and unchanged lifetime checks.
- Record the rollback validation without promising a speedup relative to
  earlier or matched runs. A full-suite, gameplay, cross-platform, or
  isolated timing-causality claim is not implied by this scope.
- This step records the decision only; production code remains unchanged
  here. Next reviewable scope: implement and validate the approved rollback,
  then stop before resuming other performance work. Retirement growth and
  owner/value-buffer changes remain unapproved.

#### C2 assignment-scratch rollback implementation and validation

- Restored only the temporary `std::vector<RuntimeValue>` in
  `InterpreterStorage::assign`, sized by `getAssignmentScratchSize()`.
  Empty scratch passes null to the existing `Type::assignValue` call.
  Exact source-size validation still precedes scratch construction.
  Replaced the now-unused memory include with the directly used vector include.
- Removed only
  `interpreter_same_type_updates_allocate_only_required_assignment_scratch`,
  whose zero/one-request targets belonged to the abandoned optimization.
  Existing tests still use InterpreterContext and vectors, so those includes
  remain. No assertion was weakened or unrelated test removed.
- Type copying, failure cleanup, destination preservation, aliasing,
  physical-storage addresses, logical variable IDs, owned buffers, and all
  retirement/publication code remain unchanged. Other conditional-array
  scratch consumers are untouched. Prior allocation failure points and Debug
  container overhead are restored as approved.
- `nomad_test_remaining` passes through the workspace CTest integration,
  without a separate pre-test build. Existing empty-value, stable-address,
  independent-snapshot, failed-copy, native-reentry, and nested-retirement
  coverage remains green. No new CTest entry was added.
- The unchanged opt-in benchmark builds and all checked sections execute
  successfully in Debug and Release, sequentially, with bundled language
  DLL hashes matched to the current configuration before execution.
  The benchmark, building guide, and all earlier baseline/array/matched
  comparison evidence are retained.
- All measured Debug requests match the original baseline across the eight
  storage cases and six retirement cases. Restored same-type requests are
  one for empty/integer/string and two for the pair; minimum and maximum
  equal the shown median across five fresh samples. Release allocation probes
  remain unavailable. Retirement source/old-string counts also match.

| Shape | Variables | Restored Debug requests per update |
| --- | --- | --- |
| empty/integer/string | 64/256 | 1.000 |
| pair | 64/256 | 2.000 |

Rollback storage timings, median `[min,max]` microseconds per operation,
using the unchanged C2 protocol:

| Configuration | Shape | Variables | Prepared storage (us) | New binding (us) | Same-type update (us) |
| --- | --- | --- | --- | --- | --- |
| Debug | empty | 64 | 0.362 [0.330,0.500] | 5.322 [4.292,16.117] | 2.481 [1.332,4.209] |
| Debug | empty | 256 | 0.345 [0.306,0.678] | 12.544 [10.561,13.651] | 4.719 [4.245,5.525] |
| Debug | integer | 64 | 1.078 [0.547,1.178] | 5.595 [4.720,21.066] | 1.958 [1.603,2.495] |
| Debug | integer | 256 | 1.042 [0.527,2.090] | 14.502 [10.617,32.587] | 5.067 [4.452,6.251] |
| Debug | string | 64 | 1.167 [0.969,1.450] | 5.845 [5.816,7.888] | 2.258 [1.629,2.581] |
| Debug | string | 256 | 0.887 [0.730,1.270] | 11.537 [11.234,21.076] | 4.620 [4.022,5.210] |
| Debug | pair | 64 | 1.128 [0.734,1.770] | 6.031 [5.780,8.963] | 2.997 [2.887,3.229] |
| Debug | pair | 256 | 1.000 [0.983,1.155] | 14.710 [11.346,46.686] | 5.325 [5.310,6.631] |
| Release | empty | 64 | 0.041 [0.025,0.056] | 1.414 [0.597,116.223] | 0.360 [0.185,1.213] |
| Release | empty | 256 | 0.042 [0.039,0.044] | 3.034 [2.620,15.089] | 1.020 [0.685,6.167] |
| Release | integer | 64 | 0.081 [0.062,0.092] | 0.802 [0.616,0.866] | 0.304 [0.264,1.224] |
| Release | integer | 256 | 0.095 [0.075,0.205] | 3.052 [2.497,3.660] | 1.227 [0.694,3.001] |
| Release | string | 64 | 0.059 [0.050,0.070] | 0.692 [0.592,1.252] | 0.254 [0.214,0.448] |
| Release | string | 256 | 0.102 [0.053,0.104] | 3.116 [1.550,3.336] | 1.084 [0.873,1.139] |
| Release | pair | 64 | 0.106 [0.086,0.144] | 0.822 [0.730,1.434] | 0.344 [0.335,0.485] |
| Release | pair | 256 | 0.098 [0.070,0.116] | 1.744 [1.599,5.202] | 0.814 [0.772,1.162] |

Unchanged retirement checks and timings, median `[min,max]` microseconds.
Mutation time includes borrowing the old slot; outermost-exit time is the
total for the batch. Debug request minimum/maximum equal the shown average.
No exit-allocation or retained-byte measurement is added.

| Configuration | Mutation | Changes | Source strings | Held old strings | Mutation (us each) | Outermost exit (us total) | Requests each |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Debug | type_change | 1 | 1 | 1 | 5.200 [3.800,51.800] | 1.900 [1.600,4.100] | 4.000 |
| Debug | type_change | 16 | 9 | 8 | 1.531 [1.469,42.119] | 8.200 [5.900,218.200] | 4.000 |
| Debug | type_change | 64 | 33 | 32 | 2.095 [1.936,7.291] | 22.700 [21.500,27.100] | 4.000 |
| Debug | clear_rebind | 1 | 2 | 1 | 4.700 [3.700,4.900] | 1.800 [1.400,1.900] | 4.000 |
| Debug | clear_rebind | 16 | 17 | 16 | 2.163 [1.394,2.281] | 8.000 [6.500,9.500] | 3.500 |
| Debug | clear_rebind | 64 | 65 | 64 | 1.952 [1.367,9.931] | 30.500 [26.800,54.200] | 3.188 |
| Release | type_change | 1 | 1 | 1 | 0.400 [0.400,0.500] | 0.100 [0.100,0.200] | unavailable |
| Release | type_change | 16 | 9 | 8 | 0.181 [0.163,2.744] | 0.500 [0.400,1.900] | unavailable |
| Release | type_change | 64 | 33 | 32 | 0.234 [0.198,0.283] | 1.800 [1.600,2.000] | unavailable |
| Release | clear_rebind | 1 | 2 | 1 | 0.400 [0.200,12.600] | 0.100 [0.100,0.200] | unavailable |
| Release | clear_rebind | 16 | 17 | 16 | 0.144 [0.131,0.281] | 0.500 [0.400,0.900] | unavailable |
| Release | clear_rebind | 64 | 65 | 64 | 0.136 [0.136,0.319] | 1.900 [1.700,4.500] | unavailable |

- Checked values, source references, same-type addresses, binding IDs, VM
  state, old borrowed spans, nested reference retention, outermost release,
  current binding survival, and final live-object counts all remain valid.
  Sources are caller-owned, so last-reference payload destruction is not
  measured.
- All timing ranges are retained, including the 116.223-us Release
  new-binding sample and the 218.200-us Debug retirement-exit sample.
  This is rollback behavior/count validation, not another matched A/B
  comparison or evidence that the rollback improves Release speed.
- The selected test and checked builds/executions validate this bounded
  change. No full-suite rerun, gameplay, cross-platform, concurrency,
  retained-byte benefit, or isolated timing-causality claim is made.
- The approved scratch experiment closes with vector restored. Further
  C2 owner/value-buffer or retirement-capacity work remains unapproved.
  Next reviewable scope: verify and measure I5/Q2's current dynamic-format
  parsing/evaluation costs before discussing a compiled-segment cache.
  Do not implement caching or other storage changes automatically.

#### I5/Q2 dynamic-format and fresh-expression baseline

Verified current path:

- `Interpreter::formatString` scans the complete template on every call,
  resets transient evaluation state, and evaluates each placeholder against
  live bindings. Each placeholder creates a Compiler and CompilerContext,
  tokenizes one expression, parses/resolves it, evaluates an owned value,
  converts complete storage to text, and releases temporary state.
- Compiled `$template` uses the same formatter with a fresh frame-backed
  InterpreterContext that owns copied callable parameter/local metadata.
  The measured script has three parameters and no local variables. The
  host path instead uses a pre-existing context with two owned variables.
  Their complete call costs differ; no exact frame-copy cost is inferred.
- Runtime still runs registered Compiler setup callbacks when creating a
  Compiler. This fixture adds no custom setup callbacks. No Compiler,
  token stream, AST, or resolved-expression cache is introduced.

Bounded protocol:

- Extended only the existing opt-in benchmark, plus its guide and this plan.
  Production code, public APIs, CMake targets/dependencies, and the prior
  C2 rollback remain unchanged. No new unit test or CTest entry was added.
- Six templates: empty, `static text`, `{value}`, `{value}/{value}`,
  `{value + 7}`, and `{text}`. Their lengths are 0/11/7/15/11/6 bytes;
  placeholder counts are 0/0/1/2/1/1. Integers alternate between 17 and 29;
  string inputs alternate between two distinct 128-byte values.
- Report `template_scan`, `host_format`, `compiled_dynamic`, and
  `compiled_plain` separately. The compiled paths use the checked
  Interpreter host wrapper with prepared owned arguments; the plain
  function returns the template without formatting. Scanning retains its
  returned segment container. These are reduced-work controls, not a
  subtractive decomposition of one call.
- Five fresh fixtures per case/configuration use 32 warmups, 512 timed calls,
  and separate fresh 128-call Debug allocation fixtures. Runtime, compilation,
  VM/context/source/argument preparation, live-binding changes, checks, and
  whole-call returned-value destruction are outside measurement.
- Each timed invocation uses its own clock pair. Small controls and phases
  include clock/wrapper overhead; no overhead subtraction or timing threshold
  is applied. Debug probes count current-thread allocation/reallocation
  requests, including Debug-library bookkeeping, not retained byte volume.
  Release probes are unavailable and do not invoke their callback.
- A separate fresh host expression pipeline follows the existing public
  operations for `value`, `value + 7`, and `text`: source copy,
  Compiler plus CompilerContext construction, tokenization/line selection,
  AST parsing/end-of-line validation, resolution/diagnostic transfer,
  evaluation with register restoration, text conversion, and grouped cleanup.
  Expression lengths are 5/9/4 bytes with 1/3/1 tokens.
- The staged pipeline has the same sample/warmup/iteration/probe counts.
  It never reuses an AST. Binding changes, the outer evaluation guard, and
  checks are excluded. Cleanup includes value, AST, tokenizer, compiler
  context, Compiler, source, and transient evaluation-state release.
  It is not instrumentation inside the private formatter and omits full
  template scanning/assembly and frame-context construction. Do not sum or
  subtract these medians as an exact full-call cost breakdown.
- Checks verify exact current outputs, segment contents/types/byte positions,
  expression types/tokens, source reference ownership, stable binding
  addresses/IDs, unchanged VM registers/stack, no added callable/instruction
  registrations, clean transient state, and final live-object counts.
  Empty templates use the process-lifetime shared empty string, so reference
  conservation is checked against its captured starting count.

Windows x64 MSVC full-call baseline, median `[min,max]` microseconds:

| Configuration | Shape | Template scan (us) | Host format (us) | Compiled dynamic (us) | Compiled plain (us) |
| --- | --- | --- | --- | --- | --- |
| Debug | empty | 1.534 [0.883,2.007] | 4.199 [2.236,4.891] | 36.734 [24.078,39.362] | 5.263 [4.612,6.272] |
| Debug | literal | 2.224 [1.701,3.955] | 5.162 [3.278,8.737] | 39.525 [32.757,45.626] | 5.231 [3.146,7.746] |
| Debug | identifier | 3.688 [2.490,5.602] | 297.536 [286.868,368.516] | 345.342 [300.705,371.751] | 4.483 [3.225,6.043] |
| Debug | repeated | 9.581 [5.798,12.130] | 598.323 [533.167,667.847] | 606.735 [577.793,697.702] | 4.700 [3.242,7.496] |
| Debug | arithmetic | 3.755 [2.551,5.710] | 335.376 [307.738,370.815] | 376.969 [340.400,425.681] | 4.115 [3.372,6.253] |
| Debug | reference | 4.183 [2.383,6.839] | 317.513 [237.938,324.372] | 367.856 [327.469,399.412] | 3.570 [3.399,6.468] |
| Release | empty | 0.023 [0.021,0.023] | 0.066 [0.064,0.067] | 0.747 [0.744,0.791] | 0.326 [0.313,0.334] |
| Release | literal | 0.079 [0.055,0.118] | 0.168 [0.115,0.366] | 1.172 [0.864,3.233] | 0.471 [0.357,0.904] |
| Release | identifier | 0.137 [0.089,0.216] | 7.850 [5.162,8.910] | 10.713 [8.170,11.258] | 0.370 [0.326,0.668] |
| Release | repeated | 0.289 [0.225,0.874] | 11.912 [10.135,13.579] | 16.105 [12.623,18.523] | 0.464 [0.425,2.025] |
| Release | arithmetic | 0.150 [0.146,0.228] | 7.587 [5.918,8.915] | 8.232 [7.354,11.323] | 0.458 [0.318,0.512] |
| Release | reference | 0.123 [0.093,0.141] | 5.563 [4.595,6.255] | 8.796 [6.357,10.399] | 0.357 [0.310,0.627] |

Debug requests per full call. All five samples have equal median/min/max
for each reported count; Release counts are unavailable.

| Shape | Template scan | Host format | Compiled dynamic | Compiled plain |
| --- | --- | --- | --- | --- |
| empty | 4 | 8 | 61 | 4 |
| literal | 7 | 11 | 67 | 4 |
| identifier | 9 | 212 | 268 | 4 |
| repeated | 20 | 422 | 478 | 4 |
| arithmetic | 9 | 246 | 302 | 4 |
| reference | 9 | 213 | 270 | 4 |

Fresh expression phases, median `[min,max]` microseconds per placeholder:

| Configuration | Shape | Source copy (us) | Compiler/context (us) | Tokenize (us) | Parse (us) |
| --- | --- | --- | --- | --- | --- |
| Debug | identifier | 2.645 [2.363,11.303] | 47.634 [40.474,50.695] | 165.891 [155.485,190.412] | 19.050 [18.721,21.106] |
| Debug | arithmetic | 2.606 [2.190,2.882] | 44.143 [39.614,52.862] | 197.171 [185.236,224.979] | 25.622 [21.641,27.926] |
| Debug | reference | 2.183 [1.987,2.457] | 42.736 [39.666,44.787] | 157.345 [146.624,172.022] | 19.075 [17.138,21.614] |
| Release | identifier | 0.057 [0.053,0.112] | 1.405 [1.275,2.591] | 2.408 [1.811,4.342] | 0.775 [0.733,1.559] |
| Release | arithmetic | 0.072 [0.051,0.130] | 1.533 [1.312,3.131] | 2.927 [1.982,5.223] | 1.474 [0.872,2.051] |
| Release | reference | 0.062 [0.052,0.075] | 1.663 [1.260,2.098] | 2.431 [1.883,2.826] | 0.910 [0.733,1.136] |

| Configuration | Shape | Resolve (us) | Evaluate (us) | Convert (us) | Cleanup (us) |
| --- | --- | --- | --- | --- | --- |
| Debug | identifier | 5.172 [4.023,5.601] | 6.317 [5.804,7.295] | 3.299 [3.150,4.425] | 26.384 [20.443,29.081] |
| Debug | arithmetic | 5.643 [5.186,9.340] | 10.321 [9.763,10.829] | 3.304 [2.898,3.837] | 23.599 [21.241,27.282] |
| Debug | reference | 4.516 [4.149,4.862] | 6.630 [6.074,7.195] | 3.735 [2.973,4.183] | 23.461 [20.507,28.445] |
| Release | identifier | 0.177 [0.165,0.310] | 0.233 [0.130,0.252] | 0.042 [0.041,0.090] | 0.680 [0.657,1.408] |
| Release | arithmetic | 0.251 [0.185,0.508] | 0.322 [0.256,0.629] | 0.065 [0.037,0.124] | 0.864 [0.684,1.655] |
| Release | reference | 0.206 [0.148,0.258] | 0.190 [0.140,0.252] | 0.102 [0.076,0.148] | 0.941 [0.682,1.111] |

Debug phase requests, with equal median/min/max across five samples:

| Shape | Source copy | Compiler/context | Tokenize | Parse | Resolve | Evaluate | Convert | Cleanup |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| identifier | 5 | 98 | 67 | 23 | 2 | 4 | 3 | 0 |
| arithmetic | 5 | 98 | 88 | 31 | 2 | 9 | 3 | 0 |
| reference | 5 | 98 | 67 | 23 | 2 | 4 | 3 | 0 |

Interpretation and validation:

- Tokenization has the largest staged median in every expression/configuration,
  followed by Compiler/Context preparation. Release tokenization is
  2.408-2.927 us versus 1.405-1.663 us for preparation. This identifies
  candidates for review, not a promised cache speedup or exact percentage of
  the full production call.
- Template scanning is much smaller than full placeholder formatting in
  these cases. A cache of template segments alone would not remove the
  repeated compiler/tokenizer/parser pipeline. Caching bound expressions or
  reusing Compiler setup would have different lifetime and behavioral
  implications and is not approved by these measurements.
- The historical Q2 note recorded approximate Debug plain/literal/two-value
  call totals of 5-7/33/650 us and attributed about 28 us to context copying.
  That earlier fixture is not this protocol; the current baseline does not
  isolate an exact context-copy cost or establish a before/after speedup.
- Only successful simple expressions and same-type binding changes were
  timed. Type replacement, native calls, receiver/closure contexts, custom
  compiler setup, diagnostics/error recovery, metadata growth, wider values,
  nested formatting/reentry, cache hit/miss behavior, and retained memory
  are not measured. Existing format/ownership regression tests remain intact.
- Two initial harness errors (unsupported function return annotations and
  a fresh-reference assumption for the shared empty string) were corrected
  before any I5 result rows were emitted. No engine/parser change or
  optimization was needed to make the checks pass.
- Both selected Debug CTests pass: `nomad_test_language` and
  `nomad_test_remaining`, run directly without a separate pre-test build.
  The final extended benchmark builds and all earlier/new sections execute
  successfully in Debug and Release with current bundled DLL hashes matched.
  No full-suite rerun, gameplay, cross-platform, or cache performance claim
  is made; all captured ranges remain visible.
- I5/Q2's measurement scope is complete. Next reviewable scope: discuss
  bounded placeholder-tokenization or Compiler-preparation reuse, broader
  bound-expression caching, or deferral. Preserve live bindings, mutable
  metadata, setup-callback semantics, diagnostic coordinates, and ownership;
  do not freeze declarations or add language restrictions to simplify a
  cache. No implementation is authorized before that design decision.

#### I5/Q2 approved generic lexical-grammar reuse prototype

- The user selected `thread_local_grammar`. Prototype reuse of the existing
  const `LineGrammar<NomadString::const_iterator>` in
  `Tokenizer::tokenizeLines` with function-local static thread-local storage,
  rather than rebuilding it for every Tokenizer. This applies generically
  to all Tokenizer callers, not just dynamic formatting.
- The existing grammar contains fixed lexical rules and actions that
  construct Token values. It does not capture a Runtime, Compiler,
  InterpreterContext, source, or resolved declaration. Reuse the same grammar
  type/rules; do not add a new grammar abstraction, configuration, public API,
  tokenized-source cache, AST cache, or format-specific compiler rule.
- Keep input iterators, output tokens, columns, line buffers, source names,
  diagnostics, and parser cursors local to each Tokenizer/call. Current
  malformed-token reporting still targets that Tokenizer's CompilerContext.
  Parsing, resolution, evaluation, live types/bindings, and metadata lookup
  remain fresh; no declaration freeze or new language restriction is approved.
- Use a const grammar per thread, not one shared mutable process-wide parser.
  Do not add synchronization or mutable parse state to the grammar. Preserve
  same-thread reentry and independent source/context state across threads.
  Context-owned tokens and ordinary Type-driven value ownership stay unchanged.
- This intentionally retains one fixed grammar until its thread exits,
  including after any particular Runtime or context is destroyed.
  First-use construction is not eliminated; do not promise startup or
  retained-memory improvement. Keep initialization failures visible and
  introduce no fallback/catch framework. Successful later calls naturally
  avoid reconstruction-related allocation failure points.
- Leave Compiler creation and setup callbacks unchanged. Reusing a Compiler
  could change callback frequency and carry preparation state between
  placeholders; that separate behavior is not approved by this prototype.
  Runtime, Interpreter, CompilerContext, and parser APIs remain unchanged.
- The existing tokenization phase includes grammar construction, source
  splitting, scanning, and token storage/copies. Its measurements do not
  isolate the grammar-construction share. No precise saving or Release
  improvement is promised; compare actual post-change tokenization and
  full host/compiled formatting results, not an assumed percentage.
- Add focused coverage through existing public APIs and the existing Debug
  allocation probe: reproduce repeated construction cost before the change,
  check cold versus warm tokenization on an isolated thread, and preserve
  token contents, independent source/diagnostic coordinates, and thread/context
  isolation. Retain existing lexical, malformed/nested-format, live-binding,
  and reentry regressions. Do not add production inspection or fault APIs.
- Run the selected language/remaining CTests directly without a separate
  pre-test build, then build and execute the unchanged checked Debug/Release
  benchmark sequentially. Keep all ranges, request counts, first-use/lifetime
  limitations, and existing controls. The format baseline remains the
  reference; no token/AST caching or Compiler reuse may be added implicitly.
- This review edits only the plan. Next reviewable scope: implement and
  validate the approved grammar-storage-duration prototype, then stop for
  retention review before any other preparation or caching change.

## Corrections to earlier review notes

- R25's alleged size conversion is not present: `NomadIndex` is `std::size_t`. Its lookup-result revision is implemented separately.
- V14's accessor-asymmetry finding is obsolete: the current function-variable get/set APIs are already symmetric. Only its remaining parameter/local naming required changes.
- R9 said three Runtime accessors have no callers. They have test-only callers, so they move to step 2.
- `getIntegerEnclosingParameter` was treated as production-used. It is test-only (`ObjectConstructionTest.cpp`), so it moves to step 2.
- I14 is a redundancy finding, not dead code. `formatDynamicString` had production callers in `Expression.cpp` and `OpCode.cpp`. Under D4 it is removed, and those callers are migrated.
- V12 flagged missing null checks in `callNativeFunction`. Under D3 the Compiler owns native ID validity, so the VM no longer checks it. The host-facing null-result check in `createNativeObject` stays.
- R29 is dismissed. `EventType` holds a copy of `EventDefinition`, so `&m_events.back()` does not dangle.
- Recursion is legal. `Compiler::isRecursiveFunctionCall` only rejects recursive return-type inference, so it is not a bug.

## Open questions

- **Q1 (resolved):** The debug-only kind check was removed. The accessors are `getUncheckedNative*`, inline in `Runtime.hpp`.
- **Q2 (step 8, deferred):** The generic per-thread fixed lexical-grammar reuse prototype remains approved but unimplemented; performance work is skipped for now. Explicitly agree to resume before implementation/retention review. Token/AST caching and Compiler reuse remain unapproved.
- **Q3 (step 4):** Resolved. `InterpreterContext` reads the frame indexes through the read-only `getParameterIndex()` and `getVariableIndex()` accessors, and the `friend` declaration is removed.
- **Q4 (open):** Should the Interpreter or VM reject a runtime whose script compilation did not succeed? The standing rule says script initialization is a host precondition and the VM does not validate readiness. Confirm that rule or change it explicitly.
- **Q5 (planned hub types):** When hub functions become methods, what are their `this` and unqualified member-lookup semantics? Existing hub functions require fully qualified hub-member references; do not silently change receiver or lexical lookup behavior.
- **Q6 (planned hub types):** Are hub singletons first-class values that can be aliased, passed, and returned, such as `alias = game`? Prefer ordinary reference behavior if enabled, but approve the language change explicitly rather than inferring it from the internal singleton representation.
- **Q7 (planned hub types):** Do dotted hubs remain independent qualified names, or become properties of parent hub instances? Today `game.audio` neither requires nor creates `game`, and parent hub modes do not control it. An object hierarchy would change that contract.

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
| R1 | P1 | perf | Name-keyed linear scans over all callables (`getCallableId`, overload counts, `resolveFunction`, `resolveMethod`, `resolveConstructor` per call site, multi-pass `registerNativeFunction`); no name index | `Runtime.hpp/cpp`: function-name index/publication and owner resolution; catalog tests and RuntimeLookupBenchmark | Free-function paths indexed; method/constructor candidate scans scoped to existing owner catalogs and measured without another index; representative/large-overload validation remains pending | 8 |
| R2 | P1 | perf | Linear interning and lookups: `getStringId` O(S), `internContextVariable` O(C), `getEventId`, `getKeywordId`, `getDynamicVariableId`, `getFormatStringId`, `getContextId`, `getInstructionId(name)` | `Runtime.cpp`: catalog lookups; RuntimeLookupBenchmark string workload | String baselines recorded; index deferred with approval because current production text lookup is compiler-side; other catalogs remain pending | 8 |
| R3 | P1 | correctness | `getString(id)` and `getStringByName(missing)` index `m_strings` with the INVALID id (out of bounds) | `Runtime.cpp`: getString, getStringByName; `LanguageRegistryIdTest.cpp`: failed-lookup regression | Fixed: null on failure without logging; caller owns logging policy, valid/empty-string results and interning preserved | 6 |
| R4 | P1 | correctness | Function registration publishes a hub member even when `addCallableToTable` returned INVALID | `Runtime.cpp`: registerHubNativeFunction, registerScriptFunction; `LanguageHubTest.cpp` | Fixed: native and script publication are validity-gated; existing registration tests pass; callable-capacity exhaustion source-reviewed, not runtime-tested | 6 |
| R5 | P2 | correctness | Iterator current getter copies into owned (retained) storage, then raw-assigns the result; possible leak for object values | `Runtime.cpp`: iterator current getter; `RuntimeValue.hpp`: trivial copy/destruction; `Type.cpp`: assignValue; `IteratorObject.cpp`: current ownership | Resolved by review: assignment transfers ownership to empty getter output without an extra retain or release; targeted language/runtime tests pass; no C++ change | 6 |
| R6 | P2 | correctness | Unchecked accessors index vectors with invalid ids (instruction, dynamic variable, context name, constants) while sibling accessors validate | `Runtime.cpp`: instruction/dynamic-variable/context/constant lookups; `Runtime.hpp`: unchecked constant accessors; registry and VM storage regressions | Fixed: checked lookups honor the approved non-logging failure contracts; VM constant reads remain unchecked and preserve owned-load/borrowed-lookup semantics; callback invocation belongs to R7 | 6 |
| R7 | P2 | correctness | Missing dynamic-variable callbacks can be reached without the required permission validation | Compiler/format permission checks; `Runtime.cpp`: checked host dispatch; `Runtime.hpp`: unchecked VM dispatch; registry and VM storage regressions | Fixed: compiler enforces capabilities, host calls reject invalid IDs/missing callbacks without logging, VM dispatch stays trusted, and string temporaries are exception-safe | 6 |
| R8 | P2 | dead-code | `NOMAD_FUNCTION_DOC` blocks reference commented-out docs; the macro is never defined; keyword docs are discarded | `Runtime.cpp`: registerKeyword/registerDynamicVariable; `Documentation.hpp`: unconditional doc fields; `LanguageControlFlowTest.cpp`: keyword registration regression | Fixed: obsolete guards removed, supplied documentation retained, and keyword original-doc behavior regression-tested without a feature flag | 6 |
| R9 | P2 | dead-code | getFunctionAtLine, getFunctionByStartAddress, and getStringContextVariableValue were recorded as having no callers. **Corrected:** they have test-only callers | `Runtime.cpp`; `Runtime.hpp` declarations | verified (corrected) | 2 |
| R10 | P2 | redundancy | Hub qualification repeated in registerHubConstant, registerHubEnum, and registerHubNativeFunction instead of `qualifyHubMemberName` | `Runtime.cpp`: registerHub* functions; `LanguageHubTest.cpp`: invalid-owner diagnostic regression | Fixed: existing qualification helper reused; diagnostic normalization explicitly approved; registration results, validation order, and explicit native ownership preserved | 7 |
| R11 | P2 | redundancy | Name-conflict check chains duplicated (registerHub, registerHubVariable, registerEvent, registerDynamicVariable); publishTypeNamespace repeats canRegisterTypeName | `Runtime.cpp`: canRegisterHubMember and callers; registerHub/registerHubVariable policies; publishTypeNamespace exceptions | Resolved: shared hub/member reservation predicate consolidated; broader category policies and publication rechecks intentionally retained | 7 |
| R12 | P2 | policy | registerHub validates identifier segments in Runtime. This conflicts with the decision that the Compiler owns identifier syntax validation | `Runtime.cpp`: registerHub; `StatementParsers.cpp`: parseHubHeader; `Tokenizer.cpp`: nextIdentifier; `LanguageHubTest.cpp`: host/source naming regressions | Resolved by review: host names are not restricted to script identifiers; remaining namespace-structure checks are intentional; compiler token validation and existing language tests pass; no C++ change | 6 |
| R13 | P2 | perf | rebuildHubMembers rebuilds all hub maps on each registerHub, duplicating incremental registerHubMember; per-member linear hub lookups | `Runtime.cpp`: new/reopened registration and rebuilding; failure/retry regression; checked empty/mixed unrelated/child benchmark | Performance optimization deferred with approval; full refresh retained; separate exceptional-registration rollback implemented and validated by selected CTests and checked Debug/Release runs | 8 |
| R14 | P2 | quality | Can-register predicates log errors as side effects; callers log again | `Runtime.cpp`: namespace checks/public declaration entry points; `TypeManager.hpp/cpp`: catalog registration; type-manager rejection/boundary regressions | Fixed and revised with approval: class/catalog rejection returns INVALID without logging; type-name policy and publication moved to Runtime, with opaque catalog names and integrity preserved | 7 |
| R15 | P2 | perf | Registration makes several full passes over callables (quadratic startup) | `Runtime.cpp`: registerNativeFunction, getFunctionOverloadCount, canOverloadCallable; RuntimeLookupBenchmark | Same-name free-function scans replaced by the shared index and distinct-name registration measured; broader startup/large-overload workloads not measured | 8 |
| R16 | P2 | redundancy | Duplicate lookup pairs: getScriptCallableAtLine vs getFunctionAtLine; getScriptCallableAtInstructionIndex vs getFunctionAtInstructionIndex; getScriptCallableByStartAddress vs getFunctionByStartAddress | `Runtime.hpp/cpp`, `VirtualMachine.hpp/cpp`; catalog, source-navigation, and frame tests | Fixed: function-only navigation APIs and the test-only current-script getter removed; general callable navigation and function-only ID views retained | 2, 7 |
| R17 | P2 | const-correctness | Const methods return mutable pointers or mutate state: getFormatString, getFunctions, getVariableContext, setContextVariable* | `Runtime.hpp/cpp`; documentation reader; registry/catalog constness tests; context contracts | Complete with approval: owned format/function metadata has const-correct views; test-only navigation removed under R16; mutable live-context facade intentionally retained and documented | 7 |
| R18 | P3 | typing | Well-known type getters perform string-keyed lookups on every call (about 490 call sites, mostly registration); getStringType returns a different pointer type; unchecked `static_cast` downcasts where other code uses `asX()` | `Runtime.hpp/cpp`: fixed views; checked type-lookup benchmark; identity/allocation/category tests | Complete with approval: typing cleanup and ten constructor-bound Runtime views; dynamic function lookup retained; two selected Debug CTests and checked Debug/Release comparisons pass | 7, 8 |
| R19 | P3 | redundancy | getParameterShape mirrors the parser's parameter classification; drift risk | `TypeManager.cpp`: shared getParameterShape; `Runtime.cpp`: query facade; `Parser.cpp`: parseCallableArguments | Fixed: parser, Runtime registration, and inference share the catalog classifier; token consumption, diagnostics, and callback/predicate precedence retained | 7 |
| R20 | P3 | redundancy | Six context-variable accessors repeat the same log-and-return pattern | `Runtime.cpp`: context-variable accessors and private lookup; `LanguageContextVariableTest.cpp`: invalid-handle regression | Fixed with approval: shared lookup owns one unchanged error per rejection with common provenance; specialized returns, context state and ownership retained | 7 |
| R21 | P3 | correctness | `getConstantValue` silently returns on an invalid id, leaving the value unchanged | `Runtime.cpp`: getConstantValue; `LanguageRegistryIdTest.cpp`: constant lookup regression | Fixed under R6: host reads throw without logging or changing output; compiler-resolved VM reads use unchecked access | 6 |
| R22 | P3 | consistency | `dynamic_cast` in registration code (isDeferred, validateNativeFunctionSignatures) while other code uses `asX()` | `Runtime.cpp`: registerNativeFunction, validateNativeFunctionSignatures; deferred-native/category tests | Resolved with approval: native validator reuses the mutable category lookup; compiler-placeholder RTTI intentionally retained without Type API expansion | 7 |
| R23 | P3 | perf, correctness | registerDynamicVariable pushes a copy instead of moving; string setters free manually, which is not exception safe | `Runtime.cpp`: dynamic registration/string setters; registry/context lifetime regressions; checked dynamic-registration benchmark | Complete with approval: RAII string setters and local registration move; focused copy-count regressions, selected language CTest, and checked Debug/Release comparisons pass | 6, 8 |
| R24 | P3 | style | Mixed style: `ScriptFunction *` spacing, reference alignment, brace placement, single-line `if` without braces, trailing whitespace, snake_case loop variable `op_code`, parameter lists not one per line | `Runtime.cpp`: instruction/event/native registration and lookup, constant getter, context registration; removed navigation APIs | Fixed: concrete remaining naming/formatting findings addressed manually and token-checked; concurrent getter rename preserved with approved recursion fix | 7 |
| R25 | P3 | typing | getNativeMemberId returns a `MemberReference` with a `PropertyId::INVALID` sentinel; `size_t` converts implicitly to `NomadIndex` | `Runtime.hpp/cpp`: native-member lookup; compiler/interpreter callers and native metadata tests; `Nomad.hpp`: NomadIndex alias | Fixed with approval: optional lookup result and caller migration; count-conversion claim dismissed because NomadIndex is std::size_t | 7 |
| R26 | P3 | perf | catalogNativeProperty and catalogNativeMethod scan `m_nativeMembers` linearly; validateNativePropertyTypes looks up the same property again | `Runtime.cpp`: existing-name-bucket guards, rollback append, single mutable fetch; duplicate/order/failure-retry regressions and checked benchmarks | Complete with approval: consistent publication and indexed guards implemented; selected Debug CTests, failure/retry coverage with documented proxy-size exclusions, and checked Debug/Release comparisons pass | 8 |
| R27 | P3 | perf | getVariableContextIdByPrefix computes `getHubId(getContainingHub)` twice | `Runtime.cpp`: shared hub-variable owner resolution; precedence/allocation regressions; checked prefix/member benchmark | Complete with approval: one shared discovery with unchanged membership/fallback/public APIs; two selected Debug CTests and checked Debug/Release comparisons pass | 8 |
| R28 | P3 | naming | getCallableId matches only Function type despite its name; getFunctionId, getNativeFunctionId, and getFunctionTableSize (counts all callables) differ from getFunctionCount (script only) | `Runtime.hpp/cpp`: count, script metadata and name-based lookups; compiler/LSP/game consumers and catalog tests | Complete with approval: category-consistent names, specialized function existence/ID/list/count queries, source filters retained and callers migrated | 7 |
| R29 | — | dismissed | EventType stores `&m_events.back()`. Not a dangling pointer, because EventType copies the definition | `Event.hpp:46` `const EventDefinition m_event` | verified-not-a-bug | — |
| R30 | P3 | perf | getEventDefinition returns an optional copy of EventDefinition on each call | `Runtime.cpp`: getEventDefinition; stable-growth/reentry regressions; checked event benchmark | Complete with approval: stable vector-owned descriptors, checked direct-ID const-pointer lookup, explicit snapshots, two selected Debug CTests, and checked Debug/Release comparisons | 8 |

### Interpreter

| ID | Priority | Kind | Finding | Evidence | Status | Step |
| --- | --- | --- | --- | --- | --- | --- |
| I1 | P2 | correctness | `runNativeCallable` preserves callback changes to the running flag rather than restoring its caller | `Interpreter.cpp`: runNativeCallable | Fixed after policy approval: existing ExecutionStateGuard restores complete caller state on return/throw | 5 |
| I2 | P2 | consistency | Native function/method stop behavior depends on owner and struct-signature heuristics | `Interpreter.cpp`: runFunction, runStructMethod, runObjectMethod | Fixed with I1: one native function/method restoration policy; duplicate helper/heuristic removed | 5 |
| I3 | P2 | correctness | Format reset policy differs for expression, literal-only, and empty templates using fresh or live caller contexts | `Interpreter.cpp`: evaluateFormatExpression, formatString; `Expression.cpp`: DynamicFormatExpression/FormatStringLiteral::onEvaluate; `OpCode.cpp`: op_dynamic_format_execute | Fixed under approved disposable-context/current-call-diagnostics policy: boundary/segment resets, live bindings retained, no preservation guard | 5 |
| I4 | P2 | redundancy | Specialized source and placeholder compilation repeat setup/diagnostic handling; each dynamic expression creates a Compiler and runs setup callbacks | `Interpreter.cpp`: execute, evaluateFormatExpression; `Runtime.cpp`: createCompiler; `LanguageFormatStringFunctionTest.cpp`: compiler-creation regression | Resolved by user decision: keep specialized parsers and modest duplication; retain per-placeholder setup behavior, no shared-session abstraction or reuse | 5 |
| I5 | P2 | perf | Dynamic templates repeat scanning and per-placeholder compiler/tokenizer/parser preparation | `Interpreter.cpp`: formatString/evaluateFormatExpression; `Tokenizer.cpp`: fixed grammar reconstruction; checked full-call/staged-phase benchmark | Baseline/design retained; approved grammar prototype unimplemented and deferred at user request; token/AST caches and Compiler reuse unapproved | 8 (Q2, deferred) |
| I6 | P3 | correctness | execute overloads use different catch scopes; the string wrapper dereferences getCallable without a null guard | `Interpreter.cpp`: execute overloads; `InterpreterContext.cpp`: constructors, getCallable, validateFrame; `InterpreterTest.cpp`, `InterpreterReceiverTest.cpp` | Resolved by review: callable is owned/non-null; stale-frame validation throws; both source paths report failures as diagnostics while typed calls propagate | 5 |
| I7 | P2 | redundancy | Closure count validation is duplicated; raw and typed argument validation have distinct contracts | `Closure.cpp`: acceptsArgumentCount; `Interpreter.cpp`: runFunction(Closure); `VirtualMachine.cpp`: run(Closure) | Shared equivalent count check; specialized runtime/type/storage/result checks retained intentionally | 5 |
| I8 | P3 | redundancy | Compiled-body range check repeated at host boundaries and accepts an unset end sentinel | `ScriptCallableBase.cpp`: hasCompiledBody; `Interpreter.cpp`: callable entry points; `VirtualMachine.cpp`: run(Closure); `FormatString.cpp`: appendFunction | Fixed: shared range predicate rejects unset endpoints, empty/reversed ranges; no new prepared-VM readiness check | 5 |
| I9 | P2 | correctness | getStoredProperty's subspan has no local receiver-range guard | `Interpreter.cpp`: getStoredProperty; `InterpreterValue.cpp`: constructor; `StructType.cpp`: getSize; `CompositeType.cpp`: finalizeFieldLayout, registerProperty | Resolved by review: complete owned receiver storage and finalized nested layouts establish the range; no supported reproducer or extra guard | 5 |
| I10 | P2 | consistency | Stored-property assignment requires exact type equality; call arguments use acceptsArgument. Verify against the Compiler's assignability rules | `Interpreter.cpp`: validateStoredPropertyAssignment, validateCallArgument; `SyntaxTree.cpp`: MemberAssignmentStatement::onResolve; `Compiler.cpp`: resolveStructField/resolveObjectField; `Argument.cpp`: ExpressionArgument::onResolve | Resolved by review: Compiler and Interpreter both use exact descriptor identity for stored-field assignments and acceptsArgument for calls; no behavior change | 5 |
| I11 | P2 | perf | assignStoredProperty constructs a std::vector scratch buffer on every struct field write | `Interpreter.cpp`: conditional scratch; RuntimeLookupBenchmark assignment controls | Approved conditional array implemented; two Debug CTests and checked Debug/Release runs pass; one fewer Debug request, mixed Release timings recorded | 8 (done) |
| I12 | P3 | redundancy | Stored-property path lookup copies each segment into a NomadString; per-owner validation is required | `CompositeType.cpp`: getProperty(NomadStringView); `Interpreter.cpp`: resolveStoredPropertyPath; `InterpreterReceiver.cpp`: resolveProperty | Name-copy removal implemented using existing borrowed-view lookup; required segment/owner checks and diagnostics retained | 5 |
| I13 | P2 | api | setProperty name shared by a test-only functional API and production in-place API; size-check order differs between PropertyId and path variants | `Interpreter.cpp`: in-place setProperty overloads; `Interpreter.hpp`; `InterpreterStructAssignmentTest.cpp`: validation-order regression | Fixed: functional API removed; both retained setters use approved storage-first validation, preserving valid mutation behavior | 5 |
| I14 | P3 | redundancy | Free function formatDynamicString forwards to Interpreter::formatString | `Interpreter.cpp`: formatDynamicString; `FormatString.hpp`:34 | verified | 1 (done) |
| I15 | P2 | const-correctness | Interpreter::getVirtualMachine() const returns VirtualMachine&; const execution methods use the borrowed VM | `Interpreter.hpp`: borrowing/accessor contract; `Interpreter.cpp`; `InterpreterTest.cpp`: const facade and bound-VM coverage | Resolved by user decision: preserve and document const-callable facade over a caller-owned mutable VM; no signature or behavior change | 5 |

### InterpreterContext

| ID | Priority | Kind | Finding | Evidence | Status | Step |
| --- | --- | --- | --- | --- | --- | --- |
| C1 | P2 | const-correctness | Shared private const storage resolvers return writable variable/parameter/receiver spans | `InterpreterContext.cpp`: getVariableStorageById, getParameterStorage and explicit mutable accessors; `InterpreterContext.hpp` | Fixed internally: reads use existing const storage APIs; writes remain non-const; redundant private resolvers removed, public contracts unchanged | 5 |
| C2 | P2 | perf | Separate local owner/value-buffer allocations and same-type assignment scratch; retired slots remain valid until outermost evaluation exit | `InterpreterContext.hpp/.cpp`: owned/retired storage; `InterpreterStorage.cpp`: vector assignment scratch; checked storage/retirement benchmark | Array experiment rolled back with approval; restored Debug counts and selected CTest/checked Debug/Release runs pass; evidence retained, further storage redesign unapproved | 8 |
| C3 | P3 | api | Diagnostic names obscure append/clear-all behavior; review overlap and deleted temporary-interpreter constructors | `InterpreterContext.hpp`, `InterpreterContext.cpp`: constructors and diagnostics API | Approved renames implemented: addError and clearDiagnostics, no aliases; distinct source/metadata entry points and temporary-lifetime rejection retained | 5 |
| C4 | P3 | api | Name-keyed host accessors coexist with resolved-ID variable/parameter storage APIs | `InterpreterContext.hpp`, `InterpreterContext.cpp`: name/ID access; `Expression.cpp`: IdentifierExpression::onEvaluate; `SyntaxTree.cpp`: assignment; `DebugConsole.cpp`: console editing | Resolved by review: distinct host/binding and resolved-AST contracts retained, honoring step-2 convenience decisions; no further API or performance change | 2, 5 (done) |

### VirtualMachine

| ID | Priority | Kind | Finding | Evidence | Status | Step |
| --- | --- | --- | --- | --- | --- | --- |
| V1 | P1 | correctness | Stack bound checks run after the write and are compiled out in release | `VirtualMachine.cpp`: pushValue, pushBoolean, pushInteger, pushIndex, pushId, pushFloat write `m_stack[m_stackIndex]` before TEST_STACK_OVERFLOW; the macros exist only under NOMAD_DEBUG | verified | 3 |
| V2 | P2 | quality | TEST_STACK_* macros are not hygienic; UNDERFLOW uses the overflow condition | `VirtualMachine.cpp`: macro block at the top of the file | verified | 3 |
| V3 | P2 | dead-code | Unused VM accessors: freeObjectVariable, typed setXVariableValue, initStringVariable, getFunction*VariableValue, setVariableValue forwarder | `VirtualMachine.hpp` (about lines 223–258, 272); `VirtualMachine.cpp`: freeObjectVariable | verified | 1 (done) |
| V4 | P1 | perf | getCurrentCallable() is a linear scan over all callables; reached from closure creation, freeObjectVariable, and dynamic formatting | `VirtualMachine.cpp`: frame-header ID and return; `Compiler.cpp`: ID-based call-target linking; RuntimeLookupBenchmark query/call workloads | Fixed: five-slot frame owns callee ID; no new VM/snapshot field or index; six Debug CTests and checked Debug/Release comparisons pass; no-query call cost recorded | 8 (done) |
| V5 | P1 | perf | RTTI conversion on stored-property access; removing it requires a safe receiver-storage boundary | `VirtualMachine.cpp`: getProperty, setProperty, getStoredPropertyAddress; `NomadObject.cpp`, `ScriptObject.cpp`: storage query and allocated field snapshot | Fixed: measured checked storage query replaces VM RTTI; existing guards/layouts retained, native ABI rebuild required | 4 (done) |
| V6 | P1 | perf | Native property access copies NativePropertyInfo (with std::function members) by value on every access | `NativeProperty.cpp`: guarded stored-callback invocation; `VirtualMachine.cpp`: descriptor dispatch; `InterpreterObjectReceiver.cpp`: method routing; RuntimeLookupBenchmark property workload | Fixed with approved persistent callback state and retained snapshots; three Debug CTests and checked Debug/Release comparisons pass | 8 (done) |
| V7 | P2 | redundancy | Six execution entry points with near-identical result copy and free logic, and inconsistent error policy (log and return, invalid_argument, VirtualMachineException) | `VirtualMachine.cpp`: executeFunction overloads, runFunction, runClosure, run(ScriptFunction), run(Closure) | Fixed: shared lookup/result finalization; specialized runner contracts retained, host result-copy failures release VM results | 4 |
| V8 | P2 | redundancy | Save and restore of m_result and m_intermediate duplicated; BOOST_SCOPE_EXIT used instead of RAII guards | `VirtualMachine.cpp`: executeCreationBody, createNativeStruct, createNativeObject; `Interpreter.cpp`: evaluation entry points | Resolved by review: existing call-frame protection retained; local restoration outside call frames, no separate register guard | 4 |
| V9 | P2 | redundancy | Obsolete current-frame snapshot API duplicates closure capture-source resolution, including struct receiver layout logic | `VirtualMachine.cpp`: createClosure; `InterpreterCallFrame.cpp`: current-frame capture preparation | Fixed: removed test-only snapshot API; migrated tests to production frame and closure paths | 4 |
| V10 | P2 | correctness | Reported as a catch block reading moved-from storage. Not reproduced: the Closure constructor takes the vector only as its last statement, so the catch frees storage only when ownership has not transferred | `VirtualMachine.cpp`: createClosure try/catch; `Closure.cpp`: constructor | not reproduced | 4 |
| V11 | P2 | correctness | getStoredPropertyAddress bounds-checked only lastOffset and indexed firstOffset | `VirtualMachine.cpp`: getStoredPropertyAddress | Fixed: checks firstOffset <= lastOffset < storage size; regression in StructTypeTest | 4 |
| V12 | P2 | correctness | Missing null checks: asScriptMethod and asScriptConstructor results, getReturnType dereference in createNativeObject, getNativeFunction in callNativeFunction | `VirtualMachine.cpp`: initializeStructStorage, createScriptObject, createScriptStruct, createNativeObject, callNativeFunction | Resolved: compiler-selected metadata is trusted; absent initializer and native null-result guards remain | 1 (native, done), 4 (reviewed) |
| V13 | P3 | const-correctness | getCurrentFunction() const returns a non-const ScriptFunction*; receiver validation repeats on copies and binding | `VirtualMachine.hpp`: getCurrentScript; `VirtualMachine.cpp`: getThis, bindThisFromStack, copyThisToResult, copyThisToIntermediate | Fixed: const script getter and unchecked bound-receiver copies; host/receiver-binding null checks intentionally retained | 2 (done), 4 (done) |
| V14 | P3 | naming | `stack_size` parameter; `integer_value` and `float_value` locals; memberId used for both CallableId and PropertyId; get*Function*VariableValue and set*VariableValue are asymmetric | `VirtualMachine.hpp/cpp`: constructor, scalar operand readers, native method/property parameters and function-variable accessors | Complete: remaining parameter/local names normalized; obsolete accessor asymmetry resolved by review with APIs unchanged | 7 |
| V15 | P2 | quality | runNativeIndexer uses magic offsets (+2, +3) and convoluted release index arithmetic; duplicates the m_result reset in callNativeIndexer | `VirtualMachine.cpp`: runNativeIndexer, callNativeIndexer | Implemented: named receiver slot count and position, explicit copied-argument count; early reset retained for copy failures | 4 |
| V16 | P3 | perf | Host-call bookkeeping allocates per call; Closure argument/layout validation repeatedly classifies parameter types through name scans | `VirtualMachine.cpp`: inline-four cleanup; `InterpreterCallFrame.cpp`: owned result array/inline-one metadata; RuntimeLookupBenchmark host/frame workloads | Approved bounded storage/width changes implemented and measured; four Debug CTests and checked Debug/Release comparisons pass; heap fallback retained | 8 (done) |
| V17 | P3 | quality | fault() is not `[[noreturn]]`; many fault-then-continue patterns | `VirtualMachine.hpp`: fault declaration | Fixed: `[[noreturn]]` | 4 |

### Compiler

| ID | Priority | Kind | Finding | Evidence | Status | Step |
| --- | --- | --- | --- | --- | --- | --- |
| K1 | P1 | correctness | Struct-typed class fields compile to native property get and set ops, but `ScriptObject::getField` and `setField` reject struct-typed fields | `Compiler.cpp`: addPropertyAccess, resolveObjectField; `ScriptObject.cpp`: validateSingleSlotFieldAccess | Script paths pass the K1 repro; direct struct-field fallback now reports an internal error | 3 |
| K2 | P1 | correctness | No reserved stack slot count is computed; the VM relies on debug-only checks | Compiler.cpp: verifyReservedStackSlots, computeReservedStackSlots | Fixed: verifier computes the reserved slots per callable; script call opcodes carry the count as an operand and frame entry checks it | 3 |
| K3 | P2 | correctness | `addLoadFloatValue` compares `value == 0`, so `-0.0` compiles to `+0.0` | `Compiler.cpp`: addLoadFloatValue | from review | 3 |
| K4 | P3 | robustness | `Compiler::inferTypes` iterates to a fixed point with no iteration cap | `Compiler.cpp`: inferTypes | Fixed: pass count bounded, diagnostic on overflow | 3 |
| K5 | P3 | redundancy | Call emitters repeat the script and native split and operand setup (addFunctionCallInstruction through addNativeMethodCall) | `Compiler.cpp`: call emitters, existing instruction/argument helpers, addScriptCallTarget and linkFunctions | Resolved by review with approval: existing shared work retained; remaining specialized opcode/operand and storage-path duplication is intentional | 7 |
| K6 | P3 | redundancy | getIdentifierType and getIdentifierDefinition duplicate identifier resolution | `Compiler.hpp/cpp`: identifier APIs and shared reserved-name query | Complete with approval: reserved-name checks shared and redundant full classification removed; distinct precedence, syntax and context-resolution contracts retained | 7 |
