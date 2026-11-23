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
| `game.debug` | Whether the in-game debug console and physics debug drawing are enabled. Does not change logging settings. | Optional; `false` |
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

The selected `log` module applies `[log]` and `[[log.sinks]]` settings in
application mode, replacing existing sinks. Analysis and packaging validate
without changing the host logger. See the [logging schema](nomad-log.md#project-logging-schema)
for accepted values and defaults.

See [runtime modules](modules.md) for module selection, dependency ordering,
SDK layout, and packaged applications. Set `nomad.modules` to
`["system"]` for non-graphical script APIs or `[]` for the fundamental
language runtime only. Running an application requires a module with a run
callback; `system` alone only supplies script APIs, not an application
entry point.

`window.resolution` and `window.size` must each contain exactly two positive
integers. When `window.size` is omitted, the window uses the configured logical
resolution. The splash image path must be a non-empty, resource-relative file
path and cannot escape the resource directory. Explicit window settings are
retained in packaged configuration files.

`--debug` and `--no-debug` override `game.debug` for an individual run.
See the [debugging guide](debugging.md) for the in-game console, physics debug
drawing, and persisted debug settings.

Configured asset and script paths must be non-empty, relative to their base
directory, remain inside it, and not repeat. Directory roots also cannot overlap.
Audio, text, and sprite paths are resource-relative; script paths are
project-relative.

For audio and sprites, a missing implicit default directory is allowed. Explicit
directories must exist in development projects; packaged games allow them to
remain absent until a mod supplies them.

## Audio directories

`audio.directories` contains directories relative to `resources.directory`.
Nomad recursively discovers WAV, Ogg Vorbis, MP3, and FLAC files beneath each
configured directory. Set the property to an empty array to disable automatic
sound-effect discovery.

See [Sound and music](sound.md) for resource names and playback.

## Text directories

`text.directories` contains CSV localization files relative to
`resources.directory`. Files are loaded in order, and values in later files
replace values with the same language and key from earlier files. Set the
property to an empty array to disable text loading.

Entries must name `.csv` files. When `[text]` is omitted, Nomad loads
`text/text.csv`.

See the [text and localization guide](text.md) for the CSV format, script
constants, override order, and long-form text files.

## Script directories

`scripts.directories` contains directories relative to the project root,
independently of the game module's `resources.directory`.
Nomad recursively loads `.nomad` files from each directory before compiling
the configured entry function. Set the property to an empty array to disable
script loading.

When `[scripts]` is omitted, Nomad uses `["scripts"]`.
Generated game projects explicitly use `["res/scripts"]`.

## Sprite directories

`sprites.directories` contains directories relative to `resources.directory`.
Nomad recursively discovers PNG files beneath each configured directory. Set
the property to an empty array to disable automatic sprite discovery.

See the [sprite guide](sprites.md) for directory layout, resource naming,
animation conventions, and packing behavior.

`sprites.max-texture-size` must be a positive integer. Logical sprite groups
that do not fit in one texture are split into multiple pages of at most this
width and height.

`sprites.padding` must be a non-negative integer. It specifies the exact
transparent distance between adjacent packed sprites; padding is not added at
the outer edge of a texture page.

## Packaged configuration

Packages contain a reduced `nomad.toml` without `project.executable` or
`[package]`. Project identity, required SDK version, entry function, and explicit
module settings are retained.

The game module places assets under `res` and remaps script roots within them.
Other script roots keep their project-relative locations; module-owned scripts
travel with their modules. Raw sprites are packed automatically.
See [module packaging](modules.md#packaging) and
[sprite packaging](sprites.md#source-and-compiled-sprite-roots) for the resulting
layout.
