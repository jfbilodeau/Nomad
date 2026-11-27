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
- The game exposes `Audio`, `Font`, and native window/view classes. Scripting
  `Scene` and `Entity` classes remain proposed.
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

Status: implemented and validated; awaiting review. Later migrations remain
proposals, not approved implementation scope.

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

Discuss and approve these contracts before implementation:

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
