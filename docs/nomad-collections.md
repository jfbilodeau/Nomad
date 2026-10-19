# Collections module

Select `nomad-collections` in `nomad.toml` to use typed lists and fixed-size
arrays:

```toml
[nomad]
modules = ["nomad-collections"]
```

Keep the project's existing `nomad.version`. Add other modules to the same list
when needed; for example, use `["nomad-game", "nomad-collections"]` in a game
that uses collections. See [Runtime modules](modules.md) for module setup.

## Lists

Lists start empty, support `push` to append a value, and expose a read-only
`count` property:

```nomad
words = new List<string>
words.push "first"
words.push "second"
word = words[0]
words[1] = "updated"
length = words[1].length
letter = words[1][0] # "u"
```

The index must be an `int`, and elements have type `T`. Indices start at zero.
Negative indices and indices greater than or equal to `count` cause a runtime
error. Indexed assignment replaces an existing element; it does not grow the
list. Assignments share the same list instance, and methods and indexed
assignments can change its contents through a parameter or another reference.

See [Object types and members](language.md#object-types-and-members) for
construction, reference semantics, and indexer syntax.

## Fixed-size arrays

Construct arrays with one `int` size per dimension. The result type is
`Array1<T>`, `Array2<T>`, or `Array3<T>`, according to the number of sizes:

```nomad
line = new Array<int> 10
grid = new Array<string> 3 4
volume = new Array<float> 2 3 4
total = grid.count # 12
```

Every explicit dimension must be positive. Zero, negative dimensions, and
element-count overflow produce runtime errors, not compile-time errors.
Elements start with their type's default value: zero, `false`, empty strings,
distinct empty lists, or shared empty arrays. Internal empty-array defaults have
zero in every dimension; they cannot be explicitly constructed with `new`.
The read-only `count` property gives the total element count. One-, two-, and
three-dimensional arrays support reading and replacing elements with zero-based
`int` indices:

```nomad
line[0] = 123
value = line[0]
grid[1 2] = "hello"
word = grid[1 2]
letter = grid[1 2][0] # "h"
volume[1 2 3] = 1.5
sample = volume[1 2 3]
```

Negative indices and indices greater than or equal to the corresponding
dimension's size log an error.
Invalid reads return the element type's default value; invalid writes leave the
array unchanged. Each invalid read of a list-valued element returns a new empty
list. Indexed assignment never resizes the array. Object-valued elements keep
reference semantics. Multiple indices are separated by whitespace, not
commas. Use parentheses when necessary, such as `grid[0 (-1)]`, to keep a
negative second index separate from subtraction.

## C++ array reads

The `Array1dObject`, `Array2dObject`, and `Array3dObject` getters accept indices
followed by a `RuntimeValue& result`. Pass an empty result: valid reads copy the
element and invalid reads initialize its type's default value.
The caller owns the result and must release it using the element type's
`freeValue` when `needsFree()` is true, including if initialization or copying
throws. Results remain valid independently of subsequent reads or destruction
of the array.
