# Embedding Nomad in C++

Nomad's scripting language can be embedded without the game engine. When building
as part of the Nomad source tree, link your host target to `Nomad::Language`.
See [building from source](building.md) for toolchain requirements and the
[language guide](language.md) for script syntax.

## Ownership

The host owns the `Runtime`, interpreters, virtual machines, and interpreter
contexts. Destroy contexts before their borrowed interpreter, interpreters before
their borrowed VM, and VMs before the runtime.
Types and callables used by a context must remain valid.

- `Runtime` owns registered APIs, types, callables, and bytecode.
- `VirtualMachine` executes compiled functions and provides the execution
  environment for native calls, object members, and dynamic variables.
- `InterpreterContext` borrows an interpreter and owns interpreter
  variables, its latest typed result, and its latest error. It can additionally
  borrow a script frame.
- `Interpreter` borrows one VM and provides execution and value operations.
  Pass the interpreter when constructing a context; the context obtains its VM
  from that interpreter. Keep the context to retain interpreter
  variables between executions. `context.getInterpreter()` returns the supplied
  instance by const reference; contexts may share an interpreter without sharing
  their variables or results. Temporary interpreters cannot be borrowed.

Neither the interpreter nor its context creates or owns a VM. Interpreters are
noncopyable, and `interpreter.getVirtualMachine()` returns the borrowed VM by reference.

## Experimental generated script bindings

