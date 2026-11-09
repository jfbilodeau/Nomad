# Struct implementation plan

Status: implementation proceeds only in separately approved, reviewable slices.
Current review stop: Step 5 is approved within its agreed scope. Step 6A's
conservative initialization/return-pair NRVO shape is approved. Step 6B1's
VM logical-local lookup is implemented and validated, awaiting review.
Initializer-time frame observation needs a separate decision before enabling
NRVO; compiler optimization remains unimplemented. Interpreter evaluation keeps
ordinary copies and does not need NRVO.
Steps 1A through 1AM have been reviewed. Step 1AO is an approved ABI design;
Steps 1AP through 1BB, Step 2 source declaration analysis, and statement-owned
block discovery have been reviewed. The first compiled source execution slice
has been reviewed, along with temporary struct method receivers and free-function
return-destination forwarding, direct construction returns, and method-result
forwarding on struct receivers and temporary-rooted stored property access.
Step 5A interpreter-owned struct values and frame access, standalone vector-backed
`InterpreterValue`, and nonthrowing cleanup/runtime layout access are implemented.
Step 5B interpreter construction and free-function aggregate calls is implemented
and validated, awaiting review.
Step 5C interpreter stored-property reads is implemented and validated, awaiting
review.
Step 5D1 statement-owned field assignments for named struct roots is implemented
and validated, awaiting review.
The immutable `InterpreterValue` rename and interpreter-produced replacement
operations are implemented and validated, awaiting review.
Conventional stored-property name/path and direct-ID APIs are implemented and
validated, awaiting review.
Host struct-method calls with explicit updated receiver/result values are
implemented and validated. Borrowed source-level interpreter invocation is
implemented and validated; it requires no receiver writeback.
Struct argument copies, destination returns, and registered method calls on
locals, parameters, and `this` are supported, including nested stored struct
properties rooted in that storage. Registered methods can also borrow
object-owned struct properties with retained owners. Value reads/writes,
argument/return copies, and staged replacements through that storage are
supported.
Live stored-field assignment through named/frame roots, nested fields,
class-owned structs, and temporary roots is implemented and validated.
Borrowed-storage
property setters update only the selected field; immutable-value setters retain
their independent replacement contract.
Interpreter/host class methods now support copied struct arguments and owned
struct results, including empty structs. Aggregate native object-method calls
are synchronous; scalar native method behavior remains unchanged. This slice
is implemented and validated, awaiting review.
Compiled native/script class-method struct results now support local and
forwarded return destinations, including retained temporary receivers and
staged assignment targets. This slice is implemented and validated, awaiting
review.
Method-backed native properties are implemented for registration and typed
interpreter/host receiver access. Getter results are owned snapshots; nested
mutations do not automatically invoke setters. Compiled integration is separate.
This slice is implemented and validated, awaiting review.
Compiled method-backed getter reads and whole-property setter calls are
implemented, preserving snapshot/no-automatic-writeback behavior. Validation
of this compiled accessor slice passed; it is awaiting review.
Flat closure capture storage and full-width current/enclosing frame snapshots
are implemented, including empty captures and copied struct receivers. This
storage slice does not enable source-level struct capture or callback execution;
it is implemented and validated, awaiting review.
Typed closure invocation and source-level struct captures are implemented,
including copied struct `this`, nested propagation, empty captures, and existing
Runtime callback adapters. Aggregate calls remain synchronous; scalar callback
suspension is unchanged. This slice is implemented and validated, awaiting review.
Struct-aware static/dynamic formatting is implemented and validated. Whole
structs display their type name like class objects; explicit fields use their
ordinary conversions. Compiled placeholders use VM lookup/callable execution
with direct frame capture binding; dynamic templates use the interpreter.
String conversion borrows complete `const RuntimeValue*` storage, not a dummy
value or first-slot copy. The reviewed correction is implemented and validated,
awaiting review.
Call-frame result access now exposes borrowed writable slots through
`RuntimeValue* getResult() const` and their count through
`NomadIndex getResultSize() const`. Void and empty struct results use null/zero;
interpreter host calls continue returning independent owned values. This API
cleanup is implemented and validated, awaiting review.
Architecture choices not explicitly approved remain proposals; stop for review
after each slice.

This draft is not part of the published language guide.

## 1. Agreed behavior

### Values and storage

- Add `struct Name ... end`, following existing class member syntax for fields,
  overloaded `init` constructors, and methods.
- Infer fields from explicit initializers and constructor assignments, as for
  classes. In the proposed `Point`, `x`, `y`, and `z` are three logical fields.
- Embed structs in their containing storage. Do not box each struct in a heap
  object or introduce shared ownership for the struct itself.
- Use existing `RuntimeValue` slots, not packed byte storage.
- Expose slot size through pure virtual `Type::getSize()`, specialized by type
  category rather than stored in a base-class size field. `ValueType` and
  `ClassType` return one; `VoidType` returns zero. The future struct type will
  provide its own layout size.
- Distinguish logical fields, variables, and parameters from physical offsets
  and slot counts.
- Empty structs are valid and occupy zero slots. They remain real types, distinct
  from `void`. Their construction and method calls can still have side effects.
- Copy struct fields by value. Nested structs copy recursively; class and
  collection fields preserve their existing reference semantics.
- Assignment, ordinary parameters, returns, and captures must not introduce
  aliases to the original struct's storage.
- Preserve existing scalar, string, class, callback, and event behavior.

### Construction and mutation

- Retain `new Point ...` and existing constructor overload-selection rules.
- Use a generated `Point$init` for field initialization, following the class
  initialization pattern. Explicit construction also runs the selected user
  constructor body; copying does not reconstruct the copied value.
- Support mutation of locals, parameter copies, and nested stored fields,
  including `player.location.x = ...` and `player.location.move ...`.
- A method's `this` borrows its receiver's storage. That does not make ordinary
  struct parameters reference parameters.
- Allow methods to mutate temporary structs. Discard the temporary afterward,
  preserving ordinary method side effects.
- Preserve RHS-before-replacement semantics. A callee must not observe premature
  writes to an existing assignment target through another alias.

### Returns and optimization

- Struct-returning calls use caller-provided destination storage.
- The callee uses the same destination mechanism for stack storage and stored
  object fields; it does not select behavior based on the destination's owner.
- Preallocate local storage using physical sizes. Include compiler temporaries
  when planning frame storage.
- Use final assignment storage directly only when doing so preserves behavior.
  Otherwise, use caller-owned temporary result storage and replace the target
  after RHS evaluation completes.
- Design for direct construction into return storage and eligible named
  return-value optimization (NRVO). Keep initial eligibility rules conservative.
- Never substitute an alias of the caller's original struct for a copied
  parameter, including when that parameter is returned.
- Interpreter calls return independent struct values, without forwarding caller
  destinations or applying RVO/NRVO. Internal compiled-call ABI storage does not
  change that value-returning contract.

### Initial scope and deferrals

- Include compiled execution, the interpreter, frame inspection, callbacks,
  captures, format expressions, and low-level host interoperability for the
  supported struct operations.
- Defer structs as generic type arguments, including `Array<Point>`,
  `List<Point>`, `Map<string Point>`, and nested occurrences of those arguments.
  Existing generic collections of non-struct types remain unchanged.
- A struct may contain a reference to an existing collection such as `Array1<int>`.
  This does not make the struct itself a generic argument.
- Defer generic struct declarations and automatic `==`/`!=` for structs.
- Defer generated C++ struct wrappers and automatic native C++ struct mapping.
  Existing generated bindings must remain correct when field offsets change.
- Open structs are deferred for this milestone, as approved during Step 2.
  Closed structs support field discovery inside constructors, not external
  field introduction.
- Future struct collection initialization should run a parameterless constructor
  when available, otherwise the generated `$init`. Record this requirement, but
  do not implement struct collections in this milestone.
- VM faults are treated as fatal for the current implementation work. Fault
  unwinding, post-fault recovery, and failure-path ownership cleanup are deferred;
  successful calls must still release their owned values normally.

## 2. Engineering constraints

- Discuss and obtain approval before architectural changes, new opcodes, shared
  helpers, or local wrapper/helper functions.
- Search existing mechanisms before proposing shared logic. Do not copy parser,
  resolver, or lifecycle functionality everywhere to avoid the approval gate.
- Let the compiler resolve types, widths, offsets, and lifecycle operations.
  Execution must not repeat compiler type checks or select field operations by
  inspecting runtime value tags.
- Retain genuine runtime checks such as stack capacity and bounds checks.
  Distinguish host API input validation from rechecking compiled script types.
- Favor reasonably fast execution: known offsets, preallocated storage, existing
  scalar instructions where suitable, and no per-instance struct boxing.
- Do not assume a polymorphic location hierarchy, universal aggregate framework,
  new intermediate representation, or general alias-analysis engine is needed.
- Keep code explicit and self-documenting. Preserve unrelated worktree changes;
  do not reformat, commit, or push.

## 3. Architecture approval gate

Before step 1, present concrete declarations and one lowered example for each
case below. These are review deliverables, not permission to implement a
particular representation.

### A. Layout metadata

Propose the smallest metadata needed for:

- Type size: existing scalars/references occupy one slot, `void` and empty structs
  occupy zero, and nonempty structs occupy the sum of their fields' sizes.
- Known versus unresolved layouts. Zero must not be used as an unresolved-size
  sentinel.
- Logical field/parameter/variable IDs, physical offsets, and total storage size.
- A deterministic field layout finalized after type inference and extension
  discovery, before allocation or instruction emission.
- Finite-layout validation. Reject direct and indirect inline struct cycles;
  references through classes or collections do not form inline-layout cycles.
- Overflow checks when accumulating sizes and reserving frame/object storage.

Use the existing compilation phases if possible. Discuss any new public phase
or change to phase-hook contracts first.

### B. Destination and receiver representation

Compare concrete lightweight representations, such as a frame-qualified slot
index and an object-field location versus a direct storage view with guaranteed
lifetime. Select one only after review.

The proposal must establish:

- How the callee accesses destination slots without knowing their owner.
- How a caller-local destination remains tied to the correct frame during calls.
- How an object owning a destination or receiver is kept alive across argument
  evaluation, nested calls, and native reentry.
- How nested field offsets compose without copying the receiver.
- How zero-width destinations and receivers work without dereferencing a
  nonexistent slot.
- How return destinations and borrowed method receivers are saved/restored.
- Why internal borrowed locations cannot escape into an ordinary struct value,
  return value, or capture.

Do not treat a native getter's returned value as addressable field storage.
Present the initial behavior for accessor-backed struct properties separately;
do not silently add write-back semantics or a new language restriction.

### C. Initialization, ownership, and replacement

Define separate contracts for:

1. Reserved but not yet initialized storage.
2. Empty destination storage receiving an initialized result.
3. An existing owned value being replaced.
4. A borrowed receiver being mutated.
5. Cleanup of partially initialized storage after a runtime failure.

Review how `Type::initValue`, `copyValue`, `assignValue`, `freeValue`, `needsFree`,
and `TypeOpCodes` should support aggregate ranges. Decide whether to evolve
signatures or add narrowly scoped operations; do not introduce range helpers
or overloads without approval.

Specify field-wise ownership for strings and object references, safe
self-assignment, and overlapping source/destination storage. Slot reservation
alone must not trigger script constructors or permit freeing uninitialized data.

### D. Calling convention and bytecode

Show lowering for:

- `printPoint p:Point`: one logical parameter, three parameter slots.
- `updatePoint p:Point x:float y:float`: three logical parameters, five parameter
  slots, and a separate internal return destination.
- A mutating method on `player.location`, including nested calls.
- A zero-slot argument before, between, and after nonempty arguments.
- `player.location = calculate player`, with result staging when necessary.

Keep logical arity and overload resolution independent of physical slot counts.
Account for existing special argument shapes, such as event callbacks; do not
replace every argument's stack count with its type size indiscriminately.

Compare compiler-expanded scalar instructions with narrowly scoped aggregate
instructions using those examples. For every proposed new or changed opcode,
present its name, operands, stack/register effects, ownership, zero-width
behavior, and reason existing instructions are insufficient. Approval is required
before adding it. Update registration, dumps, and instruction verification along
with any approved bytecode changes.

Define low-level host argument/result access and interpreter value storage
without requiring generated struct wrappers or breaking existing scalar APIs.

## 4. Reviewable implementation sequence

Dependencies are explicit below. Each step includes its focused tests and a
review stop. Split a cross-cutting step further only after agreeing on coherent
boundaries; do not expose executable struct paths backed by placeholders.

### Step 1: Type sizes and physical layout foundations

Prerequisite: architecture gate approved.

Approved first slice (Step 1A):

- Add pure virtual `Type::getSize()` and implementations for existing type
  categories. Compiler-deferred types use their existing unresolved-type error.
- Add size tests and embedding API documentation.
- Do not add a struct type, layout-finalization state, physical offset mappings,
  opcodes, helpers, or call-frame changes in this slice.
- Build and run the focused regression tests, then pause for review.

Step 1A validation:

- The `nomad_test` target built successfully with the active Windows Debug
  configuration.
- The `nomad_test_language` regression group passed, including type-size and
  compiler-deferred-type coverage.
- All four documentation preparation tests passed.
- No struct execution, physical-layout changes, or gameplay validation are
  included in this slice.

Approved second slice (Step 1B):

- Add `VariableMap::getVariableOffset(VariableId)` and `getStorageSize()`.
- Calculate offsets and total size from the current `Type::getSize()` values,
  without cached layouts, invalidation state, shared helpers, or a new layout
  abstraction.
- Preserve logical IDs and counts. Offset queries need the preceding types'
  resolved sizes; total-size queries need all resolved sizes.
- Explicitly report missing/deferred sizes, arithmetic overflow, and invalid
  offset-query IDs. These are metadata calculations, not execution-time
  revalidation of compiled script types.
- Leave VM access, opcodes, parameter layouts, and object storage unchanged.

Step 1B validation:

- The Windows Debug `nomad_test` target built successfully.
- The `nomad_test_language` regression group passed, including mixed widths,
  zero widths, live metadata changes, unresolved types, overflow boundaries,
  invalid IDs, and existing scalar-variable behavior.
- All four documentation preparation tests passed.
- Mixed/zero/maximum-width descriptors are test-only fixtures, not executable
  struct support. No gameplay validation is claimed.

Approved third slice (Step 1C):

- Add the shared metadata query `Parameter::getStackValueCount()`, together with
  `Callable::getParameterOffset(NomadId)` and `getParameterStorageSize()`.
- Expose the callable queries through the existing `ScriptCallableBase` forwarding
  pattern, without a new layout abstraction or cached state.
- Ordinary parameters use `Type::getSize()`; event callbacks occupy two stack
  slots despite their one-slot value type.
- Event dispatch expands to an event ID and a call-site-selected payload.
  Signature-only width queries explicitly report this indeterminate layout.
  Offsets up to and including the dispatch parameter remain computable when
  preceding widths are known; total size and subsequent offsets cannot be
  calculated from the signature alone. Dispatch calls remain supported and
  unchanged.
- Preserve logical arity and positions. Include captures already appended to the
  parameter list; exclude receivers, saved call frames, and return destinations.
- Report unresolved sizes, overflow, and invalid parameter positions explicitly
  during metadata calculation. Leave VM access, opcodes, and call emission
  unchanged.

Step 1C validation:

- The Windows Debug `nomad_test` target built successfully.
- The `nomad_test_language` regression group passed, covering parameter widths,
  callable/script forwarding, captures, unresolved metadata, event-dispatch
  indeterminacy, zero-width and maximum-size boundaries, and invalid positions.
  Existing callback and event execution tests also passed.
- All four documentation preparation tests passed.
- Synthetic type widths remain test-only fixtures. This slice does not implement
  executable structs or change the calling convention.

Approved fourth slice (Step 1D):

- Add `ClassType::getFieldOffset(PropertyId)` and `getFieldStorageSize()`.
- Sum stored fields' current `Type::getSize()` values on demand. Native
  properties occupy no slots and their value sizes are not queried.
- Preserve `getFieldIndex()`, logical property identity, and property counts.
  Empty/native-only owners have field-storage size zero and reference size one.
- Offset queries require only preceding stored sizes; total-size queries require
  all stored sizes. Propagate unresolved-size errors and report arithmetic
  overflow explicitly.
- The new offset query throws `NomadException` for native, missing, and foreign
  property IDs, unlike the legacy `getFieldIndex()` sentinel. This preserves the
  full representable offset range, including a zero-width field at the maximum
  `NomadIndex`.
- Do not add helpers, cached state, opcodes, object-allocation changes, or VM
  wiring. Synthetic widths remain test-only metadata fixtures.

Step 1D validation:

- The Windows Debug `nomad_test` target built successfully. The build reported
  `[[nodiscard]]` warnings in untouched test files.
- The `nomad_test_language` and `nomad_test_remaining` regression groups passed,
  including field-layout tests and existing class, script-object, interpreter,
  and deferred-native-property coverage.
- Focused cases cover mixed and zero widths, native-property exclusion, stable
  logical indices and IDs, appended fields, size resolution, maximum offsets,
  overflow, and invalid/foreign property IDs.
- All four documentation preparation tests and whitespace checks passed.
- No executable structs, aggregate allocation, calling-convention changes, or
  gameplay validation are included in this slice.

Approved fifth slice (Step 1E):

- Extract an abstract `RecordType : Type` base for names, openness, initializer
  names, properties, constructors, methods, and the existing field-layout queries.
- Derive `ClassType` from `RecordType`, retaining its one-slot reference size,
  object lifecycle/opcodes, type arguments, native indexers, and iterable metadata.
  The future `StructType` will be a sibling rather than a class subtype.
- Add `Type::asRecordType()` and `Property::getOwnerType()`. Preserve the existing
  class APIs, property IDs, borrowed pointers, registration checks, and callback
  behavior.
- Keep `Type.hpp` compatible for existing includes. Place the base `Type`
  declaration in `TypeBase.hpp` so `RecordType` can have a standalone header
  without circular includes.
- Do not add a struct descriptor, layout-finalization state, syntax, lifecycle
  operations, opcodes, or executable struct paths in this slice.

Step 1E validation:

- The Windows Debug `nomad_test` target built successfully, including the new
  standalone record header/source. The initial rebuild reported `[[nodiscard]]`
  warnings in untouched test files and dependency CMake deprecation warnings.
- The `nomad_test_language`, `nomad_test_remaining`, and `nomad_test_bindings`
  regression groups passed.
- Focused coverage verifies shared class/record catalog identity, property-owner
  compatibility, abstract-base/category conversions, unchanged class size and
  object opcodes, and deferred native-property resolution. Opcode assertions use
  registered IDs rather than cross-DLL function-address comparisons.
- Existing field-layout, callable, object ownership, interpreter, native callback,
  and binding tests passed. All four documentation preparation tests and
  whitespace checks passed.
- No struct execution, layout finalization, new opcodes, calling-convention
  changes, or gameplay validation are included in this slice.

Approved sixth slice (Step 1F):

- Rename `RecordType` to `CompositeType` and `asRecordType()` to
  `asCompositeType()` throughout the public APIs, implementation, and tests.
  This supersedes the Step 1E names and header organization.
- Use `Type.hpp/.cpp` for the base type, storage categories, and opcode metadata;
  remove the separate `TypeBase.hpp`.
- Place `ValueType`, `CompositeType`, and `ClassType` in their matching headers
  and sources. Group existing concrete built-in descriptors in
  `BaseTypes.hpp/.cpp`.
- Keep existing dedicated descriptors in their modules. Move the existing
  `EventType` implementation beside its declaration in `Event.cpp`.
- Update consuming includes, CMake sources, and embedding documentation. Do not
  retain an umbrella include in `Type.hpp` or introduce compatibility aliases
  for the just-added record name.
- Preserve type behavior, ownership, member catalogs, and instruction settings.
  No struct descriptor, layout-finalization state, syntax, or execution changes.

Step 1F validation:

- The Windows Debug `nomad_test` and `nomad-lsp` targets built successfully.
  Removing transitive type includes exposed missing callable-ID includes in the
  game headers; both now include `Callable.hpp` directly. Existing
  `[[nodiscard]]` warnings remain outside this reorganization.
- The `nomad_test_language`, `nomad_test_remaining`, `nomad_test_bindings`, and
  `nomad_test_tooling` regression groups passed. The `nomad_lsp_process` test
  also passed.
- All four documentation preparation tests and whitespace checks passed.
- Verified all five requested header/source pairs, their corresponding first
  source includes, removal of the superseded files, and absence of obsolete
  record/base-header names from code and CMake sources.
- Existing regression coverage exercises the renamed composite view, type
  sizes/conversions, member ownership/layout, native callbacks, interpreter,
  generated bindings, module compilation, and LSP behavior.
- No struct execution, layout finalization, new opcodes, calling-convention
  changes, or gameplay validation are included in this slice.

Approved seventh slice (Step 1G):

- Add explicit `CompositeType::finalizeFieldLayout() const` and
  `isFieldLayoutFinalized()`, with unresolved/resolving/finalized states separate
  from slot size.
- Recursively finalize only stored inline-struct dependencies. Class references
  and native properties do not create inline-layout dependencies.
- Cache the total size after successful finalization; retain the existing offset
  queries. Empty layouts finalize to zero slots.
- Report cycles, unresolved sizes, and arithmetic overflow. Failed owners remain
  unfinalized and retryable; successfully completed dependencies remain finalized.
- Reject new stored fields once explicit finalization starts or succeeds.
  Preserve same-type redeclarations, native property/method/constructor
  registration, initializer metadata, and field-value mutation.
- Keep existing unfinalized class behavior unchanged. No automatic compiler
  finalization, new compilation phase, shared helpers, opcodes, VM changes, or
  executable struct descriptor in this slice.
- Use metadata-only inline test descriptors, not boxed or placeholder executable
  structs.

Step 1G validation:

- The Windows Debug `nomad_test` target built successfully. Existing
  `[[nodiscard]]` warnings remain in untouched test files.
- The `nomad_test_language` and `nomad_test_remaining` regression groups passed.
- Focused coverage verifies empty and nested layouts, direct/indirect inline
  cycles, reference-cycle breaks, maximum offsets, overflow, unresolved-size
  recovery, completed dependencies, cached-size query counts, frozen stored-field
  registration, and continued field-value mutation.
- Deferred native-property resolution and later native-property registration
  passed with an explicitly finalized owner. Layout queries do not invoke
  native callbacks.
- All four documentation preparation tests and whitespace checks passed.
- Inline descriptors remain metadata-only test fixtures. There is no production
  struct descriptor, automatic compiler finalization, new opcode, VM change, or
  gameplay validation in this slice.

Approved eighth slice (Step 1H):

- Add `Runtime::finalizeFieldLayout(TypeId)` to finalize the runtime's owned
  mutable composite descriptor; reject invalid IDs and non-composite types.
- Make `CompositeType::finalizeFieldLayout()` non-const and remove `mutable`
  from its layout state and cached size. Keep inspection APIs const.
- Route nested inline finalization through Runtime. Preserve existing cycle,
  overflow, recovery, caching, and field-registration behavior.
- Migrate host examples and test calls to the Runtime API. Add rejection tests
  and compile-time coverage for const access and strongly typed IDs.
- No `friend`, `const_cast`, new shared helper, compiler-phase change, opcode,
  or executable struct support in this slice.

Step 1H validation:

- The Windows Debug `nomad_test` target built successfully.
- The `nomad_test_language` and `nomad_test_remaining` regression groups passed,
  including invalid/non-composite ID rejection, nested finalization, cycles,
  recovery, caching, and continued stored-field mutation.
- All four documentation preparation tests and whitespace checks passed.
- No gameplay validation or executable struct support is claimed.

Approved ninth slice (Step 1I):

- Replace the existing `Type::initValue`, `copyValue`, and `freeValue` reference
  parameters with slot pointers. Keep one API set without compatibility
  overloads or forwarding helpers.
- Each pointer addresses one typed value occupying `getSize()` contiguous, valid
  `RuntimeValue` slots. Initialization receives cleared, unowned storage and does
  not run script constructors.
- Zero-slot types access no storage and may receive null pointers. Void
  initialization is now a zero-slot no-op rather than clearing a supplied slot.
- Preserve existing scalar/reference copy, self-copy, release, and exception
  cleanup behavior. Retain the scalar `assignValue` reference API and adapt its
  internal lifecycle calls without changing its replacement semantics.
- Migrate every existing implementation and caller, including deferred types,
  compiler/interpreter/VM paths, collections, game event dispatch, native
  integration, and test fixtures.
- Add pointer-signature, selected-slot, owned-reference replacement, self-copy,
  and zero-slot regression coverage. Update the embedding contracts.
- The Runtime finalization entry point is now named
  `finalizeCompositeFieldLayout`, following the reviewed rename.
- No concrete struct descriptor, new opcode, VM storage change, aggregate
  assignment API, or `needsFree()` redesign in this slice.

Step 1I validation:

- The Windows Debug `nomad_test`, `nomad-runtime`, and `nomad-lsp` targets built
  successfully.
- The `nomad_test_language`, `nomad_test_remaining`, `nomad_test_bindings`, and
  `nomad_test_tooling` groups passed, together with `nomad_lsp_process`.
- Focused tests verify the exact pointer API signatures, unchanged scalar
  assignment signature, selected-slot access, owned-reference replacement,
  self-copy, and null/no-access behavior for void.
- Existing reference-count, partial initialization/copy cleanup, failed
  replacement, collection, native-module, and interpreter regressions passed.
- All four documentation preparation tests and whitespace checks passed.
- No struct execution, aggregate assignment, or gameplay validation is claimed.

Approved tenth slice (Step 1J):

- Add `OpCodeEmitter` directly to `script/OpCode.hpp`, exposing the existing
  instruction-function, ID, and index append operations. Compiler implements
  that interface directly without an adapter or intermediate opcode collection.
- Add type-owned cleanup emission for results, intermediate registers, stack
  values, function variables, and parameters. Migrate every compiler cleanup
  site and remove the old opcode-taking `Compiler::addFreeValue` helpers.
- Keep existing opcode tables as temporary backing for scalar cleanup. Parameter
  cleanup is supplied by the type categories rather than an object-specific
  compiler branch.
- Preserve the existing no-cleanup emission for unresolved deferred metadata.
- Add exact instruction/operand tests, direct emitter error tests, and a type
  override regression. The user reported all tests passing after running the
  tests directly; that running process blocked a subsequent module-copy build.

Approved eleventh slice (Step 1K):

- Remove `TargetOpCodes`, `TypeOpCodes`, `getTypeOpCodes()`, and `m_typeOpCodes`.
- Move `ExpressionTarget` unchanged from compiler syntax metadata to
  `script/OpCode.hpp`, retaining its name and numeric values.
- Reuse the former compiler target dispatch as `OpCodeEmitter::addLoadTarget`,
  accepting three instruction functions directly. Types and constant loading
  share it without additional tables or duplicate dispatch helpers.
- Supply virtual type-owned initialization, load, store, copy, move, push, pop,
  and cleanup operations. Migrate all remaining compiler and test consumers.
