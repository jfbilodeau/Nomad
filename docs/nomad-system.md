# System module

Select `nomad-system` in `nomad.toml` for logging, math functions, conversions,
and orderly application shutdown:

```toml
[nomad]
modules = ["nomad-system"]
```

Keep the project's existing `nomad.version`. This module does not open a game
window. It is also loaded automatically when a project selects `nomad-game`.
See [Runtime modules](modules.md) for module setup and startup behavior.

## Logging

Use `log.info` to write a message:

```nomad
log.info "Application started"
```

Messages include the script location. See [Debugging](debugging.md) for game
logging and the in-game console.

## Math and conversions

| Function | Behavior |
| --- | --- |
| `math.ceiling value` | Round a `float` upward, returning a `float`. |
| `math.floor value` | Round a `float` downward, returning a `float`. |
| `toFloat value` | Convert an `int` or `string` to a `float`. |
| `toInt value` | Convert a `float` or `string` to an `int`. |

String conversions return zero when the string does not start with a number.

```nomad
amount = toInt "42"
rounded = math.ceiling 2.3
log.info $"Amount: {amount}, rounded: {rounded}"
```

## Shutdown

Call `system.exit` to request orderly shutdown after the current script
invocation completes:

```nomad
system.exit
```

Modules stop their background work before the application releases them.
Without an event-loop module, an application also finishes normally when its
entry function returns.
