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

`log` is independent of the other modules. Select it explicitly to use
`log.debug`, `log.info`, `log.warning`, `log.error`, and `log.critical`; selecting
`system` or `game` alone no longer provides `log.info`.

Selecting `os` automatically loads `collections`. It does not require
the game or system modules and does not open a window.

`nomad.modules` is required in development and packaged configurations.
There is no implicit module.

Projects using the previous `nomad-collections`, `nomad-game`, `nomad-log`,
`nomad-os`, or `nomad-system` identities must update their module lists to
`collections`, `game`, `log`, `os`, or `system`, respectively. Update module
manifest dependencies too, and rebuild native modules with the new filenames.
The SDK does not provide compatibility aliases for the old identities.

## SDK modules

Development commands use the modules in the selected SDK:

```text
modules/
  collections/
    module.toml
    ...
  log/
    module.toml
    ...
  system/
    module.toml
    ...
  game/
    module.toml
    ...
  os/
    module.toml
    ...
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
`libgame.nomad.dylib` on macOS. Engine libraries and launcher executable names
are unchanged.

Modules must be built for the same Nomad SDK, platform, architecture, and
compatible compiler/runtime settings. Native modules execute trusted native
code; only use SDKs and modules you trust.

## Module scripts

A native module can also supply `.nomad` files. Its manifest may select one
directory, relative to the module directory:

```toml
[scripts]
directory = "scripts"
```

The default is `scripts`. An absent default directory means the module has no
scripts; an explicitly configured directory must exist and be a directory.
The directory must remain below the module directory. This is separate from
the application's `scripts.directories` list.

Selected modules and their transitive dependencies contribute their scripts
automatically to compilation, checking, instruction dumps, documentation, and
editor analysis. Discovery is recursive and deterministic, using the same
source-loading rules as application scripts. Repeated paths are deduplicated.
If an application root also includes a module's files, they retain module
ownership rather than being loaded again as application scripts.

Module file-body identities use `$<owner>.<relative-file-stem>`. For example,
`os/scripts/main.nomad` becomes `$os.main`, and
`os/scripts/utilities/paths.nomad` becomes `$os.utilities.paths`. These identities
remain stable when the module directory moves. Application file-call names are
unchanged. Dotted-name collisions such as `utilities.paths.nomad` and
`utilities/paths.nomad`, or a file claimed by two modules, are errors.

Source ownership does not add a language namespace. Public functions, hubs,
classes, and enums keep their authored names and normal conflict rules.
Qualifying public APIs with a hub is recommended, not required. Module file
bodies are internal; declare a public function for callable helpers. Merely
loading or compiling a module does not execute its file bodies.

Diagnostics retain the actual source path. Open editor buffers replace matching
module files without changing their owner or relative identity. Opening a
module file alongside a consuming project includes that buffer in the project's
analysis.

Module-owned scripts are not yet included in application packages or copied
from module source directories into SDK bundles. Those packaging changes are
the next increment; for now, script discovery uses the installed module directory.

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
then runs its main-thread event loop. API-only modules such as `system`
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
