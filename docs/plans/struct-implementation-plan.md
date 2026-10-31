# Struct implementation plan

Status: implementation proceeds only in separately approved, reviewable slices.
Steps 1A through 1F have been reviewed; Step 1G below is implemented and awaiting review.
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

Next design discussion: the real struct descriptor and its size/lifecycle API
contracts. Wire finalization at the existing Resolve-to-Emit boundary once struct
declarations and member inference are available. Object allocation and executable
aggregate accesses remain later work.

Remaining Step 1 work, requiring separate design approval:

- Add the approved real struct descriptor and its finalized-size contract.
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
