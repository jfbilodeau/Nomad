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

Results are owned by the context. Copy a result using `TypedValue` if it must
survive another evaluation; do not free the context's result manually.
`listVariables()` lists interpreter-owned variables, not borrowed frame slots.
Different contexts have independent interpreter variables even when borrowing
the same VM. Access to a shared context or VM must be serialized by the host.

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

The dynamic script `$expression` formatting operator is not implemented yet.
This context API provides the environment needed for that later feature.