- Make `needsFree()` an independent virtual ownership query, false by default
  and true for class references. Keep runtime lifecycle APIs unchanged.
- Preserve existing scalar/reference bytecode, operand widths, emitted word
  positions, dynamic getter ownership, and string initialization.
- Leave compiler literal-loading logic in place. No new opcode, compiler
  dependency in Type, intermediate opcode collection, struct execution, or
  physical storage/calling-convention change.
- Add exact target/load/store/register/initialization tests and type-override
  delegation coverage. Invalid emission targets raise an explicit error.

Steps 1J and 1K validation:

- The Windows Debug `nomad_test`, `nomad-runtime`, and `nomad-lsp` targets built
  successfully. The earlier module-copy blockage cleared after the user's test
  process completed; no build-script workaround was introduced.
- The `nomad_test_language`, `nomad_test_remaining`, `nomad_test_bindings`, and
  `nomad_test_tooling` groups passed, together with `nomad_lsp_process`.
- Emission tests verify every load target, instruction ID, operand width, return
  position, initialization variant, register ownership operation, store
  instruction, type override, and invalid-target error.
- Direct emission assertions account for the compiler's existing initial
  `op_stop`; no production initialization behavior was changed.
- No references to the removed tables or getter remain in C++ source or headers.
- All four documentation preparation tests and whitespace checks passed.
- No struct execution, aggregate assignment, or gameplay validation is claimed.

Approved twelfth slice (Step 1L):

- Add `Type::getAssignmentScratchSize()`, returning zero for zero/one-slot values
  and the full slot count for larger values, without cached state or a policy
  flag.
- Replace the scalar reference assignment API with one pointer-based
  `assignValue(source, destination, scratch = nullptr)` operation. No mutually
  exclusive overloads are introduced.
- Zero-slot assignment accesses no storage. One-slot assignment uses a local
  `RuntimeValue` when external scratch is omitted; larger values require
  caller-provided scratch and explicitly reject its absence.
- Supplied scratch is cleared, spans `getSize()` slots, and is disjoint from
  source and destination. Source/destination overlap and self-assignment remain
  safe through staging.
- Delegate copy and release to the type's existing lifecycle operations. Copy
  before releasing the destination; transfer owned slots and clear scratch.
  Clean up partially copied scratch on failure. If destination release fails,
  also clean up the prepared copy rather than leaking it.
- Migrate all existing host/interpreter/collection callers to pointers without
  adding heap scratch allocation to their single-slot paths.
- Add native multi-slot storage fixtures covering reference ownership, overlap,
  self-assignment, failure at each copied slot, missing scratch, zero size,
  scratch reuse, and source/destination cleanup.
- Leave VM storage, bytecode, struct syntax, generic collections, and caller
  return-destination handling unchanged. Already-staged return results will
  transfer directly in the later aggregate execution work.

Step 1L validation:

- The Windows Debug `nomad_test`, `nomad-runtime`, and `nomad-lsp` targets built
  successfully.
- The `nomad_test_language`, `nomad_test_remaining`, `nomad_test_bindings`, and
  `nomad_test_tooling` regression groups passed, together with
  `nomad_lsp_process`.
- Focused coverage verifies the single pointer API, zero/one/multi-slot scratch
  sizes, scalar and class assignment with optional scratch, forward/backward
  overlap, self-assignment, failure after each copied slot, scratch reuse,
  missing-scratch diagnostics, and prepared-copy cleanup when release fails.
- Existing collection, interpreter, native property, partial-copy cleanup, and
  reference-lifetime behavior remains covered. Deferred sizes propagate errors.
- All four documentation preparation tests and whitespace checks passed.
- Multi-slot descriptors remain native storage test fixtures, not executable
  struct support. No gameplay validation is claimed.

Approved thirteenth slice (Step 1M):

- Add concrete `StructType : CompositeType` with dedicated header/source files,
  identity-based matching, and `Type::asStructType()`.
- Add closed host descriptor registration through `Runtime::registerStruct`;
  extend stored-property registration to composite owners.
- Require finalized field layouts for slot size, ownership queries, and storage
  lifecycle operations. Empty structs remain zero-slot, non-void values.
- Implement field-wise initialization, copy, release, and scratch-backed
  assignment using existing lifecycle APIs. Native properties consume no slots.
  Zero-width fields access no storage and copying does not run constructors.
- Preserve caller-managed partial-failure cleanup. Direct copy requires disjoint
  storage except self-copy; assignment stages overlapping replacements.
- Explicitly reject every struct bytecode emission operation without emitting
  scalar fallback instructions. Formatting reports the type name only.
- No parser change, automatic compiler finalization, aggregate VM allocation,
  struct accessor registration, generic support, or open-struct language behavior.

Step 1M validation:

- Windows Debug builds passed for `nomad_test`, `nomad-runtime`, and `nomad-lsp`.
- The language, remaining, binding, and tooling regression groups passed,
  together with the LSP process test.
- Real struct coverage verifies registration, identity, unresolved layouts,
  empty/nested field offsets, scalar copies, owned-reference cleanup, inline
  cycles, reference-cycle breaks, field freezing, partial nested initialization
  and copy failure, and rejection of every emission operation.
- The strengthened partial nested failure tests passed after rebuilding the
  test target. All four documentation preparation tests and whitespace checks
  passed.
- This is host-owned inline storage support, not compiled/interpreted struct
  execution or gameplay validation.

Next design discussion: aggregate VM storage addressing and type-owned emission.
Wire finalization at the existing Resolve-to-Emit boundary once struct
declarations and member inference are available. Object allocation and executable
aggregate accesses remain later work.

### Step 1N: Runtime-owned type catalog

Runtime now composes a `TypeManager`, accessible through const/non-const
`getTypeManager()` overloads. The manager owns type descriptors, reserves IDs,
provides lookup and catalog-owned mutable category access, and finalizes
composite layouts. Existing Runtime lookup and finalization APIs delegate to it.

Existing registration entry points remain on Runtime for this initial slice;
Runtime still coordinates hub publication, specialization factories, member
registration, and callable IDs. The manager's low-level catalog operations
preserve unused reserved IDs and reject invalid ownership or occupied slots;
they do not publish namespace members.

The manager occupies the former type catalog's lifetime position so runtime
values and the VM are cleaned up before type descriptors. This extraction
does not add friendship, struct syntax, native struct callback contracts, or
aggregate bytecode.

Next slice after review: move type/member registration coordination to
TypeManager and introduce cohesive native/script class/struct registration
entry points while retaining Runtime's callable ID catalog.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Manager coverage verifies composed access, catalog identity, stable descriptor
  pointers, append-style enumeration, reserved holes, invalid publication,
  nested finalization, mutable owner validation, and descriptor lifetime through
  runtime-owned constant/context cleanup.
- Language, remaining, binding, tooling, and LSP process regressions passed.
- All four documentation preparation tests and whitespace checks passed.
- No aggregate execution or gameplay validation is claimed.

### Step 1O: Explicit stored-field registration

`TypeManager::registerScriptClassProperty` and `registerScriptStructProperty`
declare stored fields for their respective owner categories. Existing
value-type, member-collision, redeclaration, and layout-freezing behavior is
preserved. Property IDs remain runtime-wide, and metadata lookup and initializer
flags retain their existing Runtime APIs.

The compiler declares class fields through the explicit manager API.
`Runtime::registerProperty` remains a compatibility delegator. Native accessor
registration remains separate and unchanged; no native struct callback or
aggregate execution contract is introduced.

At the user's request, the runtime identifier checker and all of its uses
were removed. Host field, native-member, and hub-member registration no longer
enforce script identifier syntax. Source declaration validation remains the
compiler's responsibility. Ownership, value-type, collision, signature,
and layout checks remain; other existing registration preconditions are unchanged.

Next slice after review: migrate native class accessor registration, then
class callable registration, keeping Runtime's callable ID catalog. Struct
accessors and callables still require the separately approved aggregate ABI.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Language, remaining, binding, tooling, and LSP process regressions passed;
  the language group was rerun after updating the typed hub-registration test.
- New coverage verifies category-specific registration, invalid owners and
  value types, identity-preserving redeclaration, member collisions, frozen
  layouts, nested/empty structs, and host names outside identifier syntax.
- All four documentation preparation tests and whitespace checks passed.
- No aggregate execution or gameplay validation is claimed.

### Step 1P: Native class property registration

`TypeManager::registerNativeClassProperty` owns native class accessor validation
and construction. `Runtime::addProperty` delegates for compatibility.
`NativePropertyDefinition` is available through `NativeMember.hpp`, without a
TypeManager-to-Runtime header dependency.

Runtime retains native-member signature checks, shared native-member indexes,
property IDs/lookup, and deferred property resolution. The approved public
`canRegisterNativeMember` and `catalogNativeProperty` operations coordinate
registration without friendship. Invalid or duplicate catalog requests throw.

TypeManager uses NativeProperty's existing resolution state to recognize
deferred types without adding a compiler dependency. Registration preserves
callbacks, permanent IDs, unresolved callback guards, collisions, frozen-layout
behavior, and host-name semantics. Native properties add no stored-field slots.

Next slice after review: migrate class methods and constructors through
TypeManager while callable IDs stay in Runtime. Native struct accessors and
callables remain deferred until their aggregate storage ABI is approved.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Language, remaining, binding, tooling, and LSP process regressions passed.
- Direct manager coverage verifies read-only/write-only accessors, invalid
  definitions and owners, runtime catalog identity, callback behavior,
  shared-signature collisions, duplicate catalog rejection, and registration
  after layout finalization without adding storage slots.
- Deferred enum properties resolve through the manager path in both analysis
  and executable modes while preserving IDs and callback guards.
- All four documentation preparation tests and whitespace checks passed.
- No aggregate execution or gameplay validation is claimed.

### Step 1Q: Class callable registration

TypeManager provides `registerNativeClassMethod`, `registerNativeClassConstructor`
(including a distinct constructed-type overload), `registerScriptClassMethod`,
and `registerScriptClassConstructor`. Existing Runtime entry points delegate.
The compiler registers class members and generated initializers through the
explicit manager APIs.

Callable IDs and lookups remain in Runtime, while class descriptors continue
to own methods and constructors. The approved `Runtime::catalogNativeMethod`
indexes an already registered native method without friendship. Registration
preserves parameter/return checks, receiver handling, native-member signatures,
constructor collision rules, source metadata, and frozen field layouts.
Runtime's native free-function path no longer includes class registration.

ParameterDefinition, NativeMethodDefinition, and NativeConstructorDefinition
are available through NativeMember.hpp. No struct callable ABI is introduced.

Next slice after review: migrate class/struct type registration and namespace
publication through TypeManager; specialization/family registration follows.
Aggregate struct addressing and callable ABI design remain subsequent work.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Language, remaining, binding, tooling, and LSP process regressions passed.
- Direct manager coverage verifies shared callable IDs, source and owner
  metadata, distinct constructed types, invalid owners and definitions,
  duplicate registration/indexing, native-member lookup, frozen layouts,
  and callback invocation without registration-time execution.
- Compiler class construction, methods, and generated initializers pass
  existing executable language regressions through the manager path.
- All four documentation preparation tests and whitespace checks passed.
- No aggregate struct execution or gameplay validation is claimed.

### Step 1R: Class/struct creation and full type publication

TypeManager owns class creation (including custom descriptor templates),
struct creation, request validation, and full descriptor registration.
Runtime's existing entry points delegate, and compiler class predeclaration
uses the manager.

The approved Runtime `canRegisterTypeName` and `publishTypeNamespace` operations
maintain hub indexes without exposing general hub mutation or friendship.
`TypeManager::registerType` validates a same-runtime, unused reserved ID,
publishes namespace metadata, and takes catalog ownership. Low-level
`catalogType` retains its namespace-free behavior.

Reserved holes, nested construction, descriptor identity, event/callback
namespace handling, hub collisions, and host naming semantics are preserved.
Other Runtime type factories publish through the manager's full registration
path while remaining on Runtime in this slice.

Next slice after review: move parameterized type families and specialization
state into TypeManager, preserving Runtime factory signatures and callable IDs.
Enum/opaque-ID/callback creation and remaining type registration surfaces will
follow before returning to aggregate struct addressing and ABI design.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Language, remaining, binding, tooling, and LSP process regressions passed.
- Direct manager coverage verifies class/struct namespace publication,
  documentation, stable descriptor identity, hub collisions, invalid modes,
  reservations, foreign ownership, failed publication, and late hub creation.
- Custom descriptor and nested/failed construction regressions exercise the
  manager API; existing Runtime template and hub wrappers remain covered.
- Existing event, callback, enum, and specialization regressions pass through
  the full manager publication path.
- All four documentation preparation tests and whitespace checks passed.
- No aggregate struct execution or gameplay validation is claimed.

### Step 1S: Parameterized families and specialization ownership

TypeManager owns parameterized type factories, initializers, constructor
inference signatures, member templates, and specialization caches. Existing
Runtime APIs and Runtime* callback signatures remain compatible. Compiler
annotations and inferred construction resolve through the manager.

The approved `Runtime::publishTypeFamilyNamespace` and shared
`canRegisterMemberSignature` operations maintain namespace and parser-signature
coordination without friendship. Runtime's combined member signature lookup
consults manager-owned templates before native members. Hub rebuilding enumerates
manager-owned family names, including families registered before a hub exists.

Specializations preserve reserved IDs, invalid factory handling, callback
ordering, identity caching, and nested registration. Factory and initializer
callbacks are copied before invocation so family-catalog growth remains safe.
Constructor-family callbacks and specialization state remain on Runtime for now.
This extraction adds no struct generic support or aggregate ABI.

Next slice after review: migrate remaining enum, opaque-ID, and callback type
creation into TypeManager; then address remaining constructor-family and type
registration surfaces before aggregate struct ABI design.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Language, remaining, binding, tooling, and LSP process regressions passed.
- Direct manager coverage verifies family namespace and metadata publication,
  constructor inference, Runtime compatibility, specialization identity and
  callback counts, late hub creation, invalid publication, and family-catalog
  growth inside both factories and initializers.
- Existing invalid factory/reserved-ID and member-template regressions exercise
  manager registration. Compiler annotation and inferred-construction behavior
  remains covered by executable language regressions.
- All four documentation preparation tests and whitespace checks passed.
- No aggregate struct execution or gameplay validation is claimed.

### Step 1T: Enum, opaque-ID, and callback creation

TypeManager owns enum and opaque-ID descriptor creation and callback signature
interning, including predicate types. Existing Runtime APIs delegate.
Compiler enum declarations and event callback signatures use the manager.

Enum constant IDs and equality operators remain in Runtime. The approved
`registerEnumOperators` operation retains compiler-owned folding functions
without adding a compiler dependency to TypeManager. Enum definitions are
validated before publication; existing namespace, duplicate, value, callback
identity, and ownership behavior is preserved.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Language, remaining, binding, tooling, and LSP process regressions passed.
- Direct manager coverage verifies enum constants and equality folding, opaque-ID
  publication, callback signature identity, and Runtime compatibility.
- All four documentation preparation tests and whitespace checks passed.
- No aggregate struct execution or gameplay validation is claimed.

Next slice after review: migrate native parameterized constructor families and
their specialization state, then review remaining Runtime type surfaces before
returning to aggregate struct storage addressing and callable ABI design.

### Step 1U: Native parameterized constructor families

TypeManager owns native parameterized constructor family registrations,
initializers, and specialization caching. Runtime registration and lookup APIs
remain compatibility wrappers. Compiler and signature-help resolution and the
Array constructor family registration use the manager.

Runtime's existing `publishTypeFamilyNamespace` accepts registered type or
constructor families; late hub creation includes both manager-owned catalogs.
Initializer callbacks are copied before invocation so registration of additional
families cannot invalidate the active callback. Missing constructors and thrown
initializers remain uncached, allowing subsequent attempts.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Language, remaining, tooling, and LSP process regressions passed.
- Direct manager tests verify namespace publication, late hubs, append-only name
  enumeration, specialization reuse, catalog growth during initialization,
  retries after missing constructors or exceptions, and script constructors.
- Existing Runtime constructor-family validation and executable Array
  construction regressions passed.
- All four documentation preparation tests and whitespace checks passed.
- No aggregate struct execution or gameplay validation is claimed.

Next slice after review: review remaining Runtime type surfaces (events,
iterables, indexers, and convenience lookups) for cohesive manager ownership,
then return to aggregate struct storage addressing and callable ABI design.

### Step 1V: Native class indexers and iterable associations

TypeManager registers native class indexers and iterable factory methods and
associations, resolves iterator specializations, and provides iterable lookup.
Native creation definitions live in `NativeMember.hpp`; the existing
`NativeInterableDefinition` spelling remains compatible.

Runtime retains indexer IDs and their catalog/lookup operations. Existing
registration and iterator/iterable APIs delegate to the manager. Compiler loop
resolution and built-in collection/range registration use the manager.

The remaining-surface review keeps event registration and event definitions/IDs,
operator tables, and built-in convenience getters in Runtime. Event descriptor
creation already uses manager-owned type registration.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Language, remaining, binding, tooling, and LSP process regressions passed.
- Direct manager coverage verifies indexer catalog identity, iterator reuse,
  invalid class/struct/value inputs, frozen-layout registration, iterable
  factory identity, failed-registration publication, and Runtime compatibility.
- Existing executable collection indexing and iteration regressions passed.
- All four documentation preparation tests and whitespace checks passed.
- No aggregate struct execution or gameplay validation is claimed.

Next slice after review: design aggregate struct storage addressing, borrowed
receivers, and caller-provided return destinations. Discuss any necessary VM
instructions or compiler special cases before implementation.

### Step 1W: Approved borrowed storage representation

The contiguous storage and owner-lifetime contracts below remain applicable.
Step 1AO supersedes the separate VM binding/save-restore representation for
compiled calls: compiler-sized storage pointers travel in hidden argument slots.
Spans need not be constructed or checked by trusted opcode execution.

Use non-owning contiguous slot views: `std::span<RuntimeValue>` for mutable
destinations and receivers, and `std::span<const RuntimeValue>` for sources.
These views do not own a struct or its enclosing object. No polymorphic location
hierarchy, struct boxing, or shared ownership is introduced.

The VM stack currently allocates its slots once; it must not relocate storage
while a view is borrowed. Object field storage is likewise fixed after
allocation. Interpreter-owned values must keep their storage stable across
borrows. Empty structs use empty views without forming a pointer to a
nonexistent slot. Nested fields compose finalized offset/size subviews.

The caller owns local and temporary storage. For an object-field receiver or
destination, retain the enclosing object before argument evaluation and release
it after the operation, including failure cleanup. Internal views cannot become
ordinary struct values: arguments, returns, and captures copy slots.

Existing scalar result/intermediate registers and object `this` remain unchanged.
Struct calls use separate internal receiver and return-destination views, saved
and restored across script calls, native calls, and host reentry. Bind the views
after argument evaluation so nested argument calls do not inherit the pending
outer call's destination.

Lowering constraints:

- `printPoint p:Point` copies one logical argument into three parameter slots.
- `updatePoint p:Point x:float y:float` has physical parameter offsets 0, 3, and
  4 and five argument slots, with a separate caller-owned return destination.
- Preserve right-to-left argument evaluation. Within each aggregate argument
  block, fields occupy increasing physical offsets; parameter addressing must
  account for the existing descending logical parameter order.
- Empty arguments keep logical IDs and consume no slots.
- `player.location.move ...` borrows the retained player's location subview.
  Nested calls save and restore that borrow.
- `player.location = calculate player` initially stages the RHS in caller-owned,
  cleared storage, then invokes `Type::assignValue` with disjoint scratch after
  RHS completion. Do not write into the live destination prematurely.
- New local initialization may subsequently construct into empty final storage.
  NRVO eligibility, initialization tracking, failure cleanup, and exact opcode
  contracts still require separate design review.

Accessor-backed struct properties return owned temporary copies, not addressable
field storage. Methods mutate the temporary without implicit setter write-back.
Native struct accessor API design remains a separate approval gate.

This slice changes the plan only. No VM instructions, runtime storage, source
syntax, or aggregate execution have changed.

Next reviewable implementation slice: migrate ScriptObject allocation and field
access to finalized physical offsets and sizes, with host-level nested-struct
lifecycle tests. Do not enable struct syntax or add opcodes in that slice.

### Step 1X: Physical ScriptObject field storage

ScriptObject snapshots stored-field types, physical offsets, and sizes and
allocates the total physical slot width. Allocation keeps its existing zeroed
storage behavior without running type defaults or constructors. Destruction
releases each logical field through its type at the captured physical offset,
including nested struct fields.

Approved compatibility choice: inline struct layouts must already be finalized,
but allocation does not freeze the enclosing class. The snapshot and its storage
remain fixed when an open class later gains fields.

`getFieldCount`, `getField`, and `setField` retain logical stored-field indices.
Single-slot APIs reject structs, including empty and one-slot structs, rather
than treating aggregate storage as a scalar. Const/mutable `getFieldStorage`
overloads expose borrowed contiguous field views. `setFieldStorage` validates
source/scratch widths and uses the existing type-owned assignment operation.
Empty fields have distinct logical indices and empty views.

Existing VM properties and generated scalar bindings keep logical field-index
access and therefore use the captured physical offsets without API changes.
No struct syntax, aggregate VM instructions, or compiled aggregate calls are
enabled in this slice.

Validation:

- Debug tests, runtime, and LSP targets built successfully. A constructor
  assertion initially failed to compile due to C++ declaration parsing; brace
  initialization corrected the test.
- Language, remaining, and binding regression groups passed.
- Host-level coverage verifies nested struct slot widths, scalar fields after
  aggregates, independent instances, retained strings/object references,
  self-assignment, overlapping sources, empty and one-slot structs, stable
  snapshots, invalid spans, and cleanup after partial copy failure.
- All four documentation preparation tests and whitespace checks passed.
- No compiled aggregate execution or gameplay validation is claimed.

Next slice after review: settle frame/parameter storage addressing and the exact
aggregate instruction contracts, including receiver and return-destination
save/restore, before changing the VM ABI.

### Step 1Y: Approved frame and parameter storage addressing

Local storage starts at `m_variableIndex + physicalOffset`. Parameters retain
right-to-left argument evaluation and pushing, while each aggregate argument
occupies a contiguous block in increasing field-offset order.

For a nonempty parameter of width `W` and cumulative parameter offset `O`,
its storage starts at `m_parameterIndex - O - (W - 1)`. The parameter anchor
points at the last slot of the first nonempty parameter, not necessarily its
first field. Empty parameters return empty views before index arithmetic.

For `updatePoint p:Point x:float y:float`, physical memory from low to high is
`[y, x, p.x, p.y, p.z]`. The parameter anchor points at `p.z`. Logical offsets
are `p=0`, `x=3`, `y=4`; the Point block begins two slots below the anchor.
Nested fields use subviews of that block, not scalar parameter-ID arithmetic.
Zero-width parameters before, between, or after nonempty parameters retain
logical IDs without consuming slots or shifting the argument anchor.

Approved storage-view APIs for the next implementation slice:

- Const/mutable `getLocalStorage(offset, count)`.
- Const/mutable `getParameterStorage(offset, count)`.
- Const/mutable `getEnclosingParameterStorage(offset, count)`.

The enclosing-parameter APIs use the active script frame's argument anchor,
derived from its saved-frame boundary, independently of an intervening native
call's parameter anchor. All views borrow the fixed VM stack and must not
outlive their owning frame. Check physical stack bounds and arithmetic; do not
revalidate compiled types. Empty views must not form pointers to nonexistent
slots.

Existing scalar VM APIs and opcodes remain unchanged in this foundation.
Do not reinterpret logical VariableId or parameter IDs as physical offsets.
Later compiler lowering resolves logical IDs to physical addresses before
emission and retains logical IDs for source metadata and host-facing access.

Argument emission must retain special shapes such as the two-slot event
callback. `ArgumentList::getArgumentCount` currently counts emitted slots,
despite its name; later call lowering must distinguish that count from logical
arity without changing overload resolution.

This slice records the design only. No storage-view API, frame reservation,
aggregate argument emission, receiver/return-destination state, or opcode has
been implemented.

Next reviewable implementation slice: add these borrowed storage views with
low-level tests for multi-slot and empty blocks, nested script frames, native
reentry, and arithmetic/bounds failures. Then review physical frame reservation,
aggregate lifecycle emission, and exact receiver/return-destination instruction
contracts separately before changing the aggregate call ABI.

### Step 1Z: Borrowed VM storage views

VirtualMachine provides the six approved const/mutable local, parameter, and
enclosing-parameter storage-view overloads. Nonempty ranges check arithmetic and
live physical stack bounds before forming spans; empty requests return empty
views without index arithmetic. Mutable overloads reuse const validation.

Parameter blocks retain increasing field order with descending argument offsets.
Enclosing parameter views derive their anchor from the saved script-frame
boundary, independently of the active native parameter anchor. Bounds checks
do not validate logical declarations or types. Views remain caller-managed
borrows and carry no ownership or lifecycle operations.

Existing scalar APIs, frame layout/reservation, argument emission, and opcodes
remain unchanged. No executable aggregate call paths are enabled.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Remaining and language regression groups passed.
- Low-level tests verify contiguous multi-slot parameter order, mutable/const
  views, local offsets, nested frame restoration, enclosing parameter access
  during native calls and script reentry, empty requests, and overflow/bounds
  rejection.
- All four documentation preparation tests and whitespace checks passed.
- No aggregate call execution or gameplay validation is claimed.

Next slice after review: design physical frame reservation and aggregate
lifecycle emission, then exact receiver/return-destination instruction contracts
before changing the aggregate call ABI.

### Step 1AA: Trusted VM storage accessors

The storage accessors now perform address calculation only, with empty-view
handling before arithmetic. Removed repeated frame-state and physical range
validation, including mutable-to-const validation forwarding. Nonempty accesses
require valid caller-provided ranges in the intended live frame.

This revises the per-access validation contract approved in Steps 1Y and 1Z.
The compiler owns offset/width correctness; host callers must meet the same
preconditions. Stack reservation, argument pushing, and frame entry retain
their existing capacity checks. No scalar API or opcode contract changes.

Tests retain valid contiguous addressing, nested calls, native reentry, and
empty-view coverage; invalid nonempty requests are no longer executed.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Remaining and language regression groups passed, including storage-view tests.
- All four documentation preparation tests and whitespace checks passed.
- No aggregate call execution or gameplay validation is claimed.

Next slice after review: physical frame reservation and aggregate lifecycle
emission design, followed by receiver/return-destination instruction contracts.

### Step 1AB: Callable-local physical metadata

ScriptCallableBase exposes `getVariableOffset(VariableId)` and
`getVariableStorageSize`, delegating to its existing VariableMap. Functions,
methods, and constructors share the same metadata contract. Logical variable
IDs and counts remain independent of physical sizes.

Approved addressing direction: local load, store, and cleanup instructions will
receive compiler-computed physical offsets, not logical IDs. Runtime execution
will use the frame base plus that offset without a layout lookup. Parameter
instruction addressing must likewise account for physical argument layout.
This slice does not change existing opcode operands or VM access.

For locals `[counter:int, marker:Empty, location:Point, label:string]`, the
logical IDs are 0, 1, 2, and 3, while offsets are 0, 1, 1, and 4 and total width
is five slots. Layout queries reflect type changes and additional temporary
registrations and retain existing unresolved-size and overflow errors.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Remaining and language regression groups passed.
- Callable coverage verifies all three script callable categories, real
  multi-slot/empty structs, stable logical IDs, changed types, appended scratch
  locals, unresolved/deferred layouts, invalid IDs, and storage-size overflow.
- All four documentation preparation tests and whitespace checks passed.
- No physical bytecode migration, aggregate execution, or gameplay validation
  is claimed.

Next slice after review: agree on the coherent migration of local instruction
operands and all local frame consumers to physical offsets, including frame
reservation and cleanup. Review necessary operand/API changes before execution
changes; aggregate receiver and return-destination contracts follow separately.

### Step 1AC: Physical local instruction offsets

Local load, store, and cleanup instructions use full-width physical Index
operands. Type-owned load/store emission accepts NomadIndex offsets, and all
compiler local emission paths resolve logical IDs through callable metadata.
Loops and direct string defaults use those offsets as well.

Entry and return instructions reserve/pop physical local storage totals while
metadata iteration retains logical counts. Local instruction execution uses
the borrowed local-storage API without runtime layout lookup or narrowing.
Instruction descriptions and dumps treat these operands as numeric offsets.
Host-facing VariableId access, frame-backed interpreter access, and local
captures resolve IDs through the appropriate callable's layout.

Existing scalar/reference behavior and ownership are preserved. StructType
local load/store/cleanup emission remains explicitly unsupported. Parameter
instructions, aggregate call ABI, and return destinations are unchanged.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Language, remaining, binding, tooling, and LSP process regressions passed.
- New coverage executes scalar locals following three-slot padding, checks
  host and frame-backed interpreter access, verifies string initialization and
  physical frame teardown, and checks numeric instruction dumps.
- Type emission tests verify full-width offsets beyond the NomadId range.
- An initial dump regression used an executable-side instruction thunk; using
  the registered instruction name corrected the Windows test setup.
- All four documentation preparation tests and whitespace checks passed.
- No struct local execution or gameplay validation is claimed.

Next slice after review: type-owned aggregate local cleanup using finalized
field offsets and existing cleanup instructions. Aggregate load/store,
parameter/call lowering, and receiver/return-destination contracts follow
separate design approvals.

### Step 1AD: Type-owned aggregate local cleanup

Implemented:

- `StructType::emitFreeFunctionVariable` requires a finalized layout and
  delegates cleanup to owned stored-field types at physical local offsets.
- Nested structs recursively emit existing field cleanup instructions; no new
  opcodes or runtime layout lookups are introduced.
- Empty and scalar-only structs emit no cleanup instructions.
- Existing function-return cleanup now releases nested struct locals before
  popping their physical storage. Cleared, untouched local storage is releasable.
- Aggregate initialization, loads, stores, parameter cleanup, and return
  destinations remain unsupported.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Language and remaining regression groups passed.
- Emission coverage checks unresolved layouts, empty/scalar-only structs,
  nested string/object fields, and full-width offsets beyond the NomadId range.
- Execution coverage checks nested string release, untouched struct locals,
  neighboring scalar return values, and repeated calls without leaked strings.
- All four documentation preparation tests and whitespace checks passed.
- This verifies cleanup execution, not struct language syntax or gameplay.

Next slice after review: agree on aggregate local initialization and load/store
lowering, including temporary ownership and replacement semantics. Physical
parameter/call lowering and borrowed receiver/return destinations follow.

### Step 1AE: Type-owned local storage initialization

Approved scope and implementation:

- Add `Type::emitInitFunctionVariable` for reserved, cleared local storage.
- Existing scalar and class types retain cleared defaults. String types emit
  existing string initialization and local store instructions.
- Structs recursively delegate stored-field initialization at finalized offsets.
  Empty structs emit no instructions.
- Function entry delegates to types instead of selecting top-level strings.
- Script field initializers and constructors do not run during storage setup.
- Aggregate loads, stores, parameter/call lowering, and return destinations remain
  unsupported; no new opcodes or runtime layout inspection are introduced.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Language, remaining, and binding regression groups passed.
- Emission coverage checks unresolved layouts, empty/scalar-only structs,
  nested string initialization, unchanged scalar/class defaults, and full-width
  physical offsets.
- Execution coverage verifies nested empty-string defaults, scalar neighbors,
  replacement and cleanup, and repeated calls restoring shared-empty-string
  reference counts and live-string counts.
- All four documentation preparation tests and whitespace checks passed.
- This verifies local storage setup, not script constructors or gameplay.

Next slice after review: design explicit aggregate source/destination storage
and scratch-backed replacement. Parameter/call lowering and borrowed receivers
and return destinations follow.

### Step 1AF: Local copy and scratch-backed assignment emission

Approved scope and implementation:

- Add type-owned local copy emission with explicit source/destination physical
  offsets. Direct copy replaces owned destination fields and requires disjoint
  ranges; exact self-copy emits nothing.
- Scalars and references reuse existing load, cleanup, and store instructions.
  Structs recursively delegate stored-field copies at finalized offsets.
- Assignment stages the complete source into disjoint same-type scratch before
  replacing any destination fields, then releases scratch-owned fields.
- Empty values emit no instructions. Caller-supplied scratch may contain cleared
  or valid owned storage and remains releasable and reusable after assignment.
- No new opcodes, aggregate registers, runtime layout inspection, compiler
  scratch allocation, or expression lowering are introduced.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Language and remaining regression groups passed.
- Emission coverage verifies full-width source/destination/scratch offsets,
  complete staging before replacement, unresolved layouts, empty structs, and
  exact self-copy/assignment no-ops.
- Execution coverage verifies nested scalar independence, string/class reference
  sharing, replacement of owned destinations and scratch, scratch reuse, and
  overlapping reference assignment in both directions.
- Repeated calls restore live-object, live-string, and shared-empty-string
  reference counts.
- All four documentation preparation tests and whitespace checks passed.
- This verifies low-level local copying, not struct expressions or gameplay.

Next slice after review: design aggregate expression destinations and compiler
temporary allocation. Physical parameter/call lowering, borrowed receiver and
return destinations, and object-field addressing follow separately.

### Step 1AG: Local-destination expression entry point

Approved scope and implementation:

- Add `Expression::compileIntoLocal` with a resolved same-type local VariableId.
  Emission translates the ID to its physical offset.
- Scalar expressions retain their compilation path and replace the destination.
- Struct-local identifier expressions use existing type-owned local copy
  emission; source/destination must be disjoint or identical.
- Other struct expression sources remain explicitly unsupported.
- Expression cache cleanup now releases only an actually cached value, rather
  than treating the single-slot cache of a type-only expression as aggregate
  storage.
- No new opcodes, automatic temporary allocation, assignment rewiring, parameter
  changes, or struct syntax are introduced.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Language, remaining, and binding regression groups passed.
- Execution coverage verifies nested struct-local copying, scalar independence,
  reference sharing, integer/string literal destinations, and repeated-call
  cleanup without leaked strings.
- Empty struct destinations and identical locals emit no instructions.
- Unsupported struct parameter/call sources, mismatched destination types, and
  unresolved expressions fail without writing instructions.
- Type-only aggregate expressions are destroyed without freeing aggregate
  storage through their single-slot cache.
- All four documentation preparation tests and whitespace checks passed.
- This verifies the compiler entry point, not struct syntax or gameplay.

Next slice after review: compiler-owned aggregate destinations and assignment
staging allocated during resolution, before physical frame sizes are computed.
Parameter/call lowering and borrowed receiver/return destinations follow.

### Step 1AH: Compiler-owned struct assignment staging

Approved scope and implementation:

- Struct-local assignment registers one same-type staging local during script
  resolution. Repeated resolution retains the same VariableId.
- Internal names contain `@` and are hidden by the existing variable-visibility
  API. Their storage is included in physical frame reservation and return cleanup.
- RHS local-destination compilation completes before replacing the target;
  owned staging fields are released after each assignment.
- Scalar assignment behavior is unchanged. Empty structs have logical staging
  locals but consume no physical slots.
- Layout finalization and offset calculation remain deferred until emission.
- No new instructions, syntax, struct parameters/object fields/calls, or
  interpreter aggregate assignment are introduced.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Language, remaining, and binding regression groups passed.
- Tests verify staging registration before layout finalization, stable IDs
  across repeated resolution, hidden names, and physical frame widths/offsets.
- Execution coverage checks nested copies, scalar independence, self-assignment,
  staging reuse, release immediately after use, and scalar assignment behavior.
- Repeated calls restore live-string and shared-empty-string reference counts.
- Empty struct staging adds no physical slots or assignment instructions.
- All four documentation preparation tests and whitespace checks passed.
- This verifies compiled assignment staging, not struct syntax or gameplay.

Next slice after review: design physical parameter/call lowering and borrowed
receiver/return destinations. Struct call expressions and object-field locations
will extend the local-destination path in separately approved slices.

### Step 1AI: Physical parameter bytecode offsets

Approved scope and implementation:

- Parameter load/store/cleanup emission and operands use full-width physical
  Index offsets, not logical parameter IDs.
- Compiler identifier, assignment, loop, and captured-receiver emission resolve
  parameter IDs through callable layout metadata.
- Execution accesses scalar slots directly through existing parameter-storage
  views without runtime layout lookup.
- Native callback parameter APIs and special argument shapes remain unchanged.
- Struct parameter emission, aggregate argument lowering, receiver and return
  destinations remain unsupported.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Language, remaining, binding, tooling, and LSP process regression groups passed.
- Emission coverage verifies full-width parameter offsets beyond the NomadId
  range for all scalar/reference load targets, stores, and reference cleanup.
- Execution coverage manually supplies a three-slot parameter block followed
  by scalar/string parameters, verifying compiler-computed offsets, replacement,
  all load targets, native reentry inspection, and numeric instruction dumps.
- Repeated calls release replaced strings and preserve scalar results.
- All four documentation preparation tests and whitespace checks passed.
- Aggregate argument lowering, host struct parameters, and gameplay are not
  claimed as validated.

Next slice after review: field-wise struct parameter access and copying, then
aggregate argument lowering and borrowed receiver/return-destination state.

### Step 1AJ: Struct parameter-to-local copies and explicit cleanup

Approved scope and implementation:

- Type-owned parameter-to-local emission reuses scalar parameter loads and local
  replacement instructions.
- Structs recursively delegate using reverse anchor-relative parameter field
  offsets and increasing local destination offsets.
- Struct parameter identifiers compile into local destinations; existing
  local-assignment staging supports parameter sources.
- Explicit struct parameter cleanup releases owned nested fields. Function
  return does not automatically release caller-owned arguments.
- Empty structs emit nothing. No new opcodes, parameter assignment, aggregate
  argument pushing, or return destinations are introduced.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Language and remaining regression groups passed.
- Emission tests verify nested reversed parameter offsets, increasing local
  offsets, full-width operands, empty structs, and unresolved-layout failures.
- Execution tests manually supply aggregate argument blocks and verify
  parameter-to-local assignment staging, independent scalar mutation,
  string/class reference ownership, explicit parameter cleanup, and adjacent
  scalar parameters.
- Repeated calls restore live-string and live-object counts.
- All four documentation preparation tests and whitespace checks passed.
- Aggregate argument lowering, host struct parameters, and gameplay remain
  unvalidated and unsupported.

Next slice after review: local-to-parameter replacement, followed by aggregate
argument lowering and borrowed receiver/return destinations.

### Step 1AK: Local-to-parameter copying and staged parameter assignment

Approved scope and implementation:

- Type-owned local-to-parameter copy emission replaces owned parameter fields
  using existing scalar instructions and reversed nested parameter offsets.
- Struct parameter assignment allocates hidden same-type staging during script
  resolution, reusing it on repeated resolution.
- RHS completion precedes parameter replacement, supporting local and parameter
  sources and self-assignment without modifying the caller's original struct.
- Scalar assignment and caller-owned argument cleanup are unchanged.
- Empty structs consume no physical staging slots or copy instructions.
- No new opcodes, struct syntax, aggregate argument lowering, borrowed receivers,
  or return destinations are introduced.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Language, remaining, and binding regression groups passed.
- Emission tests verify full-width local/parameter operands, reversed nested
  parameter offsets, empty values, and unresolved-layout failures.
- Execution coverage verifies replacement of nested scalar/string/class fields,
  staged parameter self-assignment, repeated staging reuse, hidden stable local
  IDs, immediate staging release, and adjacent scalar parameters.
- Caller storage retains its original values and reference counts after
  parameter replacement; repeated calls restore live-string/object counts.
- Empty parameter assignments consume no physical slots or instructions.
- All four documentation preparation tests and whitespace checks passed.
- Aggregate argument lowering, host struct parameters, and gameplay remain
  unsupported and unvalidated.

Next slice after review: aggregate argument lowering, followed by borrowed
receiver/return destinations.

### Step 1AL: Aggregate expression argument lowering

Approved scope and implementation:

- Struct expression arguments allocate hidden `$argument`-prefixed same-type staging locals during
  script resolution, retaining IDs on repeated resolution.
- RHS compilation into staging precedes increasing-order field pushes using
  type-owned local-to-stack emission and existing scalar instructions.
- Staging ownership is released after pushing; existing caller argument cleanup
  recursively releases nested stack fields and pops physical widths.
- Arguments remain evaluated right-to-left. Empty structs consume zero slots;
  scalar and specialized event argument widths remain unchanged.
- Local and parameter struct expressions are supported. Struct-returning calls,
  host/interpreter aggregate argument APIs, and receiver/return destinations
  remain unsupported. No new opcodes or syntax are introduced.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Language, remaining, binding, tooling, and LSP process regressions passed.
- Emission tests verify nested increasing-order pushes, reversed stack cleanup
  offsets, full-width operands, empty structs, and unresolved-layout failures.
- Execution tests verify local arguments to script calls, parameter forwarding
  to native callbacks, mixed multiple struct/empty/scalar arguments, right-to-left
  evaluation, independent scalar copies, and reference ownership.
- Repeated resolution retains staging IDs and physical frame sizes; repeated
  calls restore live-string/object counts and preserve caller values.
- All four documentation preparation tests and whitespace checks passed.
- Host/interpreter aggregate argument APIs and gameplay remain unvalidated.

Next slice after review: design borrowed receiver and caller-provided return
destination state, including save/restore across calls and native reentry.

### Step 1AM: Direct struct identifier argument pushes

Approved scope and implementation:

- Struct local and parameter identifier arguments push owned field copies
  directly, without allocating or copying through `$argument` staging.
- Stack-target struct loads recurse in increasing physical field order; parameter
  source offsets remain reverse anchor-relative. Existing scalar push opcodes
  retain reference fields.
- Other expression forms retain the existing staging destination path.
- Argument evaluation order, physical widths, caller cleanup, and pass-by-copy
  behavior remain unchanged. No new opcodes or aggregate registers are added.
- Empty identifier arguments allocate no staging and push no slots.

Validation:

- Built `nomad_test`, `nomad-runtime`, and `nomad-lsp`.
- Language, remaining, and bindings regression groups passed.
- Tests cover direct local/parameter copies, nested full-width offsets, ownership,
  repeated resolution/calls, mixed argument evaluation, empty structs, and
  unresolved-layout rejection before emission.
- Four documentation tests and whitespace checks passed.

Next slice after review: borrowed receiver and caller-provided return-destination
state, including nested calls and native reentry.

### Step 1AN: Borrowed struct call-state foundations

This validated prototype has been replaced by Step 1AP. Step 1AO supersedes its
per-script-call vector snapshots and optional VM bindings; they are not the
approved final compiled-call ABI.

Approved scope and implementation:

- Separate optional borrowed receiver and return-destination spans distinguish
  unbound storage from bound empty structs. Const access exposes read-only spans;
  accessing an unbound span faults explicitly.
- Explicit state is supplied at script/native function or method call entry,
  after argument evaluation. Ordinary calls start unbound.
- Typed VM-owned script frame snapshots preserve enclosing state without
  changing existing four-slot value frames or physical offsets.
- Native call frames and host script-run snapshots preserve bindings on normal
  completion and exception propagation. Failed host script runs discard their
  nested typed snapshots.
- Native object properties and indexers isolate and restore aggregate state.
- Views never retain/release enclosing objects or own struct values. Stable
  storage and owner lifetime remain the caller's responsibility.
- No new opcodes, syntax, compiled struct calls, aggregate host argument APIs,
  or aggregate yield/resume support are introduced.

Validation:

- Built Debug tests, runtime, and LSP targets.
- Language, remaining, bindings, tooling, and LSP process regressions passed.
- Focused tests cover local-backed/nested spans, mutation without copying or
  retaining referenced fields, bound empty versus unbound state, native success
  and failure, repeated host script reentry with failed nested frames, object
  members/constructors, and stack-overflow preservation.
- Four documentation tests and whitespace checks passed.
- No compiled struct receiver/return expressions or gameplay were validated.

Next slice after review: exact receiver/return-destination binding and access
instruction contracts, followed by struct method and destination-return lowering.
Object-field lowering will retain enclosing owners before argument evaluation;
this foundation does not implement that lifetime management.

### Step 1AO: Approved minimal hidden-pointer call ABI

This is an approved design step only. No runtime or bytecode implementation has
changed in this slice.

Execution constraints:

- The compiler resolves storage sizes, physical field offsets, hidden argument
  positions, ownership operations, and required call shape.
- Call/frame bookkeeping uses the existing fixed VM value stack, without C++
  heap allocation or a separate aggregate-state vector.
- Emit only required storage descriptors and ownership operations. Favor
  compiler-selected instructions over runtime shape checks or dispatch.
- Trusted storage operations do not validate spans, look up logical variable
  IDs, or inspect runtime type tags. Existing stack-capacity checks remain.

Hidden argument layout:

- A nonempty struct-returning call receives one borrowed destination pointer.
- A method with a nonempty borrowed struct receiver receives one receiver pointer.
- Push the destination pointer first when required, then the receiver pointer
  when required, then explicit arguments in their existing right-to-left order.
- Each descriptor occupies one `RuntimeValue` slot containing a dedicated
  `RuntimeValue*` storage address. It contains no width, optional flag, or type
  tag and is not an object reference. Add explicit internal address support;
  do not cast storage pointers to `NomadObject*`.
- Let `N` be the explicit arguments' total physical width. The receiver
  descriptor, when present, is at anchor-relative offset `N`. The destination
  descriptor, when present, is at `N + 1` if a receiver descriptor exists,
  otherwise at `N`.
- Hidden descriptors do not enter logical parameter IDs or ordinary parameter
  width metadata. Existing explicit parameter offsets and native scalar argument
  APIs remain unchanged. The compiler computes their positions from the signature.
- Empty struct receivers and results require no descriptors or runtime presence
  markers. Their types remain distinct from `void` in compiler metadata.
- Scalar/void results have no hidden destination descriptor. Ordinary struct
  arguments still pass owned field copies, not borrowed pointers.

Call and lifetime behavior:

- Reuse existing script/native call and return instructions where their
  parameter anchors and scalar/object behavior suffice. Do not add call variants
  merely to encode combinations of absent/present hidden pointers.
- Descriptors below evaluated arguments do not alter any VM binding register.
  They become the callee's hidden parameters only when the call establishes its
  argument anchor. Nested argument evaluation cannot inherit them.
- Returning restores existing anchors; enclosing descriptors remain in their
  original argument slots. No aggregate receiver/destination register or
  aggregate-state save/restore stack is required.
- Callers release owned argument fields and pop the explicit and hidden slots
  using compile-time counts. Hidden pointers are never released as objects.
- Native calls and synchronous host script reentry use existing parameter-frame
  restoration. Aggregate host argument construction remains a separate slice.
- Descriptor storage is stable for the operation. For object-field storage,
  retain the enclosing owner before argument evaluation and release it after
  completion or failure. Any owner-lifetime slots are separate from descriptors.
- Destination storage follows the approved initialization/replacement contract.
  Existing live targets initially stage the RHS before replacement; passing an
  address does not authorize premature mutation or unsafe aliasing.

Required instruction semantics:

- Push a local storage address using a compiler-computed physical local offset.
- Push a parameter storage address using the compiler-folded distance from the
  argument anchor to the first slot, including its finalized block width.
- Compose a borrowed subfield address from a hidden pointer at a constant
  parameter offset plus a constant physical field offset.
- Load/store through a hidden pointer and constant field offset, selecting
  scalar or reference ownership behavior during compilation. Reuse existing
  result/intermediate/argument registers and type-owned field-wise emission.
- No instruction switches on storage owner, aggregate width, or call shape.
  Compiler-known widths need not be saved on the stack or read by the callee.
- Object-field address extraction and exact retained-owner lowering follow
  separately; accessor-backed properties still produce owned temporaries.

Next implementation slice:

- Replace the Step 1AN prototype with internal address slots and minimal
  local/parameter-backed hidden-pointer execution.
- Verify descriptor counts, unchanged explicit parameter offsets, nested script
  and native calls, host reentry/failure restoration, zero-width omission, owned
  field copies/cleanup, and absence of call-bookkeeping heap allocation.
- Do not add struct syntax, generic collections, object-field lowering, or
  complete constructor/return-expression lowering in that slice.

Then wire struct methods and caller-destination returns, followed by retained
object-field owners and conservative RVO eligibility. Review exact instruction
names and any additional required specialization during implementation.

### Step 1AP: Stack-pointer execution foundation

Approved scope and implementation:

- Removed `StructCallState`, optional binding accessors, and the per-call
  aggregate-state vector. Existing script/native frame shapes and parameter
  anchors handle nested calls and host reentry without aggregate bookkeeping.
- Added dedicated borrowed `RuntimeValue*` address slots without object casts,
  ownership changes, or an increase in the value union's required pointer size.
- Added trusted local/parameter address access and indirect field references;
  no span, presence, size, or type checks execute in these paths.
- Registered address-push and scalar/reference indirect load/push/store/cleanup
  opcodes with full-width `Index` operands. Parameter address instructions take
  the compiler-folded anchor distance to the block's first slot.
- Existing call/return instructions execute low-level hidden-pointer fixtures.
  Explicit argument offsets and logical metadata remain unchanged.
- Pointer descriptors are passed/popped as unowned slots. Reference field copies
  retain ownership; indirect replacement uses explicit cleanup before storing.
- Compiler-driven method/return-expression lowering, struct syntax,
  object-field owners, aggregate host arguments, and yield/resume remain deferred.

Validation:

- Built Debug tests, runtime, and LSP targets successfully.
- Language, remaining, bindings, tooling, and LSP process regressions passed.
- Instruction fixtures cover full-width pointer/field offsets and address dumps,
  compiler-folded parameter distances, all scalar/reference indirect operations,
  explicit argument offsets, borrowed mutation, forwarding, and owned cleanup.
- Repeated bytecode calls restore string counts. Nested script/native calls and
  host reentry failures preserve enclosing pointer descriptors and argument anchors.
- Exact-capacity tests show calls need only existing frames and requested
  pointer slots; calls with no descriptors incur no aggregate-state slots.
- A Windows Debug CRT allocation hook measured zero allocations across 100
  iterations of nested script calls and a native pointer-writing callback.
  This checks call bookkeeping, not allocations performed by script operations,
  constructors, host argument setup, or arbitrary native callbacks.
- Four documentation tests and whitespace checks passed.
- Actual compiler RVO selection, struct syntax, and gameplay remain unvalidated.

Next slice after review: type-owned emission to borrowed destinations and
compiler-owned hidden-argument layout/lowering. Then wire struct methods and
caller-destination returns, followed by retained object-field owners and
conservative RVO eligibility.

### Step 1AQ: Type-owned borrowed storage emission

Approved scope and implementation:

- Types emit copies from locals and ordinary parameters into borrowed storage,
  and from borrowed storage into locals, using existing address-based opcodes.
- Structs recurse through stored fields with finalized increasing borrowed/local
  offsets and reverse anchor-relative ordinary parameter offsets.
- Class/string fields select reference-retaining load instructions and release
  replaced ownership before storing. Scalar fields select scalar instructions.
- Borrowed cleanup releases owned nested fields without releasing descriptors
  or enclosing objects. Stack-target borrowed struct loads push field copies in
  increasing order; result/intermediate aggregate loads remain unsupported.
- Empty types emit no instructions. Unfinalized struct layouts fail before
  emission. Source/destination ranges must be same-type and disjoint; possible
  overlap requires prior staging into disjoint local storage.
- The emitter does not allocate scratch or initialize destinations/run constructors.
  Copies retain existing result-register ownership and clobbering contracts.
- No new opcodes, runtime checks, VM allocation, or language syntax are introduced.

Validation:

- Built Debug tests, runtime, and LSP targets successfully.
- Language, remaining, and bindings regression groups passed.
- Emission tests verify full-width pointer/source/field offsets, nested parameter
  reversal, scalar/reference opcode selection, stack order, cleanup offsets,
  zero-width omission, and unfinalized-layout/register-load rejection.
- Execution tests exercise repeated local-to-borrowed, parameter-to-borrowed,
  and borrowed-to-local copies of nested scalar/string/class fields, borrowed
  subranges, ownership replacement, independent scalar copies, stack copies,
  sentinel preservation, and restored live object/string counts.
- Four documentation tests and whitespace checks passed.
- Automatic hidden-call lowering, actual RVO selection, and gameplay remain
  unvalidated and are not implemented by this slice.

Next slice after review: compiler-owned hidden-argument layout and lowering,
using these emitters for caller destinations. Then struct methods and returns,
retained object-field owners, and conservative RVO eligibility.

### Step 1AR: Stored-property opcode specialization

Approved optimization detour and implementation:

- A shared compiler property-access path selects native accessors versus
  single-slot scalar/reference stored fields, resolving physical offsets before
  emission. Aggregate stored fields retain the existing unsupported path.
- Four specialized load/store opcodes use full-width `Index` offsets and omit
  property-ID lookup, native/stored dispatch, field-index resolution, declared
  type lookup, and stored-field-width validation.
- Reference writes retain the new value before releasing the old one; result
  ownership remains with the caller. Scalar operations copy their slots.
- Property expressions, assignments, constructor-inferred `this` writes,
  explicit field initializers, and inferred defaults use the shared emitter.
- Native callbacks and checked host/interpreter APIs retain existing behavior.
- Receiver validity and actual instance-capacity guards remain: the compiler
  does not prove every receiver is a non-null ScriptObject, and older instances
  do not resize when an open class gains stored fields.
- No aggregate field lowering, call-ABI changes, or generic collection support
  are introduced. No runtime performance percentage is claimed without a benchmark.

Validation:

- Built Debug tests, runtime, and LSP targets successfully.
- Language, remaining, bindings, and tooling regression groups passed.
- Emission/execution tests verify offsets above `NOMAD_ID_MAX`, physical versus
  logical field addressing after embedded storage, native accessor fallback,
  scalar/reference load/store selection, self-assignment, result ownership,
  and instruction dumps.
