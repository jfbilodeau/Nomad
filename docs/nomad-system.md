# System module

Select `system` in `nomad.toml` for math functions, conversions,
and orderly application shutdown:

```toml
[nomad]
modules = ["system"]
```

This module does not open a game
window. It is also loaded automatically when a project selects `game`.
See [Runtime modules](modules.md) for module setup and startup behavior.

The `math` and `system` hubs are open. Scripts may add members using matching
`open hub math` or `open hub system` blocks.

## Plain output and logging

Use the built-in `print` function for plain output; it requires no module:

```nomad
print "Application started"
```

For structured logging, select the independent [log](nomad-log.md) module.

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
print $"Amount: {amount}, rounded: {rounded}"
```

## Shutdown

Call `system.exit` to request orderly shutdown after the current script
invocation completes:

```nomad
system.exit
```

This module supplies APIs only. Running an application requires a separate
runnable module, which owns its entry point and determines when execution ends.
