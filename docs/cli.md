# CLI reference

The project manager and compiler use `program <verb> [arguments] [options]`.
`--help`/`-h` and `--version`/`-v` are standalone top-level actions.
`help [verb]` and `version` are also verbs. Use `<verb> --help` for
command-specific help. Options belong to individual verbs.

## Project manager

| Command | Behavior |
| --- | --- |
| `nomad init [directory]` | Create a minimal project; refuse to overwrite generated files. |
| `nomad check [directory]` | Validate project scripts and sprite packing through the sibling `nomadc`. |
| `nomad run [directory] [--debug\|--no-debug] [-- application-arguments...]` | Launch the sibling runtime from the project root, forwarding application arguments unchanged. |
| `nomad package [directory] [--force] [--dry-run]` | Create a standalone game ZIP using the included runtime bundle. |
| `nomad generate-bindings <module-directory> [--output <directory>] [--sdk <directory>] [--check]` | Generate experimental C++ enum and stored-field configuration interfaces from module scripts. |
| `nomad version` | Print the SDK version. |

Project discovery searches the requested directory and its parents for
`nomad.toml`. Commands reject projects requiring a newer SDK.
Packaging additionally requires an exact match between the runtime bundle
version and `nomad.version`.

For `run`, the first `--` ends launcher options. Everything after it, including
option-looking strings, belongs to the application. With `os` selected,
scripts read these strings through `os.argv`; the executable, project directory,
launcher options, and separator are excluded. For example,
`nomad run --debug -- "Player One" --help` supplies two application arguments.

`package --dry-run` lists the selected files without writing a package.
Resources are selected using `package.exclude`. Output is named
`<project.executable>-<platform>-<project.version>.zip`.
Entries are sorted with normalized timestamps and permissions.
`--force` (`-f`) replaces only the matching archive, preserving other output files.
Cross-target runtime selection is not supported.

### Experimental module bindings

`generate-bindings` reads `module.toml` and the module's scripts without running
them or loading native modules. Pass `--sdk <directory>` when the scripts depend
on other SDK modules.

The default output is `<module-directory>/generated`. An explicit `--output` is
relative to the current working directory. Commit the generated files:

- `include/nomad/generated/<module-name>/Bindings.hpp`
- `src/Bindings.cpp`
- `nomad-module.cmake`

Supported declarations are enums and configuration classes with primitive or
enum stored fields and a zero-argument script constructor. Methods, structs,
parameterized classes, and native properties are not supported.

`--check` reports stale or missing generated files with a nonzero exit code
without writing anything. Generation preserves unrelated output files and rejects
unsafe output paths or name collisions. CMake does not run generation automatically.
See [embedding](embedding.md#experimental-generated-script-bindings) for CMake
integration and wrapper usage.

Packaging automatically packs raw sprites; see [Sprites](sprites.md#source-and-compiled-sprite-roots).

## Compiler

| Command | Behavior |
| --- | --- |
| `nomadc check [path]` | Compile-check without running scripts or opening a window. |
| `nomadc dump [path] [--function <name>]` | Print VM instructions as text without execution. |
| `nomadc docs [path] --output <file>` | Generate language and engine API Markdown. `-o` aliases `--output`. |
| `nomadc version` | Print the compiler version. |

A path may name one `.nomad` file or a directory to compile recursively.
Without a path, the compiler discovers the current project and uses its
resource directory. Project modules supply their own validation rules. For
game projects, checks require `game.entry` to exist, take no arguments, and
return `void`.

Checks expose the configured module APIs without opening a window or executing
scripts. Diagnostics include source locations; errors return a nonzero exit code.

Instruction dumps include script functions, methods, constructors, and generated
class initializers. `--function` limits the dump to a named free function.

Generated API Markdown includes registered types, constructors, fields, methods,
and functions. Documentation generation does not execute scripts.

Project-aware checks also validate sprites and report packing results; see
[Inspect packing](sprites.md#inspect-packing).
An explicit `nomadc check <path>` remains a script-only check.

## Language server

`nomad-lsp` is a separate application. Run it with `--stdio` or no arguments to
serve LSP over standard input/output; logs go to standard error.
`--help`/`-h` prints usage and `--version`/`-v` prints the version.

It provides diagnostics, completion, hover documentation, and signature help using
the nearest project configuration and unsaved editor changes. Scripts are not
executed, but analysis can load trusted native modules. See the
[VS Code extension guide](https://github.com/jfbilodeau/Nomad/blob/main/editor/README.md)
for setup and current limitations.

## Runtime

`nomad-runtime` discovers the current project's configuration, initializes scripts,
and invokes its runnable module. The game module executes `game.entry` (default
`main`) before entering its event loop. `--project-path <directory-or-nomad.toml>` selects a project
instead of discovering one from the current directory. Resource paths are
resolved from that project's configuration. `--debug` enables the debug console and physics debug drawing;
`--no-debug` disables project-configured debug mode. Neither option changes logging settings.

Direct launches accept application arguments after `--` as well:
`nomad-runtime --project-path MyProject -- "Player One" --help`.
Packaged executables use the same convention: `MyGame -- "Player One" --help`.
After the separator, `--help`, `--project-path`, `--debug`, and `--no-debug` are
application data, not runtime options. Without `--`, `os.argv` is empty.
See [os](nomad-os.md) for startup environment and path snapshots.

See the [debugging guide](debugging.md) for runtime logs, console controls,
physics debug drawing, and persisted settings.
