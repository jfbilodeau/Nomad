# Nomad Programming Language

Nomad is a compiled, strongly typed scripting language designed to be embedded in C++ applications. It emphasizes explicit behavior, predictable execution, and callback‑driven design. Nomad is currently used by the Nomad game engine.

Nomad is case‑sensitive.

---

## Table of Contents

- [Core characteristics](#core-characteristics)
- [Quick start](#quick-start)
- [Comments](#comments)
- [Identifiers and naming rules](#identifiers-and-naming-rules)
- [Data types and numeric ranges](#data-types-and-numeric-ranges)
- [Variables and variable kinds](#variables-and-variable-kinds)
- [Constants and predefined values](#constants-and-predefined-values)
- [Type rules and conversions](#type-rules-and-conversions)
- [Control flow (if) and No loops](#control-flow-if-and-no-loops)
- [Assertions](#assertions)
- [Format strings](#format-strings)
- [Operators](#operators)
- [Functions](#functions)
- [Scripts: file layout and compilation model](#scripts-file-layout-and-compilation-model)
- [Native functions](#native-functions)
  - [Overloading](#overloading)
- [Saving and loading](#saving-and-loading)
- [Callbacks and Events](#callbacks-and-events)
- [Error handling](#error-handling)
- [Language rules and reserved words](#language-rules-and-reserved-words)

---

## Core characteristics

- Compiled to intermediate machine‑like instructions
- Strong static typing
- Designed for embedding in C++ runtimes
- Callback‑oriented execution model
- Script‑based with directory‑driven compilation

---

## Quick start

1. Place your `*.nomad` scripts under a compile root directory.
2. The Nomad compiler is pointed at the compile root and scans all `*.nomad` scripts in the directory tree.
3. Refer to section [Scripts: file layout and compilation model](#scripts-file-layout-and-compilation-model) for naming rules and collisions.
4. The engine can execute a compiled function by name, for example the function compiled from `init.nomad`.

---

## Comments

Nomad supports single‑line comments starting with `#`. Comments can appear after statements.

```nomad
# This is a comment

score = 0 # Initialize score.
```

---

## Identifiers and naming rules

Function names (whether compiled from a script or declared with `fun`), native function names, and variable names all follow the same identifier rules:

- Allowed characters: letters (A–Z, a–z), numbers (0–9), underscore (`_`) and dot (`.`).
- Must begin with a letter or underscore.
- Must end with a letter, number, or underscore (i.e. may not end with a dot).

Examples:

```nomad
player
_player
entities.player
score_1
```

Notes:

- Dots are used by the compiler to form function names from file paths (see compilation model). For example, the script `./entities/player.nomad` would be named as `entities.player` in the runtime.
- Within engine‑provided dynamic variables a full identifier such as `this.x` may be treated as a single identifier by the engine.
- Since the same rules apply to native functions, a native function like `log.info` is valid.

---

## Data types and numeric ranges

Nomad provides the following built‑in types:

| Type | Literal examples | Description |
| --- | --- | --- |
| `int` | `10`, `-42` | Signed 64‑bit integer (range: -9,223,372,036,854,775,808 .. 9,223,372,036,854,775,807) |
| `float` | `0.5`, `10.0` | 32‑bit IEEE‑754 single‑precision float (approximate range ±3.4e38, ~7 decimal digits precision) |
| `bool` | `true`, `false` | Boolean value |
| `string` | `"hello"` | UTF‑8 string; supports escape sequences `\n`, `\r`, `\t` |

Types whose names start with `$` (for example `$stringref`) are internal to the runtime and cannot be named in Nomad code.

String literal notes:

- The escape sequences `\n` (linefeed), `\r` (carriage return), `\t` (tab), `\"` (double quote) and `\\` (backslash) are supported inside double‑quoted strings and format strings.
- Example: `text = "Line1\nLine2\tIndented"`.

Numeric literal notes:

- Integer literals may be written in decimal (`42`) or hexadecimal (`0x2A`) and cover the full 64‑bit range.
- A `-` immediately followed by a digit is part of the numeric literal. `a - 3` is a subtraction, but `a -3` is `a` followed by the literal `-3`. This lets native function arguments such as `this.getMaskAt 0.0 -1.0` be written without parentheses. Always put spaces around the binary `-` operator. A `+` is never part of a literal, so `5+3` is an addition.

---

## Variables and variable kinds

Variables in compiled code are implicitly declared through assignment. The assigned value determines the variable type and that type is then fixed for that variable.

```nomad
value = 10      # int
ratio1 = 0.5    # float
ratio2 = 1.0    # float
text = "hello"  # string
flag = false    # bool
```

Nomad supports three variable kinds:

- Function variables: scoped to the function where they are defined. Type is inferred from first assignment and cannot change. For example, `a = 10` defines `a` as an integer variable and `b = 1.0` defines `b` as a float variable.
- Context variables: bound to a named context; identified by a prefix and a dot (for example `global.score`). Contexts are provided by the embedding engine and have engine‑specific semantics for storage and persistence. The `global.` context is pre-defined by the default Nomad runtime. The game engine provides `inventory.*` for saved game state and `settings.*` for user preferences. The `global.*`, `scene.*`, `this.*`, and `other.*` contexts are not persisted by the save and settings native functions.
- Dynamic variables: engine‑defined identifiers (for example `this.x`) managed directly by engine code; the engine decides read/write permissions and behavior.

The interactive interpreter is an exception to compiled-code typing rules. Local variables created in the interpreter may be reassigned with a value of a different type. Context variables and engine-defined dynamic variables continue to follow the type rules of their contexts and definitions.

---

## Constants and predefined values

Constants are declared with `const` and are globally visible and immutable.

```nomad
const MAX_SCORE = 1000
```

Predefined constant:

- `pi` — the value of `std::numbers::pi` from the host C++ runtime.

---

## Type rules and conversions

- Nomad does not perform implicit type conversions. All operands of an operator call must be of the same type. Examples that are disallowed and will fail to compile:

```nomad
1.2 + 3      # float + int — not allowed
10 > "20"    # int > string — not allowed
true == 1    # bool == int — not allowed
```

- Use the built‑in conversion native functions to convert explicitly:

```nomad
r = toFloat 10   # convert int to float
i = toInt 13.2   # convert float to int
n = toInt "42"   # convert string to int
f = x == 1       # convert int to bool
```

Conversion to `string` should be performed via format strings. For example:

```nomad
score = 10
scoreText = $"{score}"  # scoreText == "10"
message = $"Score: {score}"
```
(See the section on [format strings](#format-strings) for more details)

---

## Control flow (if) and No loops

Only `bool` expressions are allowed in `if` conditions. Numeric values are not implicitly converted to `bool`.

```nomad
if condition
    # statements
end
```

There are currently no loop constructs (`for`, `while`, etc.) yet in Nomad.

---

## Assertions

Use `assert` to require a boolean expression to be `true`. When the expression is `false`, the runtime reports an assertion failure that includes the function name, source line and assertion text.

```nomad
assert score >= 0
```

Assertions are compiled into the function and are intended for detecting invalid runtime state. They cannot be caught from Nomad code.

---

## Format strings

Format strings allow embedding values using `$"..."`. Each `{name}` placeholder must be a variable, parameter, constant, or a function that takes no parameters (the function is called and its return value is inserted). Expressions, native functions and functions with parameters are not allowed inside placeholders.

```nomad
name = "J-F"
score = 10
text = $"Player {name} got {score} points"
# text is now "Player J-F got 10 points"
```

---

## Operators

Nomad evaluates operators according to the following precedence order, from highest to lowest. All operator operands must be of the same type; mixed‑type operators are compilation errors.

| Precedence | Operator(s) | Category | Operand Types | Notes |
|-----------:|-------------|----------|---------------|-------|
| 1 | `( )` | Grouping | any | Explicit precedence control |
| 2 | function / native function call | Invocation | any | Calls are evaluated before surrounding operators |
| 3 | `!` | Logical NOT | `bool` | Produces a `bool` |
| 3 | `-` | Unary negation | `int`, `float` | Negates the operand |
| 3 | `+` | Unary absolute value | `int`, `float` | Returns the absolute value of the operand (`+(0 - 5)` is `5`) |
| 3 | `sin`, `cos`, `tan` | Trigonometric | `float` | Unary trigonometric operators |
| 4 | `*`, `/` | Arithmetic | `int`, `float` | Multiplication and division; operands must match |
| 4 | `%` | Modulo | `int` | Integer remainder |
| 5 | `+`, `-` | Arithmetic | `int`, `float` | Addition and subtraction; operands must match |
| 6 | `\|` | Bitwise OR | `int` | Integer only |
| 7 | `^` | Bitwise XOR | `int` | Integer only |
| 8 | `&` | Bitwise AND | `int` | Integer only |
| 9 | `>`, `>=`, `<`, `<=` | Comparison | `int`, `float` | Produces a `bool`; operands must match |
| 9 | `==`, `!=` | Equality | `bool`, `int`, `float`, `string` | Produces a `bool`; operands must match. Strings compare by content (case-sensitive) |
| 10 | `\|\|` | Logical OR | `bool` | Short-circuit evaluation |
| 11 | `&&` | Logical AND | `bool` | Short-circuit evaluation |
| 12 | `=` | Assignment | matching types | No implicit type conversion in compiled code |

Following standard arithmetic precedence, multiplication, division and modulo are evaluated before addition and subtraction.

Examples:

```nomad
a = 10 + 2 * 3       # 16
b = (10 + 2) * 3     # 36
c = a > b && false   # false
d = 17 % 5           # 2
e = 6 ^ 3            # 5
f = !c               # true
```

Notes:

- String concatenation is not supported via operators; use format strings. The only string operators are `==` and `!=`.
- Bitwise operators operate on `int` only.
- Logical operators operate on `bool` only.
- No implicit type conversion is performed.

---

## Functions

### Declaring a function

```nomad
fun simple
    # statements
end
```

### Parameters and returns

Parameters are declared as `name:type`. Parameters are read-only: assigning to one is a compilation error. Copy it to a local variable first:

```nomad
fun shout text:string
    loud = $"{text}!"
    return loud
end
```

Return type is always inferred from `return` expressions. If no `return` is used the function returns `void`.

```nomad
fun add v1:int v2:int
    return v1 + v2 # Returns an int
end

fun noReturn
    # no return -> returns void
end
# or
fun noReturn
    return # Nothing is returned -> void
end
```

Use `return` to return a value or to exit a function early. `return` with no value exits without returning a value (i.e. `void`).

If some branches return a value and others do not, that is a compilation error — return types must be consistent and inferred unambiguously.

A function that returns a value must end every path with a `return`. An `if` only counts as returning when it has an `else` and every branch (including each `else if`) returns. Functions that do not return a value (`void`) may fall through their end.

```nomad
fun clamp x:int
    if x > 10
        return 10
    end
    # Compile error: missing `return` at the end of function 'clamp'.
end
```

Statements after a `return` can never run, so they are a compilation error. This includes statements after an `if` whose branches all return. Declarations (`fun`, `const`, `event`, `on`) emit no code and may still follow a `return`.

```nomad
fun oops
    return 1
    x = 2   # Compile error: unreachable code.
end
```

---

### Calling a function

```nomad
# Declaring the function `simple`
fun simple
    log.info "called!"
end

# Declaring the `add` function
fun add value1:int value2:int
    return value1 + value2
end

# Calling a function called `simple`
simple
# Calling a function `add` that takes two `int`s and returns an `int`.
total = add 10 20
```

---

## Scripts: file layout and compilation model

Nomad source files are called *scripts*. A script is a `*.nomad` file on disk; once the compiler has processed it, the result is a *function* that the runtime can call. Scripts are the authoring unit, functions are the runtime unit.

The Nomad compiler is pointed to a directory (the compile root). It scans that directory and all subdirectories for scripts matching `*.nomad` and compiles each script into a function whose name is derived from the relative path.

- A script at `<compile_root>/init.nomad` becomes the function `init`.
- A script at `<compile_root>/entities/player.nomad` becomes `entities.player`.
- Directory separators become `.` in function names.

Name collision rules:

- A function declared inside code with the same fully qualified name as a compiled script will cause a compilation error. For example, a script `/scene/play/intro.nomad` compiles to `scene.play.intro`. You cannot also have a script named `/scene.play.intro.nomad` — that would create a conflicting name.

---

## Native functions

Native functions are callable entities provided by the runtime/engine and invoked like functions.

```nomad
log.info "Example"
```

Inline callback parameters support three forms:

1. `fun ... end`
2. `then ...`
3. Function name reference (for example `existing.callbackHandler`)

Nested `fun` callbacks and inline `then` callbacks capture variables and parameters from the surrounding scopes.

Explicit callback example:

```nomad
fun afterCreateScene
    # statements...
end

game.createSceneByNameThen "test" afterCreateScene
```

`fun` (inline) callback example:

```nomad
game.createSceneByNameThen "test" fun
    scene.z = 100
    log.info "scene created!"
end
```

`then` (continuation) callback example:

```nomad
game.createSceneByNameThen "test" then
scene.z = 100
log.info "scene created!"
```

`then` notes:

- `then` may declare callback parameters (for example `then x y`).
- There is no closing `end` for a `then` callback.
- `then` consumes the remaining body of the current function as the callback body.
- `then` applies only to callback arguments.

Predefined native functions:

- `log.info` — Write an "info" level string to the log.
- `math.floor` — Floor a float value.
- `toFloat` — Convert an `int` or a `string` to a `float`.
- `toInt` — Convert a `float` or a `string` to an `int`.
- `breakpoint` — Request a C++ breakpoint. (Nomad has no built‑in debugger; this native function can be used to signal the host runtime.)

Window native functions:

| Native function | Behavior |
| --- | --- |
| `window.maximize` | Maximize the window. |
| `window.minimize` | Minimize the window. |
| `window.clearOnClose` | Clear the close-request callback. |
| `window.clearOnGainFocus` | Clear the focus-gained callback. |
| `window.clearOnLoseFocus` | Clear the focus-lost callback. |
| `window.clearOnMaximize` | Clear the maximize callback. |
| `window.clearOnMinimize` | Clear the minimize callback. |
| `window.clearOnMove` | Clear the move callback. |
| `window.clearOnResize` | Clear the resize callback. |
| `window.clearOnRestore` | Clear the restore callback. |
| `window.onClose callback:callback` | Register a callback that is invoked when the user tries to close the window, such as by clicking its close button. This notification does not close the window. |
| `window.onGainFocus callback:callback` | Register a callback that is invoked when the window gains focus. |
| `window.onLoseFocus callback:callback` | Register a callback that is invoked when the window loses focus. |
| `window.onMaximize callback:callback` | Register a callback that is invoked when the window is maximized. |
| `window.onMinimize callback:callback` | Register a callback that is invoked when the window is minimized. |
| `window.onMove callback:callback` | Register a callback that is invoked with `x:int` and `y:int` when the window is moved. |
| `window.onResize callback:callback` | Register a callback that is invoked with `width:int` and `height:int` when the window is resized. |
| `window.onRestore callback:callback` | Register a callback that is invoked when the window is restored. |
| `window.toggleFullScreen` | Toggle the window between windowed and full-screen modes. |

Each `window.clearOn*` native function takes no arguments. Clearing an unregistered callback has no effect; clearing a callback while it is running does not interrupt that invocation.

Window state variables:

| Variable | Behavior |
| --- | --- |
| `window.hasFocus:bool` | Whether the window currently has focus. |
| `window.isFullScreen:bool` | Whether the window is currently in full-screen mode. |
| `window.isMaximized:bool` | Whether the window is currently maximized. |
| `window.isMinimized:bool` | Whether the window is currently minimized. |

Native function naming follows the same identifier rules as variables and functions.

### Overloading

A native function name may be registered more than once, as long as the overloads differ in the types of their parameters. The call is resolved from the types of its arguments:

```nomad
i = toInt 13.2    # calls toInt value:float
j = toInt "42"    # calls toInt value:string
```

An overload whose parameter types match the arguments exactly is preferred over one that merely accepts them. A call that matches no overload, or that matches several equally well, is a compile error.

Because a call is parsed before its overload is chosen, every overload of a name must be parsed the same way. The runtime rejects an overload that would break this:

- All overloads must take the same number of parameters.
- All overloads must return a value, or all must return nothing. (The parser decides whether a call is a statement or an expression from the name alone.)
- Only ordinary value parameters may differ between overloads. A name that takes a callback, a `then` continuation, a predicate or an event cannot be overloaded.
- Two overloads may not have the same parameter types.

Overloading applies to native functions. Functions written in Nomad are not overloadable: one name means one function.

### String lifetime (host integration)

Script authors only ever see `string`. Internally, the runtime manages string memory with these rules:

- **The compiler manages the lifetime of temporaries.** For strings held in registers, on the stack, in parameters, locals or return values, the compiler emits the instructions that create, copy, move and free them, using each type's `TypeOpCodes` (`getTypeOpCodes()`). The compiler does not own these values; it guarantees the generated code releases them. Discarded string results, including native function and function results used as statements, are freed.
- **Containers own their values.** Context, dynamic and constant variables keep their own copy. Assigning a new string to one frees the previous value (`RuntimeValue::setStringValue`).
- **Receivers copy what they keep.** A `const NomadChar*` handed to C++ code is only valid for the duration of the call. Code that needs the text later must copy it into a `NomadString`.
- **`$stringref` is an internal type** for borrowed strings. Types whose names start with `$` are internal and cannot be named in Nomad code. Native functions may declare `$stringref` parameters (`runtime.getStringRefType()`). Any string argument is accepted, and literals and variables are passed by pointer without a copy. `getStringParameter(i)` reads it the same way as a `string` parameter. Documentation lists these parameters as `string`.
- **Hidden source parameters** `$file` and `$function` are also borrowed references. They point into the runtime string table, which is filled during compilation and must not change while functions run. `$line` is an `int`.
- **String arguments to Nomad functions are lent when safe.** Literals, the caller's local variables and the caller's parameters are passed by pointer, because the callee cannot modify them: parameters are read-only and locals belong to the caller's frame. Context and dynamic variables are always copied, because the callee could reassign them during the call. Any other argument (native function or function results, format strings) is an owned temporary that the caller frees after the call. Inside the callee, reading a parameter (to return it, store it or capture it in a closure) produces an owned copy.
- **`Runtime::executeFunction` / `Game::executeFunction`:** overloads that take a `returnValue` give the caller an owned copy, which must be freed through the function's return type. Overloads without `returnValue` free the result.

Limitation: if a function faults (the VM throws), strings held by temporaries at that point are not freed.

---

## Saving and loading

Set `game.organization` and `game.name` before using persistence. The engine passes both values to SDL's preference-path API and stores JSON files under the resulting `<pref>` directory:

| API | Behavior | File |
| --- | --- | --- |
| `game.inventory.save saveName:string` | Save all defined `inventory.*` variables. | `<pref>/save/<saveName>.json` |
| `game.inventory.load saveName:string` | Load matching `inventory.*` variables. | `<pref>/save/<saveName>.json` |
| `game.inventory.saveExists saveName:string` | Return whether the save file exists. | `<pref>/save/<saveName>.json` |
| `game.settings.save` | Save all defined `settings.*` variables. | `<pref>/settings.json` |
| `game.settings.load` | Load matching `settings.*` variables. | `<pref>/settings.json` |
| `game.debug.console.visible:bool` | Show or hide the console in debug mode. Saved automatically when changed. | `<pref>/debug.json` |
| `game.debug.console.scale:float` | Set the console scale from `0.8` to `2.0`. Saved automatically when changed. | `<pref>/debug.json` |

Context variables are defined by assignment in Nomad code; loading never creates variables. Full dotted names are written as flat keys in a pretty-printed JSON object. This allows both a variable and its child, such as `inventory.weapon` and `inventory.weapon.attack`, to be saved without ambiguity. For example:

```nomad
inventory.gold = 25
inventory.playerName = "Nomad"
inventory.weapon.attack = 10
inventory.weapon.speed = 0.5
game.inventory.save "slot1"
```

produces:

```json
{
    "inventory.gold": 25,
    "inventory.playerName": "Nomad",
    "inventory.weapon.attack": 10,
    "inventory.weapon.speed": 0.5
}
```

The declared Nomad variable type controls conversion during loading, not how the number is written in JSON:

- An `int` accepts a JSON integer or a whole, in-range JSON number such as `10.0`, but not `10.5`.
- A `float` accepts either a JSON integer or floating-point number.
- A `bool` or `string` accepts only the corresponding JSON type.
- Missing keys keep their current values, and unknown JSON keys are ignored.
- Invalid values for individual variables are reported and ignored.
- Malformed JSON or a non-object root changes nothing.
- Loading never changes a variable's type.

Only `int`, `float`, `bool`, and `string` variables are persisted. Save names may optionally end in `.json`, but cannot be empty, `.` or `..`, contain control characters or `< > : " / \ | ? *`, or end in a dot or space.

```nomad
game.organization = "Example Studio"
game.name = "Example Game"

settings.fullscreen = true
game.settings.load

if game.inventory.saveExists "slot1"
    game.inventory.load "slot1"
end

game.settings.save
```

Debug settings are loaded lazily after both `game.organization` and `game.name` are set. Assigning a debug setting before those values are available does not save it.

---

## Callbacks and Events

Nomad supports named events and callbacks. Events are global (declared once and visible to all functions). Callback scope — i.e. how and when callbacks are invoked and what context they receive — is defined by the native function or embedding engine.

The language provides the `event` declaration. Registering callbacks and dispatching events are performed by native functions supplied by the embedding engine; `on` and `trigger` are not standalone Nomad statements. For example, the game engine provides the `this.on` and `this.trigger` entity native functions.

### Declaring an event

```nomad
event bespokeEvent x:int y:float z:string
```

### Registering an entity callback handler

```nomad
this.on bespokeEvent fun x y z
    log.info $"x: {x}, y: {y}, z: {z}"
end
```

Named callback functions may also be passed to native functions that accept a compatible callback:

```nomad
fun handleBespokeEvent x:int y:float z:string
    log.info $"x: {x}, y: {y}, z: {z}"
end

this.on bespokeEvent handleBespokeEvent
```

### Triggering an entity event

```nomad
this.trigger bespokeEvent 10 20.0 "notification"
```

Notes:

- Events are global names. Multiple handlers can register for the same event and the engine defines the dispatch order and lifetime semantics.
- Event callback and dispatch native functions use the event declaration to determine their parameter types.
- Native function names are host-defined. Other contexts may expose different event registration and dispatch native functions.
- The embedding engine decides callback invocation context and lifetime.

---

## Error handling

Nomad does not support exceptions inside functions. Errors detectable at compile time prevent compilation. Runtime errors (e.g. division by zero) will originate as C++ exceptions inside the engine and cannot be handled within Nomad functions.

---

## Language rules and reserved words

- Nomad is line‑based: one statement per line.
- Line continuation is not (yet) supported.
- Indentation is optional but encouraged for readability.
- Blocks (functions, if, event handlers, callbacks, etc.) are terminated with the `end` keyword. `end` is a reserved word.

Reserved words (cannot use as identifiers):

- `const`
- `else`
- `end`
- `event`
- `fun`
- `if`
- `params`
- `return`
- `then`