- Source-level tests cover field initializers, constructor and ordinary `this`
  writes, chained reads, reference replacement, nested RHS calls, and repeated
  live-object/string restoration.
- Invalid/null receivers and instances allocated before field extension retain
  explicit errors rather than unchecked memory access.
- Four documentation tests and whitespace checks passed.
- Gameplay and a before/after performance benchmark were not run.

Next slice after review: resume compiler-owned hidden-argument layout/lowering,
then struct methods and caller-destination returns. Scalar local/parameter span
elimination and native-indexer specialization remain separate optimization work.

### Step 1AS: Consistent terminology and compiler-owned destination calls

Approved scope and implementation:

- Renamed the new stored-property opcodes to `op_stored_property_load_r`,
  `op_object_stored_property_load_r`, `op_stored_property_set`, and
  `op_object_stored_property_set`, and the VM accessor to `getStoredProperty`.
  Existing logical field-storage APIs retain their established names.
- Callable/script-callable metadata computes explicit physical argument width,
  optional borrowed receiver/return-pointer offsets, and arguments-plus-hidden
  slot count. It omits empty-struct descriptors and scalar/void return pointers.
- Static queries use signature widths, including expanded event callbacks;
  call-site queries accept compiler-resolved widths for event dispatch.
  Unresolved layouts and total-slot overflow fail explicitly.
- `Compiler::addFunctionCallIntoLocal` emits script/native free-function calls
  into reserved same-type struct locals, preserving argument evaluation order,
  ordinary call/return instructions, argument ownership cleanup, and unowned
  pointer popping. Empty results emit no pointer or pointer cleanup.
- Callee fixtures use type-owned destination-copy emission. This helper does not
  resolve arguments, initialize destination storage, or choose RVO eligibility.
- No new opcodes beyond the terminology rename, runtime layout queries,
  per-call allocation, struct syntax, or automatic return-expression lowering
  are introduced.

Validation:

- Built Debug tests, runtime, and LSP targets successfully.
- Language, remaining, bindings, and tooling regression groups passed.
- Verified no references to the old opcode/accessor names remain in code/tests.
- Layout tests cover logical/physical parameter preservation, expanded event
  callbacks, call-site event-dispatch widths, borrowed receiver/return offsets,
  empty/scalar omission, unresolved sizes, and total-slot overflow.
- Destination-call tests execute linked script and native calls, preserve
  caller values when the callee mutates parameter copies, evaluate nested
  arguments, replace owned destination fields, omit empty-result pointers,
  reject wrong destination types before emission, and restore string counts.
- Four documentation tests and whitespace checks passed.
- Automatic return-expression lowering, actual RVO selection, and gameplay
  remain unvalidated and are not implemented by this slice.

Next slice after review: wire struct-returning function expressions and return
statements into caller destinations, including staged existing assignment
targets. Borrowed struct methods and retained object-property owners follow.

### Step 1AT: Compiled struct expressions and destination returns

Approved scope and implementation:

- Struct-returning script/native free-function expressions compile into reserved
  local destinations. Existing assignment and argument staging use this path.
- Script returns evaluate into a compiler-owned local, then copy its fields into
  the caller's hidden destination before ordinary local cleanup. This is a
  conservative copy path, not return-call forwarding or NRVO.
- Discarded struct calls receive local storage and release their owned fields.
- Empty results still evaluate calls and side effects without a destination
  pointer or physical result slots.
- Hidden locals are reused across resolution passes and remain invisible as
  source variables. Scalar/class/void paths retain their existing behavior.
- Scalar-register and interpreter aggregate call expressions fail explicitly.
  Host free-function calls reject unsupported struct arguments/results before
  using single-slot host storage.
- No new opcodes, per-call runtime layout selection, or call-state allocations
  are introduced.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- End-to-end source coverage exercises nested returns, local/parameter copies,
  parameter replacement, existing assignments, aggregate arguments, early
  branches, discarded results, empty-result side effects, repeated resolution,
  and string ownership restoration.
- Language, remaining, bindings, and tooling regression groups passed.
- Unsupported scalar-register, interpreter, and host return paths are covered
  by rejection tests; callbacks do not execute on rejected host returns.
- Four documentation tests and whitespace checks passed.
- Struct syntax, borrowed methods, direct return-call forwarding/NRVO, and
  gameplay are outside this slice.

Next slice after review: borrowed struct method calls and retained object-property
owners. Direct return-call forwarding and conservative NRVO remain separate
optimization work.

### Step 1AU: Compiler-owned borrowed struct method calls

Approved ABI implementation slice:

- Added `Compiler::addStructMethodCall` for resolved script/native methods,
  borrowing local, ordinary parameter, or enclosing struct-method receiver
  storage. This is an emission helper, not automatic member-expression lowering.
- Struct results use reserved same-type locals; scalar/reference results use the
  existing result register, and void results use no destination.
- Pushes only nonempty destination/receiver pointers, in ABI order. Parameter
  anchor distances are calculated during compilation; nested receivers use the
  existing indirect-address opcode.
- Explicit arguments remain owned copies, with existing evaluation order and
  cleanup. Hidden pointers use specialized one/two-slot cleanup without object
  ownership operations.
- Existing script/native method call frames preserve object `this`; no extra
  receiver binding or `push_this`/`pop_this` pair is emitted.
- Receiver/destination metadata is checked before emission. No new VM opcode,
  runtime receiver selection, or call-state allocation was added.
- TypeManager's convenience method registration and source-level member
  resolution remain class-only. Low-level composite registration supplies the
  method fixtures, without broadening those APIs in this slice.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Remaining regression group passed, including executable method fixtures for
  local mutation, parameter-copy isolation, nested borrowed receiver forwarding,
  script/native calls, right-to-left argument evaluation, struct destinations,
  scalar/reference/void results, and empty descriptor omission.
- Repeated calls restore string/object counts, and invalid metadata is rejected
  before emission.
- Language, bindings, and tooling regression groups passed.
- Four documentation tests and whitespace checks passed.
- Source-level struct methods, object-property owner retention, gameplay, and
  performance benchmarks remain outside this slice.

Next slice after review: struct method registration/resolution and source-level
receiver lowering. Retained object-property addressing requires its own approved
lowering/opcode design; return-call forwarding and NRVO remain later work.

### Step 1AV: Registered struct methods in source expressions

Approved scope and implementation:

- Added native/script struct-method registration entry points alongside the
  existing class APIs. Shared composite registration preserves parameter,
  ownership, duplicate, callable-catalog, and member-signature contracts.
  Runtime convenience APIs dispatch by owner category; class-only APIs continue
  to reject struct owners, and struct-only APIs reject class owners.
- Member resolution finds methods on either composite category. Compiled method
  calls borrow struct locals, parameters, or the enclosing method's `this`,
  using the Step 1AU emitter without new VM opcodes.
- Struct-returning method expressions support staged assignments, arguments,
  return statements, and discarded calls. Explicit struct arguments remain
  owned copies, including ordinary uses of `this`.
- A struct method can copy and return `this` through borrowed storage, and
  forward its receiver through nested registered methods.
- Temporary/property receivers, direct struct property access, struct closure
  captures, and interpreter/host aggregate method storage fail explicitly.
  Method registration retains source but does not automatically compile it.
- Type inference enumerates composite properties and reports unresolved member
  receiver types instead of dereferencing null while inference is incomplete.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Remaining regression group passed with executable source/AST fixtures covering
  native and script calls, forward return inference, receiver mutation,
  parameter-copy isolation, nested calls, struct/reference/void results,
  self-assignment staging, ordinary `this` arguments without extra argument
  staging, discarded results, empty calls, and repeated string-count restoration.
- Registration tests cover category checks, catalog/overload identity, duplicate
  signatures, and invalid callback/return metadata.
- Language, remaining, bindings, and tooling regression groups passed,
  including unsupported-path, host-method, interpreter, and capture rejection
  coverage.
- Four documentation tests and whitespace checks passed.
- Struct declarations, property addressing, owner retention, copy-elision
  selection, gameplay, and performance benchmarks remain outside this slice.

Next slice after review: shared struct property addressing for locals,
parameters, and borrowed receivers, followed by retained object-property owners.
Discuss any necessary new address opcode before implementing it. Return-call
forwarding and NRVO remain later optimization work.

### Step 1AW: Shared stored struct property addressing

Approved scope and implementation:

- Added compiler-only storage locations rooted in logical locals, ordinary
  parameters, or borrowed `this`, with flattened stored property offsets.
  Emission computes local offsets and reverse parameter anchor distances from
  finalized layouts. Existing opcodes handle all addressing and copies.
- Stored struct properties support scalar/reference reads and writes, aggregate
  local copies, direct argument pushes, returns, and discarded values.
  Nested struct properties can borrow their storage for registered methods.
- Scalar/reference writes evaluate the RHS before releasing and replacing the
  old leaf. Aggregate writes stage the full RHS before replacing any destination
  slots, preserving self-assignment and receiver-mutating call behavior.
- Source `this.property` assignments on structs use member expressions while
  class field inference retains its existing path.
- Empty properties emit no storage pointer or field access; RHS calls and
  methods still execute. Reference properties retain existing class semantics.
- Unknown stored struct properties and unsupported temporary/object-owned
  struct receivers fail explicitly. This slice does not add declaration syntax,
  property inference, owner retention, or new VM opcodes.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Language, remaining, bindings, and tooling regression groups passed.
- Executable fixtures cover nested local/parameter/borrowed property mutation,
  parameter-copy isolation, scalar/reference writes, aggregate replacement,
  self-assignment, receiver-mutating RHS calls, nested method receivers,
  argument/return copies, empty side effects, and discarded empty properties.
- Reference-field tests preserve shared class mutation while isolating property
  replacement; repeated executions restore live string/object counts.
- Bytecode tests verify nested parameter address and copy anchor operands.
  Negative tests cover unknown properties, mismatched assignments, temporary
  receivers, and unsupported object-owned struct addressing.
- Four documentation tests and whitespace checks passed.
- Gameplay and performance benchmarks were not run.

Next slice after review: retained object-property owner/address lowering. Discuss
the owner lifetime and any necessary new address opcode before implementing it.
Return-call forwarding and NRVO remain later optimization work.

### Step 1AX: Checked stored property address primitive

Approved scope and design:

- Add the address primitive first, independently of compiler owner-lifetime
  lowering and source-level object-owned struct access.
- The original evaluated object must remain alive through argument evaluation
  and the borrowed call, even if an argument replaces its former variable.
  The compiler will keep that owner on the value stack in the following slice.
- Use one new opcode with compiler-computed owner-stack distance and inclusive
  first/last physical slot offsets. Computing the last slot at compilation
  avoids runtime width/offset arithmetic.
- Check script-object storage and actual allocation capacity before borrowing.
  Do not allocate, build spans, copy aggregate values, or retain an owner inside
  the address opcode. Empty structs require no address.

Implementation:

- Added `op_stored_property_address_push` with three full-width index operands.
  It reads an already-retained owner without consuming it and pushes one
  borrowed pointer, preserving result/intermediate registers.
- Added `VirtualMachine::getStoredPropertyAddress` for ordered, nonempty
  inclusive ranges. Existing scalar stored-property access shares its storage
  checks without changing faults or ownership behavior.
- No compiler emission or source-level support is enabled in this slice.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Language and remaining regression groups passed.
- Executed hand-emitted script fixtures verify nested address mutation,
  retained-owner stack distances with and without a destination descriptor,
  register preservation, unchanged owner reference counts, descriptor cleanup,
  and restoration of live string/object counts.
- Opcode metadata and instruction dumps include all three index operands.
  Faulting bytecode covers null/non-storage receivers, empty allocations,
  older objects after class extension, partial missing ranges, and a last-slot
  operand above `NOMAD_ID_MAX`.
- Four documentation tests and whitespace checks passed.
- Source-level object-owned struct access, gameplay, and performance benchmarks
  were not validated or enabled in this slice.

Next slice after review: compiler-managed owner retention and stored struct
property method receivers, using the existing hidden-pointer call ABI. Keep the
owner alive until argument, descriptor, and call cleanup have completed.
Property reads/writes and aggregate destination lowering follow separately.
Return-call forwarding and NRVO remain later optimization work.

### Step 1AY: Retained object-owned struct method receivers

Approved scope and implementation:

- Extended compiler storage locations with a stored struct property rooted in a
  resolved class expression. Nested property selection flattens offsets while
  retaining the same owner expression.
- Source method calls on these locations evaluate the owner once into an owned
  stack slot before destination/receiver descriptors and explicit arguments.
  The existing property-address opcode receives compiler-computed inclusive
  slot bounds and the owner's stack distance.
- Script/native callees keep the existing hidden-pointer ABI. Explicit arguments
  retain right-to-left evaluation and owned-copy behavior. Replacing the
  original object variable during argument evaluation cannot destroy or
  redirect the borrowed receiver.
- Argument and descriptor cleanup precede owner release; scalar/reference
  results remain owned and struct results occupy their independent destination.
  Empty receivers/results omit their descriptors while owner evaluation and
  call side effects still execute.
- Existing local/parameter/borrowed receivers emit no extra owner operations.
  No new VM opcode, runtime call state, or per-call allocation was added.
- Object-owned struct value loads, writes, and aggregate copies remain explicit
  unsupported paths. This slice does not change the VM's documented fatal-fault
  behavior or add exception unwinding.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Language, remaining, bindings, and tooling regression groups passed.
- Executable source fixtures cover nested stored receivers, native/script
  methods, class-method `this`, temporary class owners, owner replacement during
  argument evaluation, nested calls, and scalar/reference/struct/void results.
- Evaluation tracing verifies the owner is evaluated once before right-to-left
  arguments. Copied results survive owner destruction without aliasing the
  struct's value storage.
- Empty methods execute on retained owners, including objects with zero
  physical storage, without emitting a receiver-address opcode.
- Bytecode checks cover compiler-computed inclusive nested ranges and owner
  distances both with and without a result descriptor. Existing local/parameter
  call regression coverage continues to pass.
- Repeated execution restores live string/object counts. Unsupported direct
  value reads, scalar/aggregate writes, temporary struct receivers, and unknown
  properties still produce compilation errors.
- Four documentation tests and whitespace checks passed.
- Gameplay, performance benchmarks, and fault-unwind cleanup were not tested.

Next slice after review: object-owned struct property reads and writes,
including aggregate argument/return copies and staged replacement. Reuse shared
storage lowering and retain evaluated owners across RHS effects. Return-call
forwarding and NRVO remain later optimization work.

### Step 1AZ: Object-owned struct value reads and writes

Approved design:

- Add six specialized indirect-storage opcodes: scalar/reference load,
  scalar/reference push, store, and reference release. Intermediate holds the
  borrowed address; physical property offsets are compiler-computed.
- Validate the nonempty object range once with the existing address opcode.
  Indirect field operations add no spans, type checks, source-kind branches,
  or allocation.
- Retain the evaluated destination owner and capture its address before the
  RHS. Restore that address afterward and stage complete aggregate replacements.
- Keep existing local/parameter paths and fatal-fault behavior unchanged.

Implementation:

- Indirect VM operations and type-owned scalar/reference/recursive struct
  emission, including defaults and local copies.
- Object-owned value loads, direct argument pushes, return copies, and staged
  assignments through shared storage locations. Pure field-push sequences keep
  their owner in result while preserving the address in intermediate.
- Class `this` aggregate assignments, explicit aggregate property initializers,
  and defaults for constructor-inferred struct properties use the same storage
  operations rather than single-slot result initialization.
- Expanded regression fixtures for ownership, source/destination aliasing,
  RHS-side owner replacement, empty storage, reference fields, and class
  property initialization.

Validation:

- The user completed the previously blocked build and reported a successful
  CTest run. Expanded regression validation identified one negative-test
  expectation using the wrong wording for an existing class-field diagnostic;
  the expectation was corrected without changing compiler behavior.
- Debug tests and LSP targets built successfully. Language, remaining,
  bindings, and tooling regression groups passed.
- Executable coverage verifies reads/writes through temporary and retained
  owners, direct aggregate argument pushes, return copies, nested properties,
  reference ownership, self-assignment, and RHS-side owner replacement.
- Class-field tests cover explicit struct initializers, constructor-inferred
  defaults, and aggregate `this` assignments.
- Empty values preserve owner evaluation and RHS side effects without accessing
  nonexistent storage. Repeated execution restores live string/object counts.
- Four documentation tests and whitespace checks passed. Gameplay, performance
  benchmarks, and fault-unwind cleanup were not validated.

Next after review: agree on the next construction/declaration slice. Struct
construction and declaration syntax remain unsupported; return-call forwarding
and NRVO remain later optimization work.

### Step 1BA: Struct constructor ABI metadata and registration

Approved scope:

- Struct constructor callables return `void` and borrow the caller's destination
  as `this`. Nonempty destinations require one hidden receiver pointer; empty
  destinations require none. Construction expressions retain the constructed
  struct type, distinct from the callable result.
- Implement metadata and registration only. Destination-based `new` execution,
  initializer sequencing, and declaration syntax remain separate slices.

Implementation:

- Native constructors retain independent constructed-type metadata. Class native
  factories still return their declared created class, including factories whose
  created class differs from their declaring owner.
- TypeManager exposes explicit native/script struct constructor registration.
  Runtime dispatches compatible registration APIs by owner category.
- Shared composite registration validates and publishes constructor overloads;
  duplicate signatures are rejected across native and script sources.
- Call storage layouts describe nonempty borrowed constructor destinations and
  copied explicit arguments. Parser constructor lookup includes structs.
- Source construction, struct constructor body compilation, host object
  construction, and class-native calls explicitly reject struct constructors
  until destination lowering is implemented.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Language, remaining, bindings, and tooling regression groups passed.
- Constructor tests cover owner category rejection, invalid registration,
  native/script duplicates, overload catalogs, constructed-type identity,
  empty/nonempty destinations, and aggregate argument widths.
- Class factory tests preserve differing owner/created types and class results.
- Source construction, constructor body compilation, and host/native class call
  rejection are tested; rejected callbacks are not invoked.
- Four documentation tests and whitespace checks passed. Struct constructor
  execution, gameplay, and performance remain unvalidated in this metadata slice.

Next after review: destination-based `new` calls and generated field initializer
sequencing, followed by struct declaration syntax. Return forwarding and NRVO
remain later optimization work.

### Step 1BB: Specialized stack-fed object and struct creation

Approved scope:

- Native object constructors remain factories. Expose `createNativeObject` and
  `createScriptObject` with constructor IDs and prepared VM-stack arguments.
- Expose `createNativeStruct` and `createScriptStruct` with fresh borrowed
  `RuntimeValue*` destinations and stack arguments. No struct allocation,
  argument vectors, or heap-allocated call bookkeeping.
- Keep allocation-only script object creation distinct as `allocateScriptObject`.
- Wire destination-based struct expressions and property initialization.
  Keep declaration syntax and interpreter aggregate values out of scope.

Implementation:

- Removed the vector-fed, source-dispatching object construction API and migrated
  host bindings, interpreter construction, and tests to specialized entry points.
- Dedicated object and struct creation opcodes select native/script execution.
  Struct destination distances are compiler-computed; empty destinations omit
  the pointer slot and use an explicit absent-distance operand. Callable operands
  retain named constructor targets in instruction dumps.
- Struct creation initializes properties before native/script bodies. Registered
  generated property initializer bodies share class initializer sequencing;
  otherwise struct types provide ordinary defaults. Creation into locals supports
  staged assignment, constructor arguments, return copies, and class properties.
- Explicit Runtime class/struct registration APIs mirror TypeManager. Generic
  registration entry points remain compatible delegates.
- Struct lifecycle traversal and initializer lookup no longer allocate catalog
  copies. Scalar indirect default initialization now loads the actual type default
  rather than relying on a no-op scalar register initializer.
- At this milestone, interpreter constructor arguments retained left-to-right
  evaluation and were reversed in-place into stack-call order. Step 5B below
  aligns shared interpreter argument evaluation with compiled code.
- Creation paths trust compiler-verified constructor categories, compiled bodies,
  initializer signatures, and prepared stack arguments rather than checking them
  again at runtime. Direct host callers share these preconditions.

Validation:

- Debug tests, runtime, and LSP targets built successfully.
- Language, remaining, bindings, and tooling regression groups passed, including repeated ownership
  restoration, object factory compatibility, stack-fed host reentry, nested
  aggregate constructor arguments, property defaults, explicit initializers,
  class property construction/replacement, and empty constructor side effects.
- MSVC Debug allocation hooks verify zero allocations for repeated native/script
  struct creation. Object creation adds no allocations beyond direct object
  storage, including Debug standard-library iterator proxies.
- Shell-based documentation tests, whitespace validation, and worktree inspection
  were unavailable because the sandbox disabled the local-network access required
  to start shell commands. CMake Tools build and regression execution succeeded.
- Complete fault unwinding, gameplay, and performance benchmarks are not covered.

Next after review: struct declaration syntax and member discovery, using these
registered construction paths. Return forwarding and NRVO remain later work.

Remaining Step 1 work, requiring separate design approval:

- Integrate the real struct descriptor with approved aggregate emission.
- Preserve current sizes and behavior for existing non-struct types.
- Preserve logical IDs, names, catalog entries, and API signatures where possible.
- Extend the layout coverage to the real struct descriptor, including unresolved
  and zero-size types remaining distinct from `void`.
- Do not add struct syntax or generic collection support in this step.

Review stop: metadata changes, compatibility results, and any revised architecture
proposal.

### Step 2: Struct declarations and semantic analysis

Prerequisite: step 1 approved.

- Register struct type symbols before resolving cross-file signatures.
- Reuse approved declaration/member-resolution paths for explicit fields,
  constructor-inferred fields, methods, overloaded constructors, and hub names.
- Finalize nested layouts after inference, not during early symbol registration.
- Enforce the agreed generic-argument exclusion during compilation, including
  nested generic arguments. Report source diagnostics rather than runtime
  failures.
- Keep empty structs distinct from `void` in expressions and signatures.
- Present the open-struct reuse assessment and obtain its scope decision.
- Test declarations, forward references, inference, invalid inline cycles,
  duplicate/conflicting members, zero-size fields, and generic diagnostics.

Review stop: analysis-only behavior and the open-struct decision. Do not claim
executable struct support at this stage.

Approved scope:

- Implement source declaration analysis in one reviewable slice. The reviewed
  follow-up specializes class/struct declaration and field-resolution paths
  rather than selecting their behavior with type-condition branches.
- Finalize source struct layouts after inference and member discovery in the
  existing Resolve phase. Do not add a compilation phase, opcode, or ABI.
- Retain resolved property paths during inference and query physical offsets
  only during validation/emission. A private property-offset query was approved.
- Defer open structs for this milestone. Reuse requires more than parsing:
  the descriptor currently fixes closed mode, and external field introduction
  must be generalized and completed before layouts freeze.
- Test semantic analysis, preserve class behavior, then stop without committing.

Implementation:

- `struct Name ... end` publishes symbols before cross-file signatures and
  supports hub-qualified declarations, explicit initializer fields,
  constructor-inferred fields, methods, overloaded constructors, and generated
  `$init`/default constructors.
- Separate `ClassDeclaration` and `StructDeclaration` ASTs retain their concrete
  type descriptors. Struct members and field initializer statements have their
  own classes, and class/struct registration and field resolution use specialized
  entry points.
- Empty structs retain their type identity in parameters and return expressions.
  Nested layouts finalize after semantic resolution, with source diagnostics for
  inline cycles.
- Struct generic arguments are rejected before specialization in annotations,
  explicit generic construction, and inferred generic construction. Recursive
  annotation parsing rejects nested occurrences.
- Open structs receive an explicit unsupported diagnostic. Interpreter aggregate
  values, captures, temporary receivers, NRVO, and complete executable source
  struct behavior remain outside this slice.

Validation:

- Windows Debug tests, compiler, runtime, and LSP targets built successfully.
- All five unit-test groups passed: language, remaining, bindings, tooling, and
  audio. Compiler export and LSP process integration tests also passed.
- New analysis-only cases cover cross-file/hub symbols, empty type identity,
  inferred fields, constructor/method overloads, nested and zero-width fields,
  closed-field conflicts, inline cycles, reference cycle breaks, and explicit,
  nested, and inferred generic-argument diagnostics.
- Verified the agreed metadata example: `Point` has three logical fields/slots;
  `Player` has two logical fields/four slots; `printPoint` has one logical
  parameter/three slots; `updatePoint` has three logical parameters/five slots.
- Analysis tests assert that instructions are not emitted and callable entry
  points remain uncompiled. Existing compiled/interpreter/host regressions pass,
  but complete executable source structs and gameplay were not validated.
- Whitespace validation passed. Published language documentation is unchanged.

Declaration-specialization follow-up:

- Restore class-specific declaration parsing and metadata. Add matching
  `StructDeclaration`, `StructMemberDeclaration`, and
  `StructFieldInitializerStatement` headers/sources for structs.
- Preserve declaration syntax, inference, layout timing, and construction
  semantics. Use existing explicit class/struct registration APIs.
- Add direct AST/type-contract tests and migrate registered-struct initializer
  fixtures to the struct-specific statement.
- Validate through the workspace CTest build-and-test workflow, without a
  separate preliminary build invocation.
- Stop for review after specialization. Statement-owned block classification
  remains the next separate reviewable step; do not change block discovery here.

Specialization validation:

- The CTest workflow built the changes before running tests; no separate build
  invocation was needed. An assertion-message macro compilation error in the
  new AST test was corrected before the successful retry.
- Language, remaining, bindings, and tooling unit groups passed, including
  concrete class/struct AST type contracts, struct member spans, constructor
  metadata, and the migrated registered-struct initializer execution fixture.
- Compiler export and LSP process integration tests passed.
- Source struct coverage remains analysis-only; this refactoring does not claim
  additional executable struct functionality or gameplay validation.

Statement-owned block discovery follow-up:

- Statement registrations now declare whether they open a block at the start
  of a line, anywhere on a line, or not at all. Existing custom registrations
  default to non-block statements.
- Optional statement-owned classifiers handle contextual `if`, `open`, and
  `fun` forms. Inline loop controls, supported open declarations, inline
  callbacks, and closing lines retain their block behavior.
- Class/struct member scans, function pre-parsing, and language-server source
  scans use the same registered block rules. No syntax, opcode, or VM changes
  were introduced.
- Regression coverage includes built-in forms, string/format-string and comment
  contents, multiple inline callbacks, custom registered blocks and classifiers,
  custom blocks inside function bodies, and nested struct member source spans.
- CTest built the changes directly. Language, remaining, and tooling unit groups,
  compiler export, and LSP process integration tests passed. Four documentation
  preparation tests and whitespace validation passed.
- Validation initially caught a test-only AST return-type mismatch, invalid
  comma/comment syntax in scanner fixtures, and a missing native callback
  registration in the struct fixture; these were corrected before passing runs.
