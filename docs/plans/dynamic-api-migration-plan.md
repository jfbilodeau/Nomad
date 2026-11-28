# Dynamic API migration plan

## Goal

Move engine-defined dynamic variables and ordinary functions with dotted names
to explicitly declared hub members or class-instance members. Preserve existing
behavior unless a change is reviewed and approved.

## Approved boundaries

- Implement one reviewable step at a time, then stop for approval.
- Do not change engine ownership, compiler architecture, or language semantics
  without discussing the change first.
- Reuse existing callable, property, hub, and compilation APIs. Do not introduce
  parallel implementations, compatibility aliases, new syntax, or new opcodes
  without approval.
- Prefer Nomad implementations for behavior that existing primitive members and
  ordinary functions can express. Keep native integration where live engine
  state, resources, or asynchronous scheduling require it.
- Declare the `math` and `system` hubs in the system module's `.nomad` sources,
  not in C++. Both hubs are open.
- Attach module-native hub functions through the existing module parse callback,
  after script declarations are available. Their availability before compilation
  intentionally changes. Unqualified primitives remain available during module
  registration.

## Current state

- The game, audio/music, debug, logging, and OS hubs already exist. Some functions
  and constants are already genuine hub members.
- Dynamic variables can already be indexed inside hubs, but remain dynamic
  variables. Adding a hub alone does not replace their getter/setter mechanism.
- Exact qualified identifiers and context prefixes take precedence over ordinary
  member access. Lexical class/struct `this` correctly takes precedence over the
  game's `this.*` context.
- The game exposes `Audio`, `Font`, native window/view classes, and a `game.Scene`
  reference with selected methods. Scene properties/camera/events and scripting
  entity instances remain proposed.
- `other.*` property reads use the first selected entity; writes and methods
  operate across the selection. A single entity reference is not equivalent.

See [the language guide](../language.md) and
[the embedding guide](../embedding.md) for the existing contracts.

## Proposed API destinations

These destinations are proposals, not approval to implement later steps.

| Existing family | Proposed destination |
| --- | --- |
| `math.*`, `system.exit` | Explicit members of script-declared hubs. |
| `window.*` | Properties and methods of a live window object. |
| `scene.*` | Properties and methods of a scene instance. |
| Entity `this.*` APIs | Properties and methods of an explicit entity receiver. |
| `other.*`, `select*` | An explicit selection and ordinary Nomad iteration where equivalent. |
| Game/audio/music/debug dynamic settings | Live service-object properties or ordinary hub getter/setter functions. |

Hubs do not currently provide ordinary computed-property declarations. Replacing
`game.volume = value` therefore requires an approved object-member path or
getter/setter API; do not add computed hub properties as an implicit workaround.

Some names cannot become ordinary member chains unchanged. For example,
`scene.tilemap.width.pixel` treats `width` as an integer, and
`scene.trigger.layer` treats `trigger` as a function. Prefer straightforward
approved names such as `widthInPixels` and `triggerLayer` over artificial objects
introduced only to preserve spelling.

## Step 1: system module hub registration

Status: implemented, validated, and reviewed.

Approved implementation scope:

1. Add module sources declaring `open hub math` and `open hub system`.
2. Register `math.ceiling`, `math.floor`, and `system.exit` as native hub members
   during the existing parse callback.
3. Preserve their names, parameter and return types, mathematical behavior, and
   orderly shutdown behavior. Keep conversions and `breakpoint` unqualified and
   available during module registration.
4. Reuse the existing native numeric primitives and shared registration code,
   rather than duplicating their implementations.
5. Report missing declarations and failed native registrations as compilation
   errors, without silently creating C++ hubs or falling back to free functions.
6. Update the system module documentation and add focused regression coverage.

Direct calls to `registerSystemFunctions` and the game's owned-runtime path retain
their existing immediate registration behavior in this increment. Migrating those
non-module paths requires a separate decision about how they receive script-owned
declarations; do not add automatic source loading or new Runtime APIs here.

Validation:

- Check that hubs come from module scripts and accept matching open declarations.
- Check native hub membership, positive/negative rounding, class initializers
  calling the deferred functions, and shutdown after the current invocation.
- Check both executable compilation and analysis.
- Check that missing module declarations fail explicitly.
- Run the relevant CTest groups and compilation validation, then stop for review.

## Step 2: one live-state family

Status: implemented, validated, and reviewed.

Approved decisions:

- Expose one read-only `game.window` instance, without a `window` compatibility
  alias. Preserve existing member names under the new path.
- Keep `mouse` and `resolution` as live child objects.
- Replace window dynamic variables, ordinary free functions, and callbacks with
  native class members using the existing registration APIs.
- Keep SDL ownership with the game and reject access through retained references
  after game destruction. Do not add a window-creation API.
- Nomad source cannot add methods to an already registered native class. Use
  native bindings for this family rather than add language rules or duplicate
  helper APIs.

