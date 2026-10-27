# Runtime modules

Modules add native types, functions, and application services to the scripting
language. Select the modules a project needs; their APIs become available to
scripts, the compiler, and editor diagnostics.

Nomad projects select native modules in `nomad.toml`:

```toml
[nomad]
version = "0.1.8"
modules = ["nomad-game"]
```

The SDK provides:

| Module | Features |
| --- | --- |
| [nomad-collections](nomad-collections.md) | Typed lists, fixed-size 1D/2D/3D arrays, and string-keyed maps with indexed access. |
| [nomad-log](nomad-log.md) | Structured logging through the open `log` hub. |
| [nomad-system](nomad-system.md) | Math functions, conversions, and orderly shutdown. |
| [nomad-game](nomad-game.md) | Windows, input, graphics, audio, scenes, entities, and game resources. Requires `nomad-system`. |
| [nomad-os](nomad-os.md) | Startup arguments, environment, platform and architecture enums, and paths through the `os` hub. Requires `nomad-collections`. |

Dependencies are selected automatically. Listing only `nomad-game` also
loads `nomad-system`. A system-only project can select `["nomad-system"]`
without opening a game window. An empty list selects only the fundamental
language runtime, including built-in strings and `print`.

`nomad-log` is independent of the other modules. Select it explicitly to use
`log.debug`, `log.info`, `log.warning`, `log.error`, and `log.critical`; selecting
`nomad-system` or `nomad-game` alone no longer provides `log.info`.

Selecting `nomad-os` automatically loads `nomad-collections`. It does not require
the game or system modules and does not open a window.

`nomad.modules` is required in development and packaged configurations.
There is no implicit module.

## SDK modules

Development commands use the modules in the selected SDK:

```text
modules/
  nomad-collections/
    module.toml
    ...
  nomad-log/
    module.toml
    ...
  nomad-system/
    module.toml
    ...
  nomad-game/
    module.toml
    ...
  nomad-os/
    module.toml
    ...
```

Each module directory contains its native library, supporting libraries,
packaging inventory, licenses, and a `module.toml` manifest. For example:

```toml
schema = 1

[module]
name = "nomad-game"

[nomad]
modules = ["nomad-system"]
```

Module names start with a lowercase ASCII letter and contain only lowercase
ASCII letters, digits, and hyphens. Names do not contain paths or extensions.
Native filenames follow the platform convention: `nomad-game.dll` on Windows,
`libnomad-game.so` on Linux, and `libnomad-game.dylib` on macOS.

Modules must be built for the same Nomad SDK, platform, architecture, and
compatible compiler/runtime settings. Native modules execute trusted native
code; only use SDKs and modules you trust.

## Ordering and startup

Nomad visits the requested modules in listed order, visits their dependencies
in manifest order, and initializes dependencies before their dependents.
Shared dependencies are initialized once. Missing modules, invalid manifests,
and dependency cycles are errors.

All modules register their script features before compilation. After successful
compilation and resource preparation, the host executes the generated `$init`
function before invoking the runnable module. Initialization failure or a
shutdown request during initialization prevents the run callback.

Exactly one configured module must provide a run callback to run an application.
Each runnable module owns its entry convention. The game module executes
`game.entry` (default `main`), which must take no arguments and return `void`,
then runs its main-thread event loop. API-only modules such as `nomad-system`
do not provide a runnable entry.

`system.exit` requests orderly shutdown after the current script invocation.
Modules stop their background work before the application releases them.

The runtime accepts module command-line arguments; see each module's guide
for its supported options. Runtime startup errors are reported to the console
and produce a nonzero process exit status.
Use `--project-path <directory-or-nomad.toml>` to select a project explicitly
when launching the runtime from another directory.

## Checking and editor diagnostics

The compiler and language server use the project's resolved modules and
configuration to expose the same script API as the running application.
They do not execute the entry function or start the application loop.
Editor analysis resolves scripts without generating executable instructions.

Opening a project for analysis can load its configured native modules. Do not
enable language-server analysis for projects or SDKs you do not trust.

## Packaging

`nomad package` includes only the requested modules, their transitive module
dependencies, and required native libraries. It does not include every module
installed in the SDK.

Modules can transform their release settings and contribute generated or
copied files. Game packaging retains the existing automatic sprite packing.
Conflicting package outputs or failed module contributions fail the package
operation rather than publishing a partial archive.

The packaged `nomad.toml` contains the complete resolved module list in startup
order. Packaged applications load those libraries beside that configuration;
they do not need an SDK or the development module manifests. Module settings
are preserved unless explicitly transformed for release.