- Source struct coverage remains analysis-only; executable source structs and
  gameplay were not validated by this slice.
- Stop for review. Next, assess remaining storage/lifecycle and call-frame gaps
  against the foundation already implemented in Step 1, then propose a bounded
  Step 3 slice rather than duplicating existing support.

### Step 3: Storage, lifecycle, and VM call-frame support

Prerequisite: step 2 approved.

- Allocate object and local storage using physical counts.
- Implement approved aggregate initialization, field-wise copy/replacement,
  release, and failure cleanup.
- Implement destination and borrowed-receiver handling, including nested-call
  save/restore and zero-width cases.
- Reserve parameter copies and temporary result storage using physical widths.
- Add only the explicitly approved opcode/API changes; preserve existing
  scalar/class call paths.
- Test storage ownership and frame restoration through controlled runtime
  fixtures before exposing complete language execution.

Review stop: storage/ABI implementation, lifecycle evidence, and regression
results.

Successful-path assessment:

- Physical local/object widths, aggregate initialization/copy/replacement,
  parameter and temporary widths, borrowed receiver/destination handling,
  nested-call restoration, and zero-width calls already have implementation
  and registered-runtime coverage from Step 1.
- Do not duplicate those mechanisms. The user explicitly deferred VM fault
  handling and post-fault recovery, treating faults as fatal for now.
- Proceed with a bounded compiled source execution slice using the existing
  storage/lifecycle and call-frame paths. This does not claim fault-unwind
  ownership cleanup.

### Step 4: Executable struct expressions and methods

Prerequisite: step 3 approved.

- Wire `new`, generated `$init`, constructor bodies, locals, assignments,
  field access, ordinary parameter copies, and returns to the approved storage
  paths.
- Support nested structs in structs and classes.
- Borrow stored receivers for methods; independently own ordinary parameters.
- Allow temporary receivers and release their owned fields after use.
- Construct eligible return expressions directly into caller-provided storage.
- Stage existing assignment targets whenever direct construction could expose
  early writes. Evaluate receivers/arguments once in the existing language order.
- Cover early returns and all control-flow paths without leaking or freeing
  returned fields.
- Execute the original `Point`/`Player` example and focused mutation/copy tests.

Review stop: first end-to-end compiled struct behavior. Distinguish execution
tests from compilation-only checks.

### Step 4A: Compiled source construction, copies, and stored receivers

Approved scope:

- Exercise source-declared fields, generated initialization, overloaded
  constructors, local assignments, copied parameters, struct returns, stored
  receiver mutation, nested structs, and class-owned struct fields.
- Verify empty-struct side effects and normal-return string/object ownership.
  Keep VM fault unwinding and recovery out of scope.
- Use existing opcodes and ABI. Fix only demonstrated execution gaps, validate
  through CTest, and stop for review without committing.

Implementation and validation:

- Added four end-to-end source execution regressions. The existing compiler and
  VM paths handled the approved successful-path behavior; no production code,
  opcodes, or ABI changes were needed.
- Constructor tests verify exact field-initializer/body side-effect order and
  overload selection; copying does not rerun initialization or constructors.
- Parameter, nested-call, early-return, and method-return tests verify independent
  struct values and mutation through borrowed stored receivers.
- Nested struct/class tests verify independent inline fields, shared contained
  class references, property replacement, and unaffected neighboring fields.
- Cross-file hub tests verify zero-slot construction, copies, parameter/return
  flow, methods, and nested empty fields. Hub references remain explicitly
  qualified, as required by the existing language rules.
- Each scenario executes three times and checks its exact result, side effects
  where applicable, and restoration of live string/object counts after normal
  return. This is runtime execution evidence, not just analysis or compilation.
- CTest built and passed language, remaining, bindings, and tooling groups.
  Documentation preparation tests and whitespace validation passed.
- Stop for review. Next, handle temporary struct receivers and direct
  return-destination forwarding in separately approved slices, then proceed to
  interpreter/capture/formatting/host parity. NRVO remains later work. Gameplay,
  fault recovery, and full struct parity were not validated.

### Step 4B: Temporary struct method receivers

Approved scope:

- Allow direct method calls on temporary struct construction and call results,
  including chained method returns.
- Evaluate each receiver once before explicit arguments, borrow its storage for
  the call, and release owned fields immediately afterward.
- Preserve scalar, string/class, struct, and void results; support native/script
  methods and zero-width receivers.
- Reuse existing local storage, call lowering, and cleanup instructions. No
  boxing, opcode, or ABI changes. Stored receivers retain their existing paths.
- Keep property access rooted in temporary structs, return-destination forwarding,
  interpreter aggregates, and VM fault unwinding out of this slice.

Implementation and validation:

- Member resolution reserves one compiler-owned receiver local when the source
  has no stored receiver location. Repeated resolution reuses that local.
- Scalar/reference/void calls and destination-based struct results populate the
  receiver local before calling and release its owned fields before returning
  the result to the surrounding expression.
- Script execution tests cover direct construction, call results, chained method
  returns, discarded results, returned strings/class references/struct copies,
  and repeated evaluation of the same expression inside a loop.
- Live-object checks immediately after calls verify that temporary receivers
  are released before the next statement, rather than only at function return.
  Returned owned values remain valid after receiver release.
- Recorded nested calls verify exactly-once receiver-first evaluation followed
  by the existing right-to-left explicit argument order.
- Native method tests cover mutation, scalar/string/struct/void results,
  chained calls, and reference counts of copied aggregate fields.
- Empty native/script receivers execute their side effects without physical
  local slots or hidden receiver pointers. A focused metadata assertion verifies
  that repeated resolution does not reserve duplicate receiver locals.
- Language, remaining, bindings, and tooling CTest groups, compiler-export and
  LSP process integration tests, four documentation preparation tests, and
  whitespace validation passed.
- Stop for review. Next is direct return-destination forwarding. Property access
  rooted in temporary structs remains separate; interpreter/capture/formatting/
  host parity and NRVO remain later work. Gameplay and fault recovery were not
  validated.

### Step 4C: Free-function return-destination forwarding

Approved scope:

- Forward struct-returning free-function calls in `return makePoint ...`
  directly into the enclosing callable's caller-provided destination.
- Avoid the intermediate return local and field-wise copy. Let expressions
  declare forwarding support rather than adding an AST-type list to return
  handling.
- Reuse the hidden-pointer call ABI and existing storage-address instructions.
  Preserve argument order, owned argument cleanup, zero-width side effects,
  and assignment-site staging.
- Keep returns of locals, parameters, constructor expressions, and method
  results on their existing copy paths. NRVO and VM fault handling remain
  outside this slice.

Implementation and validation:

- Expressions default to no return-destination support. Resolved struct-valued
  free-function expressions opt in and use the specialized
  `addFunctionCallIntoReturn` compiler entry point.
- Nonempty calls push the enclosing callable's existing hidden return pointer;
  empty calls omit it. No opcode or VM call-ABI changes were needed.
- Return resolution does not reserve a staging local for eligible calls.
  Other return forms retain their staging and copy behavior.
- Metadata and instruction-dump assertions verify zero wrapper local slots,
  forwarding address pushes, and absence of local-address pushes and destination
  copy stores. Nonforwarded construction/parameter/method returns retain their
  return locals.
- Source execution covers script forwarding chains, copied struct arguments,
  empty results, and forwarding a free-function result from a struct method
  with distinct receiver and return-pointer offsets.
- Native execution checks exact destination-pointer identity across forwarding
  and successful host reentry, right-to-left argument order, and preservation of
  the old class-owned assignment target after the result destination is filled.
- Each new execution scenario runs three times with exact results/side effects
  and restoration of live string/object counts after normal return.
- Language, remaining, bindings, and tooling CTest groups, compiler-export and
  LSP process integration tests, four documentation preparation tests, and
  whitespace validation passed.
- Stop for review. Next, extend return-destination forwarding to construction
  and method-call expressions in separately approved slices. Temporary-rooted
  property access, interpreter/capture/formatting/host parity, and NRVO remain
  later work. Gameplay, performance, and fault recovery were not validated.

### Step 4D: Direct struct construction returns

Approved scope:

- Construct `return new Point ...` directly in the enclosing callable's return
  destination, without an intermediate return local or field-wise copy.
- Extend the existing expression forwarding capability and add a specialized
  construction emitter. Reuse the constructor ABI and existing instructions.
- Preserve field-initializer/body order, argument evaluation, empty-struct side
  effects, owned fields, and assignment-site staging.
- Keep method-result forwarding, local/parameter return copies, NRVO, and
  VM fault handling outside this slice.

Implementation and validation:

- Resolved struct constructor expressions opt into return-destination support.
  `addStructCreationIntoReturn` releases previous destination ownership, forwards
  its hidden address, and emits the existing native/script creation instruction.
- Empty construction returns omit the pointer but still evaluate arguments and
  run constructor side effects. No return-handling special case, VM changes,
  opcodes, or ABI changes were introduced.
- Metadata and instruction dumps verify zero intermediate local slots, forwarded
  addresses, and no local-address pushes or field-copy stores. Existing
  local/parameter and method-result return coverage retains the copy fallback.
- Script execution verifies overloaded constructors, field initialization before
  bodies, right-to-left argument evaluation, early return branches, copied
  aggregate arguments, and construction returns from a struct method with
  distinct receiver and return-destination offsets.
- Native constructor coverage verifies default-initialized fields, exact
  destination identity across successful host reentry, and that writing the
  constructed result does not expose premature changes to a class-owned
  assignment target. Empty native/script construction returns preserve side
  effects without destination pointers.
- Each new execution scenario runs three times with exact results and recorded
  side effects, restoring live string/object counts after normal return.
- Language, remaining, bindings, and tooling CTest groups, compiler-export and
  LSP process integration tests, four documentation preparation tests, and
  whitespace validation passed.
- Stop for review. Next is method-result forwarding, followed by temporary-rooted
  property access and interpreter/capture/formatting/host parity. NRVO remains
  separate. Gameplay, performance, and fault recovery were not validated.

### Step 4E: Struct-method return-destination forwarding

Approved scope:

- Forward native/script struct results in `return point.copy ...` directly into
  the enclosing callable's return destination.
- Reuse expression forwarding and the hidden-pointer ABI, preserving stored,
  borrowed, class-owned, and temporary struct receiver lifetimes.
- Preserve receiver-first evaluation, argument order, empty-value side effects,
  owned cleanup, and assignment-site staging. Verify intermediate return
  local/copy elimination and exact destination identity.
- Local/parameter/property returns retain their copy paths. Struct-valued
  methods on class receivers, NRVO, and VM fault handling remain out of scope.

Implementation and validation:

- Resolved struct-valued method expressions with struct receiver storage opt
  into forwarding. The specialized `addStructMethodCallIntoReturn` emitter
  forwards the enclosing return pointer and borrows the existing receiver.
- Class owners remain retained until argument and hidden-pointer cleanup
  completes. Temporary struct receivers are populated once before arguments
  and released after the call, before enclosing local cleanup.
- Existing opcodes and call layouts handle nonempty/empty receiver and result
  combinations; no VM or ABI changes were introduced.
- Metadata and instruction dumps verify removal of return locals/copy stores,
  while preserving only required local/temporary receiver storage and the
  property-return copy fallback.
- Script execution covers parameter copies, locals, enclosing borrowed receivers,
  nested class-owned fields, temporary class owners, temporary structs, chained
  method results, and empty receiver/result combinations.
- Native coverage verifies receiver/destination disjointness and pointer identity
  across successful host reentry, receiver-first/right-to-left argument order,
  preserved source fields after destination writes, and copied field ownership.
- New execution scenarios run three times with exact results/side effects and
  restoration of live string/object counts after normal return.
- Language, remaining, bindings, and tooling CTest groups, compiler-export and
  LSP process integration tests, four documentation preparation tests, and
  whitespace validation passed.
- Stop for review. Next is temporary-rooted property access, followed by
  interpreter/capture/formatting/host parity. NRVO remains separate. Gameplay,
  performance, and fault recovery were not validated.

### Step 4F: Temporary-rooted stored property access

Approved scope:

- Read and assign stored properties rooted in temporary structs, including
  nested fields and method receivers such as `(makeOuter).point.move 2`.
- Evaluate the root once before assignment RHS expressions or explicit method
  arguments. Preserve existing argument order and assignment staging.
- Keep the complete root alive until the operation finishes, then release its
  owned fields. Extracted strings, class references, and struct copies remain
  independently owned. Preserve side effects for zero-slot fields and roots.
- Native accessor-backed struct properties, interpreter aggregates, and VM
  fault cleanup remain out of scope.

Implementation and validation:

- Temporary-rooted field access reserves a compiler-owned local. Nested stored
  property paths preserve that root through existing storage lifetime hooks.
  Reads, local copies, assignments, and native/script methods release the
  complete root after use, including unrelated owned fields.
- Existing storage instructions and call layouts are reused; no boxing,
  opcodes, or ABI changes were introduced. Existing stored receivers and
  return-destination forwarding retain their behavior.
- Source regressions cover construction, free-function and method results,
  nested scalar/reference/struct reads and writes, shared class references,
  method mutation, and forwarded struct method results.
- Recorded effects verify single evaluation, receiver-before-RHS/arguments,
  and existing right-to-left argument order. Native callbacks verify the full
  root remains alive during argument/RHS evaluation, borrowed receiver and
  destination storage are disjoint, and extracted fields retain ownership.
- Immediate live-object/string checks verify root cleanup before the next
  statement, including nonempty roots with empty fields. Empty roots and
  fields preserve initialization, assignment, argument, and method effects.
  Metadata verifies zero physical slots for empty roots and no intermediate
  return local for forwarded method results.
- New execution scenarios run three times, restoring live string/object counts
  after normal return; looped reads also verify immediate cleanup.
- Language, remaining, bindings, and tooling CTest groups, compiler-export and
  LSP process integration tests, four documentation preparation tests, and
  whitespace validation passed.
- Stop for review. Next, scope a bounded Step 5 parity slice. Struct-valued
  methods on class receivers and native accessor-backed struct properties
  remain separate compiled gaps; NRVO and fatal-fault cleanup remain deferred.
  Gameplay and performance were not validated.

### Step 5: Interpreter, captures, formatting, and host parity

Prerequisite: step 4 approved.

- Make interpreter-owned values and frame-backed variable/parameter access
  aggregate-aware without a heap-boxed compiled-runtime representation.
- Store complete field-wise capture copies, including zero-slot captures.
- Preserve existing snapshot/reference behavior for ordinary captures and the
  agreed value semantics when capturing a struct receiver.
- Wire callback execution and static/dynamic format expressions through the same
  physical layouts.
- Complete the approved low-level host invocation/return contract and preserve
  existing native reentry behavior.
- Keep existing generated class bindings correct for physical field offsets.
  Handle unsupported new export shapes explicitly, not with invalid scalar
  reads or silent omission; discuss any new host-facing restriction first.
- Test compiled/interpreted/host agreement, captured copies, receiver lifetimes,
  formatting, and nested native reentry.

Review stop: parity results and any unsupported surface requiring a scope
decision.

### Step 5A: Interpreter-owned struct values and frame access

Approved scope:

- Preserve inline scalar values while giving structs owned contiguous field
  storage, including zero-slot values, without compiled-runtime boxing.
- Add span-based value access and assignment while preserving scalar APIs.
- Support whole-value interpreter reads and assignments for host-supplied
  structs and ordinary parameters/locals in active script frames.
- Preserve field-wise copies, shared contained class references, self-assignment,
  and existing scalar behavior. Keep construction, aggregate calls, properties,
  methods, captures, and formatting outside this slice.

Implementation and validation:

- `InterpreterValue` copies and releases all physical slots through existing type
  ownership operations. Struct storage survives moves without relocation;
  empty values retain their type without a slot pointer or allocation.
- Interpreter variables and results accept exact-size source spans. Replacement
  stages ownership before releasing existing values, including self-sourced
  result/variable updates. Scalar APIs reject aggregate values explicitly.
- Identifier evaluation and ordinary assignments use complete storage views.
  Empty values are distinguished from missing variables through type metadata.
  Variable listing retains the existing struct type-name summary.
- Frame reads/writes use original callable layouts for physical local offsets
  and parameter widths. Parameter access reuses enclosing-script-frame storage,
  rather than a native callback's argument area, when inspecting from native
  code. Frame metadata and compiled instructions remain unchanged.
- Regressions cover nested owned strings and class references, deep copies,
  copy/move/self-assignment, stable storage across container relocation, invalid
  spans, cleared variables, independent contexts, and result snapshots.
- Executable frame tests cover aggregate parameters, scalar parameters following
  aggregate and empty parameters, aggregate locals, empty locals/parameters,
  whole-value interpreted assignment, checked host writes, fixed frame types,
  register preservation, and unchanged original caller values.
- New scenarios run three times and restore live string/object counts after
  normal completion. Language, remaining, bindings, and tooling CTest groups,
  compiler-export and LSP process integration tests, four documentation
  preparation tests, and whitespace validation passed.
- Stop for review. Next, scope interpreter construction/call/property support,
  followed by captures, formatting, and remaining host parity. Native
  accessor-backed struct properties and class-receiver struct returns remain
  separate compiled gaps. NRVO and fatal-fault cleanup remain deferred.
  Gameplay and performance were not validated.

### Step 5A follow-up: Standalone vector-backed typed values

- Move `InterpreterValue` into its own matching header/source files, independent of
  interpreter-context definitions. Keep one owned-value abstraction rather than
  introducing a separate scalar wrapper.
- Replace the initially selected scalar/array variant with
  `std::vector<RuntimeValue>`. Each live value owns exactly its type's physical
  slot count; the vector contains raw runtime slots, not per-field typed
  wrappers. Scalars now allocate one slot rather than living inline.
- Preserve existing checked scalar access, complete storage views, type-driven
  copy/free operations, and stable slot addresses across moves. Nonempty
  one-slot structs remain allocated; empty structs and void require no
  allocation. Scalar access to empty or moved-from values fails explicitly
  rather than returning a placeholder.
- Standalone-header tests verify scalar/array/empty/void transitions, ownership,
  self-assignment, scalar and single-slot struct moves, empty scalar-access
  rejection, and partial-copy cleanup. Existing
  nested ownership and interpreter/frame regressions also pass. No memory
  reduction is claimed.
- Language, remaining, bindings, and tooling CTest groups, compiler-export and
  LSP process integration tests, four documentation preparation tests, and
  whitespace validation passed.
- Stop for review without expanding interpreter construction, calls, properties,
  captures, formatting, or host aggregate APIs.

### Step 5A follow-up: Nonthrowing cleanup and runtime layout access

- Keep layout finalization and checked compiler/host queries throwing. Publish
  stored field types, offsets, widths, and total width only after successful
  finalization; failed owners remain retryable without publishing partial
  metadata.
- Expose `noexcept` runtime views through `getLayoutSize()` and
  `getFieldLayouts()`. Access before successful finalization is fatal, not an
  empty-layout fallback. Include zero-width stored fields and exclude native
  properties.
- Make `Type::freeValue` and all overrides `noexcept`, along with
  `RuntimeValue::freeObjectValue`. Struct cleanup traverses cached field
  layouts without querying field sizes or ownership metadata. Invalid deferred
  or unfinalized cleanup terminates the process.
- Preserve throwing initialization, copying, and assignment, including
  partial-copy cleanup and destination preservation. Do not redesign mutable
  collection defaults or add a new move operation.
- Cover nested and empty layouts, stable published metadata, failed-finalization
  retries, retained references, and partial-copy cleanup. Check fatal contracts
  in isolated processes with an exact terminate-handler exit status.
- Language, remaining, bindings, and tooling CTest groups, compiler-export and
  LSP process integration tests, five isolated fatal-contract cases, four
  documentation preparation tests, and whitespace validation passed. Gameplay
  and performance were not validated.
- Stop for review without expanding interpreter support or committing.

### Step 5B: Interpreter struct construction and free-function calls

Approved scope:

- Construct registered native/script structs and pass or return complete struct
  values through free functions, including nested and empty values. Support
  struct arguments to class constructors without expanding struct property or
  method interpretation.
- Align shared interpreter argument evaluation with compiled right-to-left
  behavior, including existing scalar calls; preserve receiver-first evaluation.
- Add a narrow prepared-frame VM entry point using the existing physical ABI.
  Leave scalar host call containers unchanged. Aggregate yield/resume and VM
  fault recovery remain outside this slice.

Implementation and validation:

- Construction runs registered property initializers and native/script bodies
  using owned destination storage. Function calls retain complete arguments in
  logical parameter order, copy physical argument slots independently, and
  supply hidden destination pointers only when required by the existing layout.
- Owned call storage releases reassigned parameter values and partial copies,
  keeps returned fields alive, and restores enclosing registers and frame state.
  Empty values preserve types and side effects without physical slots or pointers.
- Expressions, assignments, nested calls, and discarded statement results share
  the complete-value call path. Scalar host APIs and scalar script call behavior
  remain available without new aggregate suspension semantics.
- Regression coverage compares compiled/interpreted construction and calls,
  nested strings/class references, parameter mutation without source changes,
  empty returns, result snapshots, native reentry, active method-frame inspection,
  receiver-first/right-to-left order, partial-copy and initialization failures,
  and exact full-width stack overflow checks before mutation.
- New ownership scenarios run three times and restore live string/object counts.
  Language, remaining, bindings, and tooling CTest groups, compiler-export and
  LSP process integration tests, the isolated cleanup-contract test, four
  documentation preparation tests, and whitespace checks passed. The LSP process
  test passed on a clean retry after an initial timeout left its fixture directory.
  Gameplay and performance were not validated.
- Stop for review without committing. Next is interpreter stored properties and
  methods, followed by captures, formatting, and remaining host parity. Native
  accessor-backed struct properties and class-receiver struct returns remain
  separate compiled gaps; NRVO and fatal-fault cleanup stay deferred.

### Step 5C: Interpreter stored-property reads

Scope:

- Read stored scalar/reference and struct fields through interpreter-owned
  values, ordinary parameters/locals, borrowed struct `this`, class-owned
  structs, and temporary construction or free-function results.
- Keep property assignments and struct method calls for subsequent slices:
  reads return owned values rather than mutable borrowed aliases.
- Preserve existing class-reference semantics, scalar property APIs, and
  explicit rejection of unsupported writes and native accessor-backed structs.

Implementation and validation:

- Resolve stored struct fields without adding compiled staging locals to
  interpreter metadata. Nested reads evaluate each receiver once and retain it
  until the extracted value is independently owned.
- Use complete field storage for class-owned struct reads. Struct reads select
  the field's physical range and copy it into the context-owned result.
- Add `getReceiverStorage()` for active struct method/constructor frames and
  use it for interpreted `this` reads. Empty receivers and fields access no
  slots and return no pointer while preserving execution side effects.
- Tests cover nested scalar/reference/whole-struct fields, one-slot and empty
  fields, class-reference sharing, temporary-root single evaluation, immediate
  cleanup of unrelated owned fields, snapshots, frame parameters/locals/receivers,
  empty receiver constructors/methods, unsupported writes, and unchanged metadata
  and VM registers. Ownership scenarios repeat three times and restore live counts.
- Language, remaining, bindings, and tooling CTest groups, compiler-export and
  LSP process integration tests, four documentation preparation tests, and
  whitespace validation passed. Gameplay and performance were not validated.
- Stop for review without committing. Next is statement-owned property
  assignment, then struct method calls, captures, formatting, and remaining host
  parity. NRVO and VM fault cleanup remain deferred.

### Step 5D1: Statement-owned assignments to named struct roots

Approved scope:

- Assign stored fields rooted in interpreter struct variables, frame locals and
  parameters, and struct `this`, including nested struct fields.
- Keep expressions value-only. Do not add writable-storage evaluation hooks or
  interpreter-driven AST accessors; the context must not inspect AST nodes.
- Copy the current root at write time, update the selected field, and replace
  the root through context setters. Preserve unrelated sibling-field changes
  made while evaluating the RHS.
- Leave class-owned and temporary-rooted struct writes for the next slice.

Implementation and validation:

- Member assignment statements retain the named target and resolve its root
  and stored property-ID path. Existing compiled assignment paths are unchanged.
- New unqualified interpreter variables are reserved during parsing so a later
  assignment in the same input can resolve a field of an earlier variable.
- `Interpreter::setProperty` returns replacement struct values using existing
  type assignment operations. Context property setters accept names/property IDs
  and typed values, not expressions or writable expression storage.
- Regression tests cover scalar/reference/nested/whole-struct writes, independent
  sources, self-assignment, owned results, shared class references, invalid paths
  and types, frame parameters/locals/receivers, empty receiver constructors and
  methods, RHS reentry changing sibling fields, and partial-copy failures before
  replacement. Ownership scenarios restore live counts over three repetitions.
- The language and remaining CTest groups, four documentation preparation tests,
  and whitespace checks passed. Gameplay and performance were not validated.
- Stop for review without committing. Next are class-owned and temporary-rooted
  struct writes, then struct method calls, captures, formatting, and remaining
  host parity. NRVO and VM fault cleanup remain deferred.

### Step 5D1 follow-up: Immutable interpreter values

Approved scope:

- Rename the value class and matching files to `InterpreterValue`, updating all
  compiler, VM, context, call-frame, test, build, and documentation references.
- Keep immutable value contents: scalars by value, independent struct storage,
  and retained object/string references. Whole-value replacement remains possible;
  referenced objects retain their normal mutable reference semantics.
- Keep only the type pointer and complete storage. Use the `const Type*` copy
  and cleanup contracts, without another ownership or cleanup discriminator.
- Move field replacement into `Interpreter`, returning an independent value and
  retaining neither returned values nor references to input values.
- Preserve current named-root assignment behavior. Context setters store the
  returned replacement rather than modifying an existing interpreter value.

Implementation and validation:

- The value no longer exposes `setProperty` or writable storage. The existing
  property-ID path API now belongs to `Interpreter::setProperty`.
- Host tests exercise const inputs, independent replacement storage, values
  surviving interpreter/input destruction, retained strings/class references,
  invalid paths/types, and cleanup. Assignment failure coverage includes partial
  copies of replacement results and copying into the destination.
- Language, remaining, bindings, and tooling CTest groups, compiler-export checks,
  four documentation preparation tests, and whitespace checks passed. Gameplay
  and performance were not validated.
- Stop for review without committing. Next are conventional property-name/direct-ID
  APIs, followed by general construction/function operations and method calls.
  Class-owned/temporary-rooted writes and remaining struct parity stay separate.

### Step 5D1 follow-up: Conventional stored-property APIs

Scope:

- Add context-free `Interpreter::getProperty` and `setProperty` overloads taking
  either a direct property ID or a field name/dotted path.
- Preserve immutable value contents and return owned field/replacement values.
- Replace public property-ID-chain spans on context variable/receiver setters
  with direct IDs or names/paths. Keep chain handling internal to the interpreter.
- Retain existing stored-struct support. Class/native property execution and
  class-owned/temporary-rooted struct writes remain separate slices.

Implementation and validation:

