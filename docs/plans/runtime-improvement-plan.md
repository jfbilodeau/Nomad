# Runtime improvement plan

Status: Step 1 is implemented and validated, awaiting review.
Step 2 has not started and is not approved for implementation.

Approved planning decisions:

- `TypeManager` is the canonical type/member registration API.
- Redundant public APIs may be removed without compatibility wrappers.
  Migrate in-repository callers, tests, and documentation in the same slice.

Implementation approval applies only to the current slice.
Obtain approval before each following slice, implement it, then stop.
Do not commit or push without a separate request.

## 1. Goals and boundaries

Reduce redundant registration surfaces, obsolete entry points, repeated
execution plumbing, and unnecessary metadata copies in
[Runtime.hpp](../../include/nomad/script/Runtime.hpp) and
[Runtime.cpp](../../src/nomad/script/Runtime.cpp).

The review baseline is 745 header lines and 3,064 implementation lines.
Forty-two implementation methods directly forward to the type manager,
accounting for approximately 200 lines. Some convenience queries will remain;
this is an inventory, not a promise to delete every forwarder.

Measure public entry points removed and net production-code changes across
all touched files. Report tests and documentation separately. Moving code into
another file or replacing old wrappers with differently named wrappers does
not count as simplification. Do not impose an arbitrary line-count target.

Preserve:

- Script syntax, overload selection, generated initializers, and hub behavior.
- Class reference semantics and inline struct value semantics.
- Callable/property IDs, descriptor identity, and existing ownership contracts.
- Empty structs, complete aggregate storage, captures, and scalar adapter limits.
- Native factory constructors whose declaring and constructed class types differ.
- Deferred native signatures and property callback guards.
- The distinction between free-function views and all-script-callable views.

Non-goals:

- New struct features, aggregate constants, generic registrars, or new VM opcodes.
- Generalized execution frameworks, additional caches, or speculative indexing.
- Identifier-syntax validation in runtime registration, or repeated compiler
  type/null checks inside native callbacks.
- VM fault recovery, general rollback guarantees, unrelated formatting, or
  changes to the completed [struct plan](struct-implementation-plan.md).

## 2. Target API boundary

| Responsibility | Canonical surface |
| --- | --- |
| Class, struct, enum, and opaque-ID descriptor registration | Existing specialized `TypeManager` APIs |
| Stored fields, native accessors, methods, constructors, and layout finalization | Existing category-specific `TypeManager` APIs |
| Parameterized types/constructors, templates, inference, and lazy callback/iterator/predicate types | `TypeManager` |
| Native class indexer and iterable registration | `TypeManager::registerNativeClassIndexer` and `registerNativeClassIterable` |
| Free-function registration and callable/property/indexer ID catalogs | `Runtime` |
| Hub qualification/publication and cross-catalog coordination | `Runtime` |
| Script initialization, compiler/VM creation, and execution adapters | `Runtime` |
| Common type lookup by ID/name, type enumeration, and fundamental-type getters | Retained `Runtime` conveniences |

Keep `Runtime::getTypeManager()`. Preserve the existing narrow public catalog,
namespace, and signature-coordination APIs used by the manager; do not replace
them with friendship or a new access-control abstraction.

Keep hub-scoped registration conveniences, but route descriptor registration
directly through the manager after qualification. Do not add new hub APIs.
Existing factory/initializer callbacks taking `Runtime*` need not change:
their bodies can access the manager.

Retire generic registration aliases and mirrored specialized registration
methods from `Runtime`. Do not recreate category-dispatching compatibility
helpers elsewhere. Specialize callers where their owner category is known.

This is a C++ source-breaking cleanup. External consumers will need source
updates and a rebuild; compatibility with unavailable consuming projects
cannot be claimed.

## 3. Execution rules

Before each slice, confirm the worktree and affected callers. Cross-check
symbol references with source/template searches; an empty editor reference
result alone is not proof that an API is unused.

Each slice must:

