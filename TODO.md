# TODO
This the author wants to do

## Open
[ ] Splash image / default titles & resolution
[ ] Sound!!!
[ ] Display physics bodies in debug mode
[ ] Debug mode should only come up when the `--debug` parameter is passed.
[ ] Indexer notation.
[ ] Persist scene
[ ] First script should start at index 1 instead of 0 in decompiler (instructions.txt).
[ ] LSP
[ ] Command overloading (WIP)

## Completed
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

## Defered
[ ] Strings might leak if a script faults
[ ] Borrowed (no-copy) getter for dynamic string variables. Deferred: as of 2026-10 the game scripts read string dynamics 0 times in `==`/`!=` and 6 times in format strings. Constraints if revisited:
    - Opt-in third callback per variable; not every getter can lend (`Scene::getName()` returns by value, entity getters fall back to a default when there is no entity).
    - Only safe where nothing runs between read and use: format-string segments, and `==`/`!=` when the other operand is side-effect free.
    - Never for command/script arguments: a later argument or the call itself may change the source.