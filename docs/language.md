# Nomad Programming Language

Nomad is a compiled, strongly typed, case-sensitive scripting language used by
the Nomad game engine and embeddable in C++ applications.

---

## Table of Contents

- [Quick start](#quick-start)
- [Modules](#modules)
- [Comments](#comments)
- [Identifiers and naming rules](#identifiers-and-naming-rules)
- [Hubs](#hubs)
- [Class declarations](#class-declarations)
- [Object scripts](#object-scripts)
- [Struct declarations and value semantics](#struct-declarations-and-value-semantics)
- [Data types and numeric ranges](#data-types-and-numeric-ranges)
- [Class types and members](#class-types-and-members)
- [Variables and variable kinds](#variables-and-variable-kinds)
- [Constants and predefined values](#constants-and-predefined-values)
- [Enumerations (`enum`)](#enums)
- [Type rules and conversions](#type-rules-and-conversions)
- [Control flow and loops](#control-flow-and-loops)
- [Assertions](#assertions)
- [Format strings](#format-strings)
- [Operators](#operators)
- [Functions](#functions)
- [Scripts: file layout and compilation model](#scripts-file-layout-and-compilation-model)
- [Native functions](#native-functions)
  - [Overloading](#overloading)
- [Callbacks and Events](#callbacks-and-events)
- [Error handling](#error-handling)
- [Language rules and reserved words](#language-rules-and-reserved-words)

---

## Quick start

In a [Nomad project](index.md#create-and-run-a-game), edit `res/scripts/main.nomad`:

```nomad
fun add first:int second:int
    return first + second
end

score = add 10 20
print $"Score: {score}"
```

Run the project with `nomad run`. Functions and arguments are separated by spaces;
blocks end with `end`. Variable and return types are inferred.

---

## Modules

Modules provide additional types, functions, and services. Core language
features, including strings and `print`, require no module. See
[Runtime modules](modules.md) for available modules and project configuration.

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
- Each segment after a dot must also begin with a letter or underscore, not a digit.
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

## Hubs

A hub groups variables, functions, constants, enums, and events in a namespace.
Hubs are optional, are not types, and cannot be instantiated. Hubs are closed by
default: `hub name ... end` permits one declaration block. Use
`open hub name ... end` when multiple blocks should contribute members.
Closed hubs still allow assignment to their existing writable variables.
`open` is a reserved keyword. Use it before `hub` to allow additional declaration blocks.

```nomad
open hub example
    x = 10
    name = "test"

    fun increment
        example.x = example.x + 1
        return example.x
    end
end

result = example.increment # 11
```

Hub variables are shared across function calls. Their types are inferred from
their initializers and cannot change. Circular dependencies that prevent type
inference are compile errors.

Safely resolved initial values are installed first. Remaining initializers run
once before the application entry, in script-name order and declaration order
within each script, even if the script itself is never called. Members awaiting
runtime initialization hold their type's default value. Renaming a script can
change initialization order; use explicit entry-function assignments when
sequencing matters. Initialization failure prevents application startup.

Declare new members inside a hub block. Qualified assignments outside a hub
can update existing variables, but cannot introduce new members:

```nomad
example.x = 20 # Updates the existing variable.
# example.y = 30 # Invalid: y has not been declared in a hub block.

open hub example
    y = 30 # Extends the same hub.
end
```

Every declaration of the same namespace must be explicitly open to allow
multiple blocks, including across scripts or after native registration.
Mixing open and closed declarations is an error in either source order;
repeating a closed declaration is also an error. Declaring the same variable
twice remains an error, not reassignment. Hubs may appear at script level or
inside other hubs, but not inside functions or control-flow blocks.

Namespaces are independent, even when their names contain dots. Declaring
`hub game.audio` does not require or create `game`, and does not extend `game`
if it exists. Nested syntax only prefixes declaration names:

```nomad
hub game
    open hub audio
        volume = 1.0
    end
end

open hub game.audio
    muted = false
end
```

The example declares `game` and `game.audio`, each with its own mode. Closing
`game` does not restrict declarations of `game.audio`. A hub has no parent or
child namespace members.

Only declaration names receive the hub prefix. All hub-member references require
their fully qualified names, including inside hub initializers, function
signatures, function bodies, and format strings. There is no implicit current-
or parent-namespace lookup. Globals, built-ins, local variables, and parameters
retain ordinary unqualified names. Hub initializers cannot read script locals,
parameters, or an object-script receiver. Enum types and values have qualified names too:
an enum `State` inside `hub game` is named `game.State`, with values such as
`game.State.Ready`.

A hub name is reserved and cannot also name a value, function, or type.
Existing dotted identifiers remain supported; they do not implicitly declare
hubs.

---

## Class declarations

Classes are closed by default. Use `open class` to allow external field
declarations during compilation.

Classes may be declared at script level or inside a hub, where their declaration
names receive the hub prefix. References to these types must use their fully
qualified names. Class symbols are available to function signatures across files,
regardless of whether the class appears before or after the function declaration.

Each class has one declaration block, even when open. Duplicate declarations and
conflicts with existing identifiers are errors. Classes cannot be declared inside
functions or control-flow blocks, or through the interpreter.

Classes containing fields, constructors, and methods can be compiled and
instantiated with `new` in compiled code and the interpreter:

```nomad
class Point
    name = ""

    init
        this.x = 0.0
        this.y = 0.0
    end

    init x:float y:float
        this.x = x
        this.y = y
    end

    fun moveTo x:float y:float
        this.x = x
        this.y = y
    end
end

point = new Point
point.moveTo 3.0 4.0
```

Explicit fields use `name = initializer`. Constructors use `init`, optionally
followed by typed parameters. Methods use `fun name` and the same typed parameter
syntax as functions. Member names are unqualified. Parameter types must be fully
qualified, including inside a hub; class types declared in other files are
available to member signatures regardless of source order.

Call a method through its receiver, such as `point.moveTo 3.0 4.0` or
`this.moveTo x y`. Methods may be overloaded by parameter count and types;
selection uses the receiver's type and prefers exact argument types over
compatible conversions. Method return types are inferred as for functions.
A call returning a value may be used in an expression or discarded as a statement;
a method returning no value can only be used as a statement.
Arguments follow the same [copy and return rules](#parameters-and-returns) as
functions. The interpreter can call methods of compiled classes but cannot
declare new classes.

`init` is contextual: at class-member declaration position it introduces a
constructor, and cannot name a field or an ordinary method (`fun init`).
Elsewhere it remains an ordinary identifier, including function names, variables,
and qualified members such as `example.init`.

Duplicate fields, field/method name conflicts, and duplicate callable signatures
are errors. Parameter names do not distinguish signatures.

Inside a member body, explicit `this` refers to the declaring class instance.
It is not a declared parameter and cannot be reassigned or used as a parameter
name. Bare names do not implicitly resolve to fields: `value = 2` creates or
updates a local, while `this.value = 2` denotes an instance field assignment.
Field types are inferred from explicit initializers and `this.field = value`
assignments in constructors, including conditional branches and constructor
overloads. Each field has one type shared by all instances; conflicting
assignments and field/method name collisions are errors. Methods may read and
assign existing fields but cannot introduce new fields. Reads of unknown fields
are errors. These names never fall back to the game's `this.*` context.
Class initialization evaluates
explicit field initializers in source order, then gives the remaining fields
their type defaults before the constructor body runs. Explicit initializers
cannot use or capture `this`, including in callbacks or format expressions.
Bare names in initializers resolve normally outside the class fields, not to
other fields. Object-valued initializer expressions are evaluated separately for
each instance. Explicitly initialized fields do not receive a separate default
initialization. Each construction runs field initialization once;
failure stops subsequent initializers and the constructor body.
`new ClassName arguments` selects a constructor overload, allocates an instance,
initializes its fields, then runs the selected constructor body. If no `init`
is declared, the class has an implicit parameterless constructor. Declaring any
`init` suppresses that implicit constructor. The `new` expression produces the
new instance.
Script constructors return `void`: they may use a bare `return` but cannot return
an expression. An early return still completes `new` with the allocated instance.
Member parameters are declared in the header, not with a later `params` statement.

An assignment through an object reference may introduce a field on an open
type. The field belongs to the type, so all instances and aliases share its
name and type. Closed types permit updates to existing fields, not additions.
For example:

```nomad
open class Point
end

fun extend point:Point
    point.extra = 42
end

fun read point:Point
    return point.extra
end
```

The introducing assignment may appear after a read or in another file. A field
without a declaration or valid introducing assignment is an error; conflicting
types and field/method names are also errors. Externally introduced fields that
are assigned but never read produce a warning. These checks do not prove that
an assignment executes before a read.

The same extension rules apply to native class types marked open by the host.
Native properties retain their getter/setter contracts and do not need a script
assignment before a read. Host-declared fields are not subject to the
external-introduction warning.
Hub members must still be introduced inside a hub block, even for open hubs.

Native objects need host-provided storage to support stored fields; see
[embedding](embedding.md#script-object-storage).

## Object scripts

An embedding host can associate source directories with a class receiver.
These object scripts are methods, not free functions. Inside an object script,
`this` is the typed receiver and cannot fall back to an engine context.

The file body can infer fields from `this.field` assignments, declare parameters
with `params`, and return values as an ordinary method. Named `fun` declarations
become shared class methods and can update existing fields, but cannot introduce
them. Call these methods with an explicit receiver, such as `this.moveHome`.
Normal method overload and name-conflict rules apply across object scripts
targeting the same class.

Object scripts require host configuration; they are not enabled automatically
by the game project's script directories. See
[object-script embedding](embedding.md#object-scripts) for receiver mappings and
initializer invocation.

## Struct declarations and value semantics

Use `struct` for a value whose fields copy independently, rather than a shared
class instance:

```nomad
struct Point
    x = 0.0
    y = 0.0

    init x:float y:float
        this.x = x
        this.y = y
    end

    fun move dx:float dy:float
        this.x = this.x + dx
        this.y = this.y + dy
    end
end

fun moved value:Point dx:float dy:float
    value.move dx dy
    return value
end

point = new Point 1.0 2.0
copiedPoint = point
copiedPoint.move 3.0 4.0
movedPoint = moved point 10.0 20.0
assert point.x == 1.0
assert point.y == 2.0
assert copiedPoint.x == 4.0
assert copiedPoint.y == 6.0
assert movedPoint.x == 11.0
assert movedPoint.y == 22.0
```

### Declaration and construction

Structs may be declared at script level or inside a hub, not inside functions,
control-flow blocks, or other type declarations. Types are available across
files before callable bodies are compiled. Hub types require fully qualified
references, including in their own member signatures.

Structs use the same field, `init`, and `fun` syntax as
[classes](#class-declarations). Fields are inferred from explicit initializers
and constructor assignments to `this.field`. Methods can update existing fields
but cannot introduce new ones. Structs are closed: `open struct` and external
field additions are errors. Different struct declarations have distinct types,
even when their fields match.

Inside a struct member, `this` always denotes that struct receiver, including
inside callbacks and format expressions. Its fields do not fall back to a
game/entity `this.*` context.

`new Point arguments` evaluates arguments right-to-left, runs explicit field
initializers once in source order, supplies defaults for remaining fields, then
executes the selected constructor. Initializers cannot use or capture `this`.
With no declared `init`, a struct has an implicit parameterless constructor;
declaring an `init` suppresses it. Constructor overloads follow the same rules as
class constructors. Constructors return no expression; a bare early `return`
still completes construction.

Copying or default-initializing nested struct storage does not run its
constructor or explicit field initializer expressions.

### Copies, fields, and methods

Assignment, ordinary arguments, returned values, and callback captures copy
struct fields. Nested struct fields copy recursively. Strings retain their value,
and class or collection fields retain references to the same objects:

```nomad
class Label
    text = "shared"
end

struct Location
    point = new Point 0.0 0.0
    label = new Label
end

location = new Location
copiedLocation = location
copiedLocation.point.move 5.0 6.0
copiedLocation.label.text = "shared edit"
assert location.point.x == 0.0
assert copiedLocation.point.x == 5.0
assert location.label.text == "shared edit"
```

This example uses `Point` from above. Methods borrow their receiver: calling
`point.move` changes `point`, whereas calling `value.move` inside `moved` changes
only that function's parameter copy. A method can return or pass `this` as an
ordinary value; those uses copy it.

Stored struct fields can be read, assigned, and used as method receivers through
locals, parameters, `this`, class-owned fields, and temporary construction or
function results. Nested paths such as `player.location.point.move 1.0 2.0`
update the selected stored value. A temporary receiver is evaluated once before
the method's arguments or an assignment's right-hand side and kept alive until
the operation finishes. Mutating a temporary changes only that temporary,
apart from shared class objects contained in it.

Whole-struct assignment evaluates its complete right-hand side before replacing
the destination. Self-assignment is safe. Right-hand-side side effects are not
rolled back if evaluation fails. A native getter returning a struct produces a
snapshot, not a stored-field alias; mutating that snapshot does not automatically
call the property's setter.

Callbacks capture struct snapshots when created, including struct parameters and
`this`. Each invocation receives independent struct copies; captured class
objects remain shared. Interpreter expressions can construct registered types
and invoke compiled methods, but cannot declare new structs or inline callbacks.

### Empty structs and limitations

An empty struct still has its own type, distinct from `void`. It can be copied,
passed, captured, returned, and used as a receiver. Its constructors and methods
still execute; copying does not rerun construction.

- Structs cannot be type arguments, directly or nested: `List<Point>` and
  `Map<string List<Point>>` are rejected. Constructor inference also rejects
  struct type arguments. A struct may still contain a `List<int>` reference.
- Structs have no built-in `==` or `!=` operation, including empty structs.
  Compare the required fields explicitly.
- Inline struct layouts cannot contain direct or indirect value cycles.
  Class-reference fields do not form inline layout cycles.

## Data types and numeric ranges

Nomad provides the following built‑in types:

| Type | Literal examples | Description |
| --- | --- | --- |
| `int` | `10`, `-42` | Signed 64‑bit integer (range: -9,223,372,036,854,775,808 .. 9,223,372,036,854,775,807) |
| `float` | `0.5`, `10.0` | 32‑bit IEEE‑754 single‑precision float (approximate range ±3.4e38, ~7 decimal digits precision) |
| `bool` | `true`, `false` | Boolean value |
| `string` | `"hello"` | UTF‑8 string; supports escape sequences `\n`, `\r`, `\t` |

Scripts and the embedding engine may also define strongly typed enumerations. See [Enumerations](#enums).

Types whose names start with `$` (for example `$file`) are internal to the runtime and cannot be named in Nomad code.

### Parameterized type annotations

Registered parameterized types can be used in script parameters, function parameters,
and event declarations. Type arguments go between `<` and `>`, separated by spaces,
and may themselves be parameterized types:

```nomad
params values:List<int>

fun process items:List<List<string>>
    return items
end

event changed items:List<int>
```

Each type family defines its required number of type arguments and must be registered
before use. Parameterized types are not restricted to any particular module.
Different specializations, such as `List<int>` and `List<string>`, are distinct,
incompatible types. Type arguments cannot be `void`, internal types, or structs,
including structs nested in another type argument. Type annotations
must fit on one line; nesting is limited to 64 types.

String literal notes:

- The escape sequences `\n` (linefeed), `\r` (carriage return), `\t` (tab), `\"` (double quote) and `\\` (backslash) are supported inside double‑quoted strings and format strings.
- Example: `text = "Line1\nLine2\tIndented"`.
- Strings are managed automatically. Assignment, arguments, and returns preserve
  their value without manual cleanup; an uninitialized string is empty, not null.

Numeric literal notes:

- Integer literals may be written in decimal (`42`) or hexadecimal (`0x2A`) and cover the full 64‑bit range.
- A `-` immediately followed by a digit is part of the numeric literal. `a - 3` is a subtraction, but `a -3` is `a` followed by the literal `-3`. This lets native function arguments such as `this.getMaskAt 0.0 -1.0` be written without parentheses. Always put spaces around the binary `-` operator. A `+` is never part of a literal, so `5+3` is an addition.

---

## Class types and members

### Object construction

Use `new` followed by a constructor name to create an object:

```nomad
first = new List<int>
second = new List<int>
alias = first
nested = new List<List<string>>
```

Each `new` creates a distinct object. Assignment copies the reference, not the
object: `alias` and `first` refer to the same instance. Reassigning `first` does not
change `alias`. References are retained through function arguments and return
values, and an object is released when its last reference is discarded.

Available constructors depend on the selected modules.
Constructor arguments follow the name and any type arguments, separated by spaces, using the same
expression syntax as function arguments. Their number and types are checked at
compile time; value restrictions are checked by the constructor at runtime. 
Construction is also available in interpreter expressions.
Constructor and function arguments evaluate right-to-left in both compiled code
and the interpreter; a method receiver is evaluated before its arguments.

Constructors can be overloaded by argument count or types. The selected
constructor determines the result's type; overloads may return different types.
For example, `new Array<int> 3 4` returns an `Array2<int>`.
When passing a construction to another call, parenthesize it to delimit its
arguments: `(new Array<int> 3)` is a one-dimensional array, while
`(new Array<int> 3 4)` is two-dimensional.

### Properties and methods

Values expose the properties and methods provided by their type through dot syntax:

```nomad
name = "Nomad"
characterCount = name.length
upperName = name.toUpper
containsName = name.contains "mad"
```

Properties do not take arguments. Methods use Nomad's normal no-parentheses calling convention. Members can be chained when the preceding property or method returns a class type:

```nomad
upperLength = name.toUpper.length
literalLength = "Nomad".length
```

Properties may be read-only, write-only or read/write. Object methods may change
their instance even when its reference comes from a
parameter or a temporary; this does not reassign the reference.

Writable properties may also change an instance through a parameter
or temporary. Changes to an object's contents are immediate and are not rolled
back if a property setter or method fails.

Reassigning a parameter changes only the called function's copy, not the
caller's variable.

Script classes support construction, methods, and stored-property reads/writes
in compiled code and the interpreter. See [Class declarations](#class-declarations)
and [struct value semantics](#struct-declarations-and-value-semantics).

The available members for each class type are listed in the [engine API](engine-api.md#object-members).

### Indexers

Use `value[index]` to read an indexed value and `value[index] = expression`
to assign through a writable indexer. Multiple indices use
`value[index1 index2 ...]`, with whitespace separating expressions and no
commas. The receiver's type determines the supported index counts, index
and value types, access permissions, and bounds behavior. Reading a write-only
indexer or assigning through a read-only indexer is a compile-time error.
Indexers can be chained with other indexers, properties, and methods.

Strings have a read-only `int` indexer. It returns one Unicode code point as a
string, consistent with `length`, rather than one UTF-8 byte or a complete
grapheme cluster. Negative indices, out-of-range indices, and invalid UTF-8 cause
a runtime error. Assigning to a string index is a compile-time error.

```nomad
text = "Nomad"
letter = text[1] # "o"
# text[1] = "a" # Invalid: string indexing is read-only.
```

Indexing binds more tightly than binary operators. Parenthesize a compound
receiver when needed: `("hello".toUpper)[0]`.
The receiver is evaluated once first. Indices are then evaluated once,
right-to-left, like function arguments. In an assignment, the right-hand side
is the last parameter, so it is evaluated after the receiver and before the
indices. Writable indexers can mutate an object through a parameter or
temporary without rebinding that reference. Object-valued elements keep reference
semantics. The number of indices must match an indexer registered for the
receiver type. Parenthesize indices when needed to keep them
separate: `value[0 (-1)]` supplies two indices, while `value[0 - 1]` supplies one
subtraction expression.

### Member names

Member names share the identifier namespace with dotted identifiers. Assigning a type to a variable is rejected if that type would introduce a qualified member name that already exists:

```nomad
name.length = 10
name = "Nomad" # Error: string would introduce the conflicting name.length member.
```

Names under a registered context prefix, such as `this.value`, remain context
variables when a native property has the same name. Assigning one creates the
context variable if it does not already exist.

---

## Variables and variable kinds

Variables in compiled code are implicitly declared through assignment. The assigned value determines the variable type and that type is then fixed for that variable.

```nomad
value = 10           # int
ratio1 = 0.5         # float
ratio2 = 1.0         # float
text = "hello"       # string
flag = false         # bool
state = state.idle   # state enum
```

Nomad supports three variable kinds:

- Function variables: scoped to the function where they are defined. Type is inferred from first assignment and cannot change. For example, `a = 10` defines `a` as an integer variable and `b = 1.0` defines `b` as a float variable.
- Context variables: bound to a named context; identified by a prefix and a dot (for example `global.score`). Contexts are provided by the embedding engine and have engine‑specific semantics for storage and persistence. The `global.` context is pre-defined by the default Nomad runtime. The game engine additionally provides `scene.*`, `this.*`, and `other.*` contexts, but no built-in `inventory.*` or `settings.*` contexts.
- Dynamic variables: engine‑defined identifiers (for example `this.x`) managed directly by engine code; the engine decides read/write permissions and behavior.

The interactive interpreter is an exception to compiled-code typing rules. Interpreter variables may be created by assignment and reassigned with a value of a different type. Frame variables and parameters retain their compiled types but may be reassigned. Runtime context variables and engine-defined dynamic variables continue to follow the type rules of their contexts and definitions.

See [Embedding Nomad in C++](embedding.md) for the interpreter context API and ownership rules.

---

## Constants and predefined values

Constants are declared with `const` and are globally visible and immutable.

```nomad
const MAX_SCORE = 1000
```

Predefined constant:

- `pi` — the mathematical constant.

---

## Enums

Enumerations define a named type and a set of globally visible, immutable values:

```nomad
enum state
    idle
    walking
    jumping
end

this.state = state.idle
```

Enum declarations and their values are globally visible across scripts. A value is referenced by its qualified name, such as `state.walking`, and variables infer the named enum type from assignment.

Enums are strongly typed:

- A value may be assigned only to a variable of the same enum type.
- Values from different enum types are incompatible, even when their internal values match.
- Enum values cannot be passed to an `int` parameter or assigned from an `int`.
- `toInt`, `toFloat`, and other conversion functions do not accept enums.
- The `==` and `!=` operators are available between values of the same enum.
- Arithmetic, bitwise, and ordering operators are not available for enums.

For example, each of these expressions fails to compile:

```nomad
this.state = state.idle
this.state = 1                    # int cannot be assigned to State
same = state.idle == 1            # State cannot be compared with int
next = state.idle + state.walking # enums do not support arithmetic
```

The embedding engine may register enum members with explicit internal values rather than compiler-assigned sequential values. This preserves corresponding C++ enum values without weakening Nomad's type rules.

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

## Control flow and loops

Only `bool` expressions are allowed in `if` conditions. Numeric values are not implicitly converted to `bool`.

```nomad
if condition
    # statements
end
```

### Conditional loops

`while` tests before each iteration and repeats while its condition is true.
`until` also tests before each iteration, but repeats while its condition is false.
Conditions must be `bool` expressions.

```nomad
while running
    # statements
end

until finished
    # statements
end
```

Use `do` for a body that executes at least once. Its closing line must include
`while` or `until` and a boolean condition.

```nomad
do
    # statements
end while running

do
    # statements
end until finished
```

### Iterating ranges and collections

Nomad provides the built-in functions `range <stop>`, `range <start> <stop>`, and
`range <start> <stop> <step>`. These functions return a reusable `Range` without
requiring any modules:

```nomad
for value in range 0 10
    print $"{value}"  # 0 through 9
end

for value in range 10 0 (-2)
    print $"{value}"  # 10, 8, 6, 4, 2
end
```

Range arguments are `int` expressions evaluated once when the function is called.
The start is inclusive and the stop is exclusive. The default start is `0` and
the default step is `1`. A direction mismatch or equal bounds produces an empty
range. A zero step raises a runtime error, including when supplied as a literal.
Iteration stops safely at integer limits without wrapping. Each traversal starts
an independent iterator; range values can be stored and traversed repeatedly.
Parenthesize negative arguments to distinguish them from subtraction.
Changing the original argument variables or reassigning the range variable does
not change an iterator that is already traversing the range.

```nomad
names = new List<string>
names.append "Ada"
names.append "Grace"
for name in names
    print name
end

for entry in scores.entries
    print $"{entry.key}: {entry.value}"
end
```

`for value in source` evaluates its source once and uses an independent iterator
for each execution, including nested loops. The loop variable is an ordinary
mutable local, or an existing local or parameter of the element type. Each
iteration assigns the next value to it. Assigning to the variable in the body
does not reposition the iterator. The variable remains available after the loop,
including after `break`, with its last assigned value. An empty loop preserves an
existing value; a new binding retains its normal default value. Nested loops may
reuse the same variable, but their iterators remain independent. Explicit
callbacks capture the value when the callback is created.

```nomad
value = 42
for value in range 0 3
    value = value + 10
end
print $"{value}"  # 12; the body assignment does not affect the next range value

for value in range 0
    # Empty: value remains 12.
end
```

Numeric iteration uses `for value in range ...`; `for value = ... to ... step ...`
is not supported.

`List<T>` and one-dimensional `Array<T>` are iterable. Multidimensional arrays and
maps are not directly iterable; use a map's `keys`, `values`, or `entries` snapshot.
Iteration is live: replacing an element or changing an object stored in a
collection is allowed, and later advances observe updated elements.

Structural changes to a list invalidate its active iterators. Append, prepend,
insert, successful removal, and clearing a nonempty list succeed normally, but
the iterator's **next advance** faults. Failed removals and clearing an empty list
do not invalidate iteration. Mutation followed immediately by `break` is valid.

### Break and continue

`break` exits the nearest enclosing loop. `continue` skips the rest of its body:
conditional loops retest their condition and iterable loops request the next
element. Both statements must be inside a loop in the
same function; a callback cannot break or continue its caller's loop.

Single-line conditional forms do not need an `end`:

```nomad
if finished break
if skipValue continue
```

Loops are synchronous. They do not wait for callbacks or suspend
execution; an unbounded loop can block the calling thread.

---

## Assertions

Use `assert` to require a boolean expression to be `true`. When the expression is `false`, the runtime reports an assertion failure that includes the function name, source line and assertion text.

```nomad
assert score >= 0
```

Assertions are compiled into the function and are intended for detecting invalid runtime state. They cannot be caught from Nomad code.

---

## Format strings

Format strings allow embedding values using `$"..."`. Simple `{name}` placeholders
use direct lookup of variables, parameters, constants, or parameterless script
functions. Other placeholders accept normal expressions, including operators,
parentheses, native and script function calls with arguments, and object members.
Each expression must return a non-void value. Placeholders are evaluated from left
to right using current values; expression results are converted to text.

Format literals are checked at compile time. `$template` evaluates a string's
`{...}` placeholders at runtime.

Format literals also work in the debug console: `$"{1+2}"` evaluates to `"3"`.

Empty placeholders and missing closing braces are compilation errors. A closing
brace outside a placeholder is ordinary literal text. Backslashes do not escape
placeholder braces; the supported string escape sequences apply to literal text.

```nomad
name = "J-F"
score = 10
text = $"Player {name} got {score} points"
# text is now "Player J-F got 10 points"
```

```nomad
x = 10
y = 20
print $"total = {x + y}"
print $"doubled = {(x + y) * 2}"
print $"nested = {$"total {x + y}"}"
print $"player = {name.toUpper}"
```

Expressions are compiled in the containing function's lexical scope, including
its parameters and captured variables. Quoted strings inside expressions may
contain braces, and nested format strings are supported. Invalid expressions
are compilation errors, not inline error text. Bare native function names still
use the simple-placeholder rules; use a call with arguments or a parenthesized
expression to select expression parsing. Object members such as `{name.toUpper}`
and `{name.length}` do not require parentheses. An exact qualified identifier takes
precedence over object member access.

Struct values use the same default representation as class objects: their type
name, without a field dump. For a `Point` struct, `{point}` produces `Point`;
`{point.x}` formats the selected field. Empty structs also produce their type
name. Nested fields, temporary construction/function results, parameters, and
struct `this` are supported in both compiled and dynamic format strings.

Compiled expression placeholders copy captured struct values for each
placeholder. A mutating method called through that capture changes only its
copy; contained class objects remain shared. Dynamic placeholders evaluate
against live interpreter/frame storage instead. For example, if `counter.next`
increments and returns a struct field initially equal to zero:

```nomad
compiled = $"{counter.next} {counter.value}" # "1 0"; counter.value remains 0
template = "{counter.next} {counter.value}"
live = $template # "1 1"; counter.value becomes 1
```

### Format string operator

The format string operator `$` interprets the value of a string-valued expression
as a dynamic format string at runtime:

```nomad
score = 10
message = "Score: {score}"
this.text = $message
```

The string operand is evaluated once. Placeholders are evaluated in order using
the current function's locals, parameters, existing captures, and runtime APIs.
The interpreter also supports this operator using its caller-maintained variables.
Each placeholder must be one value-producing expression; assignments are not allowed.

`$"..."` remains a compiled format literal. Use `$("...")` to dynamically format
a string literal, or `$(expression)` to group a string-valued expression.
Templates are parsed on each use; results and values are not cached. Runtime
formatting is slower than compiled literals and does not decode string escapes again.

Malformed dynamic placeholders preserve the surrounding text and insert an
inline error:

```nomad
score = 10
message = "Score: {sore}"
this.text = $message # "Score: {invalid identifier: 'sore'}"

message = "Score: {score"
this.text = $message # "Score: {missing `}`}"
```

Closed bad placeholders do not prevent later placeholders from being evaluated.
An unclosed trailing placeholder produces a marker and ends processing. Syntax,
name, and type errors are logged with the source location and template byte offset.
A placeholder is parsed and resolved completely before any of its calls execute.
Exceptions thrown by called functions are not converted into inline template errors.
Compiled format literals remain strict: malformed expressions are compilation errors.

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

Logical `&&` and `||` are short-circuited.

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

Parameters are declared as `name:type` and can be reassigned without changing
their type. Rebinding a parameter does not change the caller's variable. Object
parameters still share their referenced object, so member mutations remain
visible to the caller:

```nomad
fun shout text:string
    text = $"{text}!"
    return text
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

Using the functions declared above:

```nomad
simple
total = add 10 20
```

Calls execute synchronously. A value-returning call used as a statement discards
its result.

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
print "Example"
```

The built-in `print message:string` writes plain text followed by a newline
to standard output, independently of logging levels and sinks. It is available
without any modules. Use format strings to print non-string values.

Available native APIs depend on the selected modules. See the [engine API](engine-api.md),
[logging module](nomad-log.md), and [system module](nomad-system.md) for reference.

Native function naming follows the same identifier rules as variables and functions.

### Overloading

A native function name may be registered more than once, as long as the overloads differ in parameter count or types. The call is resolved from the count and types of its arguments:

```nomad
i = toInt 13.2    # calls toInt value:float
j = toInt "42"    # calls toInt value:string
```

An overload whose parameter types match the arguments exactly is preferred over one that merely accepts them. A call that matches no overload, or that matches several equally well, is a compile error.

Because a call is parsed before its overload is chosen, every overload of a name must be parsed the same way. The runtime rejects an overload that would break this:

- Overloads may take different numbers of ordinary value parameters. For nested calls,
  use parentheses to delimit the arguments of the inner call.
- Overloads involving compiler-supplied source parameters must take the same number of parameters.
- All overloads must return a value, or all must return nothing. (The parser decides whether a call is a statement or an expression from the name alone.)
- Only ordinary value parameters may differ between overloads. A name that takes a callback, a `then` continuation, a predicate or an event cannot be overloaded.
- Two overloads may not have the same parameter types.

Overloading applies to native functions. Functions written in Nomad are not overloadable: one name means one function.

---

## Callbacks and Events

Nomad supports named events and callbacks. Events are global (declared once and visible to all functions). Callback scope — i.e. how and when callbacks are invoked and what context they receive — is defined by the native function or embedding engine.

The language provides the `event` declaration. Registering callbacks and dispatching events are performed by native functions supplied by the embedding engine; `on` and `trigger` are not standalone Nomad statements. For example, the game engine provides the `this.on` and `this.trigger` entity native functions.

### Callback forms

A callback argument accepts a named function, an inline `fun ... end`, or a
`then` continuation. Inline callbacks capture surrounding variables and
parameters when created.

```nomad
fun afterCreateScene
    print "scene created!"
end

game.createSceneByNameThen "test" afterCreateScene

game.createSceneByNameThen "test" fun
    scene.z = 100
end
```

`then` consumes the rest of the current function body as its callback, without
a closing `end`:

```nomad
game.createSceneByNameThen "test" then
scene.z = 100
print "scene created!"
```

It may declare callback parameters, such as `then x y`, and applies only to
callback arguments. Use it at the root of a script or function body, not directly
inside an `if` or loop. An explicit `fun ... end` inside a block establishes a
new function body and may use `then` at its own root.

### Declaring an event

```nomad
event bespokeEvent x:int y:float z:string
```

### Registering an entity callback handler

```nomad
this.on bespokeEvent fun x y z
    print $"x: {x}, y: {y}, z: {z}"
end
```

Named callback functions may also be passed to native functions that accept a compatible callback:

```nomad
fun handleBespokeEvent x:int y:float z:string
    print $"x: {x}, y: {y}, z: {z}"
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

Compile-time errors prevent execution. Runtime errors, such as division by zero,
stop execution and cannot be caught in Nomad code.

---

## Language rules and reserved words

- Nomad is line‑based: one statement per line.
- Line continuation is not (yet) supported.
- Indentation is optional but encouraged for readability.
- Blocks (functions, enums, if, event handlers, callbacks, etc.) are terminated with the `end` keyword. `end` is a reserved word.

Reserved words (cannot use as identifiers):

- `class`
- `const`
- `else`
- `end`
- `enum`
- `event`
- `fun`
- `hub`
- `if`
- `open`
- `params`
- `return`
- `struct`
- `then`
