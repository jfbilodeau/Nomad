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

## Geometry values

The game module supplies the root struct types `Point` and `Rectangle`:

```nomad
point = new Point 10.0 20.0
rectangle = new Rectangle 0.0 0.0 100.0 50.0
copied = rectangle
copied.move point.x point.y
assert rectangle.left == 0.0
assert copied.right == 110.0
```

`Point` has writable `float` fields `x` and `y`. `Rectangle` has writable `float`
fields `x`, `y`, `width`, and `height`; `x` and `y` denote its top-left position.
Use `new Point` or `new Rectangle` for zero-valued fields, or supply all fields
to the corresponding value constructor. `move deltaX deltaY` shifts a value
without changing its size.

Rectangle edge reads use `left`, `top`, `right`, and `bottom`. They reflect its
current fields: `right` is `x + width` and `bottom` is `y + height`. Dimensions
are not normalized or restricted. Struct assignment and arguments copy the
fields, so changing a copy does not change the original.

These root names are reserved by the selected game module; use another name or
a hub-qualified type for project-defined geometry. The types do not yet change
entity properties or automatically write geometry values back to engine objects.

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

## Scenes

Read `game.scene` to obtain the scene for the current script invocation:

```nomad
current = game.scene
current.loadInputMapping "controls"
current.createEntity entities.player 100.0 100.0 0
```

Repeated reads in the same scene return the same reference. A retained reference
continues to target that scene when another scene becomes current. Reading
`game.scene` outside a scene context raises a runtime error.

The following former `scene.*` functions are now methods on this reference:

- `addAnimatedTile`
- `createEntity`, `createEntityByName`, `createEntityByNameThen`, `createEntityThen`
- `createTextByNameEntity`, `createTextEntity`, `createTextEntityAndThen`
- `loadInputMapping`, `loadMap`
- `pauseAll`, `unpauseAll`, `removeSelf`

Their arguments and creation callbacks are unchanged. Creation and removal remain
queued. `removeSelf` does not invalidate the reference until removal is processed;
using it after removal or game destruction raises a runtime error. Scenes cannot
be constructed with `new`.

Scene properties, camera and event APIs, and custom `scene.*` variables retain
their existing syntax for now. Keep scene references in locals with another name,
such as `current`, because the `scene.*` prefix still denotes the legacy context.

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
