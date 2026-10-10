# CLI reference

Both tools use `program <verb> [arguments] [options]`.
`--help`/`-h` and `--version`/`-v` are standalone top-level actions.
`help [verb]` and `version` are also verbs. Use `<verb> --help` for
command-specific help. Options belong to individual verbs.

## Project manager

| Command | Behavior |
| --- | --- |
| `nomad init [directory]` | Create a minimal project; refuse to overwrite generated files. |
| `nomad check [directory]` | Validate the project and delegate compilation to the sibling `nomadc`. |
| `nomad run [directory] [--debug]` | Launch the sibling runtime from the project root. |
| `nomad package [directory] [--force] [--dry-run]` | Create a standalone game ZIP using the included runtime bundle. |
| `nomad version` | Print the SDK version. |

Project discovery searches the requested directory and its parents for
`nomad.toml`. Commands reject projects requiring a newer SDK.
Packaging additionally requires an exact match between the runtime bundle
version and `nomad.version`.

`package --dry-run` lists the selected files without writing a package.
Resources are selected using `package.exclude`. Output is named
`<project.executable>-<platform>-<project.version>.zip`.
Entries are sorted with normalized timestamps and permissions.
`--force` (`-f`) replaces only the matching archive, preserving other output files.
Cross-target runtime selection is not supported.

## Compiler

| Command | Behavior |
| --- | --- |
| `nomadc check [path]` | Compile-check without running scripts or opening a window. |
| `nomadc dump [path] [--function <name>]` | Print VM instructions as text without execution. |
| `nomadc docs [path] --output <file>` | Generate language and engine API Markdown. `-o` aliases `--output`. |
| `nomadc version` | Print the compiler version. |

A path may name one `.nomad` file or a directory to compile recursively.
Without a path, the compiler discovers the current project and uses its
resource directory. Project checks validate that the entry function exists,
takes no arguments, and returns `void`.

The compiler initializes a headless engine with dummy video/audio drivers.
Engine functions and constants remain available. Diagnostics include source
locations; compilation errors return a nonzero exit code.

## Runtime

`nomad-runtime` discovers the current project's configuration and executes
`project.entry`. `--resource-path <directory>` overrides the configured
resource directory; `--debug` enables diagnostic logging and the debug console.

On Windows the runtime is a GUI application, so packaged games do not open a
console window. The manager and compiler remain console applications.
