# Collections module

Select `collections` in `nomad.toml` to use typed lists, fixed-size
arrays, maps, and key/value pairs:

```toml
[nomad]
modules = ["collections"]
```

Add other modules to the same list when needed; for example, use
`["game", "collections"]` in a game. See [Runtime modules](modules.md) for setup
and [C++ embedding](embedding.md) for host integration.

## Read-only collections

Call `toReadOnly` on a list, map, or array to create a new read-only snapshot:

```nomad
items = new List<int>
items.append 10
snapshot = items.toReadOnly
items[0] = 20
assert snapshot[0] == 10
```

| Source type | Snapshot type |
| --- | --- |
| `List<T>` | `ReadOnlyList<T>` |
| `Map<string V>` | `ReadOnlyMap<string V>` |
| `Array1<T>` | `ReadOnlyArray1<T>` |
| `Array2<T>` | `ReadOnlyArray2<T>` |
| `Array3<T>` | `ReadOnlyArray3<T>` |

Snapshots preserve the source's entries, element types, and array dimensions.
Later replacements, insertions, or removals in the source do not affect the
snapshot. Each call creates a separate container, including for empty sources.
Copying is shallow: stored objects share references, so a nested mutable
collection can still be changed through an element read from the snapshot.

Read-only collections retain `count` and indexed reads. Read-only lists and
one-dimensional arrays support iteration. Read-only maps retain `contains`,
`keys`, `values`, and `entries`; the latter three return independent, writable
array snapshots just as they do on mutable maps. Object values in those arrays
still share references.

Indexed writes and mutating methods are unavailable. There are no script
constructors for read-only collections and no implicit conversion between
mutable and read-only collection types. Use the snapshot type in function
parameter annotations. Default-initialized read-only collections, such as array
elements, are empty. Assigning a read-only collection shares that snapshot;
it does not make another copy.

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
references remain valid. The list can be reused after clearing.

See [Class types and members](language.md#class-types-and-members) for
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

Keys are case-sensitive strings, including empty strings. Values can have any
public, non-void type supported by collections. Assignments share the same map;
default-initialized map values are distinct empty maps. Indexed assignment
inserts or replaces an entry.

Reading a missing key logs an error and returns the value type's default without
inserting anything. Missing list- or map-valued entries return distinct empty
collections. Use `contains` to distinguish a missing key from a stored default.

| Method | Result |
| --- | --- |
| `contains key` | Whether the key exists, without logging on a miss. |
| `remove key` | `true` if removed; `false` without logging if absent. |
| `clear` | Removes all entries and returns `void`. |
| `keys` | Writable array snapshot of keys, each appearing once. |
| `values` | Writable array snapshot with one value per entry, including duplicates. |
| `entries` | Writable array snapshot of fresh `KeyValue<string V>` pairs. |

```nomad
hasHero = scores.contains "hero"
removed = scores.remove "guest" # false
entries = scores.entries
entry = entries[0]
name = entry.key
score = entry.value
scores.clear
```

Snapshot order is unspecified. Separately retrieved key and value arrays are not
guaranteed to align; use `entries` to keep each pair together.

Each snapshot is independent of later map changes and survives clearing or
releasing the map. Editing its array does not change the map. Object values are
shared references, not deep copies. Empty maps produce empty arrays. Removing
entries releases the map's references without invalidating values held elsewhere.

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
