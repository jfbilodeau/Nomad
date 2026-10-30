# Game module

Select `game` in `nomad.toml` for windows, input, graphics, audio, scenes,
entities, and game resources:

```toml
[nomad]
modules = ["game"]
```

Keep the project's existing `nomad.version`. The module automatically loads
[system](nomad-system.md). Collections are separate: add
`collections` explicitly when a game uses lists or arrays.
See [Runtime modules](modules.md) for module setup.

## Startup and configuration

The game module prepares resources and scripts before startup. Once the host
has initialized script hub values, the game executes `game.entry` (default
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

Before scripts are compiled, the game module registers the `game` hub and its
`game.audio`, `game.music`, and `game.debug` hubs. The debug hub also contains
`game.debug.console` and `game.debug.draw`. Existing native function and
dynamic-variable names stay the same; these hubs group the APIs and can be
extended by scripts.

## Features and guides

| Feature | Guide |
| --- | --- |
| Script functions, properties, scenes, and entities | [Engine API](engine-api.md) |
| Sprite discovery, animations, and packing | [Sprites](sprites.md) |
| Sound effects and music | [Sound and music](sound.md) |
| Localized text and text resources | [Text and localization](text.md) |
| In-game console and physics debug drawing | [Debugging](debugging.md) |

## Running and packaging

Use `nomad run` to run a project. `--debug` and `--no-debug` override
`game.debug` for that run; supplying both is an error.

Use `nomad check` to validate scripts and game resources without starting the
event loop. `nomad package` includes the game module, its dependencies, and the
project's resources, including automatically packed sprites.
See [CLI](cli.md) for commands and packaging options.

Use `system.exit` to request orderly shutdown after the current script
invocation. The game module releases its script state during shutdown.

## C++ audio and font ownership

Include `nomad/game/resource/AudioObject.hpp` and
`nomad/game/resource/FontObject.hpp` when using their C++ object APIs. Their
script type names are `Audio` and `Font`; neither is script-constructible.

`AudioManager::createAudioObject` and `FontManager::registerFont` return object
pointers with one caller-owned reference. Release that reference when finished.
There are no audio-instance or font IDs to look up. `Entity::setFont` retains its
`FontObject`, and `Entity::getFont` returns a borrowed pointer.

Keep the game alive while using its resources. Game shutdown invalidates the
underlying resources even if a C++ object reference survives; such references
may still be released safely afterward.
