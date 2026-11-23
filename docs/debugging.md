# Debugging

Nomad's debug mode enables the in-game debug console and
Box2D physics debug drawing. These facilities are disabled in normal game
runs.

Debug mode does not change logging levels or sinks. Configure logging separately
through the [log module](nomad-log.md).

## Enable debug mode

Enable debug mode for a project in `nomad.toml`:

```toml
[game]
debug = true
```

For an individual run, command-line options override the project setting:

```console
nomad run --debug
nomad run --no-debug
```

`--debug` enables debug mode and `--no-debug` disables it. When neither option
is present, Nomad uses `game.debug`, which defaults to `false`.

## Debug console

Select [log](nomad-log.md) explicitly to write structured messages from
scripts using the `log` hub. Engine diagnostics remain available without that
module. Built-in `print` writes directly to standard output, not the log console.

While debug mode is enabled, press the grave/tilde key (`~`) to open or close
the debug console. Use the key normally located to the left of `1`, even when
your keyboard layout shows a different character. It has no special effect when
debug mode is disabled.

The toolbar provides these controls:

- **Pause** or **Resume** suspends or resumes game updates.
- **Step** advances one update while the game is paused.
- **UI scale** changes the console's display scale.
- **Physics debug draw** shows or hides Box2D collision geometry.

The console exposes runtime logs in its **Logs** panel and provides game
inspection panels. Runtime messages also appear in the launching terminal,
including on Windows.

## Physics debug drawing

Physics debug drawing is hidden by default. Select **Physics debug draw** in
the console toolbar to show it without changing the rest of the debugging
facilities. The setting affects every scene and takes effect on the next
rendered frame.

Nomad code can read or change the same setting:

```nomad
game.debug.draw.visible = false
```

Physics debug drawing is never rendered when debug mode is disabled,
regardless of the saved visibility setting.

## Persisted settings

The following dynamic variables are saved automatically in `debug.json`:

| Variable | Behavior | Default |
| --- | --- | --- |
| `game.debug.console.visible:bool` | Show or hide the debug console. | `false` |
| `game.debug.console.scale:float` | Set the console UI scale from `0.8` to `2.0`. | `1.25` |
| `game.debug.draw.visible:bool` | Show or hide physics debug drawing. | `false` |

`debug.json` is stored in the SDL preference directory selected by
`game.organization` and `game.name`. Changes are persisted after both values
are set. These settings do not enable debug mode; use `game.debug` or
`--debug` for that.
