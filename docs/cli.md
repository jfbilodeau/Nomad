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
| `nomad version` | Print the SDK version. |

Project discovery searches the requested directory and its parents for
`nomad.toml`. Commands reject projects requiring a newer SDK.
Packaging additionally requires an exact match between the runtime bundle
version and `nomad.version`.

For `run`, the first `--` ends launcher options. Everything after it, including
option-looking strings, belongs to the application. With `nomad-os` selected,
scripts read these strings through `os.argv`; the executable, project directory,
launcher options, and separator are excluded. For example,
`nomad run --debug -- "Player One" --help` supplies two application arguments.

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
resource directory. Project modules supply their own validation rules. For
game projects, checks require `game.entry` to exist, take no arguments, and
return `void`.

The compiler initializes a headless engine with dummy video/audio drivers.
Engine functions and constants remain available. Diagnostics include source
locations; compilation errors return a nonzero exit code.

Instruction dumps include script functions, methods, constructors, and generated
class initializers. `--function` limits the dump to a named free function.

Generated API Markdown includes script classes, their constructor overloads,
read/write fields (including fields inferred from constructor assignments), and
ordinary method overloads with parameter and return types. Qualified class names
are preserved. Nested callbacks and generated initializers are omitted.
Documentation generation does not execute the scripts.

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

The server supports compiler diagnostics, API and project-function completion,
local variable and parameter suggestions, object member completion, and hover
documentation. Suggestions and diagnostics reflect unsaved editor changes and
use the nearest project configuration. Scripts are never executed. See the
[VS Code extension guide](https://github.com/jfbilodeau/Nomad/blob/main/editor/README.md)
for setup and current limitations.

Local and parameter suggestions and hover information also cover script methods
and constructors. Their receiver is available as `this`.
Member suggestions and hover include script fields and method overloads, filtered
by the receiver's type. Stored-field chains such as `owner.child.name` retain
their member types, and unsaved class changes update suggestions across the project.

## Runtime

`nomad-runtime` discovers the current project's configuration, initializes scripts,
and invokes its runnable module. The game module executes `game.entry` (default
`main`) before entering its event loop. `--project-path <directory-or-nomad.toml>` selects a project
instead of discovering one from the current directory. Resource paths are
resolved from that project's configuration. `--debug` enables diagnostic logging and the debug console;
`--no-debug` disables project-configured debug mode.

Direct launches accept application arguments after `--` as well:
`nomad-runtime --project-path MyProject -- "Player One" --help`.
Packaged executables use the same convention: `MyGame -- "Player One" --help`.
After the separator, `--help`, `--project-path`, `--debug`, and `--no-debug` are
application data, not runtime options. Without `--`, `os.argv` is empty.
See [nomad-os](nomad-os.md) for startup environment and path snapshots.

Run `nomad run --debug` and press the grave/tilde key (`~`) in the game window
to open the debug console. Runtime log messages appear in its **Logs** panel.
Press `~` again to close it. The key has no special effect when debug mode is
disabled.

Runtime messages are written to the terminal that launched `nomad run`,
including on Windows. Game applications also expose logs through the in-game
debug console.

See the [debugging guide](debugging.md) for console controls, physics debug
drawing, and persisted debug settings.
