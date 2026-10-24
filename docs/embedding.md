# Embedding Nomad in C++

Nomad's scripting language can be embedded without the game engine. When building
as part of the Nomad source tree, link your host target to `Nomad::Language`.
See [building from source](building.md) for toolchain requirements and the
[language guide](language.md) for script syntax.

## Ownership

The host owns the `Runtime`, its virtual machines, and interpreter contexts.
Destroy contexts before their borrowed VM, and destroy VMs before the runtime.
Types and callables used by a context must remain valid.

- `Runtime` owns registered APIs, types, callables, and bytecode.
- `VirtualMachine` executes compiled functions and provides the execution
  environment for native calls, object members, and dynamic variables.
- `InterpreterContext` borrows a VM and owns interpreter variables, its latest
  typed result, and its latest error. It can additionally borrow a script frame.
- `Interpreter` is stateless. Pass a context to each execution; keep that context
  to retain interpreter variables between executions.

Neither the interpreter nor its context creates or owns a VM.

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

Include `<nomad/script/NativeMemberDefinitions.hpp>` for `ParameterDefinition`,
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

These input structures do not register anything when constructed. The existing
`registerNativeMethod`, `registerNativeConstructor`, `registerNativeProperty`,
`registerNativeIndexer`, and `registerIterableType` operations retain their
current argument forms; they do not yet accept the new input structures.
Their metadata structures are now named `NativePropertyInfo`,
`NativeIndexerInfo`, and `IterableInfo`. Corresponding lookup operations are
`getNativePropertyInfo`, `getNativeIndexerInfo`, and `getIterableInfo`;
`NativeProperty::getInfo()` returns a property snapshot.

### Type lookup

`Runtime::getTypeId(name)` returns a `TypeId`, or `TypeId::INVALID`
when the type is not registered. Pass it to `getType(id)` to retrieve an optional
`const Type*`; invalid or unregistered IDs return an empty optional. Type IDs
belong to the runtime that issued them and remain stable as more types are
registered. They do not implicitly convert to integers or other ID categories.
Event parameter definitions use `TypeId` for their `typeId` field.

Strong IDs are invalid when default-constructed. Use `isValid()` and
`isInvalid()` to check their numeric validity; these checks do not verify that
the ID belongs to a particular runtime or is registered. `IdHash` supports
strong IDs as keys in standard unordered containers.
Streaming an ID prints its type and numeric value, for example `CallableId{42}`
or `CallableId{-1}`.


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

Parameter positions, capture-list positions, and stack offsets remain numeric.
Compiler identifier metadata separates `variableId` from `parameterId`.
Capture metadata retains `CaptureSource`; read `variableId` only for
`CaptureSource::Variable`, and `parameterId` only for `CaptureSource::Parameter`.
Script-callable `createCapture` overloads take either a `VariableId` for a
variable source or a numeric `NomadId` for a parameter source, followed by the
name and type. `flagVariableAsCaptured` and `flagParameterAsCaptured` provide
the corresponding source-specific registration APIs.

### Object type property metadata

`ScriptObjectType` describes a script-defined object type. `isOpen()` reports
whether external script declarations may extend it. Closed types are not immutable.

Register the owner type with the runtime before declaring its properties.
`Runtime::registerProperty(objectType, name, valueType)` declares a stored/script
property and returns a `PropertyId`, or `PropertyId::INVALID` on failure.
The value type must also be registered with the same runtime and must be public
and non-void. Repeating a declaration with the same name and type returns its
existing ID. Conflicting declarations are logged and rejected without changing
the type's properties. Host declarations are permitted on both open and closed
types; openness controls whether scripts may add external declarations.

