# Collections module

Select `nomad-collections` in `nomad.toml` to use typed lists, fixed-size
arrays, maps, and key/value pairs:

```toml
[nomad]
modules = ["nomad-collections"]
```

Keep the project's existing `nomad.version`. Add other modules to the same list
when needed; for example, use `["nomad-game", "nomad-collections"]` in a game
that uses collections. See [Runtime modules](modules.md) for module setup.

## Key/value pairs

`KeyValue<K V>` stores a key of type `K` and a value of type `V`. Both types
must be public, non-void types. Construct a pair by supplying both values:

```nomad
entry = new KeyValue<string int> "score" 100
name = entry.key # "score"
score = entry.value # 100
```

Type arguments can be omitted; the constructor infers both types from the
static types of its arguments:

```nomad
entry = new KeyValue "score" 100 # KeyValue<string int>
flags = new KeyValue 123 true # KeyValue<int bool>
nested = new KeyValue "flags" flags # KeyValue<string KeyValue<int bool>>
```

Explicit type arguments remain supported and must match the supplied values.
Partial type arguments are not supported, and the destination does not influence
inference.

The `key` and `value` properties are read-only: neither can be reassigned after
construction. Assignments share the same pair. Strings and scalar values retain
their original values; object keys and values share references rather than being
deep-copied. A referenced object can still be modified:

```nomad
numbers = new List<int>
entry = new KeyValue<string List<int>> "numbers" numbers
entry.value.append 42
# numbers[0] is now 42
```

Reading an object property keeps that object alive independently of the pair.
Pairs release their stored references when they are destroyed.

Default-initialized pairs, such as elements in an Array, are distinct instances
containing the default values of `K` and `V`. For example, a
`KeyValue<string int>` defaults to `""` and `0`. These defaults do not add a
parameterless script constructor: `new KeyValue<K V>` still requires both
arguments.

## Lists

Lists start empty, support `append` to add a value, and expose a read-only
`count` property:

```nomad
words = new List<string>
firstIndex = words.append "first" # 0
secondIndex = words.append "second" # 1
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

### Changing list contents

| Method | Result | Behavior |
| --- | --- | --- |
| `append value` | `int` | Adds at the end and returns the new element's zero-based index. |
| `prepend value` | `void` | Inserts at the beginning. |
| `insert index value` | `void` | Inserts before `index`; `index == count` appends. |
| `remove index` | `bool` | Removes the indexed element; returns `false` for negative or out-of-range indices. |
| `removeFirst` | `bool` | Removes the first element; returns `false` if empty. |
| `removeLast` | `bool` | Removes the last element; returns `false` if empty. |
| `clear` | `void` | Removes all elements; harmless if already empty. |

Insertion shifts later elements right; removal shifts them left. `insert`
accepts indices from zero through `count`, including zero for an empty list.
Invalid insertion indices cause a runtime error without changing the list.
Failed removals return `false` without logging or changing the list.
Successful removals return `true`, not the removed value.

```nomad
words.prepend "start"
words.insert 1 "middle"
removed = words.remove 1 # true
removed = words.removeFirst
removed = words.removeLast
words.clear
```

Removal and clearing release the list's references to stored objects; other
references remain valid. The list can be reused after clearing. `append`
replaces the former `push` name; `push` and `pop` are not available.

See [Object types and members](language.md#object-types-and-members) for
construction, reference semantics, and indexer syntax.

## Maps

Construct an empty map with `Map<string V>`, where `V` is its value type:

```nomad
names = new Map<string string>
scores = new Map<string int>
count = names.count # 0
scores["hero"] = 100
score = scores["hero"] # 100
scores["hero"] = 200 # Replaces the value; count stays 1
```

Only string keys are supported. The value type can be any public, non-void
type, including another collection. The read-only `count` property gives the
number of entries. Assignments share the same map instance; default-initialized
map values are distinct empty maps.

Indexed assignment inserts a new entry or replaces the value for an existing
key. Keys are case-sensitive strings, including empty strings. Object-valued
entries keep reference semantics.

Reading a missing key logs an error and returns the value type's default
without inserting an entry or changing `count`. Each missing read of a
list- or map-valued entry returns a distinct empty collection.
Use `contains` to check whether an entry exists, including entries holding a
default value such as zero. This returns a `bool` without logging or changing
the map:

```nomad
hasHero = scores.contains "hero" # true
hasGuest = scores.contains "guest" # false
```

Use `remove` to delete an entry. It returns `true` if the key existed, or
`false` without logging if it was absent:

```nomad
removed = scores.remove "hero" # true
removed = scores.remove "hero" # false
```

Removal decreases `count` and is visible through aliases. It releases the
map's reference to the value; other references to that object remain valid.

Use `clear` to remove all entries and reset `count` to zero:

```nomad
scores.clear
count = scores.count # 0
```

`clear` returns `void` and releases the map's references to its values; other
references remain valid. Aliases observe the empty map. Clearing an empty map
does nothing and does not log. New entries can be inserted afterward.

The parameterless `keys` method returns an `Array<string>` snapshot containing
each key once, in unspecified order:

```nomad
keys = scores.keys
keyCount = keys.count
```

An empty map returns an empty array. Changing array elements does not change
the map, and later map changes do not affect an existing snapshot. Each read
returns a separate array that remains valid after the map is cleared or released.

The parameterless `values` method returns an `Array<V>` snapshot. It contains one
value per entry, including duplicates, in unspecified order:

```nomad
scores["alice"] = 100
scores["bob"] = 200
values = scores.values
valueCount = values.count # 2
```

The parameterless `entries` method returns an `Array<KeyValue<string V>>`
snapshot. Each read creates a separate array of fresh pairs whose `key` and
`value` properties are read-only:

```nomad
entries = scores.entries
entry = entries[0]
name = entry.key
score = entry.value
```

Both methods return empty arrays for an empty map. Replacing snapshot elements
does not change the map. Later insertions, replacements, removals, or clearing of
the map do not change existing snapshots, which remain valid after the map is
released. There is no guaranteed correspondence between indices in separately
retrieved `keys`, `values`, or `entries` arrays; use an entry to keep its key and
value together.

Snapshots do not deep-copy object values. The map, values snapshot, and entry
pairs share references to those objects, so mutations to a referenced object are
visible through all of them. Replacing or removing a map entry does not invalidate
objects held by a snapshot.

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
