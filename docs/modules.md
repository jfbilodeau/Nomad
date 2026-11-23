# Runtime modules

Modules add native types, functions, and application services to the scripting
language. Select the modules a project needs; their APIs become available to
scripts, the compiler, and editor diagnostics.

Nomad projects select native modules in `nomad.toml`:

```toml
[nomad]
version = "0.1.8"
modules = ["game"]
```

The SDK provides:

| Module | Features |
| --- | --- |
| [collections](nomad-collections.md) | Typed lists, fixed-size 1D/2D/3D arrays, and string-keyed maps with indexed access. |
| [log](nomad-log.md) | Structured logging through the open `log` hub. |
| [system](nomad-system.md) | Math functions, conversions, and orderly shutdown. |
| [game](nomad-game.md) | Windows, input, graphics, audio, scenes, entities, and game resources. Requires `system`. |
| [os](nomad-os.md) | Startup arguments, environment, platform and architecture enums, and paths through the `os` hub. Requires `collections`. |

Dependencies are selected automatically. Listing only `game` also
loads `system`. A system-only project can select `["system"]`
without opening a game window. An empty list selects only the fundamental
language runtime, including built-in strings and `print`.

`log` is independent of the other modules. Select it explicitly for structured
logging; neither `system` nor `game` loads it automatically.

Selecting `os` automatically loads `collections`. It does not require
the game or system modules and does not open a window.

`nomad.modules` is required in development and packaged configurations.
There is no implicit module.

## SDK modules

Development commands use the modules in the selected SDK:

```text
modules/
  game/
    module.toml
    scripts/
  system/
    module.toml
```

Each module directory contains its native library, supporting libraries,
packaging inventory, licenses, and a `module.toml` manifest. For example:

```toml
schema = 1

[module]
name = "game"

[nomad]
modules = ["system"]
```

Module names start with a lowercase ASCII letter and contain only lowercase
ASCII letters, digits, and hyphens. Names do not contain paths or extensions.
Native filenames include a `.nomad` marker before the platform extension:
`game.nomad.dll` on Windows, `libgame.nomad.so` on Linux, and
`libgame.nomad.dylib` on macOS.

Keep each module's libraries and supporting files together when moving an SDK
or package. Use `--project-path` to launch a project from another directory.

Modules must be built for the same Nomad SDK, platform, architecture, and
compatible compiler/runtime settings. Native modules execute trusted native
code; only use SDKs and modules you trust.

The loader does not negotiate native C++ ABI versions.

## Module scripts

A native module can supply `.nomad` files under its fixed `scripts/` directory.
The directory is optional, must remain inside the module, and cannot be changed
through the manifest. Application roots remain configurable through
`scripts.directories`.

Selected modules and their transitive dependencies contribute their scripts
automatically to compilation, checking, instruction dumps, documentation, and
editor analysis. Discovery is recursive; overlapping application roots do not
load the same module file twice. Conflicting script names or module ownership
are errors.

Modules do not add a language namespace. Public functions, hubs,
classes, and enums keep their authored names and normal conflict rules.
Qualifying public APIs with a hub is recommended, not required. Module file
bodies are internal; declare a public function for callable helpers. Merely
loading or compiling a module does not execute its file bodies.

## Ordering and startup

Nomad visits the requested modules in listed order, visits their dependencies
in manifest order, and initializes dependencies before their dependents.
Shared dependencies are initialized once. Missing modules, invalid manifests,
and dependency cycles are errors.

Scripts are initialized after compilation and resource preparation, before the
runnable module starts. Initialization failure or a shutdown request prevents
application startup.

Exactly one configured module must provide a run callback to run an application.
Each runnable module owns its entry convention. The game module executes
`game.entry` (default `main`), which must take no arguments and return `void`,
then runs its main-thread event loop. API-only modules such as `system`
do not provide a runnable entry.

`system.exit` requests orderly shutdown after the current script invocation.

See [CLI](cli.md#runtime) for project selection and module options.
Startup errors produce a nonzero process exit status.

## Checking and editor diagnostics

The compiler and language server use the project's resolved modules and
configuration to expose the same script API as the running application.
They do not execute scripts or start the application loop.

Opening a project for analysis can load its configured native modules. Do not
enable language-server analysis for projects or SDKs you do not trust.

## Packaging

`nomad package` includes only the requested modules, their transitive module
dependencies, and required native libraries. It does not include every module
installed in the SDK.

The packaged `nomad.toml` contains the complete resolved module list in startup
order. Each module retains its native library, `module.toml`, and optional
`scripts/` directory under `modules/<name>/`. Shared native dependencies and
licenses remain at the application root.

Packaged applications resolve modules and their script directories from these
package-local manifests, without falling back to an SDK. Application script
directories do not need to list module sources, and resource exclusions do not
remove them.

Modules can transform their release settings and contribute files. Conflicting
outputs or failed contributions abort packaging. Packaging and dry runs do not
execute scripts. See [Sprites](sprites.md#source-and-compiled-sprite-roots) for
automatic sprite packing.