`Runtime::getProperty(id)` returns metadata for native or script properties;
`getNativeProperty(id)` returns only native properties. Invalid or unregistered
IDs return `nullptr`. Include `<nomad/script/Property.hpp>` for `PropertyId`
and common metadata, and `<nomad/script/NativeProperty.hpp>` for native metadata.
Use `getId()`, `getObjectType()`, `getName()`, `getValueType()`, and `getDoc()`
to inspect a property.
`ObjectType::getProperty(name)`, `getProperty(id)`, and `getProperties()` expose
the owner's properties as borrowed const pointers. IDs and pointers remain
stable as more properties are registered and are valid for the owning runtime's
lifetime. Property IDs are runtime-wide, not per-object field offsets.

This API describes type properties. Script class syntax and stored/script property
access from scripts are not yet implemented.

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
Script methods and script constructors are not yet supported.

Use `resolveFunction(name, argumentTypes)`, `resolveMethod(objectType, name,
argumentTypes)`, or `resolveConstructor(objectType, argumentTypes)` to select an
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

`registerNativeProperty` returns a `PropertyId`; `registerNativeMethod` returns
the method's `CallableId`. Their failure results are `PropertyId::INVALID` and
`CallableId::INVALID`, respectively.
`getNativeMemberId` returns a `MemberReference`, a `std::variant<PropertyId, CallableId>`.
An absent member is represented by `PropertyId::INVALID`.
Member enumeration and overload APIs use `std::vector<MemberReference>`.
`getMemberType(member)` returns `MemberType::Property`, `MemberType::Method`,
or `MemberType::Unknown` for an invalid reference.
Use `std::get_if` to inspect a reference's ID category.
`getNativePropertyInfo(member, definition)` returns a native property
snapshot, or `false` when the reference is invalid or not a native property.
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
the callable is native or script-backed. Script enumeration and source/bytecode
lookups continue to include only script functions.

Include `<nomad/script/Callable.hpp>` for common callable metadata,
`<nomad/script/NativeCallable.hpp>` for native implementations, and
`<nomad/script/ScriptCallable.hpp>` for script implementations.

## Module configuration

Read module settings through `NomadConfiguration` rather than parsing its serialized
representation. Typed getters return an empty optional for missing fields and
throw `NomadConfigurationException` for incorrect types. Required getters also
reject missing fields. Integer and string arrays are checked element by element.
`validateSection(path)` permits an absent section but rejects an existing scalar
or array at that path.

Modules supply their own defaults and validate constraints such as positive
dimensions and valid resource paths. The configuration API keeps the storage
format out of module code.

Project loading, discovery, compatibility checks, and module configuration
validation report configuration failures through `NomadConfigurationException`.
Hosts can handle these failures with a single exception type.

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

Call `Runtime::registerHub(name)` to reserve an open name prefix before
compilation. It returns the new `HubId`, or the existing ID for repeated
registration. Dotted names, such as `"game.audio"`, also register their
parent hubs. Native hub names need not follow script identifier syntax and may
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
qualified name, parent `HubId` (`HubId::INVALID` for a root hub), and locally
named members. Child hub members contain a `HubId`. Function members refer
to registered callables; types and events refer to their registered definitions.
Hub pointers remain valid when more hubs are registered.

`HubMember` stores variables, constants, events, child hubs, and dynamic
variables directly as `VariableId`, `ConstantId`, `EventId`, `HubId`, and
`DynamicVariableId`. Type members store a borrowed `const Type*`.
Use `std::get_if<ConstantId>(member)` or the corresponding alternative to
inspect a member, then pass the ID directly to its typed API. Function overloads
use `HubFunctions::ids`; generic families use `HubTypeFamily::argumentCount`.

Register native APIs under their qualified names using the existing runtime
registration functions. For example, register hub `"os"`, enum `"os.Platform"`,
and constant `"os.platform"`. Scripts may reopen native hubs and add distinct
members.

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