The user-added `math.PI` declaration remains unchanged.

- Use existing native class properties/methods over the current engine state.
- Preserve read/write permissions, validation, clamping, and side effects.
- Do not mirror live state in ordinary hub fields or initializer snapshots.
- Move suitable convenience routines into Nomad and remove their replaced native
  implementations. Candidates include sprite centering, anchor repositioning,
  and velocity clearing, subject to equivalent checks and side effects.
- Update the existing documentation, completion metadata, and debugger inspection
  paths rather than building a second API or inspector.

Validate the family with focused tests and the existing consuming game smoke
project. Stop before migrating another family.

Completed validation covers live aliases, child views, read/write permissions,
retained-reference failures in host and compiled calls, callback captures and
self-clearing, and debugger rendering. The language, tooling, and remaining
CTest groups, packaged headless graphical smoke project, CMake build, and strict
MkDocs validation passed. No manual graphical/gameplay validation was performed.

## Step 3: scenes, entities, and selection

### Step 3a: scene references and non-event methods

Status: implemented and validated; awaiting review.

- Expose `game.scene` using an ordinary parameterless hub function.
- Give each scene one stable reference object. References target their receiver,
  not whatever scene is currently active, and do not keep removed scenes alive.
- Missing current scenes and use after actual scene deletion raise runtime errors.
  Creation/removal queues and engine ownership remain unchanged.
- Move animated-tile, entity/text creation, loading, pause/unpause, and removal
  functions to native scene methods, without compatibility aliases.
- Leave scene properties, camera/events, custom `scene.*` variables, entities, and
  selection unchanged. Their data and event contracts need separate approval.

Completed validation covers receiver identity, operations outside the receiver's
ambient context, missing/removed scenes, queued entity/text creation, and callback
captures. The language, tooling, and remaining CTest groups, packaged headless
smoke project, CMake build, reproducible grammar generation, and strict MkDocs
validation passed. No manual gameplay validation was performed.

### Following increments

Next, discuss the scene state/context boundary before migrating properties and
camera access. In particular, removing dynamic properties while leaving the
`scene.*` prefix active must not silently turn old assignments into custom variables.
Then address scene events, entity receivers, and selection in separate reviewable
increments.

Discuss and approve the remaining contracts before implementation:

- How scripts obtain current scene/entity receivers without changing lexical
  class/struct `this`.
- Object identity, deletion, reference lifetime, and captured callback behavior.
- Whether custom context variables become stored fields, and who owns that
  storage across multiple references to the same engine object.
- Missing scene/entity behavior.
- Selection iteration, first-entity reads, broadcast writes, and result behavior.

Do not replace a broadcast assignment with a per-entity calculation unless that
behavior change is approved. Preserve right-hand-side evaluation and selection
semantics where the migration promises equivalence.

## Step 4: remaining state and legacy cleanup

Migrate the remaining game/audio/music/debug settings one family at a time.
Retain persisted debug settings and existing setter behavior.

Custom `global.*`, `scene.*`, `this.*`, and `other.*` context variables are a
separate approval boundary. Hub fields require explicit declarations, and class
fields have different storage contracts. Existing persistence targets variable
contexts and flat JSON keys; do not silently change saved data or invent new
serialization functionality.

Only retire the dynamic-variable registry, compiler/VM paths, and public host
APIs after all approved callers have migrated. Update interpreter and format
evaluation, instruction dumps, generated documentation, LSP metadata, debugger
inspection, and regression tests together.

Preserve directory-derived script function names, qualified type/enum names,
and dotted resource strings. Removing arbitrary dotted local identifiers or
changing directory-to-function naming requires a separate language decision.

## Object scripts: compiler foundation

Status: implemented, validated, and reviewed.

An object-script directory maps sources to a receiver class without changing their
path-derived identities. The file body becomes an initializer method; named `fun`
declarations become ordinary methods shared by every script targeting that class.
Method calls require explicit receivers. File-body `this.field` assignments infer
stored fields using constructor-style rules; named methods only update existing
fields. Returns retain ordinary method inference rather than becoming void-only.

The first increment provides compiler/embedding registration and tests using
existing class, method, field, and receiver-capture machinery. Initializer entries
are selected through the host method catalog by their source identity, such as
`entities.player`. Native Entity/Scene storage, project configuration, factory
binding, and named bound callbacks remain separate follow-up work.

The root `scene` API will be a current-context hub, not a replacement object value.
Object-script initializers and methods use their own typed `this`. Agree the
remaining data and callback contracts before changing those engine APIs.

Validation passed for receiver-bound initializer invocation, per-instance fields,
shared methods and overloads, ordinary return values, inline receiver captures,
editor snapshots, invalid receiver mappings, and native-property permissions.
The build, language/tooling/remaining CTest groups, grammar stability check, and
strict MkDocs validation passed. No new VM instructions were introduced.

