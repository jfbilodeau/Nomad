# Game module

Select `nomad-game` in `nomad.toml` for windows, input, graphics, audio, scenes,
entities, and game resources:

```toml
[nomad]
modules = ["nomad-game"]
```

Keep the project's existing `nomad.version`. The module automatically loads
[nomad-system](nomad-system.md). Collections are separate: add
`nomad-collections` explicitly when a game uses lists or arrays.
See [Runtime modules](modules.md) for module setup.

## Startup and configuration

The game module prepares resources and scripts before the project's entry
function runs. After that function returns, the game event loop handles input,
updates, and rendering until shutdown is requested.

Configure the initial window with `window.title`, `window.resolution`,
`window.size`, and `window.fullscreen`. Set `resources.directory` to choose the
resource root. See [Project configuration](configuration.md) for defaults,
resource directories, and validation rules.

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
