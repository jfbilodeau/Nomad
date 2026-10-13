# Project configuration

Nomad uses `nomad.toml` at the project root. Paths below are relative to that
root, except exclusion patterns, which are relative to the resource directory.

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

[sprites]
directories = [
    "sprites",
    "mod/sprites",
]
max-texture-size = 2048
padding = 1

[package]
output = "dist"
exclude = [
    "**/*.psd",
    "development/**",
]
```

| Property | Description | Required / default |
| --- | --- | --- |
| `schema` | Configuration format version. | Required; `1` |
| `project.name` | Human-readable game name. | Required |
| `project.identifier` | Stable application identifier used to locate saved data. | Required |
| `project.version` | Game version used in package archive names. | Required |
| `project.executable` | Executable file name without extension or directory. | Required for development projects |
| `project.entry` | Script entry function taking no arguments and returning `void`. | Optional; `"init"` |
| `nomad.version` | Required SDK version in canonical `major.minor.patch` form; packaging requires this exact runtime version. | Required |
| `resources.directory` | Resource directory relative to the project root. | Optional; `"res"` |
| `sprites.directories` | Resource-relative directories recursively scanned for sprite PNG files. | Optional; `["sprites"]` |
| `sprites.max-texture-size` | Maximum width and height of each packed texture page. | Optional; `2048` |
| `sprites.padding` | Transparent pixels reserved between adjacent packed sprites. | Optional; `1` |
| `package.output` | Package output directory relative to the project root. | Optional; `"dist"` |
| `package.exclude` | Resource-relative exclusion patterns; omitted or `[]` includes all resources. | Optional; `[]` |

The `[resources]`, `[sprites]`, and `[package]` tables may be omitted. Defaults apply
only to missing fields; invalid types and empty strings are rejected.
Development projects are identified by `project.executable`. A configuration
without it is a packaged configuration and must omit `[resources]` and `[package]`.

## Sprite directories

`sprites.directories` contains directories relative to `resources.directory`.
Nomad recursively discovers PNG files beneath each configured directory. Set
the property to an empty array to disable automatic sprite discovery.

See the [sprite guide](sprites.md) for directory layout, resource naming,
animation conventions, and packing behavior.

Configured directories must be non-empty resource-relative paths. Absolute
paths, paths that escape through `..`, duplicates, and overlapping directories
are rejected. For example, `["sprites", "sprites/ui"]` is invalid because the
first entry already includes the second.

When the `[sprites]` table is omitted, Nomad uses `["sprites"]`. A missing
implicit default directory is allowed so projects without sprites remain
valid. A directory listed explicitly must exist when sprite resources are
checked or loaded. `nomad check` decodes and packs these resources on the CPU,
then reports the physical texture pages, sprite counts, and occupancy without
uploading textures to the GPU.

`sprites.max-texture-size` must be a positive integer. Logical sprite groups
that do not fit in one texture are split into multiple pages of at most this
width and height. Each page shrinks to its smallest used non-power-of-two
dimensions.

`sprites.padding` must be a non-negative integer. It specifies the exact
transparent distance between adjacent packed sprites; padding is not added at
the outer edge of a texture page.

## Packaged configuration

Game packages contain a reduced `nomad.toml`. It retains the schema,
project identity and version, entry function, required Nomad version, and
explicit sprite directory configuration. It omits `project.executable`,
`[resources]`, and `[package]`.
Packaged resources always live in `res`.

## Runtime bundle manifest

The SDK's `runtime/runtime.json` is separate from the game's configuration.
It declares schema 1, the exact Nomad version, the target platform, and
files with `path` and `role` fields. Roles are `runtime`, `library`, and `license`.

Paths are relative to the bundle, use `/` separators, cannot escape it, and
cannot be symbolic links. The inventory requires exactly one root-level
runtime executable and at least one license. Declared files must exist;
undeclared files are ignored.

Packaging renames the runtime to the game executable name and preserves
the relative paths of libraries and licenses. The internal `runtime.json`
is not included in the game package.