The compiler consumes source text, not filesystem paths. `SourceText` owns a
`sourceName` (used in diagnostics), `text`, and an optional `filePath`. The name
does not need to exist on disk and is never interpreted as a path. `filePath`
identifies a backing file, not a request to read it: the supplied text is always
authoritative, including unsaved editor changes and files not yet created.
Untitled documents and interpreter input can leave it unset.
`ScriptSource` pairs that text with an explicit `scriptName`. This is the
source-level name used to register the script's compiled runtime function.
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

File-backed overrides match by absolute, lexically normalized `filePath`, not by
`scriptName` or `sourceName`. Relative paths use the current working directory.
Matching is case-insensitive on Windows and case-sensitive elsewhere. This is a
lexical policy: it does not resolve symbolic links, hard links, or filesystem
case-sensitivity settings (including case-sensitive Windows directories).
The caller decodes document URIs into filesystem paths.

A discovered file keeps its disk-derived script name and takes the override's
complete `SourceText`, including its diagnostic name and backing path. Unmatched
overrides, including untitled and not-yet-saved scripts, are added using their
explicit script names. The caller is responsible for project membership and
naming those scripts; overrides are not restricted to the supplied disk roots.
Duplicate file identities or duplicate untitled `sourceName` values produce
loading errors, not last-write-wins behavior. Different documents with the same
script name remain ordinary compiler collisions.

```cpp
const std::vector<nomad::ScriptSource> buffers{
    {"entities.player", {"editor:player", unsavedText, playerPath}}
};
const auto batch = nomad::loadSourcePaths(scriptRoots, buffers);
const auto result = nomad::checkSources(batch);
```

The loader returns owned, sorted snapshots without changing the disk or buffers.
Rebuild a batch with the latest buffers for each analysis; remove an override
when its document closes to use disk text again. Document versions, project
association and stale-analysis rejection belong to the editor adapter, not the
compiler. `checkSources(batch, runtime)` from
`<nomad/compiler/CompilerTools.hpp>` returns the same `CompilationResult` as
`checkPaths`, without reading files. Loading warnings permit compilation; loading
errors stop it before parsing.

## Persistent interpretation

```cpp
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <iostream>

void interpretInput() {
    nomad::Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    nomad::InterpreterContext context(*machine);
    const nomad::Interpreter interpreter;

    if (!interpreter.execute("score = 10", context) ||
        !interpreter.evaluateExpression("score + 5", context)) {
        std::cerr << *context.getError() << '\n';
        return;
    }
    const auto* result = context.getResult();
    std::cout << result->getType()->toString(result->getValue()) << '\n';
}
```

`execute(source, context)` accepts assignments, supported function calls, and
multiline `if`, `while`, `until`, `do`, and `for` statements. It also accepts a
single expression. `evaluateExpression(source, context)`
requires exactly one non-void expression; assignments and statements are rejected.
Each entry point clears the context's previous result and error.

Both entry points also accept `SourceText`, using its name for input diagnostics:

```cpp
const nomad::SourceText input{"console:input", "score + 5"};
const auto succeeded = interpreter.evaluateExpression(input, context);
```

The string overloads preserve the existing context source name. Interpretation
continues to share the compiler's tokenizer, parser, and type resolver; it does
not register input as a compiled function. Declarations, `return`, assertions,
and inline callbacks remain unsupported by the interpreter. Loops execute
synchronously on the caller's thread.

Loop bindings are ordinary mutable locals or compatible parameters and remain
available after the loop. Frame-backed interpretation can reassign script
parameters without replacing the caller's arguments; their declared types remain
fixed. Callback captures retain the values present when the callback was created.

The `SourceText` expression overload optionally accepts a one-based source line,
so expressions embedded in a larger input retain their scope and diagnostic
location.

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

Results are owned by the context. Copy a result using `TypedValue` if it must
survive another evaluation; do not free the context's result manually.
`listVariables()` lists interpreter-owned variables, not borrowed frame slots.
Different contexts have independent interpreter variables even when borrowing
the same VM. Access to a shared context or VM must be serialized by the host.

## Parameterized types