- IDs select only direct fields owned by the supplied receiver's type; dotted
  paths resolve relative to the receiver and traverse stored struct fields.
- Assignment statements keep a relative property path rather than an ID chain.
  Stored-struct expression reads delegate to the interpreter's value operation.
- Host tests cover ID/name agreement, nested field copies, empty roots/fields,
  retained references and extracted-value lifetimes, malformed/unknown paths,
  wrong-owner IDs, type mismatches, and context-result preservation.
- Direct context selector tests cover variables, frame locals/parameters, struct
  receivers, and zero-slot receivers. Existing sibling-update and copy-failure
  regressions remain enabled.
- The language and remaining CTest groups, four documentation preparation tests,
  and whitespace checks passed. Gameplay and performance were not validated.
- Stop for review without committing. Next are general construction/function
  operations, followed by method calls and remaining property/struct parity.

### Step 5D1 follow-up: General construction and function-call APIs

Approved scope:

- Add `Interpreter::callFunction` and `construct` with an explicit target VM,
  callable ID, and typed arguments in logical parameter order.
- Return owned `InterpreterValue` copies without requiring or modifying a
  context. Keep variables, frame bindings, results, and diagnostics in context.
- Remove the context's `evaluate...Call` entry points and migrate AST callers
  and the legacy scalar runtime adapter.
- Reuse existing native/script dispatch, aggregate call frames, and the scalar
  script execution path. Preserve argument evaluation order and suspension
  limitations.
- Validate host argument count, types, and physical widths against the existing
  signature rules before execution.
- Keep method calls, name-based overload selection, class/native property
  operations, captures, and class-owned/temporary-rooted writes separate.

Implementation and validation:

- Context execution methods are removed. AST expressions publish returned
  values explicitly; statements discard function results. The legacy raw
  runtime adapter retains its scalar-only diagnostic contract.
- Hidden line-number arguments now retain their declared parameter type,
  matching the existing hidden file/function argument behavior.
- Host tests cover native/script functions and constructors, scalar/struct/
  empty/void results, independent argument copies, retained results after VM
  destruction, native factories with distinct constructed types, nested native
  reentry, and enclosing register/stack preservation.
- Invalid IDs/kinds, arguments, unresolved signatures, and uncompiled functions
  are rejected without execution. Existing copy/initialization failure tests
  use the returned-value APIs. Scalar suspension and aggregate-yield rejection
  have direct regression coverage.
- CTest's integrated build and the language, remaining, bindings, tooling, and
  compiler-export targets passed, along with four documentation preparation
  tests and whitespace checks. Gameplay and performance were not validated.
- Stop for review without committing. Next are method calls, followed by
  remaining property/struct parity.

### Step 5D1 follow-up: Borrowed interpreter instances

Approved scope:

- Require both ordinary and frame-backed contexts to borrow a caller-supplied
  interpreter by const reference. The interpreter and VM must outlive the context.
- Expose the supplied instance through `getInterpreter`; do not own, copy, or
  implicitly create an interpreter in the context. Reject temporary borrowing.
- Route AST evaluation, stored-property replacement, runtime context adapters,
  and dynamic formatting through the context's interpreter.
- Keep a persistent interpreter for the debug console. Compiled dynamic
  formatting creates a scoped interpreter that outlives its scoped frame context.
- Migrate host examples and tests, retaining separate context state when an
  interpreter is shared. Keep `onEvaluate` signatures unchanged.

Implementation and validation:

- Both context constructors require the borrowed interpreter; `getInterpreter`
  returns that exact instance. Rvalue overloads are deleted to prevent dangling
  references to temporary interpreters.
- Context-based operations use the borrowed instance. The debug console owns
  its interpreter for the lifetime of its context; frame-formatting contexts
  borrow a scoped interpreter.
- Regression coverage verifies shared-instance identity with independent
  context state, frame-context identity, and compile-time rejection of
  temporary borrowing. Existing host and evaluation callers are migrated.
- CTest's integrated build and language, remaining, bindings, tooling, and
  compiler-export targets passed, along with four documentation preparation
  tests and whitespace checks. Gameplay and performance were not validated.
- Stop for review without committing. Method calls remain the next slice.

### Step 5D1 follow-up: VM-bound interpreters

Approved scope:

- Make each interpreter borrow one caller-owned VM. The VM must outlive the
  interpreter, which must outlive its contexts.
- Context construction accepts an interpreter and, optionally, an active
  callable. Contexts obtain their VM from the interpreter rather than retaining
  a separately supplied VM reference.
- Function calls and construction use the interpreter's VM without a VM
  argument. Preserve typed results, call ownership, reentry, and suspension
  behavior.
- Migrate host APIs, console ownership order, frame formatting, tests, and
  embedding examples. Keep the legacy runtime adapter unchanged in scope.

Implementation and validation:

- Interpreters require a borrowed VM and expose it by reference. They are
  noncopyable. Contexts no longer retain a separate VM reference.
- Console members are ordered so contexts are destroyed before interpreters,
  and interpreters before their VMs. Frame formatting uses the same borrowing
  order for scoped instances.
- Tests cover two interpreters bound to different VMs in one runtime, direct
  call routing, context-derived VM identity, and active-frame identity.
  Compile-time checks cover required VM construction, temporary borrowing,
  and rejection of separately supplied context VMs.
- Existing call, construction, reentry, failure cleanup, suspension, and
  retained-result lifetime coverage remains enabled. Stored-property results
  also remain valid after their interpreter and VM are destroyed.
- CTest's integrated build and language, remaining, bindings, tooling, and
  compiler-export targets passed, along with four documentation preparation
  tests and whitespace checks. Gameplay and performance were not validated.
- Stop for review without committing. The legacy runtime adapter remains;
  migrating or removing it is a separate slice.

### Step 5D1 follow-up: Removing the legacy runtime call adapter

Approved scope:

- Remove `Runtime::executeNativeFunction` and its interpreter-context dependency.
  Do not add another raw scalar adapter or duplicate signature validation.
- Migrate all in-repository callers to typed `Interpreter::callFunction` calls.
  Publish returned values in context explicitly where required.
- Preserve the interpreter's exception contracts and the VM's existing raw
  scalar API restrictions. Native and compiled script aggregate calls use
  complete typed argument/result storage.
- Document the public API removal and migration. Keep method calls and remaining
  property/struct parity for separate slices.

Implementation and validation:

- The runtime declaration, implementation, and interpreter-context dependency
  are removed. No in-repository adapter callers remain.
- Deferred native signatures fail through the interpreter's exception contract
  without executing callbacks. Scalar host calls leave context results alone
  until explicit publication.
- Aggregate native results and compiled script argument/return copies are
  verified through the typed API, including complete storage and string
  cleanup. Raw VM aggregate-rejection checks remain unchanged.
- CTest's integrated build and the language, remaining, and compiler-export
  targets passed, along with four documentation preparation tests and whitespace
  checks. Gameplay and performance were not validated.
- Stop for review without committing. Next are general method-call APIs,
  including agreeing how struct receiver updates fit immutable values.

### Step 5D1 follow-up: General scalar/reference method-call APIs

Approved scope:

- Add `Interpreter::callMethod(receiver, methodId, arguments)` returning an
  owned value, with ordinary native/script dispatch and host validation.
- Move existing method execution out of AST evaluation. Retain value-only
  expression evaluation and unchanged `onEvaluate` signatures.
- Preserve receiver-first and right-to-left argument evaluation, copied
  arguments, shared class-object mutation, owned results, and enclosing VM
  frame/register restoration.
- Keep current VM restrictions on struct receivers, arguments, and returns,
  and on yielding script methods. Do not add opcodes or a second call ABI.
- Keep property/accessor execution and struct-receiver updates separate.

Implementation and validation:

- The interpreter owns method dispatch, validation, argument adaptation, result
  copying/cleanup, and register preservation. AST method evaluation retains the
  receiver value, evaluates arguments, and publishes the returned value.
- Native and script host tests cover scalar/reference/void results, copied
  arguments, shared receiver mutations, results surviving receiver/VM lifetime,
  and enclosing receiver/stack/register preservation, including retained strings
  in saved registers.
- Invalid IDs/kinds, receivers, arguments, uncompiled methods, and unsupported
  struct storage are rejected before execution. Tests also cover receiver-first/
  right-to-left evaluation, native reentry, partial argument/result-copy failures,
  native callback failure cleanup, and the existing script-method yield rejection.
- CTest's integrated build and the language, remaining, and compiler-export
  targets passed, along with four documentation preparation tests and whitespace
  checks. Gameplay and performance were not validated.
- Stop for review without committing. Next is agreeing and implementing immutable
  struct-method receiver updates plus method results, followed by remaining
  property/struct parity.

### Step 5D1 follow-up: Host struct-method receiver/result values

Approved scope:

- Add a specialized `callStructMethod` returning a plain `StructMethodResult`
  with owned `receiver` and `result` values. Keep class-method APIs unchanged.
- Execute native and compiled script methods against a private receiver copy.
  Support complete typed arguments and scalar/reference/struct/empty/void
  results. Keep input struct fields and argument copies independent.
- Preserve ordinary shared class-reference effects, including effects before
  failure. Do not promise rollback of referenced objects.
- Reuse the call-storage layout and type-directed copy/free contracts with
  separate receiver and result storage. Do not add opcodes, ownership tags,
  writable interpreter values, or AST storage-discovery APIs.
- Keep calls synchronous and reject native/script yielding. Preserve enclosing
  execution state, object receiver, registers, arguments, and stack ownership.
- Do not enable source-level struct-method calls or automatic context writeback.

Implementation and validation:

- The public result is a plain pair of owned interpreter values, declared in
  `Interpreter.hpp`. `InterpreterCallFrame` owns the mutable receiver copy,
  arguments, and result storage, and produces independent returned values.
- Interpreter-private dispatch selects native/script execution and rejects
  yielding. Generic VM execution primitives preserve enclosing execution state
  and report completion or yielding, without struct-specific entry points.
  No opcodes or AST changes are added.
- Host tests cover scalar/reference/struct/empty/void results, nested receivers,
  copied arguments, shared objects, independent receiver/result storage, explicit
  context publication, and results surviving their interpreter and VM.
- Failure coverage includes all ten copy positions across receiver, arguments,
  callee result, and returned snapshots; partial native results; stack overflow;
  and rejection of native/script yielding with prior yield-state preservation.
  Initialization failure injection confirms method calls copy rather than
  reconstruct receivers.
- Native reentry tests verify enclosing class receiver restoration and borrowed
  script-frame parameters/locals. Source-level interpreter struct-method calls
  remain rejected, and the existing class-method API retains its restrictions.
- CTest's integrated build and the language, remaining, and compiler-export
  targets passed, along with four documentation preparation tests and whitespace
  checks. Gameplay and performance were not validated.
- Stop for review without committing. Next is source integration after agreeing
  how statements commit receiver updates without losing intervening changes or
  introducing AST storage discovery.

### Step 5D1 follow-up: Simplifying interpreter call ownership and execution

Approved scope:

- Keep `callMethod` and `callStructMethod` and their existing return contracts.
  Move `StructMethodResult` into `Interpreter.hpp`.
- Consolidate private receiver-copy ownership into `InterpreterCallFrame`;
  remove the separate struct-method frame and externally supplied receiver
  storage. Return independent receiver/result values without interpreter RVO.
- Keep native/script dispatch and host yield policy in the interpreter.
  Replace prepared/struct-specific VM entry points, including
  `runPreparedFunction`, with generic execution mechanics.
- Preserve existing class-method restrictions, scalar suspension, copy/free
  contracts, reentry, and enclosing execution state. Keep source-level receiver
  writeback separate. Validate and stop for review without committing.

Implementation and validation:

- The existing call frame owns and frees its receiver copy and returns owned
  snapshots. The separate struct-method frame and result header are removed.
- Interpreter-private `runFunction`/`runMethod` dispatch uses generic VM
  `runScript`/`runNative` execution where required. VM execution reports yielding;
  the interpreter enforces the existing synchronous-call restrictions.
- Regression coverage asserts all ten field copies on a successful struct
  method call, alongside the existing copy-failure coverage. Synchronous script
  functions also preserve prior yield state on completion and yield rejection.
- The workspace CMake test target built successfully. All six unit-test groups,
  compiler-export and LSP-process checks, four documentation-preparation tests,
  and strict documentation generation/site building passed. Gameplay,
  performance, and consuming-project execution were not validated.
- Stop for review without committing. Source-level receiver writeback remains
  a separately approved slice.

### Step 5D1 follow-up: Unified source execution and diagnostics

Approved scope:

- Keep `execute` as the single public general source-entry API for expressions
  and statements. Remove public `evaluateExpression` overloads and migrate callers.
- Source execution reports syntax, resolution, and execution failures through
  context diagnostics and returns false without a result. Direct typed host-call
  exception contracts remain unchanged.
- Keep strict expression-only validation internal to formatting. Add
  `Interpreter::formatString` and retain free `formatDynamicString` as a
  compatibility wrapper, preserving formatting diagnostics, source locations,
  side-effect prevention, and execution-exception propagation.
- Do not modify AST interfaces. Typed-value AST signatures and invocation/VM
  boundary changes require separate discussion and approval.
- Preserve unrelated worktree changes. Validate this slice and stop for review
  without committing.

Implementation and validation:

- Public source callers now use `execute` for both expressions and statements.
  Failures from native/script calls, assignments, statements, and dynamic-format
  expressions are retained as source-named diagnostics with no partial result.
- `Interpreter::formatString` owns strict placeholder evaluation; the existing
  free formatter delegates to it. Formatting retains its diagnostics, source
  locations, side-effect prevention, and exception-propagation contracts.
- Regression tests cover register/stack preservation, retained bindings,
  diagnostic clearing on subsequent input, and direct typed-call exceptions.
  The formatting entry point and compatibility wrapper produce matching results
  and diagnostics without adding compiled instructions.
- The workspace CMake test target built successfully. All six unit-test groups,
  compiler-export and LSP-process checks, four documentation-preparation tests,
  and strict documentation generation/site building passed. Gameplay and
  consuming-project execution were not validated.
- No AST interface or implementation files were changed. Stop for review without
  committing; discuss AST signatures before any later AST-interface change.

### Step 5D1 follow-up: Typed-value AST evaluation

Approved scope:

- Return owned `InterpreterValue` values from expression, statement, statement-list,
  and assignment evaluation, including virtual `onEvaluate` methods.
- Return owned argument values in logical parameter order without context-result
  intermediates. Preserve receiver-first/right-to-left call evaluation, binary
  operand order, short-circuiting, and property/indexer assignment order.
- Keep the assignment RHS as an expression parameter to preserve sequencing.
  Assignments return their assigned snapshot; statements without values return
  typed void; statement lists preserve their existing observable result behavior.
- Keep bindings, diagnostics, and loop control in the context. Internal AST failures
  throw, `execute` reports diagnostics, and formatting retains its agreed contract.
  No dummy value represents a failed AST evaluation.
- Do not add interpreter RVO, source-level struct-method writeback, or compiler
  lowering changes. Invocation orchestration and naming remain the next slice.
- Validate, then stop for review without committing.

Completed:

- Migrated AST evaluators and direct callers to owned typed return values.
  Source execution publishes only its final result; formatting consumes returned
  placeholder values directly.
- Added ownership-transfer publication for owned temporary values. This avoids
  a fallible copy after an assignment has already committed, without interpreter
  return-destination forwarding.
- Added regressions for unchanged published context results during AST evaluation,
  evaluation order, direct exceptions versus source diagnostics, and aggregate
  result ownership beyond AST/VM lifetimes.
- Expanded assignment failure injection to every measured copy stage, checking
  unchanged destinations and released references on failure.
- Validation passed: the `nomad_test` build, all six unit groups, compiler exports,
  LSP process checks, four documentation-preparation tests, strict documentation
  generation/build, and whitespace checks. No gameplay session was run.

Next, after review: move invocation orchestration into `Interpreter`, expose only
the necessary low-level VM operations, and consistently name public invocation
APIs `runFunction`, `runObjectMethod`, and `runStructMethod`. Source-level
struct-method writeback remains a separate follow-up.

### Step 5D1 follow-up: Interpreter-owned invocation orchestration

Approved scope:

- Rename public host invocation APIs to `runFunction`, `runObjectMethod`, and
  `runStructMethod`, retaining their typed arguments and return contracts.
- Move typed invocation orchestration, script/native dispatch, state restoration,
  and existing yield restrictions into Interpreter. Keep VM opcode operations
  and expose narrow execution-state capture/restoration, frame entry, and bytecode
  execution primitives without adding friendship.
- Preserve scalar script suspension through `VirtualMachine::run` as the existing
  scheduler entry point.
- Migrate raw VM invocation callers to typed Interpreter APIs and remove
  `runNativeFunction`, `runNativeMethod`, and `runScriptMethod`, along with
  the transitional VM `runScript`/`runNative` wrappers.
- Preserve scalar native yield/stop behavior, argument/receiver/result ownership,
  reentry, and existing class/aggregate restrictions. Source-level struct-method
  receiver writeback remains a separate next design step.
- Update documentation, validate, and stop for review without committing.

Completed:

- Renamed all Interpreter invocation callers and migrated raw VM wrapper tests to
  owned typed arguments/results. Removed the five VM invocation wrappers.
- Interpreter now dispatches native callbacks and compiled script bodies using
  VM execution snapshots and frame/bytecode primitives. Object calls share the
  existing owning call frame; struct calls still return independent snapshots.
- Preserved scalar script suspension and scalar native yield/stop behavior.
  Added regressions checking suspended stack retention, all frame indices,
  receiver/register restoration, and native completion/failure/control effects.
- Validation passed: the `nomad_test` build, all six unit groups, compiler-export
  and LSP-process checks, four documentation-preparation tests, strict
  documentation generation/site building, and whitespace checks.
- No gameplay or consuming-project execution was performed. No commit was made.

Next, after review: design source-level struct-method receiver writeback,
including receiver evaluation, named/nested destinations, temporary receivers,
and failure behavior, before changing AST or language semantics.

### Step 5D1 follow-up: Evaluation receivers

The original sequence below used copy-in/copy-out source invocation. The
borrowed invocation slice supersedes its receiver-writeback dependency.

Agreed implementation sequence:

1. Add public `InterpreterReceiver`, `InterpreterObjectReceiver`, and `InterpreterStructReceiver`
   classes for AST evaluation. The base provides type, owned-value snapshot, and
   property access. Object wrappers borrow the existing object; struct wrappers
   explicitly borrow frame storage or own an independent `InterpreterValue` copy.
   Backing storage stays private. Contexts own receiver wrappers, bind them to
   active class/struct frames, and route AST `this` reads and existing receiver
   assignments through them. Preserve frame validation and existing behavior.
   Validate and stop for review.
2. Extract shared typed storing operations from variable/member assignment
   evaluation. Accept already-evaluated values without synthetic AST assignments
   or compiler lowering. Preserve receiver/RHS ordering, frame/copy contracts,
   unrelated sibling changes, and assignment failure guarantees. Validate and
   stop for review.
3. Enable source-level struct methods using typed invocation and the shared
   assignment path. Writable receivers receive the returned receiver update;
   temporary receivers discard it. Complete writeback before delivering a call's
   result. Continue running compiled method bodies through the VM. Validate and
   stop for review.

Only step 1 is currently approved for implementation. Wrapper types are public
evaluation infrastructure, not required host-facing Interpreter argument/result
types. Keep existing `InterpreterValue` embedding contracts, method dispatch,
VM layouts/opcodes, and interpreter no-RVO behavior unchanged.

Before step 3, agree on receiver value timing when arguments modify the receiver,
copy-in/copy-out versus reentrant visibility, success/failure writeback behavior,
and the initial supported writable destination forms. Those semantics are not
approved by the wrapper implementation.

### Interpreter directory separation

Approved scope:

- Move `Interpreter`, `InterpreterContext`, `InterpreterValue`,
  `InterpreterReceiver`, `InterpreterObjectReceiver`, `InterpreterStructReceiver`, and
  `InterpreterCallFrame` into matching `include/nomad/interpreter/` and
  `src/nomad/interpreter/` directories.
- Update includes, CMake source lists, and embedding documentation. Retain the
  `nomad` namespace, existing language-library target, and all execution,
  ownership, and invocation contracts.
- Run unit tests through CTest, then stop for review. Uniform property access
  and source-level struct-method writeback remain separate steps.

Completed:

- Relocated all seven interpreter classes and updated C++ includes, the language
  library source list, embedding examples, and the frame-inspection header link.
  No execution or ownership behavior was changed.
- Moved a native-function registration outside its enclosing Boost assertion in
  `InterpreterReceiverTest.cpp` to avoid MSVC's diagnostic string-length limit;
  the callback body and its assertions are unchanged.
- Validation passed: all six CTest unit groups and whitespace checks. No gameplay
  session was run. Awaiting review before proceeding to uniform property access.

### Interpreter receiver naming

Approved scope:

- Rename `InterpreterComposite` to `InterpreterReceiver`, `InterpreterObject`
  to `InterpreterObjectReceiver`, and `InterpreterStruct` to
  `InterpreterStructReceiver`, including matching headers and sources.
- Rename the receiver regression test file and update C++ references, CMake
  source lists, and embedding documentation without changing behavior.
- Keep only object and struct receiver implementations. Native properties
  require class owners; registered native methods require class or struct
  owners. Strings are class types and use object receivers.
- Run all six CTest unit groups and stop for review. Uniform AST property access
  is the next step; shared typed storing and struct-method writeback follow.

Completed:

- Renamed the three receiver classes, their matching files, and the receiver
  regression test file. Updated source references, sorted CMake source lists,
  and embedding documentation. No receiver behavior or ownership contract changed.
- Validation passed: all six CTest unit groups, absence of old receiver names in
  C++ and CMake files, and whitespace checks. No gameplay session was run.
- Awaiting review before uniform AST property access.

### Specialized receiver factories

Approved scope:

- Add `Interpreter::createObjectReceiver(const ClassType&, NomadObject*)` and
  `Interpreter::createStructReceiver(const StructType&, RuntimeValue*)`.
- Object receivers retain a non-null object and release it when destroyed.
  Struct factory receivers borrow actual mutable storage without copying or
  freeing it. Null storage is valid for zero-sized structs.
- Route existing frame-backed receivers and object-property receiver creation
  through the specialized factories without changing evaluation order.
- Keep the existing owned-value struct constructor until AST callers provide
  explicit backing-storage lifetimes. Do not borrow mutable storage from an
  immutable `InterpreterValue` or enable source-level struct-method calls here.
- Validate factory ownership, shared object identity, borrowed struct mutation,
  string receivers, and zero-sized structs; run all six CTest unit groups and
  stop for review.

Completed:

- Added both specialized factories. Existing frame-backed receiver creation and
  object-property evaluation use them. Object receivers retain and release their
  objects using the existing reference-count helpers, preserving VM diagnostics
  when an unset object field is accessed.
- Struct factory receivers borrow storage without copying or freeing it.
  Regression coverage checks mutation through the borrowed address, independent
  snapshots, object/string lifetimes, null storage for zero-sized structs and
  fields, and rejection of null storage for nonzero-sized structs.
- All six CTest unit groups passed across the validation runs. After correcting
  null-object reference retention, the language and remaining groups were rerun
  successfully. Whitespace checks passed. No gameplay session was run.
- Awaiting review. Owned-value struct construction and the snapshot-producing
  host method-call contract remain unchanged.

Next: establish storage-connected AST receivers for named and nested destinations
and scoped temporaries, then route uniform property access through them. Shared
typed storing and struct-method invocation follow; argument-induced receiver
replacement, reentry, and failure behavior still require agreement.

### Mutable interpreter backing storage

Approved scope:

- Add `InterpreterStorage` with a fixed `const Type*` and owned mutable
  `RuntimeValue` slots. Keep `InterpreterValue` immutable.
- Copy and clean up using the existing type lifecycle operations. Typed
  assignment preserves slot addresses, permits overlapping sources, and retains
  existing copy-failure guarantees. Zero-sized structs remain valid typed owners.
- Use these owners for context-owned variables. Preserve same-type storage
  addresses across assignment and context variable growth; changing a variable's
  type or clearing it still invalidates its storage.
- Keep frame-backed variables, ordinary value-copy semantics, published
  snapshots, AST dispatch, and method-call behavior unchanged.
- Validate ownership, borrowed receiver mutation, zero-sized storage, self and
  overlapping assignment, failed copying, context growth, and type replacement.
  Run all six CTest unit groups, then stop for review.

Completed:

- Added the mutable storage owner and switched context-owned variables to it.
  Same-type assignments use typed in-place replacement; variable growth does not
  relocate existing buffers. Frame storage and immutable published values are
  unchanged.
- Added eight focused regression cases covering stable addresses, independent
  snapshots, retained object/string references, live borrowed struct updates,
  zero-sized storage, self-assignment, invalid widths, partial-copy cleanup,
  failed assignments, context growth, and failed type replacement.
- All six CTest unit groups passed across the validation runs. After correcting
  the new snapshot test's source syntax and assertion formatting, the remaining
  group was rerun successfully. Whitespace checks passed. No gameplay session
  was run.
- Awaiting review before scoped AST receiver work.

Next: scoped storage-connected AST receivers and uniform property access.
Keeping backing storage alive across argument evaluation and reentry remains
part of that design; no shared ownership or new language restriction is approved
by this backing-storage slice. Shared typed storing and borrowed method
invocation follow after their timing and failure contracts are agreed.

### Scoped AST property receivers (superseded)

This slice was validated, then superseded after review of its additional AST
evaluation API. The borrowed invocation slice removes that API and scope class.

Approved scope:

- Add `InterpreterEvaluationReceiver`, owning a short-lived receiver and any
  temporary `InterpreterStorage` or parent scope needed by a nested field.
  Destroy the receiver before its owned backing storage and parent scope.
- Add narrow mutable context/frame storage accessors and direct stored-property
  storage access on object and struct receivers. Do not use `const_cast`, shared
  struct ownership, or mutable access to immutable snapshots.
- Add AST receiver evaluation. Named struct roots borrow context/frame storage;
  nested stored fields borrow parent storage; temporary structs use separately
  owned backing storage. Object receivers retain their object owners.
- Route AST property reads through the common receiver API. Borrowed struct
  field reads copy only the selected field, not the whole root. Preserve ordinary
  snapshot semantics, native property dispatch, zero-sized types, existing
  assignment ordering/write-time roots, and current method-call restrictions.
- Keep context roots alive and at the same type while their storage is borrowed.
  This slice does not impose a new language restriction or claim to solve
  reentrant replacement of borrowed context storage.
- Validate live storage identity, nested and temporary lifetimes, zero-sized
  fields, unread-field copy counts, exception cleanup, and frame validation.
  Run all six CTest unit groups and stop for review.

Next: shared typed assignment operations. Before borrowed method invocation,
agree on argument-induced receiver replacement, reentry, and failure behavior.

Completed and validated:

- Added scoped AST property receiver evaluation and live stored-field borrowing.
- Added five receiver regression cases covering live nested storage, zero unread
  field copies, temporary/AST lifetime separation, retained object parents,
  zero-sized fields, and native getter exception cleanup.