1. Make one coherent change and migrate every affected repository surface.
2. Update directly related embedding documentation.
3. Build using the existing workspace CMake tasks and run focused regressions.
4. Report removals, behavior preserved, checks run, and remaining limitations.
5. Stop for review, including after each registration sub-slice below.

The order below is recommended, not an assertion that every slice depends on
the preceding one. Final validation depends on all implemented slices.
Discuss any necessary new helper or material behavioral change before editing.

## 4. Reviewable implementation sequence

### Step 1: Enforce the documented scalar constant contract

The [embedding guide](../embedding.md#host-owned-struct-storage) already forbids
passing complete structs through single-slot constant APIs. The review found
that registration does not enforce this host precondition; this is not a
claim that ordinary source constants are broken.

- Centralize the existing hub constant value-type checks in
  `Runtime::registerConstant`, before catalog or storage mutation.
- Reject struct types explicitly, including empty, one-slot, multi-slot, and
  unfinalized structs. Do not depend on querying an unfinalized layout's size.
- Keep invalid host registration explicit through the existing error logging
  and invalid-ID convention. Do not add identifier-syntax checks.
- Let hub registration qualify the name and use the canonical constant path.
- Clarify rejection behavior in the guide's constant-ID section.

Acceptance:

- Both direct and hub registration reject unsupported types without publishing
  an ID/member or retaining a value.
- Primitive, enum, string, and class-reference constants retain their existing
  values and lifetime behavior.
- Add focused coverage in [LanguageConstantTest.cpp](../../tests/LanguageConstantTest.cpp)
  and [LanguageHubTest.cpp](../../tests/LanguageHubTest.cpp); retain enum and
  reference-lifetime coverage.

Review stop: scalar-only boundary enforcement, not aggregate constant support.

Completion evidence (2026-11-10, Windows Debug):

- Constant type validation is centralized before catalog/storage mutation.
- New regressions cover null, void, internal, foreign-runtime, empty/one-slot/
  multi-slot/unfinalized struct rejection through both registration paths,
  unchanged string reference counts on rejection, and retained class references.
- The `nomad-development` build and the language and remaining-unit CTest
  groups passed. All four documentation preparation tests passed; reference
  generation with the built tools, strict MkDocs, and whitespace checks passed.
- No public API was removed; production code has no net line-count change.
  Other platforms/configurations and manual gameplay were not validated.

### Step 2: Remove dead layers and obsolete code

- Move the private `registerNativeCallable` implementation into its sole
  caller, `registerNativeFunction`; remove the redundant declaration/wrapper.
- Remove the unused runtime `getMutableClassType` wrapper and `getEventType`
  alias after confirming their callers remain absent.
- Delete commented-out former APIs and unused direct includes.
- Remove the always-true size-change condition after successful hub insertion,
  preserving rebuild failure rollback and diagnostics.

Acceptance: no new public APIs or helper framework; native registration,
deferred signatures, overloads, and hub failure behavior remain unchanged.
Use [DeferredNativeFunctionTest.cpp](../../tests/DeferredNativeFunctionTest.cpp),
[CallableCatalogTest.cpp](../../tests/CallableCatalogTest.cpp), and hub regressions.

### Step 3A: Make property registration and layout finalization manager-only

- Migrate generic and explicit stored-field registration to the existing
  class/struct manager APIs.
- Migrate native accessor registration to `registerNativeClassProperty`.
- Migrate layout finalization to the manager.
- Remove the corresponding runtime registration aliases and forwarders.
- Retain runtime property IDs, lookup, initializer metadata, and catalog
  coordination. Native accessors must not become stored fields.

Acceptance: preserve field inference, redeclaration, layout freezing,
nested/empty structs, offsets, native accessors, and deferred type resolution.
Run focused cases from [TypeManagerTest.cpp](../../tests/TypeManagerTest.cpp),
[ClassTypeTest.cpp](../../tests/ClassTypeTest.cpp),
[StructTypeTest.cpp](../../tests/StructTypeTest.cpp), and
[DeferredNativePropertyTest.cpp](../../tests/DeferredNativePropertyTest.cpp).

### Step 3B: Make method registration manager-only

- Migrate `addMethod`, `registerScriptMethod`, and mirrored specialized runtime
  entry points to the existing native/script class/struct manager methods.
- Update both compiler declaration paths, built-in/native registration,
  module code, and test fixtures.
- Retain runtime callable IDs, native-member indexes, and signature checks.

Acceptance: unchanged receiver borrowing, parameter/result ownership, member
resolution, overload behavior, and method source metadata. Cover native/script
class and struct methods, temporary receivers, and source compilation.

### Step 3C: Make constructor registration manager-only

- Migrate generic constructor aliases and mirrored runtime constructors to
  the specialized manager APIs, then remove the runtime entry points.
- Preserve the distinct declaring/constructed-type native class overload.
- Preserve generated initializer sequencing and struct destination storage.

Acceptance: constructor catalogs and IDs remain stable; class factories,
empty/multi-slot structs, overload rejection, and initialization side effects
remain correct. Use [ObjectConstructionTest.cpp](../../tests/ObjectConstructionTest.cpp),
[ScriptObjectTest.cpp](../../tests/ScriptObjectTest.cpp), and callable/struct tests.

### Step 3D: Finish descriptor, family, and iterable/indexer migration

- Migrate descriptor creation, parameterized registration/specialization,
  templates/inference, lazy type creation, indexers, and iterables.
- Remove duplicate runtime surfaces, retaining only the convenience query
  boundary defined in section 2.
- Update hub wrappers to call the manager without changing namespace behavior.
- Preserve callback identities and existing factory callback signatures.

Acceptance: compiler, runtime, CLI/LSP, collections, and modules build without
the retired APIs. Type identity, specialization reuse, namespace collisions,
constructor inference, iterator behavior, and collection behavior are unchanged.

For every Step 3 sub-slice, include relevant
[embedding examples](../embedding.md), collection registrations, native members,
and retained module fixtures. Inspect
[binding generation](../../src/nomad/modules/BindingGenerator.cpp) and templates
for emitted API names, not just compiled references. Regenerate affected
bindings with existing tooling only when their inputs actually change; do not
hand-edit build-generated or vendored files.

### Step 4: Retire the duplicate documentation exporter

- Migrate the remaining runtime-dumper test to `generateDocumentation` in the
  existing [documentation module](../../src/nomad/script/Documentation.cpp).
- Remove `Runtime::dumpDocumentation` and its separate formatter.
- Preserve the current compiler documentation output and unresolved-signature
  coverage. Do not silently add an instruction section to the CLI reference.
- The unused legacy dumper's instruction listing retires with that API.
  If retaining that output is required, stop for a separate decision rather
  than creating another formatter.

Acceptance: existing reference-generation, compiler-export, and module-host
documentation tests pass; no generated documentation is edited manually.

### Step 5: Prune thin query adapters without merging distinct views

Retire these remaining adapters after migrating their callers:

- `getNativeFunctionFn`: callers use the native function descriptor's guarded
  callback access.
- `setScriptFunctionReturnType`: callers use the script descriptor; retain
  coverage for category narrowing and live inferred signatures.
- `getFunctions(TempVector<ScriptFunction*>&)`: use the existing standard-vector
  enumeration; remove its direct temporary-heap include if no longer needed.
- `getFunctionSize`: preserve descriptor bytecode-length coverage without a
  redundant runtime-wide entry point.

Keep the existing free-function enumeration and source/address views in this
plan. They intentionally differ from all-script-callable views, as covered by
[SourceCompilationTest.cpp](../../tests/SourceCompilationTest.cpp).
Do not forward a free-function source lookup to the broader lookup and merely
cast its selected result: selection inside a method can differ.

Acceptance: LSP, documentation, game/debug callers, invalid-ID checks, source
ranges, and uncompiled-callable behavior retain their existing contracts.

### Step 6: Reduce execution adapter plumbing

Keep the public closure/function overloads and their ownership contracts.
Before implementation, review a small concrete proposal for sharing VM
acquisition and result finalization.

- Share startup validation and lazy VM acquisition without moving their
  observable timing.
- Preserve distinct discard and owned-copy result paths; discarding must not
  copy a result first.
- Keep explicit arguments/results scalar-only in these raw adapters while
  preserving supported struct captures.
- Preserve invalid-input logging, initialization reentry checks, and existing
  yield/reentry behavior.

Acceptance: successful calls copy only when requested and release results
exactly once. Exercise strings/class references, void results, closures,
struct captures, initialization failures, and aggregate rejection.

Do not introduce a universal execution request, receiver variant, or behavior
flags. If a proposed extraction adds indirection without a net clarity/code
benefit, retain the small specialized implementations. Fault recovery remains
outside scope.

### Step 7: Borrow metadata instead of copying callback snapshots

- Use existing property descriptors for owner/name/type-only native-member
  lookup and enumeration.
- Change indexer lookup to return a borrowed `const NativeIndexerInfo*`,
  returning null for invalid IDs; migrate callers and document its lifetime.
- Retain property snapshots where guarded callback access still needs them.
  Do not bypass unresolved-property guards or redesign all native properties.

Acceptance: metadata-only member queries do not copy accessor callbacks.
Add a focused callback-copy regression where practical; verify invalid
lookups, descriptor stability through catalog growth, and deferred guards.
This is not a claim of measured gameplay performance improvement.

### Step 8: Integrated acceptance and final API audit

- Verify no repository code, emitted binding text, examples, or documentation
  still calls retired APIs. Historical plans are historical evidence, not
  current API guidance; do not rewrite the completed struct plan.
- Build all development targets and run all configured Windows Debug CTests.
- Run documentation preparation checks and strict published-documentation
  generation using matching built tools/SDK.
- Verify existing SDK/module/package consuming fixtures with the new headers.
  Do not change their scripts to conceal an engine regression.
- Report before/after API counts and net production-code changes, plus actual
  validation results and unsupported/unavailable environments.

## 5. Validation workflow

Use the existing [workspace tasks](../../.vscode/tasks.json) and
[CMake presets](../../CMakePresets.json). CLI equivalents for Windows Debug:

```powershell
cmake --build --preset windows-debug
ctest --preset windows-debug -R '^nomad_test_(language|remaining)$'
```

Select the smallest relevant cases/groups using the existing
[test configuration](../../tests/CMakeLists.txt). Include tooling and binding
groups when their callers change. Do not rerun every integration test after
each small slice; run the complete matrix at Step 8:

```powershell
ctest --preset windows-debug
py -3 -B -m unittest discover -s scripts -p "test_generate_documentation.py"
git diff --check
```

Follow [the documentation workflow](../documentation.md) for generation and
strict MkDocs validation when published pages change. A plan-only edit needs
no C++ build or SDK regeneration; plans are not copied into the published site.

Distinguish compilation, automated execution, packaging smoke checks, and
manual gameplay. Do not infer cross-platform, sanitizer, real-device, or
performance results from Windows Debug checks.

## 6. Completion criteria

- Type/member registration has one canonical public surface, with no
  replacement compatibility layer.
- Retired APIs have no active in-repository consumers.
- The scalar constant precondition is enforced without aggregate expansion.
- Documentation export has one maintained implementation.
- Retained execution/reflection APIs preserve their documented semantics.
- Ownership and index structures with different purposes remain intact:
  the owning free-function collection and non-owning all-callable catalog are
  not duplicate storage.
- Related host documentation, generated consumers where affected, and
  regression coverage match the final API.
- Net production-code reduction is reported across all touched files.
- All executed checks and remaining limitations are recorded before sign-off.

Next slice after Step 1 review: Step 2, removing dead layers and obsolete code.