`Runtime::registerParameterizedType(name, argumentCount, factory)` registers a
type family. The factory receives resolved type arguments and returns a
runtime-owned descriptor. `getParameterizedType(name, arguments)` registers a
specialization on first use and reuses it on subsequent requests. Arguments must
be non-void, public types owned by that runtime. Invalid requests are logged and
return null.

`getParameterizedTypeArgumentCount(name)` returns the registered family's arity,
or an empty optional if the family is not registered.

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
if (!runtime.registerConstructorTemplate(
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
constructor with `registerNativeConstructor`, usually in the type-family
initializer. Compiled scripts and the interpreter both support inferred
construction through the existing native constructor execution paths.

### Native object types and constructors

`ObjectType` describes an object type and its type arguments. Its default value
is null, and its text representation is its type name. Specialized types may
provide other default values and formatting.

`Type::initValue(value)` initializes a fresh value with the type's default.
Implement it when defining a type descriptor, and release initialized values
through `freeValue` when they are no longer needed.

Register a constructor through `Runtime::registerNativeConstructor` with the type
it constructs, its parameters, and a factory. `ObjectType::getConstructors()`
lists that type's registered constructors as borrowed `const Constructor*`
pointers, in registration order. These pointers remain valid until the runtime
is destroyed. A constructor returning another type is listed under its declaring
type, not its result type.

Scripts invoke constructors with `new` and the type's name:

```cpp
auto descriptor = std::make_unique<nomad::ObjectType>("Example");
const auto* type = descriptor.get();
runtime.registerType(std::move(descriptor));
const auto constructorId = runtime.registerNativeConstructor(
    type,
    {nomad::defParameter("size", runtime.getIntegerType(), "")},
    [](std::span<const nomad::RuntimeValue> arguments) {
        return new ExampleObject(arguments[0].getIntegerValue());
    },
    ""
);
if (constructorId.isInvalid()) {
    throw nomad::NomadException("Failed to register Example constructor");
}
```

Parameter types and the constructed type must be types registered with that runtime;
parameters cannot be `void`. Registration failures are logged and return `CallableId::INVALID`.
Constructors of one type may differ in parameter count or types. Duplicate parameter
signatures are rejected, even if their parameter names differ. Use an empty parameter
list for a parameterless constructor.

When a class creates a different kind of object, pass the created type after the
parameters: `registerNativeConstructor(type, parameters, createdType, factory, doc)`.
Different overloads of one class may then create different types. The created type must be a public
object type.

Constructor parameters follow the same conventions as native function
parameters, including `$file`, `$function`, and `$line`.

The compiler checks argument types and count and uses the selected constructor's
created type. The factory receives borrowed arguments in parameter order
and returns an object of that type with one owned reference. Retain any argument
object stored in the new instance. A null result raises an error.
Validate value restrictions inside the factory: construction runs at evaluation
time in both compiled scripts and the interpreter.

For a constructor with type arguments, use
`registerNativeParameterizedConstructor(name, argumentCount, initializer)`. Its
initializer receives the specialized name and resolved type arguments; it registers
a type with that name and its constructors with `registerNativeConstructor`.
Specializations are reused on subsequent requests through
`getParameterizedConstructorName(name, arguments)`.

## Native object methods

Use `Runtime::registerNativeMethod(objectType, name, callback, parameters, returnType, doc)`
for methods on an `ObjectType` descriptor. Read the instance inside the callback:

```cpp
[](nomad::VirtualMachine* vm) {
    auto* object = vm->getThisAs<MyObject>();
    object->setValue(vm->getIntegerParameter(0));
}
```

Use `registerNativeFunction` for a global function and `registerNativeMethod` for a
method on a non-null object type.
Global function lookup and enumeration exclude methods.

`ObjectType::getMethods()` lists the type's registered methods as borrowed
`const Method*` pointers, in registration order.
The pointers remain valid until the runtime is destroyed, even when additional
methods are registered. Use a method's `getId()` with Runtime's callable accessors.

`getThis()` returns the borrowed instance; `getThisAs<T>()` performs a static cast.
The host must pair the descriptor with the correct C++ class. Access without an
active method or with a null instance faults. Retain a reference explicitly if the
instance must outlive the call.

Object methods may mutate the instance through parameter and temporary references.
They do not replace the receiver binding or roll back changes to object contents.
Script-defined methods are not supported yet.

## Native indexers

Register an indexer with `registerNativeIndexer(objectType, indexTypes,
valueType, getter, setter, doc)`. The non-empty `std::vector<const Type*>`
specifies the number and types of indices. All types must belong to the runtime
and be public, non-void types. An object type may register one indexer per index
count; lookup uses the receiver type and number of indices, not index-type
overloading.

Registration and `getNativeIndexerId(objectType, indexCount)` return an
`IndexerId`, or `IndexerId::INVALID` on failure or missing lookup. Registration
failures are logged. Include `<nomad/script/NativeMember.hpp>` for the ID and
`NativeIndexerInfo`. IDs are runtime-local and remain stable as more
indexers are registered. They do not implicitly convert to integers or other
ID categories. `getNativeIndexerInfo(id, definition)` returns `false` for
invalid or unregistered IDs.

The object type owns each indexer's metadata. `ObjectType::getNativeIndexer(indexCount)`
returns a borrowed `const NativeIndexerInfo*`, or `nullptr` when absent.
`ObjectType::getNativeIndexers()` enumerates that type's indexers, while
`Runtime::getNativeIndexers()` enumerates the runtime-wide borrowed catalog in
registration order. Both enumerate const metadata pointers; these pointers
remain valid as more indexers are registered, until the runtime destroys their
owning types. `getNativeIndexerInfo` returns a metadata copy.

Hosts retaining a mutable registered object type may also call
`ObjectType::registerNativeIndexer(runtime, info)`. This existing low-level
operation accepts `NativeIndexerInfo`; supply an invalid ID and that object
type as `info.objectType`. The same registration rules
apply; foreign-runtime types, preassigned IDs, and duplicate index counts are
rejected without changing the owner or catalog.

Omit the setter for read-only indexing, or omit
the getter for write-only indexing. Invalid access is rejected during compilation.

```cpp
runtime.registerNativeIndexer(
    objectType,
    {runtime.getIntegerType()},
    runtime.getStringType(),
    [](nomad::VirtualMachine* machine) {
        const auto* object = machine->getThisAs<MyObject>();
        machine->setStringResult(object->getText(machine->getIntegerParameter(0)));
    },
    {},
    NomadDoc("Read text at the given index.")
);
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
runtime.registerNativeMethod(
    runtime.getStringType(),
    "textSize",
    [](nomad::VirtualMachine* machine) {
        const auto* instance = machine->getThisAs<const nomad::NomadStringObject>();
        machine->setIntegerResult(static_cast<nomad::NomadInteger>(instance->getLength()));
    },
    {},
    runtime.getIntegerType(),
    NomadDoc("Return the number of bytes in the string.")
);
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
2. Obtain the cursor's script type with `runtime.getIteratorType(elementType)`.
   It exposes parameterless `moveNext` and the read-only `current` property.
   Reading `current` before a successful advance or after completion faults.
3. Register a parameterless native method on the source type that returns a fresh
   cursor of that iterator type. The method name is not prescribed.
4. Register the association:

```cpp
const auto memberId = runtime.registerNativeMethod(
    sourceType, "iterator", createIterator, {},
    runtime.getIteratorType(elementType), ""
);
runtime.registerIterableType(
    sourceType, elementType, runtime.getNativeMemberMethod(memberId)
);
```

Pass a registered `Method` to `registerIterableType`; it does not accept a native
member ID. The factory must belong to the source type, take no arguments, and return the
matching iterator type. Duplicate associations and invalid registrations throw
`NomadException`. `getIterableInfo(sourceType)` returns an optional association.

The source `ObjectType` owns its iterable association. Include
`<nomad/script/Type.hpp>` for `IterableInfo`.
`ObjectType::getIterableInfo()` returns an optional snapshot containing
the element type, iterator type, and factory `CallableId`. Runtime lookup returns
the same snapshot, or an empty optional for a non-iterable or foreign-runtime type.
Hosts retaining a mutable registered object type may call
`ObjectType::registerIterableType(runtime, elementType, factory)` directly;
the same ownership, signature, and duplicate-registration checks apply.
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
if (!runtime.registerMemberTemplate("KeyValue", "key", nomad::MemberType::Property) ||
    !runtime.registerMemberTemplate("KeyValue", "value", nomad::MemberType::Property)) {
    throw nomad::NomadException("Failed to register KeyValue member signatures");
}
```

The family must already be registered with `registerParameterizedType`. A property
declaration takes no parameters. For methods, supply `TemplateParameter` entries
using concrete types or `TypeParameterIndex`:

```cpp
if (!runtime.registerMemberTemplate(
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

Use `registerNativeProperty(objectType, name, valueType, getter, setter, doc)`.
Either callback may be empty for a write-only or read-only property. Both
callbacks access the borrowed instance through `getThisAs<T>()`.

The getter's `result` is write-only output storage. Do not read or release its
previous contents. Return an owned value by assigning it or using a value setter.
For a typed copy, use `Type::assignValue` into a fresh local `RuntimeValue`, then
assign that local to `result`; this retains object references without reading the
output's previous contents.

```cpp
runtime.registerNativeProperty(
    objectType, "value", runtime.getFloatType(),
    [](nomad::VirtualMachine* vm, nomad::RuntimeValue& result) {
        result.setFloatValue(vm->getThisAs<MyObject>()->getValue());
    },
    [](nomad::VirtualMachine* vm, const nomad::RuntimeValue& value) {
        vm->getThisAs<MyObject>()->setValue(value.getFloatValue());
    },
    NomadDoc("Instance value.")
);
```

Members require a registered object type. Property setters mutate that instance,
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
auto* function = machine.getCurrentFunction();
if (function == nullptr) {
    throw nomad::NomadBug("An active script function is required");
}
nomad::InterpreterContext context(machine, *function);
const nomad::Interpreter interpreter;
const auto succeeded = interpreter.evaluateExpression("value + local", context);
```

The context borrows live local and parameter values, not copies. Writes to frame
variables update the originating frame. New interpreter variables stay in the
context and never change the compiled function's locals, parameters, or captures.
Resolution uses context-owned metadata representing both sets of variables.

Construct this context while the script's parameter frame is active, not inside
a native call that has replaced it with native arguments. The context must not
outlive the script frame or be used as another frame's context. Nested calls
restore the originating VM frame and result/intermediate registers.

Names from enclosing functions are available only if already captured by the
compiled function. Interpretation does not introduce new compiled captures.

## Errors and execution

Syntax and resolution errors are recorded in the context and return `false`.
Assignment validation happens before evaluating its right-hand side.
`execute` also reports execution exceptions through the context.
`evaluateExpression` propagates execution faults from called code; it does not
turn arbitrary function failures into malformed-template diagnostics.

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
runtime template evaluator. C++ hosts can call `formatDynamicString` (declared in
`nomad/script/FormatString.hpp`) with a template, context, source name, line, and column.
Its `DynamicFormatResult` contains partial text plus structured diagnostics for
that template. Each diagnostic includes a message, source location, and zero-based
byte offset and length in the template. Diagnostics are also logged; nested
templates emit their own diagnostics.

Syntax and resolution errors become inline markers and processing continues after
a closed bad placeholder. Execution exceptions from called code propagate,
including native expression calls. The console `execute` entry point still
reports execution exceptions in the context. No template or evaluated-value cache
is used.
