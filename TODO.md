# TODO
Things the author wants to do.

## Problems


## Open
[ ] File IO module.
[ ] Readonly collections
[ ] `doc` comments in Nomad.
[ ] Replace ref counting with garbage collector.
[ ] Game debug console--root should be Game/Inventory/Windows/etc.
[ ] Need to add `game.mouse.x/y`, `scene.mouse.x/y` and `this.mouse.x/y`
[ ] Configure logging via `nomad.toml`
[ ] Sample game
[ ] Native GUI?
[ ] Select text (`t.*`) language
[ ] `this.wait`, `scene.wait`, `game.wait`
[ ] Support to tween properties
[ ] `args`
[ ] MacOS
[ ] WASM
[ ] Indexer notation.
[ ] Persist/load scene
[ ] First decompiled script should start at index 1 instead of 0 in decompiler (instructions.txt). Or add a 'default' function at start that just stops execution.
[ ] Command overloading (WIP)
[ ] Auto publish Nomad plugin to VS Code extensions
[ ] Compile to binary
[ ] LLVM
[ ] "Nomad Doc" strings for script functions, enums, events, etc...
[ ] Gen game docs for modding
[ ] Implicit constructor name:
```nomad
a = new Array<int> 10  # Type named, `a` now `Array<int>
...
a = new 20  # Type for `a` is known. Reused.
```
 
## Completed
[X] Rename `init` to `main`
[X] `$"{1+2}"` in interpreter causes fault
[X] LSP
[X] Make runtime plugable (Game, system, etc...)
[X] Make `font` an intrinsic instead of ID.
[X] Display physics bodies in debug mode
[X] Automatic sprite texture packing
[X] Objects (intrinsic)
[X] Switch from `const NomadString&` to `NomadStringView`
[X] Ensure scripts that return a value early also have a valid `return` statement at the end.
[X] Add column number to parser
[X] Language unit tests
[X] Ensure there's no duplication between `execute()` and `parse`
[X] Rename Interpreter to something else.
[X] Replace exceptions in compiler with a CompilerContext.
[X] Implement debug command console.
[X] Replace Context/Var ID with single ContextVariableID
[X] Save/load variable context/dynamic
[X] Window events
[X] Enums
[X] Sound

## Defered
[ ] Strings might leak if a script faults
[ ] Borrowed (no-copy) getter for dynamic string variables. Deferred: as of 2026-10 the game scripts read string dynamics 0 times in `==`/`!=` and 6 times in format strings. Constraints if revisited:
    - Opt-in third callback per variable; not every getter can lend (`Scene::getName()` returns by value, entity getters fall back to a default when there is no entity).
    - Only safe where nothing runs between read and use: format-string segments, and `==`/`!=` when the other operand is side-effect free.
    - Never for command/script arguments: a later argument or the call itself may change the source.