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

[nomad]
version = "0.1.0"
modules = ["game"]

[game]
entry = "main"
debug = false

[resources]
directory = "res"

[window]
title = "Example Game"
resolution = [1280, 720]
size = [1600, 900]
fullscreen = false
splash-image = "splash.png"

[audio]
directories = [
    "audio",
    "mod/audio",
]

[text]
directories = [
    "text/text.csv",
    "text/expansion.csv",
]

[scripts]
directories = [
    "res/scripts",
    "mods",
]

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
| `game.entry` | Game script entry function taking no arguments and returning `void`. | Optional; `"main"` |
| `nomad.version` | Required SDK version in canonical `major.minor.patch` form; packaging requires this exact runtime version. | Required |
| `nomad.modules` | Requested native modules; dependencies are included automatically. Use `[]` for the bare runtime. | Required |
| `game.debug` | Whether diagnostic logging and the in-game debug console are enabled. | Optional; `false` |
| `resources.directory` | Game-module asset directory relative to the project root. Does not affect script discovery. | Optional; `"res"` |
| `window.title` | Initial game window title. Scripts may change it after startup. | Optional; `project.name` |
| `window.resolution` | Initial logical rendering resolution as `[width, height]`. | Optional; `[800, 600]` |
| `window.size` | Initial window size as `[width, height]`. | Optional; `window.resolution` |
| `window.fullscreen` | Whether the game starts in fullscreen mode. | Optional; `false` |
| `window.splash-image` | Image displayed while resources and scripts load, relative to the resource directory. | Optional |
| `audio.directories` | Resource-relative directories recursively scanned for sound effects. | Optional; `["audio"]` |
| `text.directories` | Resource-relative CSV localization files loaded in order. | Optional; `["text/text.csv"]` |
| `scripts.directories` | Project-relative directories recursively scanned for Nomad scripts. | Optional; `["scripts"]`; game templates use `["res/scripts"]` |
| `sprites.directories` | Resource-relative directories recursively scanned for sprite PNG files. | Optional; `["sprites"]` |
| `sprites.max-texture-size` | Maximum width and height of each packed texture page. | Optional; `2048` |
| `sprites.padding` | Transparent pixels reserved between adjacent packed sprites. | Optional; `1` |
| `package.output` | Package output directory relative to the project root. | Optional; `"dist"` |
| `package.exclude` | Resource-relative exclusion patterns; omitted or `[]` includes all resources. | Optional; `[]` |

The `[game]`, `[resources]`, `[window]`, `[audio]`, `[text]`, `[scripts]`, `[sprites]`, and `[package]` tables may be omitted. Defaults apply
only to missing fields; invalid types and empty strings are rejected.
Development projects are identified by `project.executable`. A configuration
without it is a packaged configuration and must omit `[package]`.

Game, window, audio, text, sprite, and resource-directory settings are validated
by the game module when it is selected, including during packaging and dry runs.
Other projects preserve those settings without interpreting them.

See [runtime modules](modules.md) for module selection, dependency ordering,
SDK layout, and packaged applications. Set `nomad.modules` to
`["system"]` for non-graphical script APIs or `[]` for the fundamental
language runtime only. Running an application requires a module with a run
callback; `system` alone only supplies script APIs, not an application
entry point. The former `project.entry` field is rejected; move it to `game.entry`.

`window.resolution` and `window.size` must each contain exactly two positive
integers. When `window.size` is omitted, the window uses the configured logical
resolution. The splash image path must be a non-empty, resource-relative file
path and cannot escape the resource directory. Explicit window settings are
retained in packaged configuration files.

`--debug` and `--no-debug` override `game.debug` for an individual run.
See the [debugging guide](debugging.md) for the in-game console, physics debug
drawing, and persisted debug settings.

## Audio directories

`audio.directories` contains directories relative to `resources.directory`.
Nomad recursively discovers WAV, Ogg Vorbis, MP3, and FLAC files beneath each
configured directory. Set the property to an empty array to disable automatic
sound-effect discovery.

Configured directories must be non-empty resource-relative paths. Absolute
paths, paths that escape through `..`, duplicates, and overlapping directories
are rejected. When `[audio]` is omitted, Nomad uses `["audio"]`. A missing
implicit default directory is allowed, while a directory listed explicitly
must exist in a development project. Packaged projects allow configured
directories to remain absent until a mod provides them.

Resource names omit the file extension and replace directory separators with
dots. For example, `audio/player/jump.ogg` is named `audio.player.jump`.
Names are unique case-insensitively across every configured root and supported
format, so `audio/hit.mp3` and `audio/Hit.wav` cannot coexist.

## Text directories

`text.directories` contains CSV localization files relative to
`resources.directory`. Files are loaded in order, and values in later files
replace values with the same language and key from earlier files. Set the
property to an empty array to disable text loading.

Each configured path must be a non-empty resource-relative path to a `.csv`
file. Absolute paths, paths that escape through `..`, and duplicate paths are
rejected. When `[text]` is omitted, Nomad loads `text/text.csv`.

See the [text and localization guide](text.md) for the CSV format, script
constants, override order, and long-form text files.

## Script directories

`scripts.directories` contains directories relative to the project root,
independently of the game module's `resources.directory`.
Nomad recursively loads `.nomad` files from each directory before compiling
the configured entry function. Set the property to an empty array to disable
script loading.

Configured directories must be non-empty project-relative paths. Absolute
paths, paths that escape through `..`, duplicates, and overlapping directories
are rejected. When `[scripts]` is omitted, Nomad uses `["scripts"]`.
Generated game projects explicitly use `["res/scripts"]`.

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
game and window settings, and explicit audio, text, script, and sprite configuration. It omits `project.executable`,
and `[package]`. The game module sets `resources.directory` to `res` and
packages its assets there. Script roots inside the asset directory are remapped
to the corresponding packaged paths; other script roots retain their
project-relative locations, except roots inside a selected module's script
directory, which follow that module under `modules/<name>/`. Projects without
the game module package their
configured script directories without interpreting game asset settings.

Selected modules retain their manifests and discovered `.nomad` sources under
`modules/<name>/`. Their script directories are loaded automatically from the
package-local manifests; the application does not need to list them separately.
Shared native dependencies remain at the package root. Packages do not fall back
to development SDK modules.

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
