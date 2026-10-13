# CLI reference

Both tools use `program <verb> [arguments] [options]`.
`--help`/`-h` and `--version`/`-v` are standalone top-level actions.
`help [verb]` and `version` are also verbs. Use `<verb> --help` for
command-specific help. Options belong to individual verbs.

## Project manager

| Command | Behavior |
| --- | --- |
| `nomad init [directory]` | Create a minimal project; refuse to overwrite generated files. |
| `nomad check [directory]` | Validate project scripts and sprite packing through the sibling `nomadc`. |
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

Packaging compiles each nonempty raw sprite root into `catalog.json` and
adjacent packed PNG pages. Individual source PNGs from that root are omitted
from the archive. Roots that already contain a catalog are preserved, while
empty roots remain uncompiled for dynamically installed mods.

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

When `nomadc check` discovers a project, it also discovers and CPU-packs the
configured sprite directories without uploading textures to the GPU. The
report lists each physical texture page, its dimensions, logical and unique
sprite counts, and pixel occupancy. Sprite discovery, image decoding, naming,
collision, size, and packing failures produce a nonzero exit code. An explicit
`nomadc check <path>` remains a script-only check.

## Runtime

`nomad-runtime` discovers the current project's configuration and executes
`project.entry`. `--resource-path <directory>` overrides the configured
resource directory; `--debug` enables diagnostic logging and the debug console.

Run `nomad run --debug` and press `F12` in the game window to open the debug
console. Runtime log messages appear in its **Logs** panel. Press `F12` again
to close it.

On Linux, runtime messages are also written to the terminal that launched
`nomad run`. On Windows the runtime is a GUI application, so it has no terminal
output even when the console-based `nomad` manager launches it. Use the in-game
debug console to inspect runtime logs on Windows. The manager and compiler
remain console applications on every platform.
