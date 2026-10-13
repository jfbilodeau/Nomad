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

[window]
title = "Example Game"
resolution = [1280, 720]
splash-image = "splash.png"

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
| `window.title` | Initial game window title. Scripts may change it after startup. | Optional; `project.name` |
| `window.resolution` | Initial logical rendering resolution and window size as `[width, height]`. | Optional; `[800, 600]` |
| `window.splash-image` | Image displayed while resources and scripts load, relative to the resource directory. | Optional |
| `sprites.directories` | Resource-relative directories recursively scanned for sprite PNG files. | Optional; `["sprites"]` |
| `sprites.max-texture-size` | Maximum width and height of each packed texture page. | Optional; `2048` |
| `sprites.padding` | Transparent pixels reserved between adjacent packed sprites. | Optional; `1` |
| `package.output` | Package output directory relative to the project root. | Optional; `"dist"` |
| `package.exclude` | Resource-relative exclusion patterns; omitted or `[]` includes all resources. | Optional; `[]` |

The `[resources]`, `[window]`, `[sprites]`, and `[package]` tables may be omitted. Defaults apply
only to missing fields; invalid types and empty strings are rejected.
Development projects are identified by `project.executable`. A configuration
without it is a packaged configuration and must omit `[resources]` and `[package]`.

`window.resolution` must contain exactly two positive integers. The splash image
path must be a non-empty, resource-relative file path and cannot escape the
resource directory. Explicit window settings are retained in packaged
configuration files.

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
checked or loaded from a development project. Packaged projects allow a
configured directory to remain absent until a mod provides it.

Each directory is loaded independently. A root containing `catalog.json` uses
its pre-packed pages; a root without a catalog discovers and packs raw PNGs.
`nomad check` validates either representation and reports the physical texture
pages, sprite counts, and occupancy without uploading textures to the GPU.

`sprites.max-texture-size` must be a positive integer. Logical sprite groups
that do not fit in one texture are split into multiple pages of at most this
width and height. Each page shrinks to its smallest used non-power-of-two
dimensions.

`sprites.padding` must be a non-negative integer. It specifies the exact
transparent distance between adjacent packed sprites; padding is not added at
the outer edge of a texture page.

## Packaged configuration

Game packages contain a reduced `nomad.toml`. It retains the schema,
project identity and version, entry function, required Nomad version, explicit
window settings, and explicit sprite directory configuration. It omits `project.executable`,
`[resources]`, and `[package]`.
Packaged resources always live in `res`.

Packaging replaces the source PNGs in every nonempty raw sprite root with a
versioned `catalog.json` and adjacent packed PNG pages. Empty roots remain
catalog-free so they can accept dynamically installed mod sprites.

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
