# Nomad

> [!IMPORTANT]
> This project is a work in progress.

[![CI](https://github.com/jfbilodeau/Nomad/actions/workflows/ci.yml/badge.svg)](https://github.com/jfbilodeau/Nomad/actions/workflows/ci.yml)

Nomad is a compiled, strongly typed scripting language
designed for embedding in C++ applications, and a 2D game engine built around it.

Nomad is a bespoke language and engine built to suit my very specific brain. I don’t expect it to make sense to most people--maybe two other around the world, tops.

The project provides the standalone `Nomad::Language`,
`Nomad::Project`, and `Nomad::Engine` libraries, compiler tooling, a generic
runtime, and tests.

## Project manager

Both `nomad` and `nomadc` use `program <verb> [arguments] [options]`.
`--help`/`-h` and `--version`/`-v` are standalone top-level actions and cannot
be combined with a verb or other arguments. `help [verb]` and `version` are
also available as verbs. Use `<verb> --help` or `<verb> -h` for command-specific
help, including commands with required arguments. Options belong to individual
verbs; for example, `nomad run --force` is rejected.

`nomad init [directory]` creates a minimal project containing `nomad.toml` and
`res/scripts/init.nomad`, along with a starter `README.md` and `.gitignore`. It
can initialize a new directory or an existing directory, but refuses to
overwrite any generated file.

The default project is stored in `templates/projects/default` and is copied
beside the `nomad` executable during the build. Initialization recursively
copies that directory. Files ending in `.in` are rendered with project
metadata and written without the `.in` suffix; other files are copied
unchanged. This keeps the project layout and starter content out of the C++
implementation.

`nomad check [directory]` discovers the project, verifies that the running CLI
is at least as new as the required `nomad.version`, and runs the sibling
`nomadc` executable from the project root.

`nomad run [directory] [--debug]` performs the same project and SDK validation,
then launches the sibling `nomad-runtime` executable from the project root.

`nomad package [directory] [--force] [--dry-run]` checks the project and creates
a standalone ZIP in `package.output`, named
`<project.executable>-<platform>-<project.version>.zip` (for example,
`example-game-windows-x64-0.1.0.zip`). The platform identifies the installed
runtime bundle target; cross-target runtime selection is not yet supported.
It copies the project resources into temporary staging while
applying `package.exclude`, adds the runtime executable and its shared
libraries, and names the executable using `project.executable`. Packaging
refuses to replace an existing archive unless `--force` is supplied. Only that
archive is replaced; other files in the output directory are preserved.
`-f` is an alias for `--force`.
`--dry-run` lists the planned package files without creating or modifying the
package output. Archive entries are sorted and use normalized timestamps and
permissions. Only the ZIP is retained; extracting it places the runtime,
`nomad.toml`, and `res` at the extraction root. ZIP compression uses statically
linked libarchive and zlib; no external archive utility is required.

`nomad version` and `nomad --version` print the Nomad version. The version is
defined once by the root CMake project and generated into the C++ targets.

`nomad` and `nomadc` normalize command-line arguments to UTF-8 on Windows, so
project paths are not limited by the active system code page. SDL provides the
equivalent UTF-8 command-line boundary for `nomad-runtime`.

## Project configuration

Nomad projects use one `nomad.toml` file. Project discovery starts at the
requested path and searches its parent directories. Schema version 1 requires:

```toml
schema = 1

[project]
name = "Example Game"
identifier = "com.example.game"
version = "0.1.0"
executable = "example-game"
entry = "init"

[nomad]
version = "0.1.0"

[resources]
directory = "res"

[package]
output = "dist"
exclude = [
    "**/*.psd",
    "development/**",
]
```

`nomad.version` is the required Nomad SDK version and must use the canonical
`major.minor.patch` form. Nomad parses it as a comparable version so CLI
commands can reject projects that require a newer SDK.

`project.executable` is an extensionless file name. `project.entry` defaults
to `init` when omitted. All other fields shown above are required; exclusion
patterns are relative to the resource directory.

Packaged projects use the same schema with a reduced runtime manifest. The
release `nomad.toml` retains `schema`, project identity and version,
`project.entry`, and `nomad.version`. It omits `project.executable`,
`[resources]`, and `[package]`; packaged resources always live in `res`.

## Runtime

On Windows, the runtime is built as a GUI application, so launching a packaged
game does not open a console window. The `nomad` and `nomadc` tools remain
console applications.

The SDK's `runtime` directory contains a generated `runtime.json` bundle
manifest (distinct from a game's `nomad.toml`). Schema 1 declares the exact
Nomad `version`, `target`, and a `files` array of `{ "path": "...", "role": "..." }`
entries. Roles are `runtime`, `library`, and `license`. Paths are relative to
the bundle, use `/` separators, and cannot traverse outside it or use symbolic
links. Exactly one root-level runtime executable and at least one license are
required. Every declared file must exist; undeclared files are ignored.
Packaging requires the bundle version to match the project's `nomad.version`,
renames the declared runtime executable, and preserves the relative paths of
libraries and licenses. The internal bundle manifest is not included in games.
The build assembles license notices for Nomad and its core runtime dependencies
under `licenses`, including the vendored font-rendering dependencies.

`nomad-runtime` discovers `nomad.toml` from the current directory or one of
its parents. It resolves `resources.directory` relative to the project root
and executes the configured `project.entry` function. The
`--resource-path <directory>` option explicitly overrides the configured
resource directory, while `--debug` enables diagnostic logging and the debug
console. Normal runtime startup does not generate documentation or instruction
dumps. Those outputs belong to the headless `nomadc` tooling.

## Compiler tooling

`nomadc check [path]` compiles one `.nomad` file or recursively compiles a
directory without opening a window or executing any function. When no path is
provided, it discovers `nomad.toml` and checks the configured resource
directory. A project check also requires the configured entry function
(`init` by default) to exist, accept no parameters, and return `void`. An
explicit path remains usable without project entry validation.
It prints source-located diagnostics and returns a nonzero exit code when
compilation fails. The full engine API is available: the tool stands up a
headless `Game` on SDL's dummy video and audio drivers, using the project's
configured resources so `game.*`, `window.*`, `scene.*` and the `t.*` text
constants resolve exactly as they do in a windowed build.

`nomadc dump [path] [--function <name>]` performs the same
headless compilation and writes the generated VM instructions. It never runs
the selected function. Instructions are written as text.

`nomadc docs [path] --output <file>` (or `-o <file>`) compiles without
execution and writes the registered language and engine API documentation.
Documentation is written as Markdown.

## Build

Nomad requires CMake 3.30 or newer and a C++20 compiler.

Pinned third-party dependencies and their build configuration live in
`cmake/Dependencies.cmake`, including the specialized libarchive and zlib
configuration. Non-Windows builds enable libarchive's system iconv support for
UTF-8 ZIP filenames; on Ubuntu, iconv is provided by glibc. Windows uses native
character-set conversion.

```console
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

On Linux, install Ninja and use the corresponding `linux-debug` preset. The
`windows-release` and `linux-release` presets create optimized builds.
The `linux-sanitizers` preset enables AddressSanitizer and
UndefinedBehaviorSanitizer for Linux diagnostics.

To create runnable SDK and runtime ZIPs, build the `nomad-distribution` target
after configuring a preset. The archives and `SHA256SUMS.txt` are written to
`distribution/<configuration>` under the build directory:

- `nomad-sdk-<platform>-<version>.zip` contains `nomad`, `nomadc`,
  `nomad-runtime`, their shared libraries, project templates, language
  documentation, licenses, and the `runtime` bundle used for game packaging.
- `nomad-runtime-<platform>-<version>.zip` contains the runtime bundle with
  its `runtime.json`, shared libraries, and licenses.

Extract the SDK into one directory and invoke its `nomad` executable, or add
that directory to `PATH`. These distributions contain runnable tools, not
C++ embedding headers or development libraries. Use Release builds for
distribution; Debug archives use the same names in a separate configuration
directory.

For faster repeat builds, install `ccache`; CMake uses it automatically when
it is available:

```console
sudo apt install ninja-build ccache
```

## Debugging in VS Code

Launch configurations are provided for both platforms. The
`Nomad Runtime - MSVC *` and `Nomad Tests - MSVC` configurations debug the
Windows preset output, while `Nomad Runtime - WSL/Linux *` and
`Nomad Tests - WSL/Linux *` attach GDB to the matching Linux preset output.
Each configuration reads from its own preset build directory, so Windows
and Linux builds never share output.

To debug under WSL, reopen the folder in WSL using the
[Remote - WSL](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-wsl)
extension. Keep the repository in the WSL filesystem (for example,
`~/src/Nomad`) rather than under `/mnt/c`; CMake and compiler filesystem
operations are substantially slower on Windows-mounted directories.

Then choose a `WSL/Linux` configuration. Because the pre-launch
tasks build the *active* CMake preset, select the preset that matches the
configuration you are launching — for example `linux-sanitizers` before
starting `Nomad Tests - WSL/Linux Sanitizers (GDB)`.

Ubuntu 24.04 ships CMake 3.28 through `apt`, which is too old to read
`CMakePresets.json`. Install a newer CMake inside the distribution and make
sure it precedes `/usr/bin/cmake` on `PATH`:

```console
sudo snap install cmake --classic
sudo ln -sf /snap/bin/cmake /snap/bin/ctest /snap/bin/cpack /usr/local/bin/
```

See [the language documentation](docs/language.md) for the current Nomad
language reference.