Next: integrate native Entity/Scene field storage, project directory configuration,
factory initializer binding, and explicit receiver binding for named callbacks,
then implement the root scene facade in reviewable increments. The compiler
foundation alone does not enable object scripts in existing game projects.

## Object scripts: native scene storage

Status: implemented, validated, and reviewed within the approved scope.

This increment completes stored-field support on the existing native scene
reference before introducing Entity bindings or automatic game integration.

- Each native scene allocates one field-layout snapshot when created and
  initializes fields to their type defaults.
- Every reference to that scene shares its field values. Different scenes remain
  independent.
- Field declarations must be compiled before scene creation. Existing instances
  are not resized when the class later gains fields.
- Actual scene removal releases the stored fields and invalidates retained
  references, without changing queued removal or engine ownership.
- Scalar and struct access reuse the existing `asScriptObject` storage contract
  in the compiler, VM, and interpreter.
- Existing `scene.*` variables, game factories, and project configuration remain
  unchanged.

Validation passed for shared aliases, independent scenes, scalar/string and struct
fields, nested struct mutation, field-layout snapshots, and releasing stored
objects/self-references on removal. The build, language/tooling/remaining CTest
groups, packaged headless smoke project, grammar stability, and strict MkDocs
validation passed. No manual gameplay validation was performed.

Known limitation, explicitly deferred from this increment: a fault during a
compiled method can leave expression/register temporaries unreleased. One
reproduction is invoking an initializer that evaluates `new ScenePayload` before
writing a field through a removed scene reference. Scene removal releases its
own stored fields correctly; this is a separate execution-unwinding problem.
Low-level opcode inputs may be borrowed, so releasing registers inside failing
opcodes is not a valid general fix. Discuss an explicit exception-ownership
contract before implementing that follow-up.

Native entity reference/storage work follows below. Engine member migration,
configured object-script directories, factories, named callbacks, and the root
current-context `scene` hub remain separate follow-up contracts.

## Object scripts: native entity storage

Status: implemented, validated, and reviewed.

- Register the non-constructible `game.Entity` reference type.
- Each native entity supplies one stable reference through its C++ API and
  allocates one stored-field snapshot when created.
- References share their entity's custom fields; different entities remain
  independent. Fields start at their type defaults.
- Scenes continue owning entities. Entity removal releases its fields and
  invalidates retained references, including when its scene is removed.
- Compile fields before creating entities. Existing allocations do not expand
  when additional fields are declared later.
- Keep legacy `this.*` and `other.*` APIs, built-in entity members, callbacks,
  factories, and project configuration unchanged. Do not add another current-
  entity accessor or a compatibility alias in this increment.
- The deferred VM fault-temporary cleanup limitation remains separate.
- Preserve the user's `game.scene` accessor rename and align its existing
  consumers and documentation, without adding an old-name alias.

Validation passed for shared aliases, independent scalar/string and struct fields,
nested struct mutation, entity/parent removal, owned-field cleanup, fixed layouts,
and unchanged legacy entity APIs. The build, language/tooling/remaining CTest
groups, packaged headless smoke project, reproducible grammar generation, and
strict MkDocs validation passed. No manual gameplay validation was performed.

After review, migrate native entity members needed by existing initializer files,
then connect object-script directory configuration and factory invocation. Named
callback receiver binding and the root scene facade follow in separately reviewed
steps. Stored fields alone do not enable existing game scripts as object scripts.

## Object scripts: Nomad geometry values

Status: implemented and validated; awaiting review.

The direct entity-property increment was paused in favor of defining geometry
and derived behavior in Nomad before adding more native bindings.

- Add root `Point` and `Rectangle` structs in the game module's scripts.
- Use float fields; rectangles store top-left `x`/`y` and `width`/`height`.
- Supply zero/default and value constructors, movement methods, and
  parameterless rectangle `left`/`top`/`right`/`bottom` methods.
- Keep ordinary struct value semantics and permit unnormalized dimensions.
- Do not add syntax, compiler rules, or automatic property writeback.
- Leave native entity members and legacy `this.*`/`other.*` bindings unchanged.

Validation passed for construction, movement, rectangle-edge calculations, value
copy independence, and module loading in analysis and executable compilation.
The build, language/tooling/remaining CTest groups, packaged headless smoke
project, reproducible grammar generation, and strict MkDocs validation passed.
No manual gameplay validation was performed.

After review, choose the minimal native geometry bridge for entity position/size
and implement derived operations in Nomad where possible. A native getter
returning a struct is a snapshot; mutations of that snapshot do not update the
entity unless the whole replacement is assigned back. Discuss any different
writeback behavior before implementation.

Entity nested views/methods, directory configuration, factory binding, named
callbacks, and the root scene hub remain subsequent reviewable increments.
