# Embedding Nomad in C++

Nomad's scripting language can be embedded without the game engine. When building
as part of the Nomad source tree, link your host target to `Nomad::Language`.
See [building from source](building.md) for toolchain requirements and the
[language guide](language.md) for script syntax.

## Ownership

The host owns the `Runtime`, its virtual machines, and interpreter contexts.
Destroy contexts before their borrowed VM, and destroy VMs before the runtime.
Types and function definitions used by a context must remain valid.

- `Runtime` holds registered APIs, types, function definitions, and bytecode.
- `VirtualMachine` executes compiled functions and provides the execution
  environment for native calls, intrinsic methods, and dynamic variables.
- `InterpreterContext` borrows a VM and owns interpreter variables, its latest
  typed result, and its latest error. It can additionally borrow a script frame.
- `Interpreter` is stateless. Pass a context to each execution; keep that context
  to retain interpreter variables between executions.

Neither the interpreter nor its context creates or owns a VM.

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

`registerSources` is the only script-input API. A single script is a batch with
one entry; the former `registerScriptFile` API is replaced by this entry point.
It copies the input, so the batch may be changed or destroyed
after registration. It appends sources; it does not replace already registered
functions. Duplicate script names are reported immediately to the supplied
`CompilerContext`. That same context collects loading and compilation diagnostics;
compilation refuses to proceed if it already contains errors. Use a fresh runtime
for independent analysis snapshots, or provide a runtime containing the host APIs
needed by the scripts. Compilation does not execute scripts.

The compiler owns `ScriptSource` snapshots alongside per-script tokenizers and
top-level function IDs. Its function compilation records separately hold ASTs
and a compiler-local `NomadId` referring to the owning script. Script IDs remain
stable when more batches are registered and when scripts are processed in sorted
order; references returned by `getScript(id)` should be reacquired after
registration.

Parser integrations use `createFunction(context, scriptId, functionName)` for
declared or generated functions, replacing `registerFunctionSource`. This creates
a function within an existing script, not another script input. The script ID can
be obtained from a compiled or currently compiling function with
`getScriptId(functionId)`. Function creation inherits the script's source name
and full text and reports name collisions to the supplied context.

For disk input, call `loadSourcePaths(paths)` with a vector or span of filesystem
paths, then register the returned batch. This replaces the former
`Compiler::loadScriptsFromPath` API. The CLI tools and game use this same adapter.
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

`execute(source, context)` accepts one line containing an assignment, an
expression, or a supported function call. `evaluateExpression(source, context)`
requires exactly one non-void expression; assignments and statements are rejected.
Each entry point clears the context's previous result and error.

Both entry points also accept `SourceText`, using its name for input diagnostics:

```cpp
const nomad::SourceText input{"console:input", "score + 5"};
const auto succeeded = interpreter.evaluateExpression(input, context);
```

The string overloads preserve the existing context source name. Interpretation
continues to share the compiler's tokenizer, parser, and type resolver; it does
not register input as a compiled function or relax its single-line restrictions.

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

## Parameterized type proof of concept

`Runtime::registerParameterizedType(name, argumentCount, factory)` registers a
type family. The factory receives resolved type arguments and returns a
runtime-owned descriptor. `getParameterizedType(name, arguments)` registers a
specialization on first use and reuses it on subsequent requests. Arguments must
be non-void, public types owned by that runtime. Invalid requests are logged and
return null.

The collections module currently registers only `List` with one type argument.
Hosts can request `getParameterizedType("List", {runtime.getIntegerType()})`.
This creates a descriptor, not a list object. Script syntax and construction are
not available yet.

`ObjectTypeBase` provides shared reference-counted value handling and object
opcodes. `ObjectType` supplies a name and type-argument metadata; its text
representation is its type name. Its initial value is a null object reference,
not a constructed collection. Specialized descriptors may override initialization
and formatting. Strings reuse the base while retaining empty-string initialization.

## Typed intrinsic methods

Register an object method with the usual `registerIntrinsicMethod(...)` callback
and read its receiver with `getIntrinsicReceiverAs<Object>()`:

```cpp
runtime.registerIntrinsicMethod(
    runtime.getStringType(),
    "textSize",
    [](nomad::VirtualMachine* machine) {
        const auto* receiver = machine->getIntrinsicReceiverAs<nomad::NomadStringObject>();
        machine->setIntegerResult(static_cast<nomad::NomadInteger>(receiver->getLength()));
    },
    {},
    runtime.getIntegerType(),
    false,
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

Use `getIntrinsicReceiverAs<const Object>()` to obtain a pointer to const.
Calling the accessor outside an active intrinsic method faults.

The header accessor can be instantiated inside a DLL for a class unknown to the
VM. RTTI is not required by the accessor. DLLs must use compatible C++ ABI settings
and remain loaded while their callbacks or objects are alive.

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