`nomad generate-bindings <module-directory>` produces a C++ header, source, and
`nomad-module.cmake` inventory. Generate explicitly and commit the outputs; use
`--check` in a separate freshness check. See [CLI reference](cli.md#experimental-module-bindings)
for supported declaration shapes and dependency discovery.

Consume the inventory explicitly; it never invokes a generator:

```cmake
include("${MODULE_DIRECTORY}/generated/nomad-module.cmake")
target_sources(my_host PRIVATE ${NOMAD_BINDING_HEADERS} ${NOMAD_BINDING_SOURCES})
target_include_directories(my_host PRIVATE "${NOMAD_BINDING_INCLUDE_DIRECTORY}")
target_link_libraries(my_host PRIVATE Nomad::Language)
```

Generated names live in `nomad::generated::<module-name>` with module hyphens
changed to underscores. Qualified script type names use underscores in C++;
for example `log.FileSinkConfiguration` becomes `log_FileSinkConfiguration`
in `nomad::generated::log`. Stored fields expose `getXXX()` and `setXXX(value)`.
Enum names and numeric mappings are preserved. Unsupported names and collisions
are errors rather than silently renamed APIs.

Enum fields additionally expose an integer getter and setter overload:

```cpp
log_Level getLevel() const;
NomadInteger getLevelAsInteger() const;
void setLevel(log_Level value);
void setLevel(NomadInteger value);
```

Each generated enum also provides `<enum-name>ToInteger(value)` and
`<enum-name>FromInteger(value)`, such as `log_LevelToInteger` and
`log_LevelFromInteger`. These are `constexpr`, `noexcept` static casts.
Enum accessors and conversions preserve every `NomadInteger` value, including
unnamed enum values; they do not validate membership. The receiving native API
is responsible for rejecting values it does not support.

Class wrappers expose `typeReference()` without requiring a runtime or compiled
scripts:

```cpp
using Settings = nomad::generated::log::log_FileSinkConfiguration;
auto configurationType = Settings::typeReference();
auto levelType = nomad::generated::log::log_LevelBinding::typeReference();
```

Both return `nomad::TypeReference`, declared in
`<nomad/modules/TypeReference.hpp>`. Its `name` owns the fully qualified script
type name, such as `log.FileSinkConfiguration` or `log.Level`; it contains no
runtime pointer or resolved ID. An enum retains its C++ `enum class` API and
gets a separate `<enum-name>Binding` companion for `typeReference()`. Companion
name collisions are generation errors. These references describe types only:
they do not register or resolve types, validate contracts, or enable deferred
native registration by themselves.

For native registration, call the generated `deferTypes(Compiler&)` once to
obtain a `DeferredTypes` bundle containing all generated enum and class types:

```cpp
const auto types = nomad::generated::log::deferTypes(*context->compiler);
const auto parameter = nomad::defParameter("configuration", types.sinkConfiguration, "");
// types.level is available for enum parameters and return types as well.
```

Bundle fields use camelCase qualified names, omitting a leading hub matching the
module name: `log.SinkConfiguration` becomes `sinkConfiguration`, while
`dependency.Mode` becomes `dependencyMode`. Field and bundle-name collisions are
generation errors. Repeated calls reuse that compiler's placeholders; nothing is
cached globally or registered in the runtime's type catalog. Every requested
type must be supplied by the script declarations before compilation completes.
Bundle pointers remain placeholders, not forwarding types; use resolved native
signatures or runtime type lookup after compilation.

Compile the module scripts into the host runtime before using a wrapper.
`ModuleHost` includes selected modules' scripts automatically. Bare runtimes
must compile those declarations explicitly.

```cpp
#include <nomad/generated/log/Bindings.hpp>
#include <nomad/modules/ScriptBindings.hpp>
#include <nomad/script/VirtualMachine.hpp>

// runtime already contains the compiled module script declarations.
nomad::ScriptBindings bindings(runtime);
nomad::VirtualMachine machine(&runtime);
using Settings = nomad::generated::log::log_FileSinkConfiguration;
const auto& binding = Settings::bind(bindings); // Optional eager validation and cache initialization.
auto settings = Settings::create(bindings, machine);
settings.setDirectory("logs");
auto sameObject = Settings(bindings, settings.getObject());
```

- `create(bindings, machine)` runs the ordinary script constructor and class field
  initializers. Defaults are not copied into C++. Compiling/generating bindings,
  wrapping an existing receiver, and copying a wrapper do not run initializers.
- The first `bind(bindings)`, construction, or wrapping validates the runtime-local class, exact stored-field
  names and types, ordinary zero-argument constructor, and referenced enum
  names/numeric mappings. Native properties and unsupported shapes are rejected.
  The helper caches the resolved class, constructor ID, property IDs, field
  indexes. Equivalent contracts share one immutable binding in
  that helper, never global mutable IDs. Subsequent wrappers reuse it. Separate
  helpers have independent caches, even when associated with the same runtime.
- Generated classes derive from `ScriptBindingObject`, inheriting receiver
  ownership, `getObject()`, `getRuntime()`, and `getBinding()`. Field accessors use
  numeric contract slots mapped to cached runtime field indexes, not string
  lookups or repeated schema validation. Contract slots are independent of script
  declaration order. Adding class members does not freeze the class or invalidate
  existing slots; bindings do not automatically revalidate later extensions.
- Wrapping borrows the input pointer and retains its own intrusive reference.
  Copies retain another reference; moves transfer it. Destruction releases it.
  `getObject()` is borrowed; use `addRef()` if retaining it beyond the wrapper.
  No `shared_ptr` is put in a `RuntimeValue`.
- The runtime must outlive the helper, wrappers, and receivers. The helper must
  outlive all returned binding references and wrappers that use them. Do not
  unload or replace registered types/bytecode while wrappers are live. Neither
  helpers nor wrappers extend runtime lifetime or make access thread-safe.
- Wrong runtime, VM, class receiver, missing declarations, field-type mismatch,
  and enum mapping mismatch throw `NomadException`.
  Moved-from handles cannot be accessed. Strings cross the interface by value
  with their full length, including embedded nulls.

`<nomad/modules/ScriptBindingObject.hpp>` supplies the shared wrapper base.
`ScriptBindings::getClassBinding(contract)` retains its own contract snapshot, so
the input contract need not outlive the call. The returned `ScriptClassBinding`
and all wrappers must remain within the helper's and runtime's lifetimes. Its primitive slot
accessors check bounds and access types; integer access can also read/write a
contracted enum without validating its numeric value.

Host-side binding support is available under `<nomad/modules/...>`, including
`BindingGenerator.hpp` for programmatic generation.

### C++ logging

Include `<nomad/log/Logger.hpp>` to use a standalone `Logger` or the engine's
`nomad::log::getLogger()`. A new logger owns a default console sink, uses the
global minimum level `Info`, and buffers ordinary messages until `flush()`.
Error, critical, and fatal messages flush immediately; fatal then throws.

The logger owns all its sinks. `createConsoleSink(ConsoleSinkConfiguration)`,
`createFileSink(FileSinkConfiguration)`, and
`createMemorySink(MemorySinkConfiguration)` return a strong `LogSinkId`.
All configuration structs inherit `active` and `level` from
`SinkConfiguration`. Sinks default to active with a minimum level of `Info`;
the global logger level still filters messages first.
To deliver debug messages, lower both the global level and the receiving sink's
level to `Debug`.

The `log` module also exposes typed script logging configuration,
including ordinary `ScriptObject` settings and distinct `log.ConsoleSink` and
`log.FileSink` objects. See
[Programmatic configuration](nomad-log.md#programmatic-configuration).
Script factories use the module context's logger and copy the supplied settings;
discarding the configuration or script handle does not remove a sink.

`<nomad/log/LogSinkObject.hpp>` provides the C++ `LogSinkObject` wrapper.
Construct it with the owning `Logger&` and a registered `LogSinkId`.
`getLogger()` returns the owning logger. `getSinkId()` retains the ID even after removal;
`getSink()` resolves it on each
call and throws for an unknown or removed sink. Construction also rejects
unknown or removed IDs. The wrapper does not own or cache the concrete sink;
destroying it does not remove or deactivate that sink.
The logger must outlive the wrapper. Coordinate lookup and use with concurrent
sink removal, just as for `Logger::getSink()`.

`log` registers the wrapper under the independent closed class types
`log.ConsoleSink` and `log.FileSink`. Both expose read/write `level` and `active`
properties; only `log.FileSink` exposes file settings. Native property access resolves the sink
ID on every read and write; removed sinks throw rather than accessing stale
storage. There is no script constructor; each script sink factory returns
its corresponding class type. `log.removeSink` has an overload for each type.
Sink functions reject objects belonging to a different logger.
Hosts must supply non-null objects matching the declared script type when
invoking native callbacks directly.
Type registration is internal to the logging module; C++ hosts obtain the
registered descriptor from the module's runtime.

The base configuration, level, and sink ID are declared in
`<nomad/log/LogSink.hpp>`. Each concrete configuration is declared in its sink's
header: `<nomad/log/ConsoleSink.hpp>`, `<nomad/log/FileSink.hpp>`, or
`<nomad/log/MemorySink.hpp>`. `<nomad/log/Logger.hpp>` includes these headers and
declares `LogDeliveryMode`.

```cpp
nomad::Logger logger;
nomad::ConsoleSinkConfiguration console;
console.active = false;
console.level = nomad::LogLevel::Warning;
const auto consoleId = logger.createConsoleSink(console);

logger.replaceSinks({consoleId});
logger.setDeliveryMode(nomad::LogDeliveryMode::Synchronous);
logger.warning("Configuration applied");
```

An inactive sink remains owned and addressable but receives no delivery
callbacks. Use inactive factories to prepare a replacement:
`replaceSinks(ids)` validates the IDs, flushes pending entries to the currently
active sinks, retains and activates the specified sinks, and destroys every
other sink. Requested IDs remain unchanged. Failed validation or delivery does
not commit the replacement.

`getSink(id)` returns a borrowed `LogSink*`, or null for an unknown or removed
ID. The const logger overload returns `const LogSink*`. Include the concrete
sink header when casting to `ConsoleSink`, `FileSink`, or `MemorySink`. The base exposes
`getActive`/`setActive` and `getLogLevel`/`setLogLevel` for later adjustment.
The pointer remains valid until that sink is removed, discarded by replacement,
or destroyed with its logger.
The global level is checked when submitting a message; sink activation and
levels are checked when delivering it.

- `removeSink(id)` flushes before destroying the specified sink; an unknown ID
  throws.
- `clearAllSinks()` flushes before destroying all sinks, without installing a
  replacement console sink. It also destroys inactive staged sinks.
- `clear()` discards pending entries without changing sinks.
- Removed IDs are never reassigned by that logger. IDs belong to the logger
  that created them; `isValid()` checks the numeric range, not registry membership.
- `setDeliveryMode(Synchronous)` flushes the buffered backlog before changing
  mode. Subsequent accepted messages are delivered before the call returns.
  `Buffered` preserves the usual host-controlled flushing behavior.

The `nomad::log` namespace offers the same factory, lookup, removal, replacement,
level, delivery-mode, and flushing operations for the engine logger.
The former borrowed-pointer `Logger(LogSink*)`, `addSink(LogSink*)`, and
pointer-based removal APIs are no longer supported.

For custom sinks, derive from `LogSink`, pass a `SinkConfiguration` to its
constructor, and transfer ownership with `createSink(std::unique_ptr<LogSink>)`.
Do not delete a pointer obtained from `getSink`. A module providing custom sink
code must remove its sinks before its native library unloads. Core console,
file, and memory sink code does not depend on the game or log module remaining loaded.

Submission, delivery, factories, replacement, and removal are serialized.
Borrowed pointers and concrete sink data still require coordination with any
thread that may remove a sink or deliver entries. In particular, do not inspect
or clear a memory sink's entries while another thread writes to it.
Sink callbacks may change activation/levels, but must not recursively log,
flush, remove, or replace sinks through the same logger; those operations throw
instead of recursing or destroying a sink while its callback is running.

Sink exceptions propagate to the caller. A failed delivery batch is consumed,
not replayed: earlier sinks or entries may already have received it. Explicitly
handle `flush()` or configuration errors if delivery is required for shutdown.
Delivery stops at the first exception; custom sinks must use RAII for cleanup
rather than relying on `end()` being called after a failed callback.
The logger destructor reports standard exception failures to standard error and
releases owned sinks without allowing those exceptions to escape.

#### File sinks

Include `<nomad/log/FileSink.hpp>` to inspect a file sink or adjust its rotation
settings. C++ callers supply an explicit `directory`; relative directories are
resolved when the sink is created. The selected `log` module supplies
project-relative defaults through its
[project logging settings](nomad-log.md#project-logging-schema).

```cpp
nomad::FileSinkConfiguration file;
file.directory = projectDirectory / "logs";
file.name = "application";
file.active = false;
const auto fileId = logger.createFileSink(file);
logger.replaceSinks({fileId});
logger.info("File logging configured");
logger.flush();
```

`name` is a base filename, not a path or extension. It defaults to `application`,
producing `application.log`. Archives are `application.1.log`,
`application.2.log`, etc.; `.1` is newest.

| Setting | Default | Behavior |
| --- | --- | --- |
| `rotationMode` | `FileRotationMode::Size` | Select `None` or `Size` for ongoing rotation. |
| `rotateAtStartup` | `true` | Rotate a nonempty existing log once when the sink starts, independently of `rotationMode`. |
| `maxSizeBytes` | 10 MiB (10,485,760 bytes) | Positive byte limit, used by `Size`. |
| `maxFiles` | 10 | Number of archives, excluding the active file. Zero discards old active contents when rotating. |

`maxSizeBytes`, `maxFiles`, their getters, and `setRotationLimits` use signed
`NomadInteger` values. The sink rejects nonpositive byte limits, negative archive
counts, and limits exceeding the platform's native size capacity. Invalid
settings are rejected before creating files, even for inactive sinks.
Invalid limit updates leave both previous settings unchanged.

- `None` disables ongoing rotation, allowing unlimited growth after startup.
  To append across runs without rotating or pruning archives, also set
  `rotateAtStartup = false`.
- Startup rotation does not repeat when disabled/re-enabled, but creating a new
  sink starts a new lifetime. Empty files are not archived.
- `Size` rotates before a complete new record would
  exceed the byte limit. A record includes its final newline. One oversized
  record is allowed in an empty file; the next record rotates it.
  With startup rotation disabled, existing content is appended and counted
  toward the limit.
- Output preserves message bytes and uses a single LF newline on every platform.
  The full formatted log entry, including timestamp and source location, counts
  toward the limit.
- Rotation prunes matching numbered archives outside retention. Other files are
  left alone. Lowering retention takes effect at the next rotation.

An active factory creates directories and opens the file immediately, including
startup rotation unless disabled. An inactive factory validates options without
filesystem writes. Such a sink starts on its first active delivery callback
(including an empty logger flush), or an explicit `begin()` call.

`getPath()` returns the resolved active file path. `getRotationMode()`, `getRotateAtStartup()`,
`getMaxSizeBytes()`, and `getMaxFiles()` inspect rotation settings;
`setRotationMode()`, `setRotateAtStartup()`, and
`setRotationLimits(bytes, archives)` adjust them.
`LogLevel`, `LogDeliveryMode`, and `FileRotationMode` have `NomadInteger`
underlying types. Logger and sink APIs reject unsupported enum values without
narrowing them first. C++ consumers must be rebuilt after this representation change.
The startup flag can be changed before a staged sink starts; changing it after
startup does not trigger or undo rotation. Other changes affect subsequent writes.
Coordinate
these operations with delivery; unlike base activation/level changes, they are
not independently synchronized. To change the directory or base name, create
a replacement sink.

Use only one sink/process per output path; external writers and external
rotation are not supported. Open, write, flush, close, rename, and retention
failures propagate to the caller. Rotation is not a filesystem transaction:
an error can leave archives partially shifted. Do not retry an entire failed
delivery batch, because some records may already have been written.

### Creation inputs and metadata

API structures use these naming conventions:

- `*Definition` contains inputs for creating or registering an item. It has no
  newly assigned ID, but may reference existing items through IDs. An owning
  type is not repeated when the operation supplies it separately.
- `*Info` describes an already registered item. An assigned primary ID comes
  first when the item has one. Metadata is borrowed through const access or
  returned as a detached snapshot; modifying a snapshot does not change the
  registered item.
- `*Declaration` is reserved for source-language declarations in the compiler.

Include `<nomad/script/Runtime.hpp>` for `ParameterDefinition`,
`NativeMethodDefinition`, `NativeConstructorDefinition`,
`NativePropertyDefinition`, `NativeIndexerDefinition`, and
`NativeInterableDefinition`. These aggregate inputs own their names,
documentation, parameter lists, and callback objects. Referenced type IDs are
runtime-local; captured callback state follows the host's C++ capture lifetime.

`ParameterDefinition` begins with its referenced `TypeId`:

```cpp
const ParameterDefinition distance{
    runtime.getTypeId("float"),
    "distance",
    "Distance to move."
};
```

These input structures do not register anything when constructed.
`addProperty(TypeId, NativePropertyDefinition)`,
`addMethod(TypeId, NativeMethodDefinition)`,
`addConstructor(TypeId, NativeConstructorDefinition)`, and
`addIndexer(TypeId, NativeIndexerDefinition)` remain Runtime compatibility APIs
for native member registration. Prefer the corresponding explicit TypeManager
registration APIs, including `registerNativeClassIndexer` and
`registerNativeClassIterable`. The latter registers a native factory method
and its iterable association. Include `<nomad/script/NativeMember.hpp>` for
the native member creation definitions.
Their metadata structures are now named `NativePropertyInfo`,
`NativeIndexerInfo`, and `IterableInfo`. Corresponding lookup operations are
`getNativePropertyInfo`, `getNativeIndexerInfo`, and `getIterableInfo`;
`NativeProperty::getInfo()` returns a property snapshot.

Fallible metadata snapshot lookups return `std::optional<Info>`. An empty
optional means the item is absent, invalid, or of the wrong kind. No output
parameter is retained or reused from an earlier lookup. A getter on an
already-valid descriptor, such as `NativeProperty::getInfo()`, returns its
snapshot directly.

### Type lookup

Include `<nomad/script/Type.hpp>` for the base `Type`, storage categories, and
lifecycle and bytecode-emission APIs. Concrete built-in descriptors such as `IntegerType`,
`StringType` and the hidden source-location types are declared in
`<nomad/script/BaseTypes.hpp>`. Include `<nomad/script/EnumType.hpp>` for
`EnumType` and `EnumValueDefinition`. The category bases have dedicated headers:
`<nomad/script/ValueType.hpp>`, `<nomad/script/CompositeType.hpp>`, and
`<nomad/script/ClassType.hpp>`. Include the descriptor's header when deriving
from it or accessing its category-specific APIs.

Use `Type::isClass()`, `isStruct()`, and `isComposite()` to query type categories.
Classes and structs are composite types; an empty struct is still a struct and
not `void`. Use `asClassType()`, `asStructType()`, or `asCompositeType()` when you
need the corresponding descriptor instead of a boolean result.

`Runtime::getTypeManager()` returns the runtime-owned `TypeManager` by reference;
the const runtime overload returns a const reference. Include
`<nomad/script/TypeManager.hpp>` to use its APIs. The manager and its descriptors
remain valid for the lifetime of the runtime; returned descriptor pointers are
borrowed, remain stable as types are registered, and must not be deleted.

`TypeManager::getTypeId(name)` returns a `TypeId`, or `TypeId::INVALID`
when the type is not registered. Pass it to `getType(id)` to retrieve an optional
`const Type*`; invalid or unregistered IDs return an empty optional. Type IDs
belong to the runtime that issued them and remain stable as more types are
registered. They do not implicitly convert to integers or other ID categories.
Event parameter definitions use `TypeId` for their `typeId` field.

`getTypeByName(name)` returns a borrowed descriptor or `nullptr`.
`getTypes(types)` appends registered descriptors to the supplied vector without
clearing its existing entries. `getTypeCount()` counts all reserved IDs,
including IDs that do not yet have a descriptor. `ownsType(type)` checks both
runtime ownership and catalog identity.

The existing Runtime lookup and finalization methods remain available with
the same behavior. Declare stored fields with
`TypeManager::registerScriptClassProperty` or `registerScriptStructProperty`.
Register native class accessors with `TypeManager::registerNativeClassProperty`.
Register class methods and constructors with the explicit
native/script class APIs on TypeManager or Runtime. Both also expose
`registerNativeStructMethod`, `registerScriptStructMethod`,
`registerScriptClassProperty`, and `registerScriptStructProperty`.
Native property accessors remain class-only. Existing generic Runtime registration
entry points remain compatibility delegates.
Register struct constructors with
`TypeManager::registerNativeStructConstructor(ownerId, definition)` or
`registerScriptStructConstructor(ownerId, parameters, path, source)`.
`Runtime::addConstructor` and `registerScriptConstructor` also accept struct
owners. Struct constructors construct their declaring type; unlike class native
factories, they cannot specify a different constructed type. Invalid owners,
invalid parameters, empty native callbacks, and duplicate signatures are
rejected without publishing a callable. Native/script duplicates share the
same owner constructor catalog.

Struct constructor callables return `void`; `Constructor::getConstructedType()`
identifies the struct value produced by construction. Their call layout includes
one borrowed destination receiver pointer after the explicit argument slots for
a nonempty struct, and no receiver pointer for an empty struct. There is no
separate return pointer. Finalize the composite layout before querying this ABI.
Class factories still return their declared constructed class type.

Registered native and script struct constructors support `new` expressions into
local destinations, including staged assignments, argument copies, returns,
and class property initializers. Compiled source can declare closed structs with
field initializers, constructor-inferred fields, overloaded `init` bodies, and
methods. Structs may be nested inline in structs and classes. Copies, ordinary
parameters, and returns have independent struct storage; contained class fields
retain reference semantics. Methods mutate their stored receiver. Empty structs
occupy no slots but still run constructor and method side effects. The interpreter
supports owned struct values, construction, and free-function arguments/results;
it can also read stored struct properties and assign fields rooted in named
struct variables or `this`. Interpreted method calls borrow named, frame-backed,
nested, object-owned, and temporary struct receivers. Host snapshot-based
struct-method calls return explicit updated receiver/result values. General
interpreter field assignments through class-owned or temporary struct roots
remain unsupported.

Register class and struct types with
`TypeManager::registerClass` and `registerStruct`; existing Runtime entry points
remain compatible. Enum, opaque-ID, and callback creation also belong to
TypeManager, with compatible Runtime entry points. Callable
IDs still belong to Runtime. `TypeManager::reserveTypeId()` and `catalogType()`
are low-level catalog operations: they do not register namespace members or
check namespace collisions. `catalogType()` requires a non-null descriptor
owned by the same runtime and an unused reserved ID, otherwise it throws
`NomadException`. An unused reserved ID is absent from lookup and enumeration.

`TypeManager::registerType(descriptor)` publishes a completed descriptor in its
namespace and transfers ownership to the manager. It requires a non-null
descriptor owned by the same runtime with an unused reserved ID; invalid
ownership, reservations, or hub collisions throw `NomadException`.
Callback descriptors are not published as hub type members; event descriptors
retain their event namespace entry. Prefer the class/struct creation APIs for
ordinary registration.

Runtime maintains hub indexes through `canRegisterTypeName(name)` and
`publishTypeNamespace(type)`. The former checks hub-name and member collisions,
not identifier syntax or catalog duplication. The latter requires a same-runtime
descriptor with an unused reserved ID and publishes a borrowed namespace entry;
it does not take ownership. It is a low-level coordination operation: the
descriptor must immediately be cataloged and remain alive for the runtime's
lifetime. Ordinary hosts should use manager registration instead.

`TypeManager::finalizeCompositeFieldLayout(id)` finalizes class or struct
stored-field layouts, including nested inline structs. Invalid IDs and
non-composite types throw `NomadException`. Mutable category accessors return
only catalog-owned descriptors, or `nullptr` when the requested owner does
not match; stored-field additions remain subject to layout freezing.

`Type::getSize()` returns the number of `RuntimeValue` slots occupied by a value,
not its size in bytes. `void` occupies zero slots; current value types and class
references occupy one slot. A class reference occupies one slot regardless of
the number of fields in its instance. Types deriving directly from `Type` must
implement this pure virtual method; `ValueType` and `ClassType` provide the
one-slot implementation. Querying the size of an unresolved compiler-deferred
type throws `NomadException`, as does querying its storage category.

Strong IDs are invalid when default-constructed. Use `isValid()` and
`isInvalid()` to check their numeric validity; these checks do not verify that
the ID belongs to a particular runtime or is registered. `IdHash` supports
strong IDs as keys in standard unordered containers.
Streaming an ID prints its type and numeric value, for example `CallableId{42}`
or `CallableId{-1}`.

#### Type-owned bytecode emission

`OpCodeEmitter` and `ExpressionTarget` are declared in
`<nomad/script/OpCode.hpp>`. `ExpressionTarget` selects the result register,
intermediate register, or stack. `Compiler` implements `OpCodeEmitter` directly.
Custom emitters implement `addOpCode(InstructionFn)`, `addId(NomadId)`, and
`addIndex(NomadIndex)`; each returns the position of the appended instruction word.
`addLoadTarget` selects one of three supplied instruction functions, writes it
and its ID operand, and returns the operand position. Invalid targets raise
`NomadBug`.

Type descriptors provide virtual `emitInit*`, `emitFree*`, `emitLoad*`,
`emitStore*`, and register copy/move/push/pop operations. These accept an
`OpCodeEmitter`, not a compiler, and write directly to its output without an
intermediate instruction collection. Operand IDs and offsets retain their
existing VM meanings.

Default value operations use bitwise register traffic and emit no initialization
or cleanup. Class types emit reference-aware loads, copies, transfers, and
cleanup; string types specialize initialization for empty strings. Runtime
`initValue`, `copyValue`, and `freeValue` remain separate from bytecode emission.
`needsFree()` is a virtual ownership query, independent of opcode selection:
false by default and true for class references.

The former `TargetOpCodes`, `TypeOpCodes`, and `getTypeOpCodes()` APIs have been
removed. Custom descriptors must override emission operations instead of setting
opcode-table fields. Types deriving directly from `Type` also implement
`emitFreeParameter`; `ValueType` and `ClassType` supply their category's behavior.


### Class registration

`TypeManager::registerClass(name, mode, documentation = {})` registers a class
type and returns its `TypeId`. The mode is required:
`ClassMode::Opened` permits external script member declarations, while
`ClassMode::Closed` prevents them. Neither mode makes existing properties
immutable or prevents the type's own declarations. Native and script objects
use the same `ClassType` descriptor; `isOpen()` reports its mode.

The runtime owns the descriptor and its name and documentation. Use
`getType(id)` to retrieve a borrowed descriptor. Empty names, invalid modes,
already-registered type names, and conflicting hub declarations are logged and
return `TypeId::INVALID`. Duplicate creation is rejected even when the mode
and documentation are identical, without replacing the existing type.

Every type descriptor receives a non-null owning `Runtime*` and a `TypeId`
as its first two constructor arguments. `Type::getRuntime()` returns that runtime,
which must outlive the descriptor. `Type::getId()` returns the immutable,
runtime-local ID. Use it when passing an existing descriptor to an ID-based API;
name lookup does not distinguish callback signatures.

`TypeManager::registerClass<Descriptor>(name, mode, arguments...)` creates specialized
`ClassType` subclasses. It calls
`Descriptor(Runtime*, TypeId, name, mode, arguments...)`, forwarding additional arguments.
Invalid registration requests are rejected before calling the descriptor constructor.
The constructor initializes the descriptor without registering it; the runtime
registers the completed descriptor and returns its `TypeId`. The descriptor's
runtime, ID, name, and mode must match the registration request.

The ID is reserved before construction and is not resolvable through `getType()`
until the descriptor is registered. Constructors may create other types without
changing the reserved ID. Failed creation leaves an unused ID; IDs are never
reassigned, and `getTypes()` lists only registered descriptors. Construct
standalone, unregistered descriptors with `TypeId::INVALID`. Registration is
managed by the runtime-owned manager's creation APIs. Runtime's class creation
overloads remain compatibility wrappers.

Type creation does not register constructors or finalize a script field layout.

### Enum creation

`TypeManager::createEnum(name, values, documentation = {})` creates an enum and
returns its `TypeId`. Each `EnumValueDefinition` supplies a member name and an
explicit integer value. The runtime owns the resulting type and copies or moves
the supplied names, values, and documentation.

Access it through `runtime.getTypeManager()`; `Runtime::createEnum` remains
compatible. Enum constants and equality operators are registered in Runtime
with their existing IDs and calling conventions.
`Runtime::registerEnumOperators(type)` registers enum equality/inequality for
an already registered same-runtime enum. Invalid ownership throws
`NomadException`; duplicate operators are rejected. Ordinary hosts should use
manager enum creation rather than register operators separately.

`TypeManager::registerOpaqueIdType(name, documentation)` creates a borrowed
opaque-ID descriptor with the existing name and namespace rules.
`getCallbackType(parameterTypes, returnType)` interns callback descriptors by
their exact parameter and return-type pointers. Matching signatures reuse a
descriptor; different signatures have distinct IDs even when their type names
match. `getPredicateType()` returns the parameterless boolean callback type.
Runtime's corresponding methods remain compatible.

Creation also registers typed constants named `EnumName.MemberName` and
same-enum equality and inequality operators. Existing script enum syntax is
unchanged. Use `getType(id)` to retrieve the descriptor and `asEnum()` for enum
member lookups; `getDocumentation()` returns the supplied type documentation.

Invalid definitions are logged and return `TypeId::INVALID`: empty names or
value lists, unnamed members, duplicate member names or integer values,
already-registered type names, and conflicts with existing constants or hub
declarations. Duplicate type creation is rejected even when the definition is
identical. Rejected definitions do not replace existing registrations.

### Constant IDs

Include `<nomad/script/Runtime.hpp>` for `ConstantId`.
`Runtime::registerConstant(name, value, type)` returns a `ConstantId`, or
`ConstantId::INVALID` when registration fails. Duplicate names are logged and
rejected without replacing the original value. `getConstantId(name)` returns
`ConstantId::INVALID` when absent. Constant IDs belong to the runtime that issued
them and remain stable as more constants are registered.

Pass a registered ID to `getConstantName`, `getConstantType`, or
`getConstantValue`. The value is borrowed; the runtime retains ownership.
`getFirstConstantId` and `getNextConstantId` enumerate registered constants in
registration order, ending at `ConstantId::INVALID`. Hub constant members also
contain a `ConstantId`. Constant IDs do not implicitly convert to integers or
other ID categories.

### Event IDs

Include `<nomad/script/Event.hpp>` for `EventId`, `EventDefinition`, and
`EventDispatch`. `Runtime::registerEvent(name, parameters)` returns an `EventId`,
or `EventId::INVALID` when registration fails. Duplicate names are logged and
rejected without replacing the original definition. `getEventId(name)` returns
`EventId::INVALID` when absent; `getEventDefinition(id)` returns a definition
snapshot, or an empty optional for an invalid or unregistered ID.

`getFirstEventId` and `getNextEventId` enumerate registered events in registration
order, ending at `EventId::INVALID`. IDs belong to the runtime that issued them
and remain stable as more events are registered. Event definitions, hub event
members, and dispatch requests use `EventId`; scene, layer, and entity IDs are
separate identities. Event IDs do not implicitly convert to integers or other ID
categories.

Event callback and dispatch native APIs receive raw IDs through the existing
VM parameter slots. Construct `EventId{machine->getIdParameter(0)}` before looking
up the definition or forwarding a dispatch request.

### String and format-string IDs

`Runtime::registerString(text)` returns a `StringId`, reusing the existing ID
for identical text. `getStringId(text)` returns `StringId::INVALID` when absent.
Pass a registered ID to `getString(id)` to borrow its string object.
Register static strings before executing functions; the static string table
must not grow while functions execute.

`registerFormatString(text, functionId)` returns a `FormatStringId`, reusing an
entry only when both its text and owning `CallableId` match.
`getFormatStringId(text, functionId)` returns `FormatStringId::INVALID` when
absent. `getFormatString(id)` returns a borrowed format string, or `nullptr`
for an invalid or unregistered ID.

Both ID types are declared in `<nomad/script/Runtime.hpp>`, belong to the runtime
that issued them, and remain stable as registrations are added. They do not
implicitly convert to integers or other ID categories.

### Dynamic-variable IDs

Include `<nomad/script/DynamicVariable.hpp>` for `DynamicVariableId`.
`registerDynamicVariable(name, setter, getter, type, doc)` returns a
`DynamicVariableId`, or `DynamicVariableId::INVALID` when registration fails.
`getDynamicVariableId(name)` returns `DynamicVariableId::INVALID` when absent.
Pass a registered ID to the dynamic-variable metadata, permission, getter, and
setter APIs. `setStringDynamicVariable` releases its temporary string after
invoking the setter; a setter that retains a value must copy it through its type.

`getFirstDynamicVariableId` and `getNextDynamicVariableId` enumerate registrations
in registration order, ending at `DynamicVariableId::INVALID`. IDs belong to the
runtime that issued them and remain stable as registrations are added. Hub
dynamic-variable members also contain a `DynamicVariableId`. These IDs do not
implicitly convert to integers or other ID categories.

### Variable-context IDs

Include `<nomad/script/VariableContext.hpp>` for `VariableContextId` and
`ContextVariableId`. Both types belong to the runtime that issued them, remain
stable as registrations are added, and do not implicitly convert to integers
or other ID categories.

`registerVariableContext(name, prefix, context)` returns a `VariableContextId`.
`getContextId(name)` and `getVariableContextIdByPrefix(variableName)` return
`VariableContextId::INVALID` when no context matches. Pass a registered context
ID to `getContextName` or `getVariableContext`; the latter returns a borrowed
pointer, or `nullptr` for an invalid or unregistered ID.
`getFirstVariableContextId` and `getNextVariableContextId` enumerate contexts
in registration order, ending at `VariableContextId::INVALID`.

`registerContextVariable(contextId, variableName, type)`,
`registerOrUpdateContextVariable(variableName, type)`, and
`getContextVariableId(variableName)` return a `ContextVariableId`, or
`ContextVariableId::INVALID` when the context or variable cannot be resolved.
Repeated resolution of the same context and local variable returns the same
handle. Variables in different contexts have distinct handles even when their
context-local IDs are equal. `registerHubVariable` also returns a
`ContextVariableId`.

Pass a registered context-variable handle to the runtime's context-variable
metadata, read/write tracking, and value APIs. Values read through
`getContextVariableValue` are borrowed; the context retains ownership.
`setStringContextVariableValue` releases its temporary string after assigning
it through the context.

The `VariableContext` interface uses context-local `VariableId` values.
Do not construct a local ID from a runtime-global handle's numeric value; use
the runtime's context-variable APIs to resolve it. Hub variable members contain
context-local `VariableId` values.

### Local variable IDs

Include `<nomad/script/Variable.hpp>` for `VariableId`. Variable maps, variable
lists, context-local APIs, script-callable locals, interpreter variable-by-ID
lookups, and VM local-variable access use this type. Scene and entity variable
access also takes `VariableId`.

`VariableId` belongs to its owning map or function's local layout, not to a
runtime-wide registry. Different owners can issue IDs with equal numeric
values. IDs remain stable as variables are added to their owner. Missing name
lookups and the end of context-local iteration return `VariableId::INVALID`.
The type does not implicitly convert to integers, `ContextVariableId`, or
other ID categories. Numeric validity does not verify ownership.

`VariableMap::getVariableOffset(id)` returns the variable's physical slot offset,
the sum of the preceding variables' `Type::getSize()` values.
`getStorageSize()` returns the total slot count, or zero for an empty map.
Logical IDs and `getVariableCount()` remain independent of physical slot counts.
Queries reflect the current metadata after variable registration or type changes;
they do not allocate storage or change existing VM access.

`ScriptCallableBase::getVariableOffset(id)` and `getVariableStorageSize()`
expose the same physical layout queries for script function, method, and
constructor locals. They include all registered locals, including compiler-owned
temporaries. Local bytecode uses compiler-computed physical offsets for load, store, and
cleanup, and frames reserve/pop physical slot totals. Logical IDs remain stable
when sizes change. VM host-facing local APIs and frame-backed interpreter access
resolve those IDs through callable metadata; instruction execution does not.
Local instruction operands use full-width `Index` values, and instruction dumps
show numeric offsets. Parameter load/store/cleanup bytecode also uses full-width
physical offsets computed with `getParameterOffset(parameterId)`. Scalar accesses
address one slot below the argument anchor at that offset. Parameter IDs remain
logical metadata; native callback parameter APIs and argument shapes are unchanged.

Struct-local cleanup releases owned stored fields, including fields in nested
structs. Empty structs and fields that need no cleanup emit no cleanup
instructions. Local storage initialization supplies empty strings for stored
string fields, including fields in nested structs; other fields retain their
cleared defaults. It does not run script field initializers or constructors.
`Type::emitInitFunctionVariable(emitter, offset)` targets reserved, cleared local
storage, not an existing owned value. Aggregate local loads and stores are not
yet supported by bytecode.

`Type::emitCopyFunctionVariable(emitter, sourceOffset, destinationOffset)`
copies between same-type local ranges, replacing owned destination fields.
Ranges must be disjoint or identical; identical ranges and empty types emit
nothing. Nested structs copy their scalar fields independently and retain
their reference fields without copying the referenced objects.

`Type::emitAssignFunctionVariable(emitter, sourceOffset, destinationOffset,
scratchOffset)` supports overlapping source and destination ranges by staging
the complete source in a same-type scratch local before replacement. Scratch
must be disjoint from both ranges and contain cleared or valid owned storage.
Its owned fields are released afterward; it can be reused and cleaned up at
function return. Identical source/destination ranges and empty types emit
nothing and leave scratch unchanged. Both emission APIs require valid reserved local ranges and
resolved sizes. They clobber the scalar result register using the existing
local load/store ownership conventions; callers must not retain an owned
result there across these operations or treat the register afterward as an
additional owned copy. They do not allocate scratch locals or
provide aggregate expression, parameter, or object-field lowering.

`Expression::compileIntoLocal(compiler, callable, destinationId)` compiles a
resolved expression into a same-type local. Scalar expressions replace the
local using their existing result-register path. Struct-local identifiers copy
into disjoint local storage; identical source/destination locals are a no-op.
Struct parameter identifiers also copy into local storage, preserving the
parameter's value. Registered aggregate-returning calls, stored property reads,
and `new` expressions also support local destinations. The destination ID is
resolved to a physical offset during emission, and its storage must be reserved
before execution. This API does not allocate temporary locals or stage assignment
targets. It follows the same result-register ownership preconditions as local
copy emission.

Compiled struct-local assignments allocate a same-type staging local during
resolution. The RHS completes there before destination replacement; owned
staging fields are released after each assignment. Staging locals participate
in physical frame reservation and return cleanup, but their internal names
are hidden by `getVisibleVariableName`. Repeated resolution reuses the same
staging local. Scalar assignments retain their existing path. Struct parameter
targets use the same staging contract and replace the parameter copy, not the
caller's original value. Object-field, call-result, and interpreter assignment
lowering remain unsupported.

`Type::emitCopyParameterToFunctionVariable(emitter, parameterOffset,
destinationOffset)` replaces a same-type local from a disjoint parameter block.
Nested fields use physical parameter offsets in reverse anchor-relative order
while destination fields retain increasing local offsets. It follows the local
copy result-register ownership contract and does not allocate storage.
`StructType::emitFreeParameter` releases owned nested fields and clears their
slots. It is explicit: function return does not release caller-owned arguments.
Empty types emit nothing; nonempty ranges require valid storage and finalized
layouts. Struct parameter sources can be assigned to locals using existing
assignment staging.
`Type::emitCopyFunctionVariableToParameter(emitter, sourceOffset, parameterOffset)`
copies disjoint same-type local storage into a parameter block, replacing its
owned fields. Nested parameter field offsets use the same reversed order as
parameter-to-local copying. It follows the same result-register ownership
preconditions and does not allocate storage. Compiled struct parameter assignment
stages the entire RHS first, including for self-assignment and parameter sources.
Argument ownership and cleanup responsibility remain with the caller.

Struct local and parameter identifier arguments push field copies directly
onto the argument stack without allocating `$argument` staging locals.
Other struct expression forms retain hidden `$argument`-prefixed same-type
staging locals allocated during resolution. Each such RHS completes in staging, then
`Type::emitPushFunctionVariable` pushes an owned copy in increasing field-slot
order and staging ownership is released. The caller releases nested argument
fields after the call using `StructType::emitFreeStackValue`, then pops physical
argument slots. Arguments still evaluate right-to-left. Empty structs consume
zero argument slots; scalar and specialized event argument shapes are unchanged.
Local and parameter struct sources and struct-returning calls are supported.
Legacy scalar host argument containers do not accept structs; interpreter calls
use complete typed storage.
Struct local/parameter load emission supports `ExpressionTarget::Stack`, returning
the first emitted operand position, or `NOMAD_INVALID_INDEX` for empty structs.
Result and intermediate register loads remain unsupported.

An offset query requires resolved sizes for the preceding variables; a total-size
query requires resolved sizes for every variable. Missing types, deferred-size
queries, and slot-count overflow throw `NomadException`. An invalid or out-of-range
ID passed to `getVariableOffset()` also throws `NomadException`; an in-range ID
does not prove ownership by this map.

Parameter positions, capture-list positions, and stack offsets remain numeric.
Compiler identifier metadata separates `variableId` from `parameterId`.
Capture metadata retains `CaptureSource`; read `variableId` only for
`CaptureSource::Variable`, and `parameterId` only for `CaptureSource::Parameter`.
Script-callable `createCapture` overloads take either a `VariableId` for a
variable source or a numeric `NomadId` for a parameter source, followed by the
name and type. `flagVariableAsCaptured` and `flagParameterAsCaptured` provide
the corresponding source-specific registration APIs.

### Borrowed struct call storage

The VM supports internal borrowed storage pointers on its existing value stack.
`RuntimeValue::setStorageAddress()` and `getStorageAddress()` store/read a
`RuntimeValue*` without ownership changes. An address slot must never be treated
as an object value or passed to object cleanup.

For a low-level call fixture, push a required destination pointer, then a required
receiver pointer, then explicit arguments in their existing right-to-left order.
If explicit arguments occupy `N` slots, the receiver pointer is at parameter
offset `N`; the destination pointer is at `N + 1` with a receiver, or `N` without
one. Empty structs require no pointers. Hidden pointers are excluded from logical
parameter metadata and do not change explicit parameter offsets.

`VirtualMachine::getLocalAddress(offset)` addresses the first local slot at a
physical offset. `getParameterAddress(distance)` uses the distance from the active
argument anchor to the first slot of a block, not its logical parameter ID.
For a block at parameter offset `offset` with width `size`, that distance is
`offset + size - 1`, folded by the compiler for nonempty blocks.
`pushStorageAddress(address)` pushes one borrowed pointer slot.
`getStorageParameter(pointerOffset, fieldOffset)` accesses a field through the
pointer parameter; const VM access exposes a read-only field reference.

These are trusted internal APIs. Callers must supply valid nonempty ranges,
storage-pointer slots, and physical offsets; access does not perform span,
presence, width, or type checks. Storage and enclosing owners must remain alive
and stable throughout the operation. No address is required for empty structs.

Address and indirect-access opcodes use full-width `Index` operands. Scalar
loads/pushes copy slots; reference loads/pushes retain objects. Indirect stores
follow existing result-to-storage ownership transfer: release any previous owned
field first, store the result, and do not treat the result register afterward as
an additional owned copy. Explicit indirect cleanup releases and clears fields.
These operations do not allocate destination storage or select initialization
versus replacement behavior.

Existing script/native calls and returns use their ordinary parameter anchors.
Nested calls and synchronous host reentry restore those anchors; no additional
aggregate-state register or per-call allocation is required. Callers clean owned
argument fields and pop hidden pointers without object cleanup. The earlier
`StructCallState`, binding accessors, and separate snapshot vector are removed.

`Callable::getCallStorageLayout()` and the script-callable view expose
compiler-side `CallStorageLayout` metadata: `argumentSlotCount`, optional
`receiverOffset` and `returnOffset`, and `stackSlotCount` for arguments plus
required hidden pointers. Existing logical parameter counts and physical
parameter offsets exclude these descriptors. Nonempty struct method receivers
use one pointer; nonempty struct results use one pointer. Empty structs and
scalar/void results need no return pointer. Ordinary object receivers and saved
call-frame slots are not included.

The overload `getCallStorageLayout(argumentSlotCount)` accepts a compiler-resolved
physical call-site width, including specialized event-dispatch arguments.
Without it, the query uses ordinary signature layout and retains its
call-site-dependent-width errors. Signature queries require resolved parameter
sizes; both overloads require resolved return/receiver sizes and reject total
slot overflow with `NomadException`. These queries do not change VM call paths
or save metadata values on the stack.

`Compiler::addFunctionCallIntoLocal(targetId, caller, arguments, destinationId)`
emits a resolved script/native free-function call into a same-type reserved struct
local. It pushes a destination address only for a nonempty result, evaluates
arguments in their existing right-to-left order, emits the ordinary call,
cleans argument ownership, and pops the unowned destination pointer. Empty
results emit no pointer or pointer cleanup. The destination must contain cleared
or valid owned fields for the callee's destination emission; use a separate
staging local before replacing an existing live target when required.
This API does not resolve arguments, initialize storage, run constructors, or
choose copy-elision eligibility.

`Compiler::addFunctionCallIntoReturn(targetId, caller, arguments)` calls a
resolved script/native free function using the enclosing callable's struct
return destination. Both return types must match, arguments must contain only
explicit free-function arguments, and destination fields must be cleared or
valid owned values. Empty results require no destination pointer. The API does
not reserve local storage or stage an existing assignment target.

`Compiler::addStructCreationIntoReturn(constructorId, caller, arguments)`
constructs a struct in the enclosing callable's return destination. The
constructed type must match the caller's result, and arguments must contain
only explicit constructor arguments. Destination fields must be cleared or
valid owned values and disjoint from borrowed sources; their previous ownership
is released before construction. Empty structs omit the destination pointer
but still evaluate arguments and execute initialization and constructor bodies.
Use separate staging storage when replacing an existing live assignment target.

Custom struct expressions can opt into this return path by overriding
`Expression::supportsReturnDestination()` and `onCompileIntoReturn`.
`compileIntoReturn(compiler, caller)` requires a resolved expression with the
same struct type as the enclosing callable's result. Expressions default to
the existing local-and-copy return path unless they opt in.

Compiled free-function expressions returning a struct support local destinations,
including staged assignments and struct arguments. Script `return` statements
support struct locals, parameters, and nested function calls. Returned fields are
copied into the caller's destination before local cleanup, except that
`return makePoint ...` forwards a struct-returning free-function call directly
to that destination, and `return new Point ...` constructs there directly,
without an intermediate return local or copy. `return point.copy ...` also
forwards a struct result from a method on a struct receiver. This also
works inside struct methods, without confusing the result destination with
the borrowed receiver. Local/parameter/property returns retain their copy paths.
Struct-valued method calls on class receivers are not supported by this path.
Existing assignment
targets are replaced only after their source expression finishes. Discarded
struct results are released, and empty results still execute their side effects.
Named-return copy elision is not yet supported.

`Compiler::addStructMethodCall(methodId, caller, arguments, receiver,
destinationId)` emits resolved script/native method calls borrowing a struct
local, ordinary parameter, or enclosing struct-method receiver. `receiver` is
an `IdentifierDefinition` with the corresponding storage category and ID.
Arguments contain only explicit arguments; do not set their object receiver
type. Struct results require a same-type reserved destination local, even when
empty. Scalar/reference/void results require no destination; scalar/reference
results use the ordinary result register.

Nonempty result and receiver pointers are pushed in that order before explicit
arguments. Empty storage contributes no pointer. Parameter addresses use
precomputed anchor distances; nested methods forward the enclosing receiver
pointer. Explicit arguments remain owned copies and retain their existing
evaluation order and cleanup. The borrowed receiver is not copied, retained, or
freed by the call emitter. Destination fields must be cleared or valid owned
values, with storage disjoint from the receiver and arguments.

The callee accesses its struct receiver through the hidden storage parameter,
not `VirtualMachine::getThis()`. Existing call frames preserve the enclosing
object receiver without an additional `push_this`/`pop_this` pair. This emission
API does not register methods, resolve source expressions, or compile method
bodies.

`Compiler::addStructMethodCallIntoReturn(methodId, caller, arguments, receiver)`
forwards a native/script method's struct result into the enclosing callable's
return destination. The method must have a struct receiver and the same result
type as the caller. `receiver` is a resolved `StorageLocation`; explicit arguments
must not include an object receiver. Destination fields must be cleared or valid
owned values, disjoint from borrowed receiver and argument storage. Enclosing
class owners are retained across argument evaluation and the call, then released.
Empty receivers and results omit only their respective hidden pointers. This API
does not allocate receiver temporaries or stage assignment targets.

Compiled member expressions can call registered struct methods on locals,
ordinary parameters, a struct method's `this`, and nested struct properties
rooted in that storage. They can also borrow stored struct properties from
class objects, including nested structs and temporary class-object results.
The object expression is evaluated once before explicit arguments, and its
owned reference remains on the stack until argument and descriptor cleanup
complete. Replacing the original variable during argument evaluation does not
change the object receiving the call. Calls mutate the borrowed
receiver, while explicit struct arguments remain independent copies. Struct
results support assignment, arguments, returns, and discarded call statements.
A script method can copy or return `this`, or pass it as an ordinary struct
argument; those uses copy its fields rather than expose the borrowed address.
Empty receivers and results still execute their calls.

Compiled struct method calls also accept temporary receivers, such as
`(new Point 3).move 2` or `(makePoint).read`. The receiver is evaluated once
before explicit arguments, and its owned fields are released when the call
finishes. Returned strings, class references, and struct copies remain valid
after that release. Chained method returns and empty temporary receivers follow
the same rules. Interpreter struct construction and member calls remain separate.

Stored struct properties rooted in locals, ordinary struct parameters,
borrowed struct `this`, class objects, or temporary struct values can be read
and assigned in compiled scripts.
Nested scalar/reference properties load and store directly. Struct property
values can be copied into locals, passed as ordinary arguments, or returned.
Aggregate property assignments evaluate the complete right-hand side in reserved
local storage before replacing the property, including self-assignment and
method calls that mutate the target. Reference properties retain ordinary
reference semantics. Empty property calls and assignment expressions still
execute their side effects.

Temporary-rooted access, such as `(makePoint).x`, `(new Outer).point.x`, or
`(makeOuter).point.move 2`, evaluates the complete root once before the
assignment's right-hand side or the method's explicit arguments. The complete
root remains alive until the read, assignment, or call finishes, then its owned
fields are released. Extracted strings, class references, and struct copies
remain independently owned. Assigning to or calling a mutating method on a
temporary field changes only that temporary, except for shared class references
stored in its fields. Empty fields still evaluate and release their root.
The interpreter can read stored struct properties through these roots and owns
the extracted value. It can invoke methods on stored and temporary struct
receivers. Interpreter field assignments through temporary roots and native
accessor-backed struct properties remain unsupported.

Object-owned reads evaluate the owner once and keep it alive until copied values
are independently owned. Writes retain the original owner and capture the
destination before evaluating the RHS. Replacing the owner's former variable
during the RHS therefore does not redirect the assignment. Aggregate writes
stage the complete value before replacement, including self-assignment and
RHS method calls that mutate the destination. Reference properties keep their
ordinary reference semantics.

Class properties can have struct-valued explicit initializers, and constructor
assignments can infer struct-valued properties. Their default initialization
uses existing type defaults recursively; copying or initializing storage does
not run a struct constructor. Existing initializer restrictions on user-written
`this` expressions remain unchanged.

Struct closure captures remain unsupported and produce compilation errors. This does not
prevent accessing ordinary class properties through a reference stored inside
a supported struct.

`Expression::getStorageLocation()` returns an optional compiler-only
`StorageLocation` for an addressable local, parameter, borrowed receiver, or
stored struct property. Its logical root remains stable until physical offsets
are computed during emission. Types and nested property layouts must be
finalized before deriving properties or emitting storage operations.
`getProperty(property)` derives same-owner stored struct property storage.
`emitLoad` loads scalar/reference registers or pushes owned aggregate copies;
`emitStoreResult` replaces a scalar/reference leaf and transfers ownership of
the result register. `emitCopyToLocal` and `emitCopyFromLocal` require same-type
locals and disjoint or identical ranges; overlapping replacements require prior
staging. `emitAddress` pushes a non-owning pointer for nonempty storage and
nothing for empty storage. `Compiler::addStructMethodCall` also accepts this
storage description for nested receivers. These operations do not allocate
runtime call state or perform runtime alias checks.
`emitAssignment(compiler, caller, expression, stagingVariable)` performs the
complete assignment lowering. The expression must be resolved and same-type;
struct assignments require a same-type reserved staging local disjoint from
the destination and source. Scalar/reference assignments need no staging local.

`StorageLocation(ownerExpression, property)` describes a stored struct property
of a resolved class expression. The expression must have the property's owner
type and remain alive through emission; native properties are not supported.
`getProperty` can further select nested stored struct properties.
`StorageLocation(property)` instead selects the enclosing class receiver; the
caller must have the matching resolved receiver type.
Both forms support receiver addresses, value loads, local copies, and assignments.
`emitRetainOwner` evaluates the class expression into one owned stack slot;
`emitFreeOwner` releases and pops that slot when it is back at the stack top.
For object-owned locations, `emitAddress` takes the owner's current stack
distance, including any result descriptor already pushed. The struct-method
call emitter sequences these operations automatically. Ordinary local,
parameter, and borrowed receiver roots emit no owner retention or cleanup.
Empty receivers still evaluate and retain
their class owner, but emit no receiver address.
Object-owned `emitLoad` and `emitCopyToLocal` evaluate and release their owner
automatically. Low-level `emitStoreResult` and `emitCopyFromLocal` instead
require a captured destination address in intermediate and a retained owner;
use `emitAssignment` to prepare and clean up that scope.

`StorageLocation(valueExpression, variableId)` describes a resolved temporary
struct in a same-type reserved local. The expression must remain alive through
emission, and the local must be disjoint from destinations and argument storage.
`getProperty` preserves this root's lifetime across nested field selection.
`emitRetainOwner(compiler, caller)` evaluates the root into the local;
`emitFreeOwner(compiler, caller)` releases the complete root's owned fields.
Value loads, local copies, assignments, and struct method calls sequence these
operations automatically, including zero-slot fields. Low-level address and
store/copy-from-local operations require the caller to establish and clean up
this lifetime scope; they do not reevaluate the root.

`op_stored_property_address_push(ownerOffset, firstOffset, lastOffset)` provides
the address primitive for object-owned struct access. `ownerOffset` is
the distance from the current stack top to an already-retained object, measured
before the address is pushed. The other operands are inclusive physical slot
offsets within that object, computed by the compiler. All three are full-width
index operands. The opcode requires a nonempty, ordered range
(`firstOffset <= lastOffset`) and pushes exactly one borrowed storage pointer.
It preserves the owner and both value registers without retaining or releasing
the object.

`VirtualMachine::getStoredPropertyAddress(receiver, firstOffset, lastOffset)`
exposes the same borrowed pointer contract. Both entry points check that the
receiver has script-object storage and that its actual allocation contains the
last slot before forming an address. This rejects missing storage on older
instances after class extension. Ordered bounds and a live owner remain caller
requirements. Empty structs do not request an address; their owner evaluation
and method side effects execute without a pointer.

This primitive itself does not manage owner lifetimes. Compiled method calls
retain the evaluated owner through argument evaluation and the borrowed call,
then release it without clobbering scalar/reference results. The existing
fatal-fault contract still applies; this lowering adds no exception unwinding.

Indirect value operations use the trusted borrowed address in intermediate:
`op_storage_load_r`, `op_storage_push`, `op_object_storage_load_r`,
`op_object_storage_push`, `op_storage_set`, and `op_object_free_storage`.
Each takes one full-width physical slot offset and performs no further type,
capacity, or alias check. Reference loads/pushes retain their copied value;
stores transfer result ownership after any required destination release.
`VirtualMachine::getStorage(offset)` exposes the same unchecked, borrowed slot.
The address must remain valid and its owner alive for the entire operation.

The corresponding type-owned APIs are `emitLoadStorage`, `emitFreeStorage`,
`emitInitStorage`, `emitCopyFunctionVariableToStorage`, and
`emitCopyStorageToFunctionVariable`. Nested structs emit recursive field
operations. Copies require same-type disjoint storage and replace
owned destination fields. They preserve the address and clobber result.
Stack loads preserve result and the address while pushing owned field copies
in increasing physical order; an intermediate-target load overwrites the
borrowed address after reading. Struct result/intermediate loads remain
unsupported. Empty types emit no field access or address dereference.
Initialization supplies existing defaults without running constructors.

The type-owned emission APIs also support borrowed storage:

- `emitCopyFunctionVariableToStorageParameter(emitter, sourceOffset,
  pointerOffset, fieldOffset)` replaces a borrowed destination from a local.
- `emitCopyParameterToStorageParameter(emitter, sourceOffset, pointerOffset,
  fieldOffset)` replaces it from an ordinary parameter block.
- `emitCopyStorageParameterToFunctionVariable(emitter, pointerOffset,
  fieldOffset, destinationOffset)` replaces a local from borrowed storage.
- `emitFreeStorageParameter(emitter, pointerOffset, fieldOffset)` releases and
  clears owned fields without releasing the pointer or enclosing owner.
- `emitLoadStorageParameter(emitter, pointerOffset, fieldOffset, target)` loads a
  scalar/reference field or pushes a struct's field copies in increasing order.
  Struct result/intermediate register loads remain unsupported. The return value
  is the first pointer operand position, or `NOMAD_INVALID_INDEX` for empty types.

All offsets are physical and resolved at emission. Nested parameter source
offsets remain reverse anchor-relative; borrowed fields and locals use increasing
offsets. The caller must provide finalized types, same-type disjoint source and
destination ranges, and cleared or valid owned destination fields. For possible
overlap, stage the source in disjoint local storage first; these APIs do not
allocate scratch, perform alias checks, or provide scratch-backed aggregate
replacement themselves. Copies retain reference fields and clobber the result
register under the existing local-copy ownership contract. Empty types emit
nothing. Initialization supplies existing type defaults separately and does not
run constructors.

Compiled source and the interpreter support struct construction and free-function
arguments/results. Aggregate yield/resume support remains unsupported.
`VirtualMachine::run` remains the scalar script scheduler/suspension entry point;
it rejects struct arguments and returns instead of treating them as single slots.
Use the typed Interpreter invocation APIs for host function and method calls.
The former VM `runScript`, `runNative`, `runNativeFunction`, `runNativeMethod`,
and `runScriptMethod` wrappers have been removed.

Low-level VM execution uses `captureExecutionState()` and
`restoreExecutionState(state)` for instruction, stack, parameter, local,
receiver, running, and yield state. Snapshots borrow receiver/storage references;
they do not retain values or preserve result/intermediate registers.
Restoration does not release values in discarded stack slots.

`enterScriptFrame(address, receiver)` requires a validated compiled entry address
and arguments/hidden pointers already on the stack. It starts script execution,
clears the yield flag, and enters a frame. `executeScript()` executes bytecode
until stopping or yielding. `enterNativeFrame(receiver)` binds the current stack
arguments and receiver without executing a callback or changing running/yield
flags. The optional receiver defaults to an empty value for free functions and
struct methods; struct receiver pointers follow `Callable::getCallStorageLayout()`.

The caller owns argument, receiver, and result storage, invokes native callbacks,
and restores the enclosing execution state before releasing and popping arguments.
The typed Interpreter APIs perform these steps and enforce existing yield rules.
Scalar script calls retain their suspension-capable scheduler path. VM faults
remain fatal; these primitives are not fault-recovery or value-unwinding APIs.

`pushStorage(storage)` copies contiguous raw slots onto the VM stack without
retaining references. The caller supplies ownership for those slots and must
eventually release and pop them. The complete range must fit; an overflow fails
before changing the stack. An empty view pushes no slots.

### Class type property metadata

`ClassType` describes both native and script-defined class types. `isOpen()` reports
whether external script declarations may extend it. Closed types are not immutable.
`Type::asClassType()` returns the same descriptor as a borrowed `const ClassType*`
for class types and their subclasses, including strings, or `nullptr` for other types.

`ClassType` inherits member metadata from the abstract `CompositeType` base
(include `<nomad/script/CompositeType.hpp>`). `Type::asCompositeType()` returns that
descriptor as a borrowed `const CompositeType*` for classes and their subclasses,
or `nullptr` for types without composite metadata. The composite view exposes the type
name, openness, initializer name, properties, constructors, methods, and field
layout queries. It refers to the same member catalog and IDs as the class view.
Existing `ClassType` member APIs remain available through
`<nomad/script/ClassType.hpp>`. Class values retain their one-slot reference and
ownership behavior.

Register the owner type with the runtime before declaring its properties.
`TypeManager::registerScriptClassProperty(ownerTypeId, name, valueType)` declares a stored/script
property and returns a `PropertyId`, or `PropertyId::INVALID` on failure.
The owner ID is resolved within the manager's runtime and must identify a class type.
The value type must also be registered with the same runtime and must be public
and non-void. Repeating a declaration with the same name and type returns its
existing ID. Conflicting declarations are logged and rejected without changing
the type's properties. Host declarations are permitted on both open and closed
types; openness controls whether scripts may add external declarations.

`registerScriptStructProperty(ownerTypeId, name, valueType)` provides the same
stored-field contract for struct owners. Each operation rejects the other
owner category, invalid or unused IDs, and invalid value types with a logged
error. Field names are stored as supplied; identifier syntax is validated by
the compiler, not by these registration operations. These declarations create storage metadata,
not native accessor callbacks. Names already used by native properties or
methods are rejected. `Runtime::registerProperty` remains a compatibility
entry point that delegates to the appropriate manager operation.

`Runtime::getProperty(id)` returns metadata for native or script properties;
`getNativeProperty(id)` returns only native properties. Invalid or unregistered
IDs return `nullptr`. Include `<nomad/script/Property.hpp>` for `PropertyId`
and common metadata, and `<nomad/script/NativeProperty.hpp>` for native metadata.
Use `getId()`, `getClassType()`, `getName()`, `getValueType()`, and `getDoc()`
to inspect a class property. `getOwnerType()` returns its borrowed
`const CompositeType*` owner; `getClassType()` retains the class-specific view.
`hasExplicitInitializer()` reports whether a stored field
has a declaration initializer; assignments inside constructor bodies do not set
this flag. `Runtime::setPropertyHasExplicitInitializer(propertyId, value)` updates
that metadata for a stored property and rejects invalid IDs and native properties.
`ClassType::getProperty(name)`, `getProperty(id)`, and `getProperties()` expose
the owner's properties as borrowed const pointers. IDs and pointers remain
stable as more properties are registered and are valid for the owning runtime's
lifetime. Property IDs are runtime-wide, not per-object field offsets.

`ClassType::getFieldIndex(id)` returns the zero-based owner-local logical index
for a stored/script property. Indices remain stable when properties are appended.
Native properties do not occupy field indices; native, invalid, and other owners'
property IDs return `NOMAD_INVALID_INDEX`. Index order follows successful
registration order, not necessarily source declaration order.

`ClassType::getFieldOffset(id)` returns the stored field's zero-based physical
offset in `RuntimeValue` slots. It sums the current `Type::getSize()` values of
preceding stored fields. `getFieldStorageSize()` sums all stored field sizes.
Native properties contribute no slots, even when their value types are unresolved;
these queries do not invoke property callbacks. Empty and native-only field
layouts have storage size zero, while the class's reference size remains one.

Without explicit finalization, offsets and storage size are calculated on demand.
A zero-width field retains
its logical index and property ID and can share an offset with another field.
Offsets require resolved sizes only for preceding stored fields; total storage
size requires all stored field sizes. Required unresolved-size errors propagate,
and arithmetic overflow raises `NomadException`. Unlike `getFieldIndex()`, the
offset query raises `NomadException` for native, missing, or other owners'
property IDs. Representable offsets, including the maximum `NomadIndex`, are
valid results rather than invalid-ID sentinels. These metadata queries do not
change the current one-slot-per-field object storage or execution APIs.

`Runtime::finalizeCompositeFieldLayout(typeId)` explicitly finalizes the stored-field
layout using an ID from the owning runtime. Invalid IDs and non-composite types
raise `NomadException`. Type descriptors remain available through const pointers;
`isFieldLayoutFinalized()` reports whether finalization succeeded. It recursively
finalizes stored inline-struct dependencies, but does not traverse class-reference
fields or native properties. Direct and indirect inline cycles, required
unresolved sizes, and arithmetic overflow raise `NomadException`. A failed owner
remains unfinalized and can be retried after its metadata is corrected;
successfully finalized dependencies remain finalized.

After finalization, `getFieldStorageSize()` returns the cached total. Empty
layouts finalize successfully with size zero. Field offsets remain available
through the existing queries, and a class's reference size remains one.
`getLayoutSize()` and `getFieldLayouts()` provide `noexcept` runtime views of
the total slot count and each stored field's type, offset, and width. They
require a finalized layout; calling either before successful finalization
terminates the process. Empty stored fields are included with width zero;
native properties are excluded. The views remain valid for the descriptor's
lifetime, including repeated finalization and later native-member registration.
Repeated finalization is a no-op. New stored-field registrations are logged and
rejected after finalization; redeclaring an existing field with the same name and
type retains its ID. Native properties, methods, constructors, initializer flags,
and stored field values may still change without altering the layout.
Finalization is explicit: ordinary compilation does not automatically finalize
existing class layouts.

Compiled code and the interpreter can read and assign stored properties on
host-provided, initialized `ScriptObject` instances, as well as instances created
with `new`. Script construction runs field initialization before the selected
source `init` body. Classes without a source `init` have a parameterless constructor.

### Host-owned struct storage

`StructType` is declared in `<nomad/script/StructType.hpp>` and derives from
`CompositeType`, not `ClassType`. Register a closed descriptor with
`TypeManager::registerStruct(name, documentation = {})`, add stored fields with
`runtime.getTypeManager().registerScriptStructProperty(ownerId, name, valueType)`,
then call `runtime.getTypeManager().finalizeCompositeFieldLayout(ownerId)`.
Invalid or reserved names are logged and registration returns `TypeId::INVALID`.
Use `Type::asStructType()` to inspect its category.

`getSize()` and `needsFree()` require a finalized layout; otherwise they throw
`NomadException`. Nested inline dependencies finalize recursively. Empty structs
occupy zero slots and remain distinct from void. Types match by descriptor
identity, not by name across runtimes.

Supply contiguous, cleared `RuntimeValue` storage for initialization or copying
into fresh storage. Initialization applies field defaults without running script
constructors. Copying preserves inline scalar values and retains references held
by string and class fields. `freeValue` releases stored fields, including nested
structs; native properties are not storage. After a partial initialization or
copy failure, call `freeValue` on the partial destination before discarding it.
Cleanup is `noexcept` and requires finalized metadata, even for an empty struct;
unfinalized struct cleanup terminates rather than throwing.
Direct copying requires disjoint source and destination, except self-copy;
use `assignValue` with disjoint scratch for overlapping replacement.

Zero-slot lifecycle operations accept null pointers and access no slots.
Formatting currently returns the type name without inspecting its fields.

This descriptor supports host-owned slot storage only. Every struct bytecode
emission operation raises `NomadException`, including empty structs. Struct
declarations, compiled/interpreted aggregate execution, native struct accessors,
generic struct collections, and aggregate VM allocation are not supported yet.
Do not pass struct storage to existing single-slot object, constant, argument,
or interpreter APIs.

### Borrowed VM storage

`VirtualMachine::getLocalStorage(offset, count)`, `getParameterStorage(offset, count)`,
and `getEnclosingParameterStorage(offset, count)` return contiguous borrowed slot
views. Each has mutable and const overloads. Offsets and counts are physical,
not logical variable or parameter IDs. Views must not outlive their owning frame;
the VM stack does not relocate during calls.

Local offsets start at the active script frame's variable base. Parameter offsets
count slots from the parameter anchor toward lower addresses; a block of width
`count` starts at `anchor - offset - (count - 1)` and its fields run in increasing
address order. Enclosing-parameter views use the script frame's anchor even during
a native call. Empty requests return empty views without address arithmetic.
These are trusted accessors: nonempty requests require a valid active frame and
offsets/counts belonging to the intended value within reserved live storage.
The compiler supplies valid physical ranges for compiled code; hosts must obey
the same contract. Invalid nonempty ranges violate the API contract and are not
reported as VM faults. Stack allocation, reservation, and frame-entry operations
retain their capacity checks.

These low-level APIs do not initialize, copy, or release values. Direct writes
must obey the type's ownership contracts. Existing scalar accessors, argument
APIs, and bytecode are unchanged; these views do not enable aggregate VM calls.

### Script object storage

`ScriptObject` (include `<nomad/script/ScriptObject.hpp>`) provides fixed
instance storage for stored properties. Construct it with a `const ClassType*`
whose field layout has been resolved. The runtime and its type descriptors must
outlive the object. Native properties do not occupy storage slots.

Direct allocation snapshots each stored field's type, physical offset, and size.
Inline struct layouts must be finalized before allocation; allocation does not
freeze an enclosing open class. Each instance's storage remains fixed if fields
are later added to that class. Native properties have no stored-field index.
`getFieldCount()` counts logical stored fields, not physical slots.
`getField(index)` and `setField(index, value)` use logical stored-field indices
and support only single-slot, non-struct fields. Their physical offsets may
differ from their indices when an earlier field is a struct.

Compiled single-slot scalar/reference stored-property access uses specialized
opcodes with full-width physical offsets. The compiler selects stored versus
native access and the field's ownership behavior; execution does not resolve
property IDs, field indices, declared types, or stored-field widths.
Reference writes copy/retain the result before releasing the previous field;
the result remains owned by the caller. Self-assignment preserves ownership.
Explicit initializers, inferred defaults, constructor writes, and ordinary
property reads/assignments use the same compiler selection path.

The VM still checks that the receiver is a non-null `ScriptObject` and that the
offset fits its allocated storage. These facts are not established by the
compiler for all host/native receivers or instances allocated before an open
class gained fields. `getStorageSize()` reports actual allocated slots;
`getStorageValue(offset)` is a trusted unchecked physical-slot API.
Native accessors and checked host/interpreter property APIs retain their existing
behavior. Aggregate stored-field lowering remains unsupported.

The specialized opcode names use stored-property terminology:
`op_stored_property_load_r`, `op_object_stored_property_load_r`,
`op_stored_property_set`, and `op_object_stored_property_set`.
`VirtualMachine::getStoredProperty(offset)` accesses the physical slot on the
intermediate-register receiver with the receiver/capacity guards described above.

Use `getFieldStorage(index)` for a borrowed contiguous view of a stored field,
including an inline struct. The const overload returns read-only slots; the
mutable overload permits direct mutation. The object must remain alive throughout
the borrow. Empty structs return empty views and still have distinct logical
field indices. Native accessor results are not field-storage views.
`setFieldStorage(index, source, scratch)` copies exactly one field's slots using
its type's assignment operation. Source width must match the field size. Supply
at least `Type::getAssignmentScratchSize()` cleared scratch slots, disjoint from
source and destination, when required. Source and destination may alias or
overlap; assignment stages the copy before releasing the destination. Invalid
indices, scalar access to structs, incorrect source widths, and insufficient
scratch storage throw `NomadException`. Direct view writes must follow the type's
ownership and lifecycle contracts; use the assignment API to replace owned values.

Direct allocation zeroes storage without running
type defaults or field initializer expressions. To create a ready-to-use script
instance from the host, use `VirtualMachine::createScriptObject()` with a compiled
constructor. Every generated constructor calls the class's `$init` before its
source body. `$init` evaluates explicit field initializers in source order, then
initializes the remaining fields with their declared type's default value.
Explicitly initialized fields do not receive an additional default
initialization. Field initializer expressions cannot use or capture `this`;
bare names resolve normally outside the class fields. Object-valued initializer
expressions run separately for each instance.
The type stores its fully qualified initializer name, available through
`ClassType::getInitializerName()` (for example, `game.Point$init`).

Compile the class and complete field discovery before allocating the object.
Initialize script-level hub variables with `initializeScripts()` first when
required. Creation APIs trust the compiler to provide a valid constructor ID,
the correct native/script and object/struct entry point, a fully compiled body,
and the selected signature's stack arguments. Direct host callers must satisfy
the same preconditions. If a field initializer fails, later initializers
and the source constructor body do not run, and no instance is returned.
Destruction releases owned field values, including values stored in a partially
populated object.

`VirtualMachine::getThis()` exposes the active script or native member receiver
as a borrowed pointer. Nested calls restore the enclosing receiver. Free script
functions and native function/constructor callbacks do not inherit a caller's
receiver; `getThis()` raises an error in those calls. Closures and format
expressions retain their explicitly captured `this`.

Native and script calls pass arguments by copy. Reassigning a parameter does not
reassign the caller's variable; object values retain normal reference semantics.
Host arguments remain borrowed, and invocation copies are released on ordinary
completion.

Constructor arguments are evaluated using the enclosing receiver before the
new instance becomes active. Script construction produces an instance of the
declared class. A native factory may produce a different registered type.
A native factory returning null raises `VirtualMachineException`.

`VirtualMachine::createNativeObject(constructorId)` invokes a native factory.
`createScriptObject(constructorId)` allocates a script object and executes its
compiled initializer and constructor body. Both consume prepared stack arguments,
not an argument container. The returned object owns one reference, which the
caller must release.

Push owned argument copies right-to-left. Within each struct argument, push its
physical slots in increasing order. After the call, the caller releases the
current argument values and pops their slots; a script constructor can reassign
its copied parameters. These APIs do not release or pop the caller's arguments.
They preserve existing result and intermediate values and restore the active
receiver and call frame, including on exceptions and native-callback reentry.
The compiler emits dedicated native/script object creation opcodes; each selects
its entry point without native/script dispatch.

`createNativeStruct(destination, constructorId)` and
`createScriptStruct(destination, constructorId)` initialize fresh, correctly sized
`RuntimeValue*` storage. They do not allocate a struct or release a previous value.
For nonempty structs, push the borrowed destination pointer before the explicit
arguments; pop it after argument cleanup. Empty structs omit the pointer and can
pass `nullptr`, but still execute initialization and constructor side effects.
Destinations may be on the fixed VM stack or in caller-owned heap storage.
The caller owns their field values and eventually calls the struct type's
`freeValue`. Assignment must stage creation before replacing a live destination.

Registered generated property initializers run before native or script struct
constructor bodies. They evaluate explicit property initializers in order and
default-initialize remaining properties. Without a registered property initializer,
creation supplies ordinary type defaults before the constructor body.
Copying a nested struct does not execute its constructor.

The low-level VM creation entry points allocate no argument or bookkeeping storage.
Native factories and script objects allocate their actual object storage;
initializer and constructor code can itself allocate values. Struct lifecycle
operations traverse properties without allocating temporary catalogs.
Interpreter construction evaluates arguments right-to-left, copies complete
argument values, and keeps destination storage alive through initialization and
the constructor body.

Script and struct constructor callables have a `void` return type;
`Constructor::getConstructedType()` reports the produced value type, including
native object factories whose produced class differs from their owner.
Failed script object creation releases its instance. Failed struct creation can
leave partially initialized fields; direct callers must release them.
VM faults are fatal to execution: stop execution and do not resume or reuse the
faulted VM. Fault recovery and complete release of nested frames, temporary
arguments, or instances are not supported yet. Class
initialization and constructor bodies cannot yield. Compile and initialize
required script-level hub variables before using this lower-level VM API.

Native factories that allocate `ScriptObject` storage must explicitly supply
any required field values. Direct allocation does not run a defaults-only pass.
The low-level `allocateScriptObject(typeId)` operation only allocates raw storage
and binds it as `this`; it does not invoke a constructor.

`getClassType()` and `getFieldCount()` describe the instance. `getField(index)`
returns a borrowed const value; `setField(index, value)` copies a value of the
field's declared type, retaining object references and releasing the previous
value. Callers must supply the correct value type. Both use owner-local
`NomadIndex` slots, not `PropertyId` values. Out-of-range slots throw
`NomadException`. Raw storage is accessible immediately; the compiler ensures
that script construction populates every field before the source constructor
body runs. A raw null string slot is not an initialized empty string.

Each instance owns its values; aliases to the same object share those values.
Instances never resize when type metadata changes. Finish field discovery
before creating instances. This API does not attach fields to native objects.
Reference cycles are not collected.

`VirtualMachine::getProperty(id, receiver, result)` and
`setProperty(id, receiver, value)` dispatch native properties to their registered
callbacks and stored properties to instance storage. A stored-property receiver
must be a `ScriptObject` of the property's owning type. Null
receivers, missing storage, mismatched owners, and invalid property IDs raise
errors. Getters copy an owned result; callers must release object-valued results
using the property's value type. Setters retain their own copy without consuming
the caller's value. Pass empty result storage to a getter.

The existing `getNativeProperty` and `setNativeProperty` host APIs remain
native-only.

### Native property creation

`TypeManager::registerNativeClassProperty(TypeId id, NativePropertyDefinition definition)` registers
a native property and returns its `PropertyId`. The owner ID must identify a
class type in that runtime. `definition.valueType` takes a `const Type*` from the
same runtime, or a placeholder obtained from its configured compiler's
`deferType(reference)`. The resolved value type must be public and non-void.
Host registration is permitted on both open and closed types.
Include `<nomad/script/NativeMember.hpp>` for `NativePropertyDefinition`.
`Runtime::addProperty` remains a compatibility entry point with identical behavior.
Native struct accessors are not supported; struct owner IDs are rejected.

Provide a getter, a setter, or both. A getter-only property is read-only; a
setter-only property is write-only. The runtime owns the registered name,
documentation, and callbacks. Captured references must remain valid whenever
the callbacks are invoked.

For an already registered class type identified by `typeId`:

```cpp
const auto propertyId = runtime.getTypeManager().registerNativeClassProperty(typeId, {
    .valueType = runtime.getIntegerType(),
    .name = "answer",
    .getter = [](nomad::VirtualMachine*, nomad::RuntimeValue& value) {
        value.setIntegerValue(42);
    },
    .setter = {},
    .documentation = "The answer."
});
if (propertyId.isInvalid()) {
    throw nomad::NomadException("Failed to register answer property");
}
```

Invalid IDs, non-class owners, invalid value types, missing callbacks,
and conflicting members are logged and return `PropertyId::INVALID` without
registering a property. Duplicate native properties are rejected even if their
definitions match. Use the existing property and native member lookup APIs to
retrieve the registered metadata.

Registration adds the property to Runtime's shared native-member indexes without
running callbacks or adding stored-field slots. It remains permitted after
class field layout finalization. Runtime retains property lookup and deferred
type-resolution APIs.

For low-level integration, `Runtime::canRegisterNativeMember` checks registered
class ownership, collisions, and shared native-member parameter signatures,
returning an error description on failure. `Runtime::catalogNativeProperty(id)`
indexes an already registered native property; invalid IDs, stored properties,
duplicate indexing, and incompatible signatures throw `NomadException`.
Ordinary host code should use the manager registration API instead.

Deferred properties receive their permanent `PropertyId` immediately. Compilation
resolves their value types before parsing script bodies, in both analysis and
executable modes. Missing or invalid types fail compilation. Native property
callbacks cannot run until type resolution completes; registration and analysis
do not invoke them. Metadata remains available while the type is unresolved.
Callbacks obtained from unresolved metadata require the owning runtime to
remain alive, including after the type has resolved.
The runtime must outlive the compiler, and unresolved placeholders must not be
used after their owning compiler is destroyed.

### Native method creation

`TypeManager::registerNativeClassMethod(TypeId id, NativeMethodDefinition definition)` registers a
native method and returns its `CallableId`. The owner ID must identify an object
type. Parameter and return-type IDs are resolved in the same runtime; IDs are
runtime-local and must not be transferred between runtimes.

`registerNativeStructMethod(id, definition)` provides the corresponding
registration for struct owners. The class and struct entry points reject the
other owner category. `Runtime::addMethod` dispatches to the appropriate entry
point. Both publish methods in the same callable and native-member catalogs.
Struct callbacks use the borrowed storage ABI above, not `getThis()`.

Each `ParameterDefinition` supplies its type ID, name, and documentation.
Parameters must have nonempty names and non-void types. Compiler-supplied
parameter types retain their existing calling conventions. The return type may
be `void`, but otherwise must be public. The callback must be nonempty.

```cpp
const auto methodId = runtime.getTypeManager().registerNativeClassMethod(typeId, {
    .name = "move",
    .parameters = {{runtime.getTypeId("float"), "distance", "Distance to move."}},
    .returnTypeId = runtime.getTypeId("void"),
    .function = [](nomad::VirtualMachine* vm) {
        vm->getThisAs<MyObject>()->move(vm->getFloatParameter(0));
    },
    .documentation = "Moves this instance."
});
if (methodId.isInvalid()) {
    throw nomad::NomadException("Failed to register move method");
}
```

The runtime owns the registered names, documentation, parameter list, and callback.
Captured references must remain valid whenever the callback is invoked.
Host registration is permitted on both open and closed types.
Invalid definitions and conflicting members are logged and return
`CallableId::INVALID` without registering a method.
Existing member-name and overload rules apply.
Use `getNativeMethod(id)`, `getMethod(id)`, or the member lookup APIs to retrieve
the registered method.

### Callable access

Use `getCallable(id)` to retrieve a callable, or `getFunction(id)`,
`getMethod(id)`, and `getConstructor(id)` to retrieve a callable of a specific
category. Category getters return `nullptr` for invalid IDs or a different
category. Function, method, and constructor IDs are accepted by the callable
accessors.

Each category offers conversions to its implementation: `asNativeFunction()` /
`asScriptFunction()`, `asNativeMethod()` / `asScriptMethod()`, and
`asNativeConstructor()` / `asScriptConstructor()`. They return the same object
with its specialized type, or `nullptr` when the implementation does not match.
Script methods and script constructors are available on compiled classes.

`Callable::getParameterOffset(parameterId)` returns the physical stack-slot
offset of a logical parameter, measured from the first parameter.
`getParameterStorageSize()` returns the total parameter slot count, or zero for
an empty parameter list. Both queries are also available through
`ScriptCallableBase`. They use current metadata, include captures already
appended to the parameter list, and exclude receivers, saved call frames, and
return destinations. They do not change logical positions, parameter counts,
VM parameter access, or the calling convention.

`Parameter::getStackValueCount()` reports the parameter's stack width. Ordinary
values use `Type::getSize()`; event callbacks occupy two slots even though their
type's value size is one. Compiler-supplied source parameters occupy their usual
single slot. Event dispatch includes an event-specific payload, so its complete
width is call-site-dependent: querying that width, a callable's total size
containing it, or a later parameter's offset throws `NomadException`. The offset
of the dispatch parameter itself remains computable if all preceding widths are
known. This does not restrict dispatch calls; their arguments are still expanded
at the call site.

Missing/deferred parameter sizes, slot-count overflow, and invalid or out-of-range
parameter positions also produce `NomadException`. Offset queries require only
the preceding widths; total-size queries require every width.

`getScriptCallables(callables)` lists all script functions, methods, and
constructors, including nested callbacks and generated class initializers.
`getScriptCallableAtLine(sourceName, line)` finds the innermost matching script
callable using one-based, inclusive source ranges. It works after parsing even
when compilation reports an error. Unknown source ranges do not match.
`getScriptCallableAtInstructionIndex(index)` finds the compiled callable containing
an instruction or operand; its end address is excluded.
`getScriptCallableByStartAddress(address)` requires an exact compiled start
address. These lookups return `nullptr` when no callable matches.
The existing function-only lookup and enumeration APIs still exclude methods
and constructors.

Include `<nomad/script/InstructionDump.hpp>` and use
`dumpInstructions(runtime, output)` to include every compiled script callable.
`dumpInstructions(runtime, callable, output)` takes a script-callable reference
and dumps just that callable. These free functions inspect existing compiled code;
they do not compile or execute scripts. The runtime exposes the data but does
not own instruction formatting.
Methods are qualified by their declaring type, and constructor and method call
targets are identified by name and callable ID.

Use `resolveFunction(name, argumentTypes)`, `resolveMethod(classType, name,
argumentTypes)`, or `resolveConstructor(classType, argumentTypes)` to select an
overload. Inspect the `ResolveResult` before retrieving the selected callable.
The selected ID or pointer can be retained without repeating name resolution.
Function overloads are selected by parameter count and types, independently of
their implementation. Different parameter counts require all parameters to be
ordinary values. Overloads with compiler-supplied source parameters
must retain the same count and parameter shapes. All overloads under a name must
agree on whether they return a value.
Registration of overloaded free functions is currently supported for native
functions; script-defined overload declarations are not yet supported.
Native and script functions use the same call syntax in expressions and
statements. A statement discards any returned value.

Functions, methods, and constructors share the `CallableId` type. Callable IDs
do not implicitly convert to integers or other ID categories.
`getCallableId(name)` returns the first free-function overload as a `CallableId`,
or `CallableId::INVALID` when absent. It does not select an overload.
`getCallableOverloads(name, ids)` lists free-function overload IDs.

`addProperty` returns a `PropertyId`; `addMethod` returns
the method's `CallableId`. Their failure results are `PropertyId::INVALID` and
`CallableId::INVALID`, respectively.
`getNativeMemberId` returns a `MemberReference`, a `std::variant<PropertyId, CallableId>`.
An absent member is represented by `PropertyId::INVALID`.
Member enumeration and overload APIs use `std::vector<MemberReference>`.
`getMemberType(member)` returns `MemberType::Property`, `MemberType::Method`,
or `MemberType::Unknown` for an invalid reference.
Use `std::get_if` to inspect a reference's ID category.
`getNativePropertyInfo(member)` returns `std::optional<NativePropertyInfo>`,
empty when the reference is invalid or not a native property.
`getNativeMemberMethod(member)` returns a native method or `nullptr`.
`getNativeMethod(callableId)` accepts the same ID returned by method registration.
Include `<nomad/script/Member.hpp>` for member references and `MemberType`; native property callbacks
and indexer definitions are available in `<nomad/script/NativeMember.hpp>`.

Use `getNativeFunction(id)` or `getScriptFunction(id)` for implementation-specific
access. These return `nullptr` for the wrong implementation kind. A const runtime
returns a `const ScriptFunction*`; a mutable runtime also permits script
parameter and inferred return-type updates. Registering additional APIs does not
change the selected targets of already resolved function, method, or constructor
calls.

Signatures expose `Parameter` entries through `getParameters()`. Script
parameters added with `addParameter(name, type, doc)` require a documentation
string; pass an empty string when no description is available. Descriptions are
available through each parameter's `doc` field.
Script source text is available through `getSourceCode()`; `getSource()` identifies whether
the callable is native or script-backed. Function-only enumeration and
source/bytecode lookups continue to exclude methods and constructors.

`getScriptCallable(id)` returns the shared script compilation interface for a
script function, method, or constructor, and `nullptr` for native or invalid IDs.
Const runtimes return a const interface. `Callable::asScriptCallable()` provides
the same view from a descriptor. Method and constructor descriptors remain owned
by their declaring class type; they are not returned by `getFunction(id)` or
`getScriptFunction(id)`.

Include `<nomad/script/Callable.hpp>` for common callable metadata,
`<nomad/script/NativeCallable.hpp>` for native implementations, and
`<nomad/script/ScriptCallable.hpp>` for script functions, methods, constructors, and
the shared implementation template. The common compilation interface is in
`<nomad/script/ScriptCallableBase.hpp>`. No receiver parameter is synthesized by
the callable constructors.

`TypeManager::registerScriptClassMethod(typeId, name, parameters, path, source)` and
`registerScriptClassConstructor(typeId, parameters, path, source)` publish script
signatures on a runtime-owned class type. They return an invalid ID and log an
error for an invalid owner, unsupported parameter types, or duplicate signatures.
These registration APIs associate members with their receiver type without adding
an implicit local or a receiver to the explicit parameter list. A parameter named
`this` is invalid. The supplied
source is retained for compiler diagnostics; registration alone does not parse,
compile, or enable invocation of the member.

Runtime's existing `addMethod`, `addConstructor`, `registerScriptMethod`, and
`registerScriptConstructor` methods remain compatibility entry points.
Callable IDs and category lookup APIs remain runtime-wide. Class descriptors
own their methods and constructors; returned pointers are borrowed for the
runtime's lifetime. Registration does not run native callbacks or alter stored
field layouts, and remains available after field layout finalization.
Struct owner IDs are rejected by these class APIs.

`TypeManager::registerScriptStructMethod(typeId, name, parameters, path, source)`
registers the corresponding struct-method signature. Class owner IDs are rejected.
`Runtime::registerScriptMethod` dispatches by owner category. Struct descriptors
own their methods under the same runtime-lifetime contract. Registration alone
still does not parse or compile the retained source; embedding code must supply
the method body through the existing compiler integration APIs.

Include `<nomad/script/NativeMember.hpp>` for `ParameterDefinition`,
`NativeMethodDefinition`, and `NativeConstructorDefinition`.
For low-level integration, `Runtime::catalogNativeMethod(id)` indexes an
already registered native method for member lookup. Invalid IDs, other callable
categories, duplicate indexing, and incompatible shared signatures throw
`NomadException`. Ordinary host code should use manager registration instead.

## API reference generation

Include `<nomad/script/Documentation.hpp>` to generate Markdown from a runtime's
registered APIs. `generateDocumentation(runtime, output)` writes the complete
reference, including script classes, fields, constructor overloads, and ordinary
method overloads. Nested callbacks and generated initializers are omitted.

`generateTypeDocumentation(runtime, output)` writes type descriptions and
constructors. Script classes are included even without a description. A
constructor is listed under the type it creates, which may differ from its
declaring type for native factories.

`generateMemberDocumentation(runtime, output)` writes native properties and
methods together with script fields and methods, grouped by receiver type.
Fields are read/write, and each method overload shows its parameter and return
types. `generateNativeMemberDocumentation(runtime, output)` remains native-only.

These generators do not invoke scripts or native callbacks. The compiler tooling
compiles source inputs before generating their reference.

## Module configuration

Read module settings through `NomadConfiguration` rather than parsing its serialized
representation. Typed getters return an empty optional for missing fields and
throw `NomadConfigurationException` for incorrect types. Required getters also
reject missing fields. Integer and string arrays are checked element by element.
`validateSection(path)` permits an absent section but rejects an existing scalar
or array at that path.
`getKeys(path)` lists a section's immediate keys; an omitted path lists root keys,
and a missing section returns an empty list. `getSectionArray(path)` returns
independently owned configuration documents for table-array entries, distinguishes
missing arrays from empty ones, and rejects non-table elements.

Modules supply their own defaults and validate constraints such as positive
dimensions and valid resource paths. The configuration API keeps the storage
format out of module code.

Project loading, discovery, compatibility checks, and module configuration type
validation report failures through `NomadConfigurationException`. Native sink
constraints checked by logging configuration propagate their original
`NomadException` errors.

The compiler source loader reads `scripts.directories` independently of game
settings. `readScriptDirectories(configuration)` returns normalized project-relative
roots, defaulting to `scripts`; an explicit empty array disables disk discovery.
`resolveScriptPaths(configuration, projectDirectory)` resolves those roots for
`loadSourcePaths`. Script directory lists reject escaping, duplicate, and
overlapping roots. Project loading preserves script settings; the compiler source
loader validates them when requested.

The game module reads `resources.directory` (default `res`) relative to the
project root. Audio and sprite directories are relative to that resource root,
not the script roots. Selecting a different resource root does not change script
discovery. Packaging preserves script discovery when a module relocates a
directory containing scripts.

## Hubs

Call `Runtime::registerHub(name, HubMode::Opened)` or
`Runtime::registerHub(name, HubMode::Closed)` before compilation. The mode is
required. Registration returns the new `HubId`; repeated registrations return
the existing ID only when both are open. Repeated closed registrations and
mixed modes fail. Dotted names such as `"game.audio"` identify independent
namespaces and do not create or require `"game"`.
Native hub names need not follow script identifier syntax and may
use keyword names. Empty names, empty dotted segments, and conflicts with existing
values, callables, or types are logged and return `HubId::INVALID` without partially
registering the name. Script hub declarations require a tokenizer-recognized
identifier; each dotted segment must start with a letter or underscore.
Native registration does not make otherwise invalid names usable in script
declarations.
Use `Runtime::isHub(name)` to test whether a hub is registered.
Include `<nomad/script/Runtime.hpp>` for `HubId` and `HubMember`, and
`<nomad/script/Hub.hpp>` to inspect hubs.
`getHubId(name)` returns a `HubId` or `HubId::INVALID`; `getHub(id)` accepts
a `HubId` and returns a borrowed `const Hub*`, or `nullptr` for an invalid or
unregistered ID. IDs belong to the runtime that issued them and are not
interchangeable with callable or other ID categories. A hub exposes its
qualified name, `getMode()`, and locally named members. Hubs have no parent or
child namespace members. Function members refer
to registered callables; types and events refer to their registered definitions.
Hub pointers remain valid when more hubs are registered.

`HubMember` stores variables, constants, events, and dynamic
variables directly as `VariableId`, `ConstantId`, `EventId`, and
`DynamicVariableId`. Type members store a borrowed `const Type*`.
Use `std::get_if<ConstantId>(member)` or the corresponding alternative to
inspect a member, then pass the ID directly to its typed API. Function overloads
use `HubFunctions::ids`; generic families use `HubTypeFamily::argumentCount`.

Use `registerHubNativeFunction(hubId, name, function, parameters, returnType, documentation)`
to register a native function with a local member name, such as `"info"` in hub
`"log"`. The supplied name is appended to the hub name without identifier validation.
The function is available under its qualified name and shares overload resolution,
documentation, and compiler-supplied source parameters with `registerNativeFunction`.
Both open and closed hubs accept host registrations. Invalid hub IDs,
callbacks, type references, and conflicting declarations are logged and return
`CallableId::INVALID` without adding a function. Referenced types must belong to
the receiving runtime; hidden source parameter types are supported.

Use `registerHubScriptFunction(hubId, name, path, source)` to register a script
function descriptor with a host-supplied member name. It returns a `CallableId` and
preserves the path and source in the shared callable catalog, just as
`registerScriptFunction` does. Registration alone does not compile the body or
make it executable. Both hub modes accept host registration. Invalid owners,
conflicting members and existing callable names are logged and
return `CallableId::INVALID`; this API does not introduce script overloads.
Use compiler source registration for normal source compilation.

Use `registerHubVariable(hubId, name)` to declare a variable using a host-supplied
name. It returns a `ContextVariableId` for the same storage used by
`registerHubVariable(qualifiedName)`. Variables start without a resolved type;
script assignments resolve their types through the existing compilation path.
For host-declared variables, supply a compiled parameterless `void` initialization
function through `setScriptInitializer` when no generated script initializer exists.
They remain writable in both open and closed hubs. Invalid owners,
duplicates, conflicts, and registration after script initialization are
logged and return `ContextVariableId::INVALID`.

Use `registerHubClass(hubId, name, mode, documentation = {})` to register a class
with a local name, such as `"ConsoleSink"` in hub `"log"`. It returns a `TypeId` and
exposes the class as `log.ConsoleSink`. Member names are not checked for identifier syntax.
The hub may be open or closed; `ClassMode` independently controls whether scripts
may extend the class. Invalid hub IDs, class modes, and conflicting
declarations are logged and return `TypeId::INVALID`.

`registerHubClass<Descriptor>(hubId, name, mode, arguments...)` also supports
specialized `ClassType` descriptors. It forwards the qualified name, mode, and
additional arguments through the same registration path as
`registerClass<Descriptor>`. Properties, constructors, and methods are then
registered using the returned class ID and the existing member APIs.

Use `registerHubEnum(hubId, name, values, documentation = {})` to register an enum
with a local name, such as `"Platform"` in hub `"os"`. Enum value names are not
checked for identifier syntax, but must be non-empty. It returns a `TypeId`; the enum and its typed constants
are available as `os.Platform` and `os.Platform.ValueName`. Empty value lists,
duplicate names or numeric values, and conflicting declarations fail without
publishing the enum or any of its values.

Use `registerHubConstant(hubId, name, value, type)` to register a locally named
constant, such as `"platform"` in hub `"os"`. It returns a `ConstantId` and
copies the value using the supplied runtime-owned, public, non-void type.
Object values are retained; the caller remains responsible for releasing its
original reference. The value must match the supplied type, just as with
`registerConstant`.

Both APIs accept open and closed hubs. Invalid hub IDs, invalid
types, and conflicts are logged and return the corresponding invalid ID.
Existing qualified-name registration APIs remain available.

Host member names are stored as supplied, including names that are not valid
script identifiers. The compiler validates names in source declarations;
host registration does not make an otherwise invalid identifier usable in
script syntax. Existing namespace qualification and internal-name handling
still apply.

Other native APIs still use qualified names with their existing runtime
registration functions. Scripts may add distinct members only to native
hubs registered as open, using matching `open hub` blocks. Existing writable
members remain assignable regardless of hub mode. Hub references must be fully
qualified, including in initializers and signatures; nesting only prefixes
declaration names. Qualified assignments cannot introduce new members.

Successful executable compilation installs safely resolved hub initial values
and typed defaults for runtime-dependent members. It does not execute application
code. Runtime-dependent initializers run in script-name order and then declaration
order. Developers are responsible for dependencies between initializers; reads
observe typed defaults or the value assigned so far.

After compilation and resource preparation, call `ModuleHost::run()` to
initialize scripts and invoke the runnable module. The module receives fully
initialized scripting state and owns its entry-point convention. The game
module executes `game.entry` through `Runtime::executeFunction`.

Use `ModuleHost::compileProject()` to load configured script roots and compile them.
It returns a `CompilationResult` containing diagnostics and error/warning counts;
the caller chooses how to display them. Application sessions produce executable
scripts, analysis sessions perform analysis, and packaging sessions cannot
compile. For supplied text or editor overrides, use `compileSources(sourceBatch, target)`.
Neither form executes initializers or an entry function. Failed compilation prevents
`run()` from initializing scripts or invoking the runnable module.

The host does not impose an entry convention or validate a configured entry
during compilation. Each runnable module executes its own entry; game entry
errors are reported during startup rather than by `nomad check`.

Create a host with `ModuleHost::create(...)`, which returns a
`std::unique_ptr<ModuleHost>`. `ModuleHost` is a pure virtual interface; callers
do not construct or depend on its concrete implementation.

`ModuleContext::compiler` is the host-owned compiler, available during native
module registration in application and analysis sessions. `ModuleHost::getCompiler()`
returns that same compiler. Use it for deferred types and compilation targets;
host compilation and phase callbacks use this instance. Packaging supplies neither
a runtime nor a compiler. Shutdown callbacks run before the compiler and runtime
are destroyed, and both are destroyed before module libraries are unloaded.

`<nomad/compiler/Compilation.hpp>` provides `CompilationResult` and
`compileSources(batch, compiler, target)`. Compilation uses the supplied compiler
and does not create a runtime, replace the compiler, or execute scripts.
Results own diagnostics and error/warning counts; `succeeded()` means there are
no errors, so warnings alone do not fail compilation.
`<nomad/compiler/Diagnostic.hpp>` provides `DiagnosticSeverity`, `Diagnostic`,
and `formatDiagnostic`, independently of compiler contexts.
`formatDiagnostic(diagnostic, stream)` writes to an existing `std::ostream`
without adding a newline. The string-returning overload produces the same text.

Exports use explicit steps: load sources, compile with the configured compiler,
check the result, then format the compiled runtime API. Neither instruction
dumping nor documentation generation loads sources, creates a compiler, or
compiles scripts.

```cpp
const auto result = session->compileSources(session->loadSources(scriptRoots));
if (result.succeeded()) {
    nomad::dumpInstructions(*session->getRuntime(), instructionOutput);
    nomad::generateDocumentation(session->getRuntime(), documentationOutput);
}
```

Include `<nomad/script/InstructionDump.hpp>` and
`<nomad/script/Documentation.hpp>` for these output functions. Callers handle
compilation diagnostics and decide whether to create or replace output files.
The compiler CLI does not emit instructions or overwrite documentation when
compilation fails; an unknown requested dump function is an error.

`ModuleContext::projectMetadata` always points to immutable, host-owned
`ProjectMetadata` and remains valid throughout the module lifetime.
Omitted metadata defaults to blank fields, including for standalone tooling.
Metadata does not control compilation.

There is no standalone `Game::create()` startup path. Use a game-module host
for applications; `Game::createForRuntime` registers the game API against a
borrowed runtime without compiling or executing scripts.

Embedders invoking callbacks or using their own VM without a module host must
call `Runtime::initializeScripts()` first. It executes script initializers once;
repeated successful calls do nothing. `VirtualMachine::run` never performs
initialization automatically. Analysis resolves initializers but installs neither
initial values nor executable initialization. `initializeScripts` rejects
analysis-only hub variables.
Initialization failure propagates to the caller, is not retried, and prevents
subsequent high-level script execution. Use a fresh runtime for a new application
execution.

## Source input and compilation

The compiler registers source snapshots. `SourceText` owns a
`sourceName` (used in diagnostics), `text`, and an optional `filePath`. The name
does not need to exist on disk and is never interpreted as a path. `filePath`
identifies a backing file, not a request to read it: the supplied text is always
authoritative, including unsaved editor changes and files not yet created.
Untitled documents and interpreter input can leave it unset.
`ScriptSource` pairs that text with an explicit `scriptName`. This is the
source-level name used to register the script's compiled runtime function.
It also retains an `owner` (empty for application sources) and `relativePath`
when loaded from a root. Module sources use `$owner.relative.file` script names;
their diagnostic source names remain paths or editor document names.
`SourceBatch` holds script sources and any diagnostics from loading them.

```cpp
#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/SourceLoader.hpp>
#include <nomad/script/Runtime.hpp>

void compileSources() {
    nomad::Runtime runtime;
    auto compiler = runtime.createCompiler();
    nomad::SourceBatch batch{{
        {"init", {"untitled:init", "return helpers.answer"}},
        {"helpers.answer", {"untitled:answer", "return 42"}}
    }, {}};
    nomad::CompilerContext context(compiler.get());
    compiler->registerSources(&context, batch);
    const auto compiled = compiler->compileFunctions(&context);
    // Inspect context.getDiagnostics() if compilation fails.
    (void)compiled;
}
```

`registerSources` accepts a batch of script inputs; a single script is a batch
with one entry. It copies the input, so the batch may be changed or destroyed
after registration. It appends sources; it does not replace already registered
functions. Duplicate script names are reported immediately to the supplied
`CompilerContext`. That same context collects loading and compilation diagnostics;
compilation refuses to proceed if it already contains errors. Use a fresh runtime
for independent analysis snapshots, or provide a runtime containing the host APIs
needed by the scripts. Compilation does not execute scripts.

`registerSourceRoots(roots)` configures additional `SourceRoot` entries before
the first `registerSources` call. Each entry supplies a path, optional owner
name, and a `required` flag. The compiler discovers these roots with the first
batch (or when compilation starts without a batch); supplied snapshots override
matching backing files. Module hosts configure their resolved module roots
automatically, including for compiler tools using the host's runtime.

Custom statements register with
`registerParseStatementFn(name, availability, parser, preParser, blockMode, blockClassifier)`.
The required `StatementAvailability` is `Compiler`, `Interpreter`, or `Both`;
dispatch rejects a statement in an unavailable mode before calling its parser.
The optional pre-parser receives `PreParsePhase::Declarations` first, then
`PreParsePhase::Signatures`. Publish early type/namespace declarations in the
first phase and resolve signatures in the second without registering declarations
again. Interpreter-only statements do not run pre-parsers during compilation.
Statements default to `StatementBlockMode::None`. Use `Statement` for a block
opened at the start of a line, or `Inline` for a block that can start anywhere
on the line, such as a callback. Each opening requires a matching `end`.
The optional `StatementBlockFn` classifier receives the complete current line
through a read-only `Tokenizer` and returns whether that occurrence opens a
block. Block discovery does not advance the tokenizer; classifiers must not
consume input. Compiler declaration scans and language-server source scans use
these registrations.
The enums and callback types are declared in `<nomad/compiler/Parser.hpp>`.
Parser and AST callbacks receive a `ScriptCallableBase*`, so the same callbacks
can work with script functions, methods, and constructors. The interface exposes
parameters, local variables, captures, source locations, and compilation ranges;
it does not make a method or constructor a free function.

`createFunction(context, scriptId, functionName)` creates a declared or generated
function within an existing script. Obtain the script ID with
`getScriptId(functionId)`. Function creation uses the script's source name and
text and reports name collisions to the supplied context.

For disk input, call `loadSourcePaths(paths)` with a vector or span of filesystem
paths, then register the returned batch.
Each directory is a separate compile root; relative paths become dotted script
names. Individual files use their filename stem. Discovery ignores non-`.nomad`
files, skips symbolic links within directories, and visits at most ten directory
levels, counting the root. Scripts are sorted by script name and source name.
Filesystem failures are diagnostics, including inaccessible paths and failed
reads; they prevent compilation rather than silently compiling an incomplete
project.

For editor buffers, pass a span or vector of `ScriptSource` overrides as the
second argument to `loadSourcePaths(paths, overrides)`. Matching buffers replace
disk snapshots **before their contents are read**, so a locked disk file does
not block analysis. An explicit missing `.nomad` file also uses its matching
buffer without a missing-path error. Other loading failures are still reported.

File-backed overrides match by absolute `filePath` with canonical parent
directories, not by `scriptName` or `sourceName`. Relative paths use the current
working directory. Matching is case-insensitive on Windows and case-sensitive
elsewhere. Directory aliases are resolved; hard links and filesystem
case-sensitivity settings (including case-sensitive Windows directories) do not
change this identity policy.
The caller decodes document URIs into filesystem paths.

A discovered file keeps its disk-derived script name and takes the override's
complete `SourceText`, including its diagnostic name and backing path. Unmatched
overrides, including untitled and not-yet-saved scripts, are added using their
explicit script names. The caller is responsible for project membership and
naming those scripts; overrides are not restricted to the supplied disk roots.
Duplicate file identities or duplicate untitled `sourceName` values produce
loading errors, not last-write-wins behavior. Different documents with the same
script name remain ordinary compiler collisions.

Use `loadSourceRoots(roots, overrides)` for owner-aware loading or
`ModuleHost::loadSources(paths, overrides)` to combine application paths and
configured module roots. Module ownership takes precedence over overlapping
application roots. A discovered or unsaved module file receives its
owner-qualified identity regardless of the override's supplied script name.
Repeated disk paths are deduplicated; conflicting module ownership and module
dotted-name collisions are loading errors.

Every module contributes its fixed, optional `<module-directory>/scripts` root,
including programmatic `ModuleManifest` descriptors. Module manifests cannot
configure that directory. A missing directory contributes no sources; a
non-directory path is an error. Application script roots remain configurable.

```cpp
const std::vector<nomad::ScriptSource> buffers{
    {"entities.player", {"editor:player", unsavedText, playerPath}}
};
const auto batch = nomad::loadSourcePaths(scriptRoots, buffers);
const auto result = nomad::compileSources(batch, compiler, nomad::CompilationTarget::Analysis);
```

The loader returns owned, sorted snapshots without changing the disk or buffers.
Rebuild a batch with the latest buffers for each analysis; remove an override
when its document closes to use disk text again. Document versions, project
association and stale-analysis rejection belong to the editor adapter, not the
compiler. `compileSources(batch, compiler, target)` accepts snapshots without
reading their backing files; configured source roots can contribute additional
scripts. Loading warnings permit compilation; loading errors stop it before
parsing. Create and configure the runtime and compiler explicitly, then use
`compileSources` with `CompilationTarget::Analysis` for analysis or
`CompilationTarget::Executable` for executable output. For module-aware
compilation, use the module host's compilation API so its configured compiler
and module lifecycle are preserved.

## Persistent interpretation

```cpp
#include <nomad/interpreter/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <iostream>

void interpretInput() {
    nomad::Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    const nomad::Interpreter interpreter(*machine);
    nomad::InterpreterContext context(interpreter);

    if (!interpreter.execute("score = 10", context) ||
        !interpreter.execute("score + 5", context)) {
        std::cerr << *context.getError() << '\n';
        return;
    }
    const auto* result = context.getResult();
    std::cout << result->getType()->toString(result->getValue()) << '\n';
}
```

`execute(source, context)` accepts assignments, supported function calls, and
multiline `if`, `while`, `until`, `do`, and `for` statements. It also accepts a
single expression, empty input, and void calls. This is the single general
source-execution API; the former public `evaluateExpression` API has been removed.
Use `context.getResult()` for the owned typed result, including a void value when
the input produces no value. Each call clears the context's previous result and
diagnostics. Syntax, type-resolution, and execution failures return `false` with
diagnostics and no result.

The `SourceText` overload uses its name for input diagnostics:

```cpp
const nomad::SourceText input{"console:input", "score + 5"};
const auto succeeded = interpreter.execute(input, context);
```

The string overload preserves the existing context source name. Interpretation
continues to share the compiler's tokenizer, parser, and type resolver; it does
not register input as a compiled function. Declarations, `return`, assertions,
and inline callbacks remain unsupported by the interpreter. Loops execute
synchronously on the caller's thread.

Resolved AST nodes can also be evaluated directly. `Expression::evaluate`,
`Statement::evaluate`, and `StatementList::evaluate` return owned
`InterpreterValue` values rather than publishing intermediate values through
the context. Expressions return their value, assignments return the assigned
snapshot, and statements without a value return typed `void`. Statement lists
return the last executed statement's value. `ArgumentList::evaluate` returns
owned arguments in logical parameter order after evaluating them right-to-left.

Direct AST evaluation throws on execution failure. `execute` converts those
failures to diagnostics and publishes the final successful result in the
context; explicit host callers can publish a returned value with
`context.setResult(value)`. Struct values retain independent storage, without
interpreter return-destination forwarding or RVO. Bindings, diagnostics, and loop
control remain in the context. Property/indexer assignments preserve their
receiver and right-hand-side evaluation order.

Publishing an lvalue with `setResult(value)` copies it; publishing an owned
temporary or `std::move(value)` transfers its ownership into the context.

Call and constructor arguments evaluate right-to-left, matching compiled code;
method receivers still evaluate before their arguments. This also applies to
existing scalar interpreter calls, which previously evaluated arguments
left-to-right.

Loop bindings are ordinary mutable locals or compatible parameters and remain
available after the loop. Frame-backed interpretation can reassign script
parameters without replacing the caller's arguments; their declared types remain
fixed. Callback captures retain the values present when the callback was created.

Use `hasError()` to check for errors and `hasWarning()` to check for warnings.
Warnings alone do not indicate failure. To display all diagnostics from the latest
evaluation, iterate over `getDiagnostics()`:

```cpp
const auto succeeded = interpreter.execute(input, context);
for (const auto& diagnostic : context.getDiagnostics()) {
    std::cerr << nomad::formatDiagnostic(diagnostic) << '\n';
}
if (!succeeded) {
    return;
}
```

`getError()` provides the first error message, or null when there are no errors.
Each new evaluation clears previous diagnostics; inspect or copy them before
executing more input. Call `clearError()` to dismiss both errors and warnings
without starting another evaluation.

Results are owned by the context. Copy a result using `InterpreterValue` if it must
survive another evaluation; do not free the context's result manually.
`listVariables()` lists interpreter-owned variables, not borrowed frame slots.
Different contexts have independent interpreter variables even when borrowing
the same VM. Access to a shared context or VM must be serialized by the host.

### Struct construction, calls, and frame inspection

The interpreter can construct registered structs and pass or return complete
struct values through native and compiled script free functions. For example,
`point = new Point 3 4` runs the registered initializer and selected constructor;
`copy = transform point` passes an independent struct copy and owns the returned
value. Struct constructor parameters may themselves be structs. Class
constructors can also accept struct arguments.

The interpreter can also read and assign complete struct values supplied by the
host or present in an active compiled script frame. For example,
`copy = point` creates an independent field-wise copy, and
`point = point` safely preserves the value. Contained class references retain
their ordinary reference semantics. Empty structs remain typed values even
though they occupy no slots.

Empty constructors and calls still execute their side effects. Returning an
empty struct retains its type without a slot pointer. A function call used as a
statement releases its result, including owned fields in a returned struct.
Aggregate calls execute synchronously; aggregate yield/resume remains unsupported.

Include `<nomad/interpreter/InterpreterValue.hpp>` to use immutable interpreter values
independently of an interpreter context. `InterpreterValue(type, storage)` copies a
`std::span<const RuntimeValue>` containing
exactly `type->getSize()` contiguous slots. The type's layout must be finalized.
`getStorage()` borrows the complete owned value. Copies own separate slot
storage; moving a value preserves its slot addresses. The runtime and type
metadata must outlive the value. Every value owns contiguous storage sized to
its type: scalars occupy one allocated slot, structs occupy their complete field
layout, and empty structs and void values allocate no slots. `getValue()`
provides checked scalar access, while `getStorage()` exposes the complete value.
Void and moved-from values have no scalar slot; calling `getValue()` on them
throws `std::invalid_argument` rather than returning a placeholder.

An `InterpreterValue` has no field setters or writable storage views. Copy/move
assignment replaces the complete value; it does not edit its fields. Struct
copies have independent storage. Strings and class references are retained for
the value's lifetime; referenced class objects retain their normal mutable
reference semantics. Copying and cleanup use the value's `const Type*`, without
a separate ownership or cleanup discriminator.

`Interpreter::getProperty(receiver, property)` returns an owned field value.
`setProperty(receiver, property, value)` returns a new `InterpreterValue` with
that field replaced. Neither operation modifies its inputs or retains returned
values. Each accepts either:

- A runtime-local `PropertyId` for a direct stored field of the receiver's type.
  An ID for a nested field cannot be applied directly to its outer struct.
- A `NomadStringView` containing a field name or dotted path, such as `"leaf"` or
  `"leaf.number"`, relative to the supplied receiver.

The receiver and every intermediate owner must be structs. Nested structs copy
by value; strings and class-valued fields retain reference semantics. Class
references can be read or replaced as final fields, but paths cannot traverse
class objects or native accessors yet. Empty fields return their type and an
empty storage view without a slot pointer.

For example:

```cpp
const Interpreter interpreter(*machine);
const auto leaf = interpreter.getProperty(rootValue, "leaf");
const auto number = interpreter.getProperty(leaf, numberPropertyId);
const auto updated = interpreter.setProperty(rootValue, "leaf.number", replacement);
```

The replacement must have the final field's exact type. Unknown names, invalid
or wrong-owner IDs, empty paths/segments, non-struct owners, and incompatible
replacement types throw `std::invalid_argument`. Failed copying releases partial
results and leaves both inputs unchanged. These value operations require no
interpreter context and do not change a context's result.

Use the span overloads of `InterpreterContext::setVariable(name, type, storage)`
and `setResult(type, storage)` for structs. Both copy the complete value before
releasing what they replace, so their own storage can be supplied as the source.
`setResult(const InterpreterValue&)` likewise takes an owned copy.
`getVariableStorage(name)` and `getVariableStorageById(variableId)` borrow
complete variable storage; `getResult()->getStorage()` borrows result storage.
Assignments to an existing context-owned variable of the same type preserve its
slot addresses and update its contents. Adding other variables does not relocate
those slots. Changing a variable's type or clearing it removes its old storage
from name/ID lookup. During evaluation, that storage remains alive until the
outermost evaluation exits; outside evaluation it is released immediately.
Replacing a result invalidates result views. Copy into an `InterpreterValue` when
an independent snapshot must survive those operations.

An empty view can mean either an empty struct or a missing variable; check
`getVariableType` or `getVariableTypeById` to distinguish them. Empty struct
views contain no slot pointer. The scalar constructors, setters, `getValue()`,
and `getVariableValue` accessors reject structs with `std::invalid_argument`,
including one-slot and empty structs; use complete storage views instead.
Incorrect view sizes also throw `std::invalid_argument` without replacing the
previous value.

Frame-backed contexts borrow locals and ordinary parameters from the active
script callable. `getParameterStorage(parameterId)` and the span overload of
`setParameter(parameterId, storage)` accept logical parameter IDs and select
their complete physical ranges, including when a native callback inspects the
enclosing script frame. Writes preserve declared types and affect the borrowed
frame, not the original caller's copied arguments. New interpreter variables
remain context-owned. Borrowed frame storage is valid only while that frame is
active; it must not be used after the frame returns.

`getReceiverStorage()` borrows the complete struct receiver of an active
method or constructor frame, including from an intervening native callback.
It returns an empty view without a pointer for an empty struct. Calling it on
a non-frame context or a class/free-function frame throws `NomadBug`.
The interpreter can read `this` and its stored fields in such struct frames;
the resulting values are owned copies, not mutable receiver aliases.

### Mutable interpreter storage

Include `<nomad/interpreter/InterpreterStorage.hpp>` for an owning, mutable slot
buffer. `InterpreterStorage(type, source)` copies exactly `type->getSize()` slots
using the type's lifecycle operations. The type and finalized layout must remain
valid until the storage is destroyed. The owner is noncopyable and has a fixed
type; destruction releases its contained values.

`getStorage()` returns a const view; `getMutableStorage()` exposes the live slots
for borrowed receiver access. Their addresses remain stable for the owner's
lifetime. `assign(source)` replaces the contents without resizing the buffer.
Source and destination may overlap, including self-assignment. Copy failures
leave the destination unchanged; null types and incorrect view sizes throw
`std::invalid_argument`. Zero-sized structs retain their type with empty views
and accept empty assignments.

Use typed assignment or receiver operations when replacing reference-valued
slots so that their ownership is maintained. Constructing an `InterpreterValue`
from the const view creates an independent snapshot; neither snapshot contents
nor ordinary struct arguments become mutable aliases.

Context-owned variables use these buffers. Frame-backed variables continue using
VM storage, and published context results remain immutable `InterpreterValue`
snapshots. A borrowed receiver does not keep its backing owner alive: destroy it
before replacing or destroying that owner. Ordinary struct parameters and
published results remain independent snapshots.

### Advanced AST receiver access

`InterpreterReceiver`, `InterpreterObjectReceiver`, and
`InterpreterStructReceiver` are public AST evaluation types. Ordinary embedding
and invocation code continues to use `InterpreterValue`; receiver wrappers are
not required Interpreter inputs or results. Their headers are under
`nomad/interpreter/`.

`InterpreterContext::getReceiver()` returns the context's receiver wrapper, or
`nullptr` when there is no receiver. Active class and struct method/constructor
frames have wrappers, including empty structs. Receiver access validates that
the originating script frame is still active. The returned wrapper must not be
used after that frame or context ends.

The common API provides `getType()`, `getValue()`, `getProperty(property)`,
`setProperty(property, value)`, and `callMethod(methodId, arguments)`.
Reads and method results return owned typed values. Property
selectors are IDs or names; dotted paths traverse nested stored struct fields.
Direct object properties can use registered native accessors. Execution and
copy failures throw; general source execution reports them through diagnostics.

For a resolved AST receiver location, include
`<nomad/compiler/StorageLocation.hpp>` and call
`location.withReceiver(context, operation, prepare)`. The operation accepts an
`InterpreterReceiver&` and returns an owned `InterpreterValue`. The optional
preparation callback runs before the operation, after evaluating and retaining
any object owner or temporary root; use it to evaluate call arguments.
Context storage is checked again after preparation. Same-type assignments remain
visible, while an unassigned or retyped context root produces an exception before
method execution. Receiver and borrowed views must not escape the operation.
Ordinary AST evaluation still uses `Expression::evaluate(context)`.

Object owners and temporary backing storage remain alive until the operation
finishes. Context-owned struct storage and VM frames are borrowed, not retained.
Clearing or retyping a context-owned variable during evaluation retires its old
storage until the outermost evaluation exits, including failure paths. An active
receiver continues using that old storage without overwriting the new binding.
Nested interpretation does not release its caller's retired storage. Same-type
updates still modify the existing buffer in place. Variable names and logical
IDs remain stable; retired storage is not exposed by variable lookup/listing.
Contexts and borrowed VM frames must themselves remain alive.

Source execution, formatting, direct AST evaluation, and scoped storage-location
operations establish these evaluation lifetimes automatically. Hosts manually
holding context-backed receivers can keep the guard returned by
`context.beginEvaluation()` in a local variable to establish the same lifetime.
Its destructor ends that evaluation on normal return or exception. Create guards
through the context; do not manually call `endEvaluation()` while a guard is
active. Guards cannot be copied or moved, and the context must outlive them.
Views must not outlive the outermost guard. No variable renaming or logical-ID
compaction occurs.

`getMutableVariableStorageById`, `getMutableParameterStorage`, and
`getMutableReceiverStorage` provide the corresponding live context/frame views.
Frame-backed access performs the same frame validation as the const accessors.
An empty variable view can still mean a missing variable or a zero-sized struct;
query its type to distinguish them. These APIs do not change assignment ordering.

`Interpreter::createObjectReceiver(classType, object)` returns a
`std::unique_ptr<InterpreterObjectReceiver>`. The receiver retains the supplied
non-null object and releases that reference when destroyed; the caller's
reference is unchanged. The interpreter and type must outlive the receiver.
Value snapshots retain that same object, and field updates remain shared.
Strings are class types and use this factory too. A frame-backed object receiver
refers to the script frame's receiver even when a native callback temporarily
uses a different VM receiver. Directly constructed object receivers also retain
their objects.

`Interpreter::createStructReceiver(structType, storage)` returns a
`std::unique_ptr<InterpreterStructReceiver>` borrowing mutable storage. The
factory does not copy, retain, or free the struct's slots. Its interpreter, type,
and backing storage must remain alive, and the storage address must remain
stable until the receiver is destroyed. Updates through the receiver reach that
storage; `getValue()` returns an independent snapshot. Zero-sized structs are
valid: their storage pointer may be null, including when they contain zero-sized
stored fields.

Directly constructed `InterpreterStructReceiver` wrappers also borrow storage
using a struct type and live storage address. For a mutable temporary or private
copy, keep an `InterpreterStorage` owner alive separately. The wrapper never
owns or frees struct slots. Empty structs retain their logical type despite
requiring no storage slots.

For already resolved calls, `Interpreter::runFunction(functionId, arguments)`
and `Interpreter::construct(constructorId, arguments)` use the interpreter's
borrowed VM and take `std::span<const InterpreterValue>` in logical parameter order.
They dispatch native and compiled script callables and return an owned
`InterpreterValue`; no `InterpreterContext` is required or modified.
Use a registered constructor ID for construction, including struct constructors
whose callable return type is void. Native class factories return the registered
constructed type, which can differ from their declaring type.

Arguments must belong to the VM's runtime and match the resolved signature's
type-acceptance and storage rules. Invalid IDs, wrong callable kinds, unresolved
signatures, uncompiled script bodies, and mismatched arguments throw
`std::invalid_argument` before execution. Execution and value-copy failures
propagate to the caller. Argument structs are independent copies; contained
class references retain their shared reference semantics. Results remain valid
after the interpreter, context, arguments, or VM are destroyed, provided the
runtime/type descriptors still exist.

Interpreter struct results are returned as independent values. The interpreter
does not forward caller destinations or apply return-value optimization to
struct storage.

Completed calls preserve the enclosing VM registers/frame. Scalar script calls
retain their existing suspension behavior; calls with struct arguments or
returns remain synchronous and cannot yield. Closure/capture arguments are not
supported by these entry points. Fatal VM fault recovery remains unsupported.
Call `context.setResult(result)` explicitly to publish a returned value in a
context. The context no longer exposes function-execution or construction APIs.
The legacy `Runtime::executeNativeFunction` context adapter has been removed.
Use typed interpreter arguments for both scalar and struct calls. For example:

```cpp
const InterpreterValue argument(runtime.getIntegerType(), RuntimeValue{NomadInteger{25}});
const auto result = interpreter.runFunction(functionId, {&argument, 1});
context.setResult(result);
```

Host call failures propagate as exceptions instead of being converted into
context diagnostics by an adapter. The call APIs do not publish or clear
context results. Callers decide how to store returned values and report
caught exceptions.
The VM's scalar script scheduler retains its existing aggregate-storage restrictions.

`Interpreter::runObjectMethod(receiver, methodId, arguments)` invokes a registered
native or compiled script method and returns an owned `InterpreterValue`.
Supply a typed receiver and arguments in logical parameter order; no context
is required, and results are published only when the caller stores them.
The overload `runObjectMethod(classType, object, methodId, arguments)` accepts a
raw object receiver and retains it for the duration of the call.
Receiver and argument types must match the method's signature and belong to
the interpreter's runtime. Invalid IDs/kinds, receiver types, argument counts
or types, null object receivers, and uncompiled methods throw
`std::invalid_argument` before execution.

This method API currently supports class/reference receivers with scalar or
reference arguments and scalar, reference, or void results. Struct receivers,
arguments, and results remain unsupported and throw `std::invalid_argument`.
Source evaluation still evaluates the receiver first and arguments right-to-left.
Arguments are copied; mutations of referenced class objects remain shared,
including changes made before a method throws. Method results retain returned
objects and strings independently of the receiver, arguments, interpreter,
and VM, provided their runtime/type descriptors remain alive.

Completed method calls restore the enclosing VM frame and result/intermediate
registers. Script methods retain their existing restriction against yielding.
Execution and value-copy failures propagate to host callers; fatal VM fault
recovery remains unsupported. Property/accessor execution is separate from
this method API.

For struct receivers, use
`Interpreter::runStructMethod(receiver, methodId, arguments)`. It accepts
complete typed arguments, including structs and empty structs, and invokes
native or compiled script methods. It returns a `StructMethodResult`, declared
in `<nomad/interpreter/Interpreter.hpp>`, containing:

- `receiver`: an owned copy of the updated struct receiver.
- `result`: the method's owned scalar, reference, struct, empty, or void result.

The method operates on a private receiver copy. Input struct fields and argument
copies are independent; contained class objects keep their shared reference
semantics. Even when a method returns `this`, the two returned structs have
independent storage. Empty structs keep their type without a storage pointer.
Both returned values can outlive their inputs, interpreter, and VM, provided
their runtime/type descriptors remain alive.

This API does not publish results or replace context bindings. For example:

```cpp
const auto invocation = interpreter.runStructMethod(receiver, methodId, arguments);
context.setVariable("value", invocation.receiver.getType(), invocation.receiver.getStorage());
context.setResult(invocation.result);
```

Receiver/owner and argument validation follows the typed method-call rules
above; a class receiver is not accepted by this struct-specific entry point.
Execution or copying failures throw without returning a replacement/result
pair. This is not a transaction for shared objects: mutations of contained
class objects remain visible even if the call fails.

Struct-method calls are synchronous and cannot yield, for either native or
script methods. They preserve the enclosing execution frame, current object
receiver, and result/intermediate registers. Fatal VM fault recovery remains
unsupported. This snapshot-based host API does not automatically replace
context variables.

### Borrowed struct-method invocation

`Interpreter::runStructMethod(structType, storage, methodId, arguments)` accepts
a mutable span containing exactly the struct's physical slots. It invokes the
method against that storage and returns only the owned method result.
`InterpreterStructReceiver::callMethod(methodId, arguments)` uses this live
invocation contract. There is no receiver copy or writeback step. Zero-sized
structs use empty spans and still execute their methods.

After struct declarations and method bodies have been compiled, interpreted
source can call methods on variables, frame locals, copied ordinary parameters,
`this`, nested stored fields, object-owned fields, and temporary structs.
For a `Counter.next` method that increments and returns `this.value`:

```nomad
c = new Counter
total = c.next + c.next
```

Each invocation updates `c` before returning its result: `c.value` is `2` and
`total` is `3`. Assignments such as `copy = c`, ordinary struct parameters, and
returned struct values still have independent storage.

Receiver expressions and object/temporary owners are evaluated once before
right-to-left explicit argument evaluation. Context-backed locations are
rechecked before invocation, preserving same-type argument changes and reporting
cleared or retyped roots without using an invalidated address. Object field
receivers continue using the evaluated, retained owner even when its former
variable is replaced by an argument.

Borrowed calls are synchronous and cannot yield. Mutations performed before a
method throws or its result cannot be copied remain visible; argument and result
cleanup and enclosing VM state restoration still occur. Source execution
reports failures as diagnostics and clears its published result, without
committing the failed assignment's target. Hosts must keep borrowed backing
storage alive throughout the method, including native reentry.

`listVariables()` displays struct type names rather than formatting their
fields. Stored fields can be read through struct variables, ordinary parameters,
borrowed struct `this`, class-owned structs, and temporary construction/call
results. For example, `copy = holder.value.point` owns a field-wise struct copy,
and `(makeOuter).point.name` retains the extracted string after releasing the
temporary root. Nested scalar/reference fields and one-slot/empty structs are
supported. Empty reads still evaluate their receiver's side effects. Contained
class references remain shared; their existing property/method APIs remain
available.

Stored fields rooted in named struct variables, frame locals/parameters, and
struct `this` can be assigned, including nested fields and complete struct fields.
For example, `root.leaf.number = 31`, `root.leaf = source`, and
`this.leaf.text = "updated"` are supported. The RHS completes before replacement.
Copies remain independent, contained class references remain shared, and empty
field assignments still evaluate the RHS.

`InterpreterContext::setVariableProperty(name, property, value)` updates a
stored field in a named variable or frame parameter/local.
`setReceiverProperty(property, value)` updates a property through the active
class or struct receiver wrapper. Each accepts either a direct `PropertyId` or
a field name/dotted stored-struct path.
Struct updates use the current value at write time, so unrelated field changes
made during the RHS are preserved. Failed copying before replacement leaves
the destination unchanged. Object updates affect the shared instance; native
accessor side effects are not rolled back. These setters do not change the
context's result; source assignment
statements separately publish their owned assignment snapshots.

Class-owned and temporary-rooted struct writes, source-level interpreter struct
method calls,
aggregate captures, and field-aware struct formatting remain unsupported.

## Parameterized types

`TypeManager::registerParameterizedType(name, argumentCount, factory, initializer = {})`
registers a type family. The factory has the signature
`std::unique_ptr<Type>(Runtime*, TypeId, const std::vector<const Type*>&)`. It receives
the owning runtime, reserved specialization ID, and resolved type arguments.
It returns a fully constructed, unregistered descriptor carrying that runtime
and ID. It must not register the
returned descriptor itself, including through `registerClass`.

The runtime-owned manager validates and registers the descriptor, then calls the optional
initializer with `(Runtime*, TypeId)`. The ID identifies the registered
specialization and can be used to retrieve it and add its members. The manager
caches the specialization after initialization succeeds.
`getParameterizedType(name, arguments)` creates a specialization on first use
and reuses it on subsequent requests without rerunning either callback. Arguments must
be non-void, public types owned by that runtime. Invalid requests are logged and
return null.

`getParameterizedTypeArgumentCount(name)` returns the registered family's arity,
or an empty optional if the family is not registered.

Access these APIs through `runtime.getTypeManager()`. Existing Runtime methods
and factory/initializer type aliases remain compatible. Constructor-inference
and member-template registration also belong to TypeManager; Runtime wrappers
retain the same behavior. `getParameterizedTypeNames(names)` appends family
names in registration order without clearing existing entries.
`getMemberTemplateSignature(name)` returns family template metadata only;
`Runtime::getMemberSignature(name)` continues to combine it with native members.

Runtime's low-level `publishTypeFamilyNamespace(name, argumentCount)` publishes
a registered manager-owned family into hub indexes. Missing families, mismatched
arity, and namespace collisions throw `NomadException`. Families registered
before their hub are included when that hub is created.
`Runtime::canRegisterMemberSignature` checks a proposed member against the
shared parser signature without registering it; an incompatible proposal
returns false and an error description. Ordinary hosts should use the manager
registration APIs.

Requesting a specialization creates a descriptor, not an object. The compiler resolves parameterized
annotations in script parameters, function parameters, and event declarations
through the same registration API.

### Constructor inference signatures

After registering a parameterized type family, hosts can register one constructor
inference signature with `registerConstructorTemplate(name, parameters)`.
These declarations are independent of whether the concrete constructor is native
or script-defined; executable script-defined types are not yet supported.
Each `TemplateParameter` describes either a concrete `Type*` or a
`TypeParameterIndex{index}` referring to the family's zero-based type-argument position.
For example, for an already registered two-argument family named `Pair`:

```cpp
if (!runtime.getTypeManager().registerConstructorTemplate(
    "Pair",
    {
        nomad::defTemplateParameter("key", nomad::TypeParameterIndex{0}, "The key."),
        nomad::defTemplateParameter("value", nomad::TypeParameterIndex{1}, "The value.")
    }
)) {
    throw nomad::NomadException("Failed to register Pair constructor template");
}
```

Every family type argument must appear at least once. Repeated references are
allowed. Concrete types must belong to the runtime and be non-void, as with native
constructor parameters. Unknown families, invalid type-argument indices,
incomplete signatures, and duplicate registrations log an error and return
`false` without changing the registered signature.

`getConstructorTemplate(name)` returns an optional copy of the signature,
or an empty optional when none is registered. Registering or retrieving this
metadata does not create a specialization or register an executable constructor.
`inferConstructorTypeArguments(name, argumentTypes)` deduces the family's
type arguments from a vector of static constructor argument types. The vector
uses signature order, including null entries for compiler-supplied source file,
function, and line arguments. Null entries cannot bind generic type parameters
or match ordinary concrete parameters.

The returned `ConstructorInferenceResult` contains a `ResolveStatus`, a complete
`typeArguments` vector on success, and an `error` message on failure.
`isResolved()` reports success. Missing signatures return `UnknownName`, incorrect
argument counts return `NoMatchingArity`, and invalid types, conflicting bindings,
or mismatched concrete parameters return `NoMatchingTypes`. Failed deduction
returns no partial bindings and does not log; callers can report the returned
diagnostic in their own context.

Generic bindings must be registered public, non-void types from the same runtime.
Repeated bindings and concrete parameters require the same type; deduction does
not use implicit conversions or guess a common type. Returned type arguments
follow the family's type-parameter order, not constructor argument order.
Deduction neither creates specializations nor invokes constructors.

Scripts can omit constructor type arguments for families with a registered
inference signature. The constructor arguments' static types determine the
specialization; the assignment destination does not participate. Explicit type
arguments bypass deduction. Families without a signature still require explicit
type arguments.

For a two-parameter family with the `Pair` signature above, `new Pair "key" 1`
selects `Pair<string int>`. Argument expressions can include variables and nested
inferred constructors. Deduction does not execute argument expressions.
The selected specialization is reused through the normal type registry, and its
concrete constructor must still match the arguments. Hosts register that
constructor with `addConstructor`, usually in the type-family
initializer. Compiled scripts and the interpreter both support inferred
construction through the existing native constructor execution paths.

### Native class types and constructors

`ClassType` describes a class type and its type arguments. Its default value
is null, and its text representation is its type name. Specialized types may
provide other default values and formatting.

The type lifecycle functions use pointers to the first slot of one typed value:

```cpp
virtual void initValue(RuntimeValue* storage) const = 0;
virtual void copyValue(
    const RuntimeValue* source,
    RuntimeValue* destination
) const = 0;
virtual void freeValue(RuntimeValue* storage) const noexcept;
```

Provide `getSize()` contiguous, valid `RuntimeValue` slots. Nonempty values require
non-null pointers. Zero-slot types, including `void`, access no storage and may
receive `nullptr`; their lifecycle operations do not alter a supplied slot.

`initValue(&value)` initializes cleared, unowned storage with the type's default;
it does not run script constructors or release existing contents.
`copyValue(&source, &destination)` copies into cleared storage or replaces an
owned value. Class and string copies retain their existing reference semantics,
and self-copy is supported. Release owned contents through `freeValue(&value)`;
this does not release the storage itself. If initialization or copying fails
after acquiring resources, release the partial destination through `freeValue`.
All `freeValue` overrides must be `noexcept`. Cleanup requires valid runtime
metadata: using an unresolved compiler placeholder or an unfinalized struct
for cleanup terminates the process. Checked layout queries continue to report
invalid metadata with exceptions. Initialization, copying, and assignment may
still throw; mutable collection defaults remain independently allocated.

Assignment uses one API with optional external scratch:

```cpp
NomadIndex getAssignmentScratchSize() const;
void assignValue(
    const RuntimeValue* source,
    RuntimeValue* destination,
    RuntimeValue* scratch = nullptr
) const;
```

`getAssignmentScratchSize()` returns zero for values occupying zero or one slot,
and `getSize()` for larger values. Zero-slot assignment accesses no storage and
accepts null pointers. Single-slot assignments such as
`assignValue(&source, &destination)` use one local temporary without allocating
scratch on the heap.

For larger values, supply `getSize()` cleared, valid `RuntimeValue` slots.
Supplied scratch must be disjoint from source and destination; source and
destination may overlap, including self-assignment. Missing required scratch
raises `NomadException` with the type name and required slot count.

Assignment copies into scratch before releasing the destination, preserving the
previous value if copying fails. The prepared contents transfer into destination
storage, and scratch is cleared after success or successful failure cleanup.
Initialization and script constructors do not run during copying. If destination
cleanup throws, the prepared copy is also released and the error propagates;
the destination may already have been partially released.

This API manages existing slot storage; it does not reserve VM frames or enable
struct declarations. An already-owned staged return result can transfer directly
when aggregate execution is implemented, without an additional assignment copy.

Hosts overriding these functions must use the pointer signatures; existing
callers passing a `RuntimeValue` reference must instead pass its address.

Register a constructor through
`TypeManager::registerNativeClassConstructor(typeId, definition)`.
`NativeConstructorDefinition` supplies parameter definitions, a `function` callback, and
documentation. By default, the constructor creates its declaring type.
The overload taking `(typeId, createdTypeId, definition)` registers a constructor
that creates another registered public class type while retaining its declaring
owner. Existing constructor signature and reserved-name rules apply.
`ClassType::getConstructors()`
lists that type's registered constructors as borrowed `const Constructor*`
pointers, in registration order. These pointers remain valid until the runtime
is destroyed. A constructor returning another type is listed under its declaring
type, not its result type.

Scripts invoke constructors with `new` and the type's name:

```cpp
const auto typeId = runtime.registerClass("Example", nomad::ClassMode::Closed);
const auto registeredType = runtime.getType(typeId);
if (!registeredType) {
    throw nomad::NomadException("Failed to create Example type");
}
const auto constructorId = runtime.getTypeManager().registerNativeClassConstructor(typeId, {
    .parameters = {{runtime.getIntegerType()->getId(), "size", ""}},
    .function = [](nomad::VirtualMachine* machine) {
        machine->getResult().setObjectValue(new ExampleObject(machine->getIntegerParameter(0)));
    },
    .documentation = ""
});
if (constructorId.isInvalid()) {
    throw nomad::NomadException("Failed to register Example constructor");
}
```

Parameter types and the constructed type must be types registered with that runtime;
parameters cannot be `void`. Registration failures are logged and return `CallableId::INVALID`.
Constructors of one type may differ in parameter count or types. Duplicate parameter
signatures are rejected, even if their parameter names differ. Use an empty parameter
list for a parameterless constructor.

When a class creates a different kind of object, use
`addConstructor(typeId, createdTypeId, definition)`.
Different overloads of one class may then create different types. The created type must be a public
class type.

All IDs are local to the receiving runtime. Invalid or non-class owner/result IDs,
invalid parameter IDs, and missing callbacks are rejected before a constructor is
published. Definitions are owned inputs; modifying one after registration does not
change the registered constructor. `addConstructor` returns its `CallableId`.

Constructor parameters follow the same conventions as native function
parameters, including `$file`, `$function`, and `$line`.

The compiler checks argument types and count and uses the selected constructor's
created type. The callback uses the same `VirtualMachine*` calling convention as
native functions: read borrowed stack arguments through `getParameter(index)` or
typed accessors such as `getIntegerParameter(index)`. Store an object of the
created type with one owned reference in the VM result. Retain any argument
object stored in the new instance. A missing or null object result raises an
error. If the callback stores an object result and then throws, that result is
released. `NativeConstructor::getFn()` exposes the registered callback.
The former `factory`/argument-span contract is replaced by this callback API.
Validate value restrictions inside the callback: construction runs at evaluation
time in both compiled scripts and the interpreter.

For a constructor with type arguments, use
`TypeManager::registerNativeParameterizedConstructor(name, argumentCount, initializer)`. Its
initializer receives the specialized name and resolved type arguments; it registers
a type with that name and its constructors with `registerNativeClassConstructor`.
Specializations are reused on subsequent requests through
`TypeManager::getParameterizedConstructorName(name, arguments)`. The manager owns
the family registrations and specialization cache. Existing Runtime registration
and lookup methods remain compatibility wrappers. `getParameterizedConstructorNames`
appends registered family names to the supplied vector.

## Deferred native function signatures

Native functions can reference script types before those declarations have been
compiled. Pass a generated `typeReference()` to `Compiler::deferType`, then use
the returned `const DeferredType*` with ordinary `defParameter` and return-type
arguments. Concrete runtime types and placeholders can be mixed in one signature:

```cpp
using Settings = nomad::generated::log::log_SinkConfiguration;
const auto* settingsType = compiler.deferType(Settings::typeReference());
const auto id = runtime.registerNativeFunction(
    "isActive",
    [&bindings](nomad::VirtualMachine* machine) {
        const auto settings = Settings(bindings, machine->getParameter(0).getObjectValue());
        machine->setBooleanResult(settings.getActive());
    },
    {nomad::defParameter("configuration", settingsType, "")},
    runtime.getBooleanType(),
    ""
);
```

Registration immediately returns the permanent `CallableId` and publishes the
declaration's name and parameter names. `NativeFunction::isSignatureResolved()`
reports readiness. Before resolution, parameter and return pointers hold
compiler-owned placeholders; `getTypeName()` exposes their declared names.
Placeholders have invalid type IDs and are not published in the runtime's type
catalog. They cannot initialize, copy, free, or format values or provide storage.
Calling an unresolved native function or obtaining its callback throws.

Compilation resolves native signatures after script pre-parsing and the `parse`
callbacks, before parsing function bodies. This also happens in analysis mode,
without running callbacks or script initializers. Missing types, invalid types,
duplicate overload signatures, and incompatible overload sets produce compilation
diagnostics. Script-dependent signatures are not usable in earlier compiler
callbacks or compile-time expressions evaluated before this boundary.

Resolution preserves callable IDs and does not republish declarations on later
compilations. References resolve independently in each runtime. Hosts with already
declared types can explicitly call `Compiler::resolveDeferredTypes()`;
it throws on unresolved types or invalid overloads. Resolving a signature does not
compile a script constructor or resolve a generated wrapper's field binding.

The compiler owns `DeferredTypeTarget` objects, which expose the requested
`TypeReference`, a diagnostic description, and `resolve(const Type*)`. Register
additional targets with `Compiler::registerDeferredTypeTarget`; null targets and
empty type names are errors. Targets must use the supplied canonical runtime type
or throw if it is invalid for their destination. Their referenced destinations
must outlive compilation.

Native function parameter and return targets are collected automatically at the
resolution boundary, including functions registered by `parse` callbacks.
`FunctionParameterTypeTarget` identifies its destination by callable ID and
parameter index; `FunctionReturnTypeTarget` uses the callable ID. They never
retain addresses into a parameter vector. All requested names must resolve before
targets are applied, and native signatures and overload sets must validate before
callbacks become usable. Successful resolution releases the targets; compilation
failure aborts startup.

For custom native-function targets, `Runtime::getMutableNativeFunction(id)`
returns the destination or null for a non-native/invalid ID.
`resolveParameterType(index, type)` and `resolveReturnType(type)` assign only
matching canonical types belonging to that function's runtime. They reject
invalid indices, non-deferred slots, and completed signatures, and leave the
function uncallable. Use `Runtime::validateNativeFunctionSignatures()` after
applying targets to validate all pending signatures and overload sets before
marking them complete. `NativeFunction::completeSignatureResolution()` validates
the individual signature; it is the final
step used by runtime validation, not a substitute for overload-set validation.

`Compiler::deferType` reuses one placeholder per name in that compiler and rejects
empty names. Resolution replaces signature pointers with canonical runtime types;
it does not turn placeholders into forwarding types. All requested placeholder
names must exist, even if a placeholder has no targets. The compiler must outlive
pending declarations and custom targets; after successful resolution, native
signatures no longer depend on its placeholders. Failed compilation aborts startup.

Deferred signatures currently support free native functions, including hub
functions, through the VM-callback API. Native properties, methods, constructors,
and automatic C++ object/enum conversion in the typed callable adapter are not
included.

## Typed native functions

For primitive and string functions, `registerNativeFunction` can infer script
parameter and return types from a C++ callable:

```cpp
nomad::NomadInteger measure(const nomad::NomadString& text, nomad::NomadFloat scale) {
    return static_cast<nomad::NomadInteger>(text.size()) *
        static_cast<nomad::NomadInteger>(scale);
}

const auto id = runtime.registerNativeFunction(
    "measure",
    measure,
    {
        {"text", NomadParamDoc("The text to measure.")},
        {"scale", NomadParamDoc("The multiplier.")}
    },
    NomadDoc("Measures scaled text length.")
);
```

The typed overload omits the explicit return type. It extracts stack arguments,
calls the C++ function, and stores its result. Parameter names and documentation
remain explicit; there must be exactly one `TypedParameterDefinition` per C++
argument. Invalid counts, empty parameter names, and null function pointers are
logged and return `CallableId::INVALID` without publishing a function.

Use the corresponding
`registerHubNativeFunction(hubId, localName, callable, parameters, documentation)`
overload for a hub member. Both APIs share the existing callable catalog and
overload resolution with VM-callback registrations.

Supported C++ values are `NomadBoolean`, `NomadInteger`, `NomadFloat`, and
`NomadString`. Arguments may be passed by value or const lvalue reference.
Results are returned by value, or `void`. The types must match exactly: for
example, use `NomadInteger` rather than `int`, and `NomadFloat` rather than
`double`. Integer arguments and results preserve the full `NomadInteger` range.

Argument references are valid only for the duration of the callback; do not
retain their addresses. A string passed by value is a C++ copy that the callback
may modify without changing the script argument. String results are copied into
owned Nomad strings, including embedded null characters.

Free functions (including `noexcept`) and copyable non-generic lambdas, including
captured and mutable lambdas, are supported. Registration stores its own callable
by value; mutable capture state persists between calls. Reference captures remain
the host's responsibility and must outlive the registered callable.

Objects, enums, hidden source arguments, methods, and arbitrary C++ numeric
conversions are not inferred. Mutable-reference arguments, volatile-reference
arguments, rvalue-reference arguments, reference results, generic lambdas,
move-only callables, and member-function pointers are rejected at compile time.
Disambiguate overloaded C++ functions explicitly before registration.

Callback exceptions follow the existing VM-callback contract; the typed adapter
adds no exception translation or additional unwind cleanup. The existing
compiled-call fault path can retain formatted temporary strings when a native
callback throws.

Keep the existing VM-callback overload for bindings needing direct VM access or
explicit script types.

## Native object methods

Use `runtime.getTypeManager().registerNativeClassMethod(typeId, NativeMethodDefinition{...})`
for methods on a registered class type. Read the instance inside the callback:

```cpp
[](nomad::VirtualMachine* vm) {
    auto* object = vm->getThisAs<MyObject>();
    object->setValue(vm->getIntegerParameter(0));
}
```

Use `registerNativeFunction` for a global function and `addMethod` for a
method on a class type identified by its runtime-local `TypeId`.
Global function lookup and enumeration exclude methods.

`ClassType::getMethods()` lists the type's registered methods as borrowed
`const Method*` pointers, in registration order.
The pointers remain valid until the runtime is destroyed, even when additional
methods are registered. Use a method's `getId()` with Runtime's callable accessors.

`getThis()` returns the borrowed instance; `getThisAs<T>()` performs a static cast.
The host must pair the descriptor with the correct C++ class. Access without an
active method or with a null instance faults. Retain a reference explicitly if the
instance must outlive the call.

Object methods may mutate the instance through parameter and temporary references.
They do not replace the receiver binding or roll back changes to object contents.
Script-defined methods use the same receiver and argument ownership contracts.
To invoke a native or already compiled script method from the host, use
`Interpreter::runObjectMethod(receiver, methodId, arguments)` with owned typed
values. It returns an owned value instead of leaving a result in VM registers.
The prior active receiver and registers are restored after the call. Invalid
IDs, uncompiled methods, and incorrect argument counts raise
`std::invalid_argument` before execution. Use `runStructMethod` for struct
receivers and its independent receiver/result snapshots.

## Native indexers

Register an indexer with
`TypeManager::registerNativeClassIndexer(typeId, NativeIndexerDefinition{...})`.
`Runtime::addIndexer` remains a compatibility wrapper.
The definition supplies `valueTypeId`, a non-empty `std::vector<TypeId>` named
`indexTypes`, `getter`, `setter`, and `documentation`.
The index vector specifies the number and types of indices. All IDs are local
to the receiving runtime, and all value/index types must belong to that runtime
and be public, non-void types. An class type may register one indexer per index
count; lookup uses the receiver type and number of indices, not index-type
overloading.

Registration and `getNativeIndexerId(classType, indexCount)` return an
`IndexerId`, or `IndexerId::INVALID` on failure or missing lookup. Registration
failures are logged. Include `<nomad/script/NativeMember.hpp>` for the ID and
`NativeIndexerDefinition` and `NativeIndexerInfo`. Runtime retains the indexer
ID catalog and lookup operations. IDs are runtime-local and remain stable as more
indexers are registered. They do not implicitly convert to integers or other
ID categories. `getNativeIndexerInfo(id)` returns
`std::optional<NativeIndexerInfo>`, empty for invalid or unregistered IDs.

Invalid or non-class owner IDs, invalid value/index IDs, and definitions with
neither callback are rejected without publishing an indexer. Definitions are
owned inputs; modifying one after registration does not change the registered
indexer.

```cpp
const auto info = runtime.getNativeIndexerInfo(id);
if (info) {
    // Inspect the detached metadata snapshot through info->...
}
```

The class type owns each indexer's metadata. `ClassType::getNativeIndexer(indexCount)`
returns a borrowed `const NativeIndexerInfo*`, or `nullptr` when absent.
`ClassType::getNativeIndexers()` enumerates that type's indexers, while
`Runtime::getNativeIndexers()` enumerates the runtime-wide borrowed catalog in
registration order. Both enumerate const metadata pointers; these pointers
remain valid as more indexers are registered, until the runtime destroys their
owning types. `getNativeIndexerInfo` returns a metadata copy.

Hosts retaining a mutable registered class type may also call
`ClassType::registerNativeIndexer(runtime, info)`. This existing low-level
operation accepts `NativeIndexerInfo`; supply an invalid ID and that object
type as `info.classType`. The same registration rules
apply; foreign-runtime types, preassigned IDs, and duplicate index counts are
rejected without changing the owner or catalog.

Omit the setter for read-only indexing, or omit
the getter for write-only indexing. Invalid access is rejected during compilation.

```cpp
const auto indexerId = runtime.getTypeManager().registerNativeClassIndexer(classType->getId(), {
    .valueTypeId = runtime.getStringType()->getId(),
    .indexTypes = {runtime.getIntegerType()->getId()},
    .getter = [](nomad::VirtualMachine* machine) {
        const auto* object = machine->getThisAs<MyObject>();
        machine->setStringResult(object->getText(machine->getIntegerParameter(0)));
    },
    .setter = {},
    .documentation = NomadDoc("Read text at the given index.")
});
if (indexerId.isInvalid()) {
    throw nomad::NomadException("Failed to register text indexer");
}
```

Callbacks use the native-method calling convention: `getThisAs<T>()` accesses
the receiver and `getParameter(i)` or a typed parameter accessor reads index
`i`. For N indices, the setter reads the assigned value from parameter N.
The receiver is evaluated first, then arguments are evaluated and pushed
right-to-left, just like function arguments. For assignment, the value is the
last parameter and is therefore evaluated before the indices.

Callbacks borrow the receiver and parameters. A getter writes an
owned result through the VM's result setters; retain any existing object returned from
the receiver. A setter must retain an object value it stores and release any
replaced value; it returns `void` and must not write a result.
Parameter and receiver frames are restored after the callback, including on
failure. Use `machine->fault(...)` to report invalid indices or other
access errors.

## Typed native object methods

Built-in string methods use the same object-method API. Read the instance with
`getThisAs<Object>()`:

```cpp
const auto methodId = runtime.addMethod(runtime.getTypeId("string"), {
    .name = "textSize",
    .parameters = {},
    .returnTypeId = runtime.getTypeId("int"),
    .function = [](nomad::VirtualMachine* machine) {
        const auto* instance = machine->getThisAs<const nomad::NomadStringObject>();
        machine->setIntegerResult(static_cast<nomad::NomadInteger>(instance->getLength()));
    },
    .documentation = NomadDoc("Return the number of bytes in the string.")
});
if (methodId.isInvalid()) {
    throw nomad::NomadException("Failed to register textSize method");
}
```

The callback borrows the receiver for the duration of the call. Keeping it
requires `addRef()` and a corresponding `releaseRef()`.

The accessor uses `static_cast`, with no runtime C++ type check. The host must
pair the registered Nomad type with its correct C++ object class and provide a
valid receiver when invoking methods directly. Script type checking selects the
method for the receiver's Nomad type; it cannot validate the host's C++ pairing.
An incorrect pairing causes undefined behaviour.

Use `getThisAs<const Object>()` to obtain a pointer to const.
Calling the accessor outside an active method faults.

## Iterable objects

Hosts can make an object usable in `for value in source` without collection-specific
compiler support:

1. Derive a cursor from `IteratorObject`, constructed with the registered element
   type. Implement `onMoveNext(RuntimeValue&)`: return `true` after writing one
   owned element into its empty output, or `false` at completion. The cursor must
   retain any source objects it needs and release them when destroyed.
2. Obtain the cursor's script type with
   `runtime.getTypeManager().getIteratorType(elementType)`.
   It exposes parameterless `moveNext` and the read-only `current` property.
   Reading `current` before a successful advance or after completion faults.
3. Use `TypeManager::registerNativeClassIterable` to register a parameterless native factory method and its
   association with the source type. The factory returns a fresh cursor of that
   iterator type. Its name is not prescribed:

```cpp
runtime.getTypeManager().registerNativeClassIterable(sourceType->getId(), {
    .valueTypeId = elementType->getId(),
    .factory = {
        .name = "iterator",
        .parameters = {},
        .returnTypeId = runtime.getTypeManager().getIteratorType(elementType)->getId(),
        .function = createIterator,
        .documentation = ""
    }
});
```

`NativeInterableDefinition`, declared in `<nomad/script/NativeMember.hpp>`,
supplies the element type ID and a
`NativeMethodDefinition` for the factory. All IDs are runtime-local. The source
must be a class type, and the element type must be registered, public, and
non-void. The factory must have a callback, take no arguments (including
compiler-supplied parameters), and return the matching iterator type.
Duplicate associations, conflicting member names, and invalid registrations throw
`NomadException`. Invalid definitions do not publish a factory or association;
resolving the iterator type may create its registered specialization.
The runtime owns the factory metadata and callback; changing the input definition
after registration does not change the registered factory.
`TypeManager::getIterableInfo(sourceType)` returns an optional association
containing its factory ID. Runtime's `addIterable`, `getIteratorType`, and
`getIterableInfo` remain compatibility wrappers.

The source `ClassType` owns its iterable association. Include
`<nomad/script/ClassType.hpp>` for `IterableInfo`.
`ClassType::getIterableInfo()` returns an optional snapshot containing
the element type, iterator type, and factory `CallableId`. Runtime lookup returns
the same snapshot, or an empty optional for a non-iterable or foreign-runtime type.
Hosts retaining a mutable registered class type may call
`ClassType::registerIterableType(runtime, elementType, factory)` directly;
the same ownership, signature, and duplicate-registration checks apply. The
already registered factory must belong to the source type and take no arguments,
and its return type must match the iterator type for the element type.
Neither lookup permits changing the registered association.

Each factory invocation must create independent iteration state. An iterator
owns its published value; consumers receive normal typed copies, including
reference ownership for object values. Iteration state is released when iteration
completes, a loop exits with `break`, or the function returns.

## Member declarations for parameterized type families

Declare member parsing signatures before compiling scripts that use inferred
constructors. This lets scripts access members before any concrete specialization
has been created:

```cpp
if (!runtime.getTypeManager().registerMemberTemplate("KeyValue", "key", nomad::MemberType::Property) ||
    !runtime.getTypeManager().registerMemberTemplate("KeyValue", "value", nomad::MemberType::Property)) {
    throw nomad::NomadException("Failed to register KeyValue member signatures");
}
```

The family must already be registered with `registerParameterizedType`. A property
declaration takes no parameters. For methods, supply `TemplateParameter` entries
using concrete types or `TypeParameterIndex`:

```cpp
if (!runtime.getTypeManager().registerMemberTemplate(
    "Box", "store", nomad::MemberType::Method,
    {nomad::defTemplateParameter("value", nomad::TypeParameterIndex{0}, "")}
)) {
    throw nomad::NomadException("Failed to register Box.store signature");
}
```

Declarations do not create types, executable members, or callbacks. Continue to
register each specialization's concrete members in its initializer. Return types,
property access permissions, and argument type checking come from those concrete
members.

Member declarations are source-independent. Hosts register them today; a future
script-type declaration pass can use the same API before parsing member bodies.
This API does not add execution support for script-defined types.

All members sharing a name must agree on parameter count and parser-dependent
parameter types, including callbacks and hidden source parameters. A property and
a zero-parameter method have the same parsing shape, as with concrete members.
Generic method parameters parse as ordinary value expressions.
Incompatible declarations and concrete registrations are rejected with a logged
error. Duplicate declarations within a family are also rejected.

`getMemberSignature(name)` returns a copy of the parsing signature, whether
declared by a family or provided by a concrete member. Generic parameter types are
represented by null pointers in this parsing signature. Existing member-ID and
overload queries continue to return only concrete members.

## Native object properties

Use `addProperty(typeId, NativePropertyDefinition{...})`.
Either callback may be empty for a write-only or read-only property. Both
callbacks access the borrowed instance through `getThisAs<T>()`.

The getter's `result` is write-only output storage. Do not read or release its
previous contents. Return an owned value by assigning it or using a value setter.
For a typed copy, use `Type::assignValue` into a fresh local `RuntimeValue`, then
assign that local to `result`; this retains object references without reading the
output's previous contents.

```cpp
const auto propertyId = runtime.addProperty(typeId, {
    .valueType = runtime.getFloatType(),
    .name = "value",
    .getter = [](nomad::VirtualMachine* vm, nomad::RuntimeValue& result) {
        result.setFloatValue(vm->getThisAs<MyObject>()->getValue());
    },
    .setter = [](nomad::VirtualMachine* vm, const nomad::RuntimeValue& value) {
        vm->getThisAs<MyObject>()->setValue(value.getFloatValue());
    },
    .documentation = NomadDoc("Instance value.")
});
if (propertyId.isInvalid()) {
    throw nomad::NomadException("Failed to register value property");
}
```

Members require a registered class type. Property setters mutate that instance,
not the variable holding its reference.

## Object reference helpers

`nomad::addRef(object)` and `nomad::releaseRef(object)` accept null pointers.
Use `current = nomad::replace(current, next)` to retain a replacement before
releasing the previous reference; self-replacement does not change the count.
The returned pointer must be assigned by the caller. This shares `next` rather
than adopting its existing reference.

Use `value.asObject<T>()` to read a borrowed, typed object pointer from a
`RuntimeValue`. It preserves null and does not change the reference count.
Like `getThisAs<T>()`, it relies on the host supplying the correct C++ class.

DLLs must use compatible C++ ABI settings and remain loaded while their callbacks
or objects are alive.

## Variable categories

| Category | Read | Assign | Change type | Add new |
|---|---|---|---|---|
| Parameters, including existing captures | Yes | No | No | No |
| Frame variables | Yes | Yes | No | No |
| Interpreter variables | Yes | Yes | Yes | Yes |

Assignment to a new unqualified name creates an interpreter variable in the
context. Existing parameters and frame variables cannot be shadowed through
assignment. Unknown qualified names report an error rather than creating an
interpreter variable.

Runtime context variables such as `global.score` and engine-defined dynamic
variables are separate from interpreter variables. They retain their runtime
access and type rules.

## Evaluating against a script frame

During execution of a script instruction, construct a frame-bound context:

```cpp
const auto* callable = machine.getCurrentCallable();
if (callable == nullptr) {
    throw nomad::NomadBug("An active script callable is required");
}
const nomad::Interpreter interpreter(machine);
nomad::InterpreterContext context(interpreter, *callable);
const auto succeeded = interpreter.execute("value + local", context);
```

The context borrows live local and parameter values, not copies. Writes to frame
variables update the originating frame. Functions, methods, and constructors
are supported; member frames also expose their receiver as `this`.
New interpreter variables stay in the context and never change the compiled
callable's locals, parameters, or captures. `context.getCallable()` exposes
context-owned resolution metadata representing both sets of variables.
`getCallable()` has mutable and const overloads, both validating any borrowed
script frame before access. The mutable overload is intended for parsing and
resolving interpreter input. The callable remains owned by the context and is
not registered with the runtime. Access through an invalidated frame-backed
context throws `NomadBug`, including read-only access.
`context.getFunction()` remains function-only and returns `nullptr` for member
frames. `machine.getCurrentFunction()` likewise remains function-only.

Construct this context while the script's parameter frame is active, not inside
a native call that has replaced it with native arguments. The context must not
outlive the script frame or be used as another frame's context. Nested calls
restore the originating VM frame and result/intermediate registers.

Names from enclosing functions are available only if already captured by the
compiled function. Interpretation does not introduce new compiled captures.

## Errors and execution

Syntax and resolution errors are recorded in the context and return `false`.
Assignment validation happens before evaluating its right-hand side.
`execute` also reports execution exceptions through the context, clears any
partial result, and returns `false`. Direct typed host calls such as
`runFunction` and `runObjectMethod` retain their exception contracts.

An execution fault is fatal even when `execute` reports it through the context
rather than throwing. Do not continue evaluating code on the faulted VM.
Syntax and resolution errors do not have this restriction.

Native and script calls use the caller-supplied VM. The host must initialize the
runtime and any engine execution context required by its registered APIs.
Context-owned typed values manage their own string lifetimes.

Binary operands are evaluated left-first in both compiled and interpreted
expressions. Logical operators retain their short-circuit behavior. In
particular, the left value is retained before a right-hand function call can
change its source variable.

`CompilerMode::Script` selects compiled script resolution; `CompilerMode::Interpreter`
selects interpreted resolution without changing compiled function definitions.
Console and frame evaluation do not require separate compiler modes.

## Dynamic format strings

The script format string operator `$expression` and interpreted format literals use the same
runtime template evaluator. C++ hosts can call `Interpreter::formatString` with a template, context, source
name, line, and column. The existing free `formatDynamicString` function
(declared in `nomad/script/FormatString.hpp`) delegates to the context's interpreter
and accepts the same arguments.
Its `DynamicFormatResult` contains partial text plus structured diagnostics for
that template. Each diagnostic includes a message, source location, and zero-based
byte offset and length in the template. Diagnostics are also logged; nested
templates emit their own diagnostics.

Placeholders require exactly one non-void expression. Assignments, statements,
and multiline input are rejected before execution. Placeholder evaluation retains
the supplied source line for resolution and diagnostics; these restrictions do
not apply to general source input passed to `execute`.

Syntax and resolution errors become inline markers and processing continues after
a closed bad placeholder. Execution exceptions from called code propagate,
including native expression calls. The console `execute` entry point still
reports execution exceptions in the context. No template or evaluated-value cache
is used.
