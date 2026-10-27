# Logging module

Select `nomad-log` in `nomad.toml` for structured logging:

```toml
[nomad]
modules = ["nomad-log"]
```

Keep the project's existing `nomad.version`. The module is independent of
`nomad-system`, `nomad-game`, and `nomad-os`; none loads it automatically.
For a game using logging, select `["nomad-game", "nomad-log"]`.

## Logging messages

The open `log` hub provides these functions. Each takes a `message:string`
and returns `void`:

| Function | Behavior |
| --- | --- |
| `log.debug` | Write a diagnostic message. |
| `log.info` | Write an informational message. |
| `log.warning` | Write a warning. |
| `log.error` | Write an error and flush pending messages. |
| `log.critical` | Write a critical error and flush pending messages, without stopping execution. |

```nomad
log.info "Application started"
log.warning $"Unexpected score: {score}"
```

Messages include a timestamp, severity, and script file, function, and line.
The default minimum level is `info`, so debug messages are omitted unless the
host enables debug logging. The default sink writes to the console; error and
critical messages use standard error.

Debug, info, and warning messages are buffered until the host flushes them.
The game runtime flushes during its loop; C++ hosts can call `nomad::log::flush`.
Fatal logging is available to C++ hosts only; there is no `log.fatal` script API.

The hub is open, so scripts may add their own logging functions:

```nomad
open hub log
    fun playerJoined name:string
        log.info $"Player joined: {name}"
    end
end
```

This module supplies APIs, not an application run callback.
See [Runtime modules](modules.md) for startup requirements and
[Debugging](debugging.md) for the in-game log console.

## Plain output

The fundamental built-in `print message` writes plain text followed by a newline
to standard output. It does not use logging sinks or levels and does not add
timestamps or script locations. It works even with `nomad.modules = []`:

```nomad
print "Hello"
print $"Score: {score}"
```

Existing projects using `log.info` through `nomad-system` must either select
`nomad-log` explicitly or use `print` when plain output is sufficient.
