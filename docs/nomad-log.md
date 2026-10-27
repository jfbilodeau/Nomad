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
The default minimum level is `info` for both the logger and its sinks. Debug
messages require the host to lower both thresholds to `debug`.
The default sink writes to the console; error and
critical messages use standard error.

By default, debug, info, and warning messages are buffered until the host flushes them.
The game runtime flushes during its loop; C++ hosts can call `nomad::log::flush`.
Fatal logging is available to C++ hosts only; there is no `log.fatal` script API.

## C++ sink configuration

C++ hosts can create logger-owned console, file, and memory sinks, configure their
activation and minimum levels, flush and replace all sinks, and select buffered
or synchronous delivery. Factories return strong `LogSinkId` handles; removed
handles cannot refer to a newly created sink. See
[C++ logging](embedding.md#c-logging) for the public API and ownership contracts.

File sinks default to size-based rotation at 10 MiB and retain ten archives.
The configurable base name defaults to `application`, producing
`application.log`, `application.1.log` (newest archive), and so on.
`Startup` rotation and `None` (unlimited append) are also available. See
[File sinks](embedding.md#file-sinks) for configuration and failure behavior.

These configuration operations are not yet exposed to Nomad scripts or
`nomad.toml`. Loading `nomad-log` currently uses the host's existing sinks.

## Extending the hub

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