- Extended existing frame tests to verify mutable/const storage address identity
  and rejection of invalidated receiver frames.
- All six CTest unit groups passed with automatic build; whitespace checks passed.
- Assignment and method-call behavior remain unchanged. The source-level
  `c.next + c.next` mutation example is not enabled by this step.

### Borrowed interpreter method invocation

Approved scope:

- Enable interpreted struct-method calls against live receiver storage using
  the existing VM call-storage ABI and compiled method bodies. Add no opcodes
  or method-body AST evaluator.
- Replace AST receiver evaluation and `InterpreterEvaluationReceiver` with a
  scoped operation on the existing `StorageLocation`. Evaluate and retain
  object/temporary owners before arguments; recheck context locations afterward.
  Do not allocate compiler locals for interpreter temporaries.
- Keep receiver implementations thin. Object receivers retain their object;
  struct receivers borrow slots only. Remove the owned-value struct constructor,
  storage variant, and mutable stored-field view virtual.
- Add common receiver `callMethod` dispatch. Call frames borrow struct receiver
  addresses and own argument/result storage only. Keep explicit-copy host
  struct calls, including independent receiver/result snapshots; retain class
  receivers at the invocation boundary without changing class call restrictions.
- Preserve copied ordinary parameters/results, zero-width receiver/result
  behavior, selected-field property reads, frame validation, and synchronous
  method/yield policy. Writes before a borrowed call fails remain visible.
- Preserve same-type argument changes. Clear/retype during argument preparation
  produces diagnostics before invocation; evaluated object owners stay alive.
  Native reentry can read/call live storage while its owner remains valid.
  Clearing/retyping a context root during the method remains an unresolved
  lifetime policy, not a new language restriction or shared ownership mechanism.
- Validate the exact Counter expression, live address identity, copied values,
  nested/object-owned/temporary and empty receivers, argument ordering and root
  replacement, reentry visibility, exception cleanup, and existing host contracts.
  Run all six CTest unit groups and stop for review.

Implementation:

- Added the storage-location bridge and common receiver invocation; removed the
  second AST evaluation path, scope class, and dual-mode struct wrapper.
- Call frames no longer copy or free receivers. Existing host snapshot calls
  own their private mutable copy separately; object calls retain their receiver
  outside the frame.
- Added source invocation regression coverage and migrated property/lifetime
  tests to scoped storage-location operations.
- All six CTest unit groups passed with automatic build. The exact Counter
  regression verifies `total == 3` and live `c.value == 2`; native address-identity
  coverage also verifies zero copies of unread receiver fields.
- Existing host snapshot/copy-failure tests, class receiver retention, argument
  order/root replacement, nested/temporary/empty receivers, native reentry, and
  writes-before-failure coverage passed. Whitespace and obsolete-code-reference
  checks passed. Gameplay and performance were not validated.
- Stop for review without committing or starting the following slice.

Next: agree on context-root clearing/retyping during an active borrowed call,
then consider simplifying the remaining snapshot-based field assignment paths.
Neither shared assignment extraction nor automatic receiver writeback is a
prerequisite for these borrowed method calls.

### Deferred interpreter storage retirement

Approved scope:

- Preserve same-type in-place assignments and logical variable IDs.
- When a context-owned variable is cleared or replaced with another type during
  evaluation, retain its old storage outside the active variable table until the
  outermost evaluation exits. Outside evaluation, release it immediately.
- Cover source execution, formatting, direct AST evaluation, scoped receiver
  operations, native reentry, and exception paths. Borrowed frames and contexts
  still obey their existing owner lifetime contracts.
- Use the existing context/storage owners; introduce no new value/receiver
  classes, shared struct ownership, variable renaming, or ID compaction.
- Validate receivers continuing after clear/retype, nested evaluation not freeing
  caller storage, cleanup after success/failure, snapshots, and stable IDs.

Implemented and validated:

- Added context-owned retired storage and nested evaluation tracking. Same-type
  assignment, active variable lookup, and logical IDs remain unchanged.
- Native clear/retype tests verify old receivers survive nested interpretation
  and retired references are released after success or failure. Scoped receiver
  and formatting tests cover independent snapshots and cleanup.
- Enabled the existing Boost.ScopeExit header-only dependency for exception-safe
  evaluation exits, without adding another value or receiver class.
- All six CTest unit groups passed with automatic build. No gameplay was run.

Next: review this lifetime policy before simplifying remaining field-assignment
copy paths. No immutable/versioned same-type assignment behavior is introduced.

### Local interpreter evaluation guard

Approved scope:

- Return a noncopyable, nonmovable `InterpreterEvaluationGuard` from
  `InterpreterContext::beginEvaluation()`. Its destructor ends one evaluation.
- Replace repeated begin/scope-exit pairs with a local guard at existing entry
  points. Preserve nested evaluation, retirement cleanup, and exception behavior.
- Remove the Boost.ScopeExit dependency. Add no context cloning, value/receiver
  ownership changes, or callback wrapper around evaluation bodies.
- Validate explicit nested guards, early returns, exception exits, and existing
  interpreter regressions; run all six CTest unit groups and stop for review.

Implemented and validated:

- Evaluation entry points now keep the returned guard in a local variable;
  paired begin/end macros and the ScopeExit dependency are removed.
- At the user's request, the guard is declared in `InterpreterContext.hpp` and
  implemented in `InterpreterContext.cpp`; no separate guard files remain.
- Guard type traits, nested scopes, early returns, exception cleanup, and all
  six CTest unit groups passed with automatic build. Whitespace checks passed.
- Stop for review without committing. No gameplay was run.

### Interpreter context metadata access

Approved scope:

- Remove `friend class Interpreter` from `InterpreterContext`.
- Expose mutable resolution metadata through `getResolutionCallable()`, retaining
  frame validation at the same execution/formatting boundaries.
- Use existing read-only callable and runtime getters elsewhere. Preserve
  metadata ownership, source diagnostics, and existing getter behavior.
- Test valid/stale frame metadata access and run all six CTest unit groups.

Implemented and validated:

- Interpreter execution and formatting use `getResolutionCallable()` for
  mutable metadata and frame validation, and existing getters for runtime/path.
  No friendship or direct context-private access remains in the interpreter.
- Valid and invalidated frame getter tests and all six CTest unit groups passed
  with automatic build. Existing read-only metadata access remains unchanged.
- Whitespace checks passed. Stop for review without committing.

### Combined callable metadata getter

Approved scope:

- Replace the separately named resolution getter with mutable/const `getCallable`
  overloads. Both validate borrowed frames; invalidated read-only access now
  throws intentionally.
- Preserve implicit and explicit source execution error reporting, including
  source names, when callable access fails frame validation.
- Update metadata access tests and documentation, run all six CTest unit groups,
  and stop for review without committing.

Implemented and validated:

- Both `getCallable()` overloads validate frames. The separately named getter
  and interpreter friendship are absent.
- Invalid-frame read-only/mutable access and implicit source diagnostics are
  covered by regression tests. Source names and cleared published results are
  preserved on failed implicit source-name lookup.
- All six CTest unit groups passed with automatic build. Whitespace and
  obsolete-reference checks passed. Stop for review without committing.

### Step 5D2: Live interpreter stored-field assignments

Approved scope:

- Use existing storage locations and typed assignment operations for writes
  through named/frame roots, nested stored fields, class-owned structs, and
  temporary roots. Preserve compiled assignment behavior and add no opcodes.
- Evaluate and retain object/temporary owners before the RHS. Recheck context
  locations afterward, preserving same-type replacement and diagnosing cleared
  or retyped roots before committing the selected field.
- Add borrowed-storage `Interpreter::setProperty` overloads for direct IDs and
  names/dotted paths, reusing the existing stored-property resolution. Keep
  immutable-value setters and their independent replacement values unchanged.
- Route receiver and context setters through selected-field assignment.
  Assignment failure follows the selected field's existing `Type::assignValue`
  contract; unrelated fields are not copied. RHS side effects are not rolled back.
- Add focused copy-count, ownership, ordering, reentry, empty-value, and invalid
  host-input coverage. Build/test, then stop for review without committing.

Implementation:

- Removed the named-root-only statement resolver and replacement path. Stored
  assignments use ordinary member resolution and scoped receiver operations.
- Struct receiver/context setters update live selected fields; object path
  setters borrow the stored struct field while retaining the object owner.
- Regression coverage includes complete/nested writes, copied source isolation,
  zero copies of unrelated fields, immutable snapshots, class-owned and temporary
  writes, RHS replacement/clear/retype, retained original owners, RHS failure
  cleanup, empty roots, wrong-owner IDs, invalid paths/types, and span widths.

Validation:

- CTest's automatic build and all six unit groups passed, as did script staging
  and compiler-export integration checks. The initial build exposed missing
  parentheses in new Boost.Test diagnostic expressions; the corrected retry
  compiled and passed the affected groups before the final combined run.
- Ownership/copy-failure regressions passed. New lifetime scenarios repeat three
  times and restore live string/object counts; selected-field writes demonstrate
  zero copies of unrelated reference fields.
- Four documentation preparation tests and the generated strict MkDocs build
  passed. Whitespace checks passed. Gameplay and performance were not validated.
- Stop for review without committing before the next Step 5 parity slice.

Aggregate captures, field-aware formatting, remaining host/native property
parity, NRVO, and fatal VM fault cleanup remain separate.

### Step 5E: Interpreter/host aggregate class-method calls

Approved scope:

- Enable class-receiver interpreter and host methods with copied struct arguments
  and owned struct results, including empty structs, using existing call storage
  and native/script dispatch. Add no opcodes or new callable APIs.
- Preserve class-reference semantics, receiver retention, argument ordering,
  enclosing frames/registers, and scalar method behavior.
- Keep aggregate native object-method calls synchronous, including zero-slot
  signatures. Preserve the existing script-method yield restriction and scalar
  native yield behavior.
- Validate native/script methods, ownership and snapshots, receiver retention,
  nested reentry, host failures, empty values, and suspension behavior. Update
  documentation, build/test, and stop for review without committing.
- Leave compiled struct-returning class-method call lowering for the next slice.

Implementation:

- Object-method validation accepts struct arguments/results while still requiring
  class receivers. Interpreter AST method evaluation uses the existing receiver
  invocation path for aggregate class results.
- Existing call frames own copied argument and result slots; native aggregate
  object-method dispatch rejects yielding and restores prior execution state.
- Tests cover nested/empty and one-slot values, script parameter/local inspection,
  temporary receivers and owner clearing during argument preparation, independent
  arguments/results, shared class fields, result lifetime after VM destruction,
  native/script reentry, partial copy/result failures, exact stack capacity,
  prior yield-state restoration, and unchanged scalar native yielding.

Validation:

- CTest's automatic build and all six unit groups passed, along with script
  staging and compiler-export integration checks.
- The first targeted run exposed test assumptions about native function versus
  script receiver access, physical struct argument indexing, and string objects
  in lifetime counts. Corrected tests passed both targeted and final combined
  runs. No compiled class-method aggregate lowering was enabled.
- New ownership/copy-failure scenarios repeat three times and restore live
  string/object counts. Native and script aggregate suspension checks preserve
  prior execution state; scalar native yielding retains its existing behavior.
- Four documentation preparation tests, generated strict MkDocs build, and
  whitespace checks passed. Gameplay and performance were not validated.
- Stop for review without committing.

Next, after review:

1. Compiled struct-returning class-method calls using the existing return ABI.
2. Native accessor-backed struct property parity.
3. Struct capture copies and callback layouts.
4. Field-aware static/dynamic formatting and remaining host parity.
5. Conservative NRVO after Step 5 is approved, then final tooling/documentation
   and integration completion checks.

Fatal VM fault recovery and gameplay/performance validation remain separate.

### Step 5F: Compiled struct-returning class-method calls

Approved scope:

- Enable compiled native/script class-method struct results in reserved locals
  and existing forwarded return destinations. Cover nested/temporary receivers,
  arguments, property initializers, staged assignments, discarded calls, and
  empty/one-slot results.
- Add specialized `Compiler::addObjectMethodCallIntoLocal` and
  `addObjectMethodCallIntoReturn` emitters using existing opcodes and return ABI.
  Retain the evaluated class receiver outside the callee's storage layout.
- Preserve receiver-first/right-to-left argument order, enclosing receiver
  restoration, independent result/argument copies, and RHS-before-replacement
  behavior. Do not add NRVO or enable native accessor-backed struct properties.
- Verify emitted destination/forwarding operations, execution, ownership, native
  reentry, and owner replacement during argument preparation. Update embedding
  documentation, build/test, and stop for review without committing.

Implementation:

- Class-valued method receivers now use the specialized destination emitters.
  Direct method returns forward the caller's storage without intermediate return
  locals/copies; ordinary assignment still uses its existing staging storage.
- Receiver references remain retained below the hidden return pointer and
  explicit arguments. Empty results omit the pointer without omitting execution.
- Regression coverage checks native/script calls, mixed zero/one/many-slot
  parameters/results, nested/temporary receiver and result field access,
  property initialization, source/result isolation and shared class fields,
  nested receiver restoration, native reentry, exact forwarded pointer identity,
  RHS target preservation, and original-owner retention during variable replacement.
- Instruction dumps verify no local return destination or aggregate return-copy
  operations in forwarded wrappers. Ownership scenarios run three times and
  restore live string/object counts.

Validation:

- CTest's automatic build and targeted language/remaining groups passed on the
  first run. After adding native one-slot returns and property-initializer
  coverage, all six unit groups, script staging, and compiler-export integration
  checks passed.
- Instruction dumps and native pointer-identity checks verify forwarded return
  storage without intermediate return locals/copies. Receiver/argument ordering,
  original-owner retention, staged-target visibility, nested/native reentry,
  empty results, and repeated ownership cleanup passed.
- Four documentation preparation tests, generated strict MkDocs build, and
  whitespace checks passed. Gameplay and performance were not validated.
- Stop for review without committing or starting the next parity slice.

Next, after review: native accessor-backed struct property parity, then struct
captures/callbacks, field-aware formatting, and remaining host parity. Conservative
NRVO follows Step 5 approval, then final tooling/documentation and integration
completion checks. Fatal VM fault cleanup remains deferred.

### Step 5G1: Method-backed native property registration and interpreter access

Approved scope:

- Let native properties reference registered native getter/setter methods,
  reusing ordinary method signatures and the existing aggregate call ABI.
  Preserve existing scalar callback properties and add no opcodes or new
  span-callback family.
- Implement registration and typed interpreter/host receiver access first.
  Getter results are owned snapshots. Whole-property writes invoke setters;
  nested writes/mutating calls affect getter temporaries without automatic
  setter writeback or a getter-only mutation restriction.
- Validate signatures and ownership at registration, preserve receiver retention,
  reentry, copied arguments/results, and empty-value effects. Report unsupported
  compiled/raw property access explicitly until its subsequent integration slice.
- Add tests and documentation, build/test, then stop for review without committing.

Implementation:

- `NativePropertyDefinition`/`Info` carry optional getter/setter method IDs;
  existing callback fields and aggregate initialization positions are preserved.
- Registration requires same-owner native methods, a zero-argument matching
  getter result, and a one-argument matching void setter. Method-backed value types
  must already resolve; callback/deferred registration is unchanged. Mixed
  callback/method definitions and invalid signatures are rejected without publishing.
- Typed object receiver access delegates to the ordinary interpreter method APIs.
  Native properties remain outside object storage layouts. Generated
  API documentation reports read/write access correctly for either property form.
- Interpreter source accepts whole-value reads/writes and snapshot-based nested
  mutation. Read-only/write-only checks account for method IDs. Compiled source
  and raw VM property APIs report explicit unsupported-access errors.
- Regression coverage includes registration/metadata/layout and generated access
  descriptions, scalar/empty/multi-slot results, getter single evaluation, shared
  contained objects, immutable snapshots, getter-only temporary mutation without
  writeback, setter argument isolation, native reentry, retained result lifetime,
  partial-copy/result cleanup, and setter side effects surviving failed source calls.

Validation:

- The initial build exposed a test-only enum-printing requirement in Boost.Test.
  The corrected retry built and passed the targeted language/remaining groups.
- After adding shared-object and getter single-evaluation coverage, CTest's
  automatic build and all six unit groups passed, along with script staging and
  compiler-export integration checks.
- New ownership and failure scenarios repeat three times and restore live
  string/object counts. Getter snapshots, setter input isolation, empty effects,
  temporary mutation without writeback, native reentry, partial cleanup, and
  setter mutations before failure passed.
- Four documentation preparation tests, generated strict MkDocs build, and
  whitespace checks passed. Gameplay and performance were not validated.
- Stop for review without committing or starting compiled accessor integration.

Next, after review: compiled method-backed accessor reads and whole-property
writes using ordinary native method calls; then captures/callbacks, field-aware
formatting, remaining host parity, and NRVO after Step 5 approval. Getter results
remain temporaries rather than addressable stored properties. Fatal VM fault
cleanup remains deferred.

### Step 5G2: Compiled method-backed getter/setter access

Approved scope:

- Lower getter reads through existing method-call emitters and whole-property
  writes through one specialized setter emitter. Add no opcodes or callback APIs;
  keep raw VM property API restrictions unchanged.
- Cover scalar/reference and empty/one/multi-slot values, forwarded getter
  returns, field initialization, argument/discarded reads, temporary mutation
  without writeback, and receiver replacement during the RHS.
- Retain evaluated setter receivers before the RHS and stage struct input before
  invoking the registered one-argument void method. Preserve read-only/write-only
  and assignment type diagnostics.
- Verify emitted operations, execution, getter evaluation counts, parameter
  isolation, native reentry, and ownership. Update documentation, build/test,
  then stop for review without committing.

Implementation:

- Native member resolution retains the selected accessor method while preserving
  property identity. Getter reads reuse ordinary method emission and existing
  local/forwarded return destinations; getters still expose no stored location.
- `Compiler::addObjectSetterCall` accepts a complete value matching a one-argument
  void class method. Struct inputs use reserved staging; copied argument slots and
  retained receivers are released after successful calls. The enclosing receiver
  is restored.
- Setter input remains independent even when the callback modifies its parameters,
  including strings. Replacing the receiver variable during the RHS does not
  redirect the invocation or destroy the original receiver prematurely.
- Tests cover scalar/string and empty/one/multi-slot accessors, local/forwarded
  getter destinations, shared references and independent snapshots, self-assignment,
  stored-field initialization, discarded/temporary reads, getter-only temporary
  mutation without writeback, native reentry, receiver replacement, preserved
  read/write/type diagnostics, and ordinary native-method opcode emission.

Validation:

- The automatic build and language group passed on the initial targeted run.
  The remaining group exposed a test-only native-function lookup error; using the
  registered native ID corrected it, and the remaining group passed on retry.
- After adding compilation diagnostic coverage, all six CTest unit groups,
  script staging, and compiler-export integration checks passed with automatic build.
- Instruction dumps verify forwarded getter destinations and ordinary method
  calls without raw native-property opcodes. Setter staging, getter evaluation
  counts, parameter isolation, temporary mutation, receiver replacement, and
  repeated string/object ownership cleanup passed.
- Four documentation preparation tests, generated strict MkDocs build, and
  whitespace checks passed. Gameplay and performance were not validated.
- Stop for review without committing or starting capture/callback work.

Next, after review: struct capture copies and callback layouts, then field-aware
static/dynamic formatting and remaining host parity. Conservative NRVO follows
Step 5 approval, then final tooling/documentation and integration checks. Getter
results remain temporaries; raw VM property dispatch and fatal-fault cleanup remain
separate.

### Step 5H1: Complete closure capture storage and frame snapshots

Approved scope:

- Give closures flat, full-width owned capture storage while preserving logical
  capture counts, including empty structs and copied struct receivers.
- Read frame captures through physical local/parameter offsets and receiver
  layouts. Cover both current-script and enclosing-script frame paths.
- Add complete capture-storage spans, retain scalar access, and make raw-buffer
  ownership transfer explicit in construction. Preserve class-reference
  identity, independent struct snapshots, and failure cleanup.
- Keep the compiler's struct-capture gate and existing callback execution
  restrictions until the subsequent invocation slice. Add no production opcodes,
  per-struct boxes, or new callback API in this foundation step.
- Add regression coverage/documentation, build/test, then stop for review.

Implementation:

- `Closure` adopts a validated rvalue slot vector and records logical capture
  count separately from its physical storage size. It is noncopyable; complete
  field-wise cleanup includes zero/one/many-slot captures.
- `getCaptureStorage` borrows complete immutable capture slots. Scalar access
  rejects all struct captures; invalid indices throw instead of returning
  placeholder values or null types. Buffer validation fails before ownership
  transfer, leaving a named caller buffer available for cleanup.
- `createClosure` copies complete local/parameter ranges and borrowed struct
  receivers, retaining class receivers normally. It validates runtime/parent
  identity and cleans partial copies if copying or closure construction fails.
- `captureCurrentFrame` uses the current script frame's physical ranges and
  returns owned typed snapshots, including empty/struct receivers.
- Tests cover logical versus physical counts, buffer adoption and rejection,
  one-slot scalar-access rejection, nested structs, empty positions, mixed-width
  parameter/local offsets, shared class fields, independent snapshots,
  class/struct/empty receivers, and completed/partial capture-copy failures.
- Source capture diagnostics and callback invocation paths remain unchanged.

Validation:

- The automatic build and targeted language/remaining groups passed on the first
  run. After adding nested-struct and invalid-buffer coverage, all six CTest unit
  groups, script staging, and compiler-export integration checks passed.
- Mixed logical/physical layouts, complete local/parameter/receiver snapshots,
  empty positions, explicit storage adoption, invalid selector/type/width errors,
  scalar callback regressions, and class-reference identity passed.
- New ownership/failure scenarios repeat three times and restore live string/object
  counts, including failures during completed and partial capture copies.
- Four documentation preparation tests, generated strict MkDocs build, and
  whitespace checks passed. Gameplay and performance were not validated.
- Stop for review without committing or starting callback invocation changes.

Next, after review: migrate callback invocation to the typed call ABI and enable
source-level struct captures, including value-semantic struct `this`; then
field-aware formatting and remaining host parity. NRVO follows Step 5 approval,
then final tooling/documentation and integration checks. Fatal VM fault cleanup
remains deferred.

### Step 5H2: Typed closure invocation and source-level struct captures

Approved scope:

- Add a typed closure overload of `Interpreter::runFunction`, supplying complete
  capture snapshots after explicit arguments through the existing call ABI.
- Enable source-level struct captures, including copied struct `this`, nested
  propagation, and empty values. Captures/ordinary parameters remain independent
  value copies; contained class references retain shared semantics.
- Route existing Runtime/game callbacks with struct captures through typed
  dispatch while retaining scalar callback suspension behavior. Aggregate
  arguments/captures/results are synchronous, including empty structs.
- Keep raw Runtime explicit arguments/results scalar-only; typed closure calls
  accept complete struct arguments/results. Add no opcodes, shared struct
  ownership, automatic receiver writeback, or new callback syntax.
- Test repeated/nested calls, receiver capture snapshots, ownership, reentry,
  failure cleanup, event adapters, host validation, and suspension. Update docs,
  build/test, then stop for review without committing.

Implementation:

- The typed overload validates the registered compiled target and explicit count,
  validates explicit arguments and capture spans separately, and reuses existing
  signature validation and call frames. Scalar-only calls preserve script
  suspension; aggregate calls restore enclosing execution state and reject yielding.
- Removed the compiler's struct-capture rejection. Existing capture propagation
  and complete frame snapshots support locals, parameters, zero/one/many-slot
  captures, nested structs, and value-semantic struct receivers.
- Runtime raw closure adapters delegate to VM closure dispatch without converting
  their arguments into typed value collections. Raw explicit argument/result
  restrictions are checked before execution; scalar callbacks retain suspension.
- Tests cover independent captured/source/parameter values, repeated invocation,
  nested propagation, shared class fields, copied and empty struct `this`, owned
  aggregate/empty/void results, VM/closure-independent result lifetime, native
  reentry, event adapter exceptions, partial-copy failures and stack bounds,
  foreign/invalid host inputs, and scalar versus aggregate suspension behavior.

Validation:

- The automatic build succeeded. The first targeted tests exposed an obsolete
  struct-capture rejection expectation and a string-literal reference-count
  expectation; both were updated to the new behavior and actual constant ownership.
  Targeted language/remaining groups then passed.
- All six CTest unit groups, script staging, compiler exports, LSP process, and
  module-runtime integration checks passed with automatic build after adding
  event-adapter failure and one-slot capture coverage.
- New ownership/copy-failure scenarios repeat three times and restore live
  string/object counts. Repeated/nested callback snapshots, copied struct `this`,
  aggregate arguments/results, native reentry, raw adapter errors, event exceptions,
  stack bounds, and scalar/aggregate suspension checks passed.
- Four documentation preparation tests, generated strict MkDocs build, and
  whitespace checks passed. Gameplay and performance were not validated.
- Stop for review without committing or starting formatting changes.

Next, after review: field-aware static/dynamic formatting and remaining host
parity, then conservative NRVO after Step 5 approval. Final tooling/documentation
and integration checks follow. Fatal VM fault cleanup remains deferred.

### Step 5H2 follow-up: Direct invocation copies from capture storage

Approved scope:

- Remove repeated argument-vector construction from Runtime and typed closure
  invocation. Read explicit arguments and existing closure capture spans
  separately, copying each value once directly into reserved invocation slots.
- Preserve captured snapshots, independent parameters, ownership cleanup,
  scalar suspension, synchronous aggregate calls, and the existing raw host
  argument/result limits. Do not start formatting work or add opcodes.
- Add exact copy-count and source/destination-address regressions, update related
  documentation, build/test, and stop for review without committing.

Implementation:

