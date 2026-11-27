# Game module

Select `game` in `nomad.toml` for windows, input, graphics, audio, scenes,
entities, and game resources:

```toml
[nomad]
modules = ["game"]
```

The module automatically loads
[system](nomad-system.md). Collections are separate: add
`collections` explicitly when a game uses lists or arrays.
See [Runtime modules](modules.md) for module setup.

## Startup and configuration

After resources and scripts are initialized, the game executes `game.entry` (default
`main`). The entry must take no arguments and return `void`. After that function
returns, the game event loop handles input,
updates, and rendering until shutdown is requested.

Configure the initial window with `window.title`, `window.resolution`,
`window.size`, and `window.fullscreen`. Set `resources.directory` to choose the
asset root; this setting does not affect script locations. Script directories
are project-relative, with `scripts.directories = ["res/scripts"]` in generated
game projects. See [Project configuration](configuration.md) for defaults,
resource directories, and validation rules.

## Game API hubs

The API uses the `game`, `game.audio`, `game.music`, `game.debug`,
`game.debug.console`, and `game.debug.draw` hubs. Scripts can extend these open
hubs with their own declarations.

## Window

Use `game.window` to read or change the running game's window:

```nomad
game.window.title = "My game"
game.window.setSizeAndCenter 800 480
game.window.setResolution 800 480

window = game.window
mouse = window.mouse
resolution = window.resolution
mouse.x = 100.0
resolution.width = 960
```

Assignments share the same live window, mouse, and resolution objects. The
`game.window` reference and its `mouse` and `resolution` properties are read-only;
their writable members remain assignable. New windows cannot be constructed
with `new`. Access through a retained reference after the game is destroyed
raises a runtime error.

Window callbacks use the same receiver, for example `game.window.onClose` and
`game.window.onResize`. A close request does not stop the game automatically when
an `onClose` callback is registered.

Existing scripts must replace the old `window.*` APIs with `game.window.*`.
Window configuration keys in `nomad.toml` remain unchanged. Available properties,
methods, and callback signatures are listed in the [Engine API](engine-api.md).

## Features and guides

| Feature | Guide |
| --- | --- |
| Script functions, properties, scenes, and entities | [Engine API](engine-api.md) |
| Sprite discovery, animations, and packing | [Sprites](sprites.md) |
| Sound effects and music | [Sound and music](sound.md) |
| Localized text and text resources | [Text and localization](text.md) |
| In-game console and physics debug drawing | [Debugging](debugging.md) |

## Running and packaging

Use `nomad run`, `nomad check`, and `nomad package` to run, validate, and
distribute a game. See [CLI](cli.md) for options.

Use `system.exit` to request orderly shutdown after the current script
invocation. The game module releases its script state during shutdown.

## Fonts

Load a font and assign it to an entity:

```nomad
font = game.loadFont "fonts/main.ttf" 16
this.setText font "Hello"
```

`Font` objects cannot be created with `new`. Assignments share the same font,
which remains alive while a variable or entity references it.
For C++ object ownership, see
[object reference helpers](embedding.md#object-reference-helpers).
