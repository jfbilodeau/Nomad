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
| `nomad run [directory] [--debug\|--no-debug]` | Launch the sibling runtime from the project root. |
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

## Language server

`nomad-lsp` is a separate application, not a `nomadc` verb. With no arguments it
speaks LSP over standard input/output using `Content-Length` framing. The explicit
`--stdio` flag is also supported, as used by the VS Code language client. Logs go to
standard error. `--help`/`-h` prints usage and `--version`/`-v` prints the version;
unknown arguments are rejected.

The server supports compiler diagnostics, registered API and project-function completion, hover
documentation, full-text document synchronization, and UTF-16 positions.
It reuses compiler source batches, applies open editor buffers
over files on disk, and discovers the nearest project configuration. It never
executes scripts. See the
[VS Code extension guide](https://github.com/jfbilodeau/Nomad/blob/main/editor/README.md)
for setup and current limitations.

## Runtime

`nomad-runtime` discovers the current project's configuration and executes
`project.entry`. `--resource-path <directory>` overrides the configured
resource directory. `--debug` enables diagnostic logging and the debug console;
`--no-debug` disables project-configured debug mode.

Run `nomad run --debug` and press the grave/tilde key (`~`) in the game window
to open the debug console. Runtime log messages appear in its **Logs** panel.
Press `~` again to close it. The key has no special effect when debug mode is
disabled.

On Linux, runtime messages are also written to the terminal that launched
`nomad run`. On Windows the runtime is a GUI application, so it has no terminal
output even when the console-based `nomad` manager launches it. Use the in-game
debug console to inspect runtime logs on Windows. The manager and compiler
remain console applications on every platform.

See the [debugging guide](debugging.md) for console controls, physics debug
drawing, and persisted debug settings.