- Runtime's closure adapter delegates directly to `VirtualMachine::run(closure,
  rawArguments)`; the raw-to-typed argument vector and scalar capture vector are
  removed. The raw result is transferred without an intermediate typed snapshot.
- Typed closure invocation validates borrowed explicit/capture spans independently
  instead of building combined value vectors or converting them to raw vectors.
- The existing call-frame owner reserves cleared VM argument slots before copying
  and records cleanup metadata, not another argument value buffer. Explicit
  values and captures copy directly to those slots. Partial construction releases
  completed/partial copies and restores the original registers/stack.
- Prepared closure execution is shared by raw and typed entry points. Suspended
  scalar calls retain their argument slots; synchronous completion/failure
  releases them normally and preserves enclosing execution state.
- Tests verify exactly one copy per argument/captured field, with original
  argument/capture addresses as sources and actual VM slot addresses as
  destinations. They cover scalar and struct captures, raw and typed callers,
  repeated calls, mutation isolation, raw/typed partial-copy failures, and zero
  argument copies when the full physical argument range will not fit.
- A two-field captured struct returned by value now requires four field-copy
  operations (two invocation copies and two result copies), with no intermediate
  capture snapshots. This is copy-count evidence, not a measured runtime speedup.

Validation:

- The automatic build and targeted language/remaining groups passed on the first
  run. All six CTest unit groups, script staging, compiler exports, LSP process,
  and module-runtime integration checks then passed.
- Exact copy counts and direct source/VM-destination address assertions passed
  for scalar and struct captures through raw and typed entry points. Two-field
  captured struct results require exactly four field-copy operations.
- Repeated invocation, argument/capture isolation, partial-copy failures, zero
  copies on argument stack overflow, scalar suspension through typed/raw/Runtime
  entry points, and existing aggregate yield restrictions passed. Repeated
  ownership scenarios restore live string/object counts.
- Four documentation preparation tests, generated strict MkDocs build, and
  whitespace checks passed. Gameplay and performance were not validated.
- Stop for review without committing or starting the next plan slice.

Next, after review: field-aware formatting and remaining host parity, then
conservative NRVO after Step 5 approval. Fatal VM fault cleanup remains deferred.

### Step 5I: Struct-aware compiled and dynamic formatting

Approved scope:

- Reuse existing VM callable execution for compiled literals, interpreter
  evaluation for dynamic templates, and shared storage/conversion machinery.
- Format whole structs like ordinary class objects: the type name only, including
  empty structs. Explicit field expressions use their normal conversions.
- Bind compiled placeholder captures directly from complete current-frame
  storage, with no capture/argument value-vector conversions.
- Preserve compiled capture snapshots versus live dynamic evaluation, placeholder
  order, diagnostics, receiver semantics, and temporary ownership.
- Verify locals, mixed-width parameters, `this`, nested fields, temporary results,
  empty/whole values, copy counts, failures, and compiled/interpreted behavior.
  Update directly related documentation, build/test, then stop for review.

Implementation:

- The initial implementation introduced a frame-context interpreter overload for
  compiled placeholders. Review clarified the required boundary; the correction
  below removes that overload and executes compiled functions directly on the VM.
- The shared call-frame owner copies current VM frame storage directly into
  reserved invocation slots. Compiled conversion reads complete result storage
  without an extra interpreter-value result snapshot.
- Dynamic formatting retains its existing interpreter expression evaluator.
  Text conversion accepts complete or empty struct results without scalar value
  access; whole-struct output follows the existing type-name conversion.
- Bare scalar parameter placeholders now resolve logical parameter IDs through
  physical offsets, including after multi-slot and empty struct parameters.
- Tests cover static/dynamic whole and field values, nested/temporary results,
  empty receivers and results, mixed parameter widths, source names/diagnostics,
  register preservation, snapshot-versus-live mutation, and repeated ownership.
- Copy-count/source-address tests demonstrate direct frame capture copies with
  no intermediate typed capture snapshots, including partial-copy/result cleanup.

Validation:

- The automatic build succeeded. Initial targeted tests exposed an unsupported
  string-concatenation operator in a new regression; rewriting it using format
  syntax corrected the test, and language/remaining groups passed.
- All six CTest unit groups, script staging, compiler exports, LSP process, and
  module-runtime integration checks passed after adding class-returned struct
  and captured struct-`this` callback coverage.
- Static/dynamic type-name and field output, mixed-width parameter offsets,
  temporary/empty values, snapshot/live mutation, diagnostic locations, direct
  capture copy counts/addresses, and partial-copy/result cleanup passed.
  Ownership scenarios repeat three times and restore live string/object counts.
- Four documentation preparation tests, generated strict MkDocs build, and
  whitespace checks passed. Gameplay and performance were not validated.
- Stop for review without committing or starting the host-parity assessment.

Next, after review: assess remaining host parity and approve Step 5 completion;
then agree conservative NRVO eligibility for Step 6. Final tooling/documentation
and integration completion checks follow. Richer object/struct display and fatal
VM fault cleanup remain separate.

### Step 5I follow-up: Complete conversion pointers and distinct execution paths

Approved scope:

- Change `Type::toString` to accept complete `const RuntimeValue*` storage, with
  `nullptr` only for genuinely zero-sized values. Migrate overrides and callers;
  preserve type-name defaults without throwing away the supplied value.
- Compiled `$"..."` must use compiled lookups/callable functions and direct VM
  execution, not interpreter dispatch or contexts. Dynamic `$text` continues to
  use interpreter evaluation of `{...}`. Share only storage/ownership/conversion
  mechanisms.
- Remove the interim interpreter/frame-context invocation API and preserve
  snapshot/live semantics, field behavior, diagnostics, and lifetime contracts.
- Verify value-dependent converters reading beyond the first slot, null storage
  for empty values, the compiled/dynamic boundary, and failures. Update docs,
  build/test, then stop for review without committing.

Implementation:

- Migrated `Type::toString`, built-in/custom test overrides, host callers, debug
  output, and documentation examples to the const-pointer contract. Dynamic
  formatting passes owned complete storage directly.
- Compiled lookup passes actual local/parameter/receiver storage; no placeholder
  struct value is synthesized. Scalar raw lookup sources retain their ownership
  contracts and reject unavailable multi-slot storage explicitly.
- Compiled callable placeholders share direct frame storage ownership and invoke
  `VirtualMachine::executeFunction`. Closure dispatch shares that prepared
  function execution mechanism. Compiled formatting constructs no interpreter
  context, parses no expressions, and reads return storage without an extra copy.
- Moved the existing dynamic formatting wrapper to interpreter implementation;
  it continues delegating to `Interpreter::formatString`.
- Tests use custom converters that read two slots and require null zero-slot
  storage, verify static formatting creates no runtime compiler while dynamic
  templates do, and check getter-result cleanup when conversion throws.
- Existing copied frame-capture regressions now require four counted field
  copies rather than five because compiled conversion no longer snapshots the
  callable result into an `InterpreterValue`.

Validation:

- The automatic build and targeted language/remaining groups passed on the first
  run. After moving the dynamic wrapper into interpreter implementation and adding
  incomplete-storage rejection coverage, all six CTest unit groups, script
  staging, compiler exports, LSP process, and module-runtime integration passed.
- Value-dependent converters read both slots of a complete value in compiled
  and dynamic formatting. Zero-slot converters require null pointers. Raw lookup
  sources without complete multi-slot storage fail before invoking their getter.
- Compiled formatting creates no runtime compiler, while dynamic templates create
  one per placeholder. Direct VM execution, snapshot/live behavior, captured
  receivers, real result storage, and converter-failure cleanup regressions passed.
- Four documentation preparation tests, generated strict MkDocs build, obsolete
  signature/caller checks, and whitespace checks passed. Gameplay and performance
  were not validated.
- Stop for review without committing or starting another plan slice.

Next, after review: remaining host parity and Step 5 completion review, then
conservative NRVO and final integration/tooling/documentation checks. Richer
format representations and fatal VM fault cleanup remain deferred.

### Step 5I follow-up: Unified call-frame result access

Approved scope:

- Replace call-frame result snapshots and storage views with
  `RuntimeValue* getResult() const` and `NomadIndex getResultSize() const`.
- Remove `getDestination()` and use the same result pointer for construction,
  return destinations, and reading completed results.
- Preserve owned interpreter results, compiled formatting behavior, and cleanup;
  build/test and stop for review without committing.

Result contract:

- Scalar/reference results borrow one writable VM result-register slot.
  Struct results borrow their complete frame-owned writable storage.
- Void and empty structs return `nullptr` and zero slots.
- Access does not copy values, retain references, or transfer ownership.
  Borrowed slots must not outlive their frame.
- `getResultValue() const` returns an independent owned `InterpreterValue`
  snapshot of the complete result, including its type. Interpreter calls use
  this accessor before frame destruction. Compiled formatting converts the
  borrowed result directly.

Validation:

- The development build and final test-target rebuild succeeded.
- Language and remaining CTest unit groups passed. The new direct-frame
  regression covers const-frame writable access, scalar register identity,
  complete struct storage and return-destination identity, empty/void values,
  stable struct addresses, nested register restoration, independent snapshots,
  reference counts, and string cleanup.
- Initial regression assertions incorrectly assumed an active native parameter
  frame and a stack return slot for an empty struct. Correcting those test
  assumptions produced a passing remaining group; no production fix was needed.
- Gameplay and performance were not validated.

Next, after review: assess remaining host parity and approve Step 5 completion;
then agree conservative NRVO eligibility for Step 6. Step 7 final
tooling/documentation and integration checks remain pending.

### Step 5J: Remaining host parity and completion assessment

Scope:

- Assess the remaining Step 5 public host surfaces against the agreed contracts.
- Add focused coverage for unsupported generated/bound struct shapes without
  introducing new restrictions, generated struct wrappers, or native mappings.
- Validate the integrated Step 5 behavior, record remaining limitations, and
  stop for review before NRVO.

Assessment:

| Surface | Result and regression evidence |
| --- | --- |
| Owned values, construction, and function calls | Complete typed storage and independent results, including empty structs, are supported through `Interpreter`. `InterpreterStructCallTest.cpp` covers construction, native/script calls, partial copies, nested native reentry, and frame register/result ownership. |
| Struct and class methods | Snapshot struct calls return explicit updated receiver/result values; borrowed calls mutate live slots. Object receivers remain retained. `InterpreterStructMethodTest.cpp` and `InterpreterStructCallTest.cpp` cover receiver/result combinations, aggregate class methods, nested calls, and synchronous aggregate execution. |
| Stored and method-backed properties | Stored fields expose complete typed values; live writes target selected storage, while getter results are snapshots without automatic setter writeback. `InterpreterStructPropertyTest.cpp`, `InterpreterStructAssignmentTest.cpp`, and `InterpreterReceiverTest.cpp` cover named/frame/object/temporary roots, lifetime, and native accessors. |
| Captures and callbacks | Complete owned captures and typed closure arguments/results preserve struct copy semantics and empty types. `LanguageCallbackEventTest.cpp` and call-frame regressions cover repeated/nested invocation, copied struct receivers, event adapters, native reentry, and result lifetime after VM/closure destruction. |
| Formatting | Compiled placeholders execute through VM lookups/callables; dynamic templates use interpreter evaluation. Format regressions cover complete multi-slot conversion, empty values, captured receivers, copy counts, and conversion failure cleanup. |
| Low-level host calls | Prepared VM calls use the callable's physical argument/result layout; `Interpreter` supplies the typed host invocation API. Existing raw `RuntimeValue` vector adapters remain scalar/reference/void-only for explicit arguments/results, rejecting structs rather than treating a first slot as a complete value. Closure adapters support complete struct captures. This existing boundary is unchanged. |
| Generated bindings | Existing scalar/enum class wrappers remain supported and retain their logical-field-index access through `ScriptObject`'s physical-offset mapping. Struct wrappers and struct-valued binding fields remain explicitly unsupported, including zero/one-slot structs; there is no scalar fallback or silent omission. |
| Object storage | `ScriptObjectTest.cpp` covers scalar fields after inline multi-slot and empty fields, complete field storage, and explicit rejection of scalar access to empty/one-slot structs. |

New regression coverage:

- Binding generation explicitly rejects selected empty, one-slot, and multi-slot
  struct declarations without creating output files.
- Dependency structs do not prevent generating an otherwise supported scalar
  class. Exporting a struct-valued field fails with the stored-field diagnostic
  and preserves every previously generated output byte-for-byte.
- Runtime binding contracts reject struct-valued fields at all three widths.
  A rejected binding does not poison subsequent supported scalar binding
  construction, reads/writes, or cache reuse.
- No production behavior, host API, or export restriction changed in this slice.

Validation:

- The development build succeeded.
- All six CTest unit groups passed: cleanup contracts, bindings, audio, language,
  tooling, and remaining tests.
- Script staging, compiler exports, LSP process, and module-runtime integration
  checks passed.
- Generated log bindings remain current through the binding freshness regression.
- Gameplay, performance, sanitizer execution, distribution/package integration,
  and graphical smoke tests were not run for this assessment.

Completion recommendation:

- No additional Step 5 production gap was found within the assessed, agreed
  scope. Step 5 is ready for explicit completion review, not automatically
  marked approved by this assessment.
- Preserve the existing deferrals: generated C++ struct wrappers/native mapping,
  struct generic arguments, automatic equality, open structs, and fatal VM fault
  recovery. Richer formatted representations remain separate.
- Step 7 must reconcile stale milestone statements in the embedding guide,
  including the old claims that struct closure captures are unsupported and
  that already-implemented interpreter/compiled operations remain separate.
  Published documentation completion is not claimed here.

Next, after Step 5 approval: present and agree a conservative named-local NRVO
eligibility rule before implementing Step 6. Verify eligible operation removal
and ineligible fallback/copy semantics. Step 7 then completes tooling,
documentation, and final integration checks. No commit or push in this slice.

### Step 6: Conservative named return-value optimization

Prerequisite: step 5 approved. Satisfied following the Step 5J review.

- Present a small, explicit NRVO eligibility rule before implementation.
- Reuse caller-provided storage for eligible named locals, avoiding redundant
  local storage and return copies.
- Keep a correct field-wise fallback for ineligible paths, multiple live return
  candidates, and possible aliasing.
- Do not expose early changes to existing targets or change parameter-copy
  semantics.
- Verify optimization with instruction dumps or focused emission tests, not only
  successful compilation or matching numeric output.
- Do not add a general optimizer, copy-constructor hooks, or custom destruction
  semantics as part of this step.

Review stop: approved eligible cases, eliminated operations, and semantic
regression tests. Do not claim a runtime speedup without measurements.

### Step 6A: Approved initial NRVO eligibility

Review decision:

- Step 5 is signed off within the agreed milestone scope.
- Start with the conservative initialization/return pair, rather than a broader
  mutable-local optimization. This slice records the rule only; it does not
  change compiler behavior or claim operation removal.

Eligible shape:

```nomad
fun makePoint
    result = new Point 3 4
    return result
end
```

- The callable returns a nonempty struct. The named local has exactly that type.
- The final two top-level executable statements first initialize that local and
  then return that same bare local identifier, with no intervening statements.
- This is the local's first initialization; it is not used earlier or within its
  own initializer and is not subsequently reassigned.
- The initializer already supports direct return-destination forwarding, such as
  a supported constructor or struct-returning call. Do not add syntax-specific
  forwarding rules or expand supported expression shapes for this optimization.
- The local is not a parameter, receiver, or capture and is not captured by a
  nested callable. There are no competing returns or conditional/loop-local
  candidate paths.
- Earlier ordinary statements and other locals do not become return candidates.
  Their evaluation order, ownership, and cleanup remain unchanged.

Fallback:

- Keep the existing field-wise copy path for parameter/receiver returns,
  captured locals, empty structs, earlier-used or reassigned candidates,
  non-forwardable initializers, competing returns, branches, and loops.
- A named local mutated or read between initialization and return is not
  eligible in this first slice, even if a future analysis could prove it safe.
- Eligibility is an optimization decision, not a new language restriction:
  ineligible programs retain their existing behavior and diagnostics.

Implementation boundaries for the next slice:

- Reuse the caller's existing return destination for the eligible initializer.
  Eliminate the candidate's redundant physical local storage, assignment/return
  staging storage attributable to the pair, and field-wise return copies.
- Preserve logical local identity and type information, including correct
  frame-backed inspection and native reentry. Do not leave an optimized local
  pointing at a removed stack offset or silently expose partially assigned
  values. If an observation cannot be preserved, retain the fallback or stop
  for a design decision rather than hiding the local.
- Retain the existing destination-isolation/staged-assignment contracts; no
  premature changes to an existing caller target may become observable.
- Cleanup must not free the returned fields as callee-owned locals. The caller
  continues owning the result; argument copies and unrelated cleanup remain.
- Reuse existing return-destination emission and opcodes. Do not add a general
  optimizer, broad alias analysis, new VM instructions, or public host-facing
  restrictions without another design discussion.

Required evidence:

- Compare emitted operations and physical local slot counts for eligible
  constructor and call initializers; assert the specific eliminated storage,
  staging copies, and callee cleanup rather than only matching numeric output.
- Verify independent parameter copies, retained reference fields, repeated
  ownership cleanup, nested calls/native reentry, and staged assignment into
  existing caller targets.
- Verify fallback emission and behavior for empty values, parameter/receiver
  returns, captured/earlier-used locals, intervening statements, reassignment,
  alternate returns, and control flow.
- Test frame-backed local inspection for optimized candidates, including
  initializer-time observation where supported.

Validation of this slice:

- Reviewed current return staging, expression destination forwarding, local
  allocation/cleanup, storage-location access, and frame-inspection consumers.
- Documentation/design only: no C++ build or runtime tests were run or needed.
  No compiler changes, performance measurements, or NRVO completion are claimed.

Next: implement and validate the approved pair as Step 6B, then stop for review.
Broader mutable-local NRVO is not automatically included. Step 7 follows Step 6
sign-off with tooling, documentation reconciliation, and final integration checks.

### Step 6B1: VM logical-local storage lookup without NRVO

Approved scope:

- Implement full-width VM logical-local lookup only, preserving current behavior.
- Keep interpreter-owned copies simple. Do not enable NRVO, add return-backed
  local metadata, change initializer-time frame visibility, or add shadow storage.

Implementation:

- Added mutable and const `VirtualMachine::getFunctionVariableStorage(VariableId)`
  overloads. They borrow the complete active script local using its logical ID,
  returning its physical slot count and a null/empty view for zero-sized locals.
- Routed scalar local value access/freeing, interpreter frame reads/writes,
  compiled frame captures, closure capture creation, and compiled format-local
  lookup through that shared VM entry point.
- Left physical bytecode operands, raw local-range APIs, argument/result layouts,
  local allocation/cleanup, and interpreter snapshot ownership unchanged.
- Updated the public embedding contract with borrowed storage, active-frame
  lifetime, const/mutable overloads, and native callback behavior.

Regression evidence:

- A new VM storage test covers scalar, empty, one-slot struct, and nested
  multi-slot struct locals containing strings and an object reference.
- Const/mutable views have identical addresses and exact complete sizes.
  Repeated lookups retain no additional string/object references. Mutations
  affect the live local while an owned interpreter snapshot remains independent.
- Frame reads/mutable views and whole-value frame assignment use the same local
  storage. Native argument slots do not displace enclosing script-local lookup.
- Nested typed host calls select their own logical locals and restore the
  enclosing callable, storage addresses, values, and native argument bindings.
  A frame containing only empty locals requires zero physical local slots.
- A separate tail-return regression preserves the initializer-time observation
  identified during Step 6B investigation: a native initializer partially fills
  its result while reading and mutating the caller's separate default local.
  Completing assignment replaces that frame mutation, and the final returned
  value remains the initializer's completed result.
- Both scenarios repeat three times and restore live string/object counts.

Validation:

- The initial development build exposed MSVC's string-literal limit when
  Boost.Test stringified a large native registration expression. Moving that
  registration outside the assertion corrected the test; the development
  rebuild succeeded.
- Language and remaining CTest unit groups passed, including frame access,
  closure/capture, compiled/dynamic formatting, and the initializer-observation
  regression.
- Four documentation preparation tests, regenerated API documentation, and the
  strict MkDocs build passed.
- No gameplay, performance, sanitizer, or NRVO operation-removal claims are made.

Remaining decision and next slice:

- The approved syntactic pair alone does not establish that aliasing a local to
  return storage preserves initializer-time frame inspection/mutation.
- Before enabling compiled NRVO, agree whether optimized frame observation may
  see in-progress return storage or whether such initializers must retain the
  existing separate-storage fallback. This slice deliberately makes neither
  choice and introduces no observation restrictions.
- After that decision, implement the approved compiler eligibility/storage/
  forwarding/cleanup changes and verify exact emission and fallback behavior.
  Interpreter evaluation continues copying; no interpreter NRVO is required.
- Step 7 follows Step 6 sign-off with tooling, documentation reconciliation, and
  final integration checks. Stop for review without committing or pushing.

### Step 7: Tooling, documentation, and completion checks

Prerequisite: step 6 approved.

- Update editor syntax, relevant language-server declaration/signature handling,
  compiler dumps, and documentation generation for logical struct types.
- Update the language guide with construction, copying, methods, empty structs,
  return behavior, and the generic/equality limitations.
- Update public embedding documentation for approved host APIs and ownership
  contracts. Keep implementation details in design documentation.
- Keep CMake source lists sorted when adding matching source/header/test files.
- Run the integrated regression suites and relevant tooling/documentation checks.
- Reassess open structs only if its earlier decision explicitly deferred the
  choice; do not silently expand scope.

Review stop: final behavior, validation evidence, limitations, and follow-up
scope. No commit or push without a separate request.

## 5. Acceptance and regression matrix

| Area | Required evidence |
| --- | --- |
| Original example | `Point` has three logical fields/slots; `Player` has two logical fields/four slots; `printPoint` has one logical parameter/three slots; `updatePoint` has three logical parameters/five slots. Do not assume field order before the layout decision. |
| Copy isolation | Local copies, parameters, returned values, and captures do not alias their source struct; `updatePoint` leaves its caller's original unchanged. |
| Reference members | Copied structs retain shared class/collection references with correct reference counts; nested structs remain independent values. |
| Mutation | Direct and deeply nested stored fields mutate in place; methods restore the correct receiver after nested calls; temporary mutation executes and cleans up. |
| Empty structs | Zero-slot locals, fields, parameters, captures, returns, and receivers preserve logical type identity and constructor/method side effects. Arguments are still evaluated. Empty-only aggregates remain zero-sized. |
| Construction | Explicit initializers execute once in source order; remaining fields receive agreed defaults; selected constructor bodies and early returns follow class-style behavior. Copying does not invoke constructors. |
| Initialization safety | Partial initialization and failing calls do not leak, double-free, or access uninitialized fields. No new strong exception guarantee for all assignments is assumed without discussion. |
| Lifetimes | Repeated construction/copy/assignment/return/capture of strings and class references returns ownership counters to baseline; self/overlapping assignment is safe. |
| Frames and arity | Mixed zero/one/many-slot parameters and locals, recursion, nested calls, special callback argument shapes, and native reentry preserve values, stack balance, and logical signatures. |
| Return destinations | Callees work with caller-local, temporary, and safe object-field destinations; owners remain alive; alias-observable assignment preserves the previous value until RHS completion. |
| Optimization | Direct-construction and approved NRVO cases omit the redundant materialization/return-copy operations; ineligible cases remain correct. |
| Restrictions | Direct/nested struct generic arguments and unsupported equality produce compiler diagnostics. Ordinary non-struct generics and reference fields still work. |
| Existing consumers | Existing scalar/class programs, bindings, interpreter inspection, callbacks, formats, and tools retain behavior. Validate an affected consuming project when available. |
| Performance structure | No per-instance struct boxes or repeated runtime type validation; no new aggregate-dispatch cost on unchanged scalar paths without an approved justification. |

## 6. Validation workflow

- Add focused cases to the existing Boost.Test harness and build the relevant
  targets using workspace CMake tasks. Use `CMake: build tests`,
  `CMake: build compiler`, and `CMake: build runtime` as needed.
- Run the smallest relevant suite selection after each code step. The existing
  `nomad_test_language`, `nomad_test_bindings`, `nomad_test_tooling`, and
  `nomad_test_remaining` groups cover different affected surfaces; do not assume
  the language group alone covers every runtime/binding regression.
- Before completion, run the unit groups together using the configured test
  preset with the `unit` label, then relevant compiler/LSP and consuming-project
  integration checks. Use existing platform presets and CI sanitizer coverage;
  do not introduce a new test or benchmark framework.
- For RVO/NRVO, assert the expected emitted operations as well as output and
  ownership behavior. Treat instruction-count evidence separately from timing.
- For changed published documentation, use the existing generation and strict
  MkDocs workflow when available. Do not rebuild C++ for documentation-only
  changes.
- If a build fails, fix relevant errors and retry once. If it still fails after
  two attempts, stop and report the failure.
- At each review stop report changed surfaces, checks actually run, compilation
  versus execution evidence, and remaining limitations.

## 7. Repository reference map

- Type/lifecycle metadata: [Type.hpp](../../include/nomad/script/Type.hpp),
  [Type.cpp](../../src/nomad/script/Type.cpp),
  [ValueType.hpp](../../include/nomad/script/ValueType.hpp),
  [CompositeType.hpp](../../include/nomad/script/CompositeType.hpp),
  [ClassType.hpp](../../include/nomad/script/ClassType.hpp),
  [BaseTypes.hpp](../../include/nomad/script/BaseTypes.hpp),
  [RuntimeValue.hpp](../../include/nomad/script/RuntimeValue.hpp).
- Members/declarations/inference:
  [ClassDeclaration.cpp](../../src/nomad/compiler/ClassDeclaration.cpp),
  [ClassFieldInitializerStatement.cpp](../../src/nomad/compiler/ClassFieldInitializerStatement.cpp),
  [StructDeclaration.cpp](../../src/nomad/compiler/StructDeclaration.cpp),
  [StructFieldInitializerStatement.cpp](../../src/nomad/compiler/StructFieldInitializerStatement.cpp),
  [Property.hpp](../../include/nomad/script/Property.hpp),
  [Compiler.cpp](../../src/nomad/compiler/Compiler.cpp).
- Logical versus physical calls:
  [Callable.hpp](../../include/nomad/script/Callable.hpp),
  [ScriptCallableBase.hpp](../../include/nomad/script/ScriptCallableBase.hpp),
  [CallNode.cpp](../../src/nomad/compiler/CallNode.cpp),
  [Argument.cpp](../../src/nomad/compiler/Argument.cpp),
  [ReturnStatement.cpp](../../src/nomad/compiler/statements/ReturnStatement.cpp).
- Execution/storage:
  [Expression.cpp](../../src/nomad/compiler/Expression.cpp),
  [ScriptObject.cpp](../../src/nomad/script/ScriptObject.cpp),
  [VirtualMachine.hpp](../../include/nomad/script/VirtualMachine.hpp),
  [VirtualMachine.cpp](../../src/nomad/script/VirtualMachine.cpp),
  [OpCode.hpp](../../include/nomad/script/OpCode.hpp),
  [RuntimeOpCodes.cpp](../../src/nomad/script/RuntimeOpCodes.cpp).
- Interpreter/captures:
  [InterpreterContext.hpp](../../include/nomad/interpreter/InterpreterContext.hpp),
  [Closure.cpp](../../src/nomad/script/Closure.cpp).
- Binding compatibility:
  [ScriptBindingObject.cpp](../../src/nomad/modules/ScriptBindingObject.cpp),
  [BindingGenerator.cpp](../../src/nomad/modules/BindingGenerator.cpp).
- Validation patterns:
  [LanguageClassTest.cpp](../../tests/LanguageClassTest.cpp),
  [ScriptMethodTest.cpp](../../tests/ScriptMethodTest.cpp),
  [ObjectConstructionTest.cpp](../../tests/ObjectConstructionTest.cpp),
  [tests/CMakeLists.txt](../../tests/CMakeLists.txt),
  [workspace tasks](../../.vscode/tasks.json).
- Public behavior and documentation:
  [language.md](../language.md), [embedding.md](../embedding.md),
  [documentation workflow](../documentation.md),
  [build/test workflow](../building.md).
