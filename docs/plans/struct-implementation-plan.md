# Struct implementation plan

Status: implementation proceeds only in separately approved, reviewable slices.
Steps 1A through 1AM have been reviewed. Step 1AO is an approved ABI design;
Step 1AP has been reviewed; Step 1AQ below is implemented and awaiting review. Struct argument copies
are supported; compiled struct receivers and destination returns remain unimplemented.
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
- Open structs are conditional: reuse existing open-class discovery if the extra
  work is modest. Present the concrete cost and get a decision before implementing
  open structs or deciding to defer them.
- Future struct collection initialization should run a parameterless constructor
  when available, otherwise the generated `$init`. Record this requirement, but
  do not implement struct collections in this milestone.

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

### Step 6: Conservative named return-value optimization

Prerequisite: step 5 approved.

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
  [InterpreterContext.hpp](../../include/nomad/script/InterpreterContext.hpp),
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
