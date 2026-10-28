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
messages require both thresholds to be lowered to `debug`.
The default sink writes to the console; error and
critical messages use standard error.

By default, debug, info, and warning messages are buffered until the host flushes them.
The game runtime flushes during its loop; C++ hosts can call `nomad::log::flush`.
Fatal logging is available to C++ hosts only; there is no `log.fatal` script API.

## Programmatic configuration

The module exposes these ordinary typed values:

- `log.Level`: `Debug`, `Info`, `Warning`, `Error`, `Critical`.
- `log.DeliveryMode`: `Buffered`, `Synchronous`.
- `log.FileRotationMode`: `None`, `Size`.
- `log.Sink`: a reference-semantic object returned by a sink factory, not an integer.
- `log.Logger`: the live host logger, available as the read-only reference `log.logger`.

Use `log.logger.level` and `log.logger.deliveryMode` to inspect or change the logger.
Both are native-backed read/write properties; assigning `log.logger` itself is not allowed.
Logger properties read and update the logger referenced by their receiver.
`log.flush` delivers pending messages and flushes active sinks.

```nomad
log.logger.level = log.Level.Debug
log.logger.deliveryMode = log.DeliveryMode.Synchronous

options = new log.SinkConfiguration
options.level = log.Level.Debug
console = log.createConsoleSink options
log.debug "Debug logging enabled for this console sink"
console.active = false
log.removeSink console
```

Factories add sinks without removing existing host or script sinks. Use
`log.clearAllSinks` to flush and remove everything without installing a replacement.
Neither operation affects plain `print` output.

### Creation settings

`new log.SinkConfiguration` creates an ordinary reference-semantic script object
with `active:bool = true` and `level:log.Level = log.Level.Info`.
Pass it to `log.createConsoleSink`.

`new log.FileSinkConfiguration` creates a separate script object with those same
two fields plus:

| Field | Type | Default |
| --- | --- | --- |
| `directory` | `string` | `"logs"` |
| `name` | `string` | `"application"` |
| `rotationMode` | `log.FileRotationMode` | `Size` |
| `rotateAtStartup` | `bool` | `true` |
| `maxSizeBytes` | `int` | `10485760` (10 MiB) |
| `maxFiles` | `int` | `10` archives, excluding the active file |

```nomad
options = new log.FileSinkConfiguration
options.name = "session"
options.maxFiles = 3
file = log.createFileSink options
log.info "Saved to logs/session.log"
log.flush
log.removeSink file
```

Relative directories resolve from the project directory, not the current working
directory. Absolute directories are also supported. Names are extensionless base
filenames; archives are `session.1.log` (newest), `session.2.log`, etc.
Limits must be positive for bytes and nonnegative for archives.
Startup rotation is independent of size rotation. Set `rotateAtStartup = false`
and `rotationMode = log.FileRotationMode.None` to append indefinitely across runs.

Each constructor creates independent settings. Assignment aliases the same
configuration object, as with other script objects. Factories copy its values:
changing the configuration afterward does not alter an existing sink.
An inactive sink (`active = false`) is owned but receives no entries and creates
no files until started. Enable it with `sink.active = true`; an active
flush, including an empty one, starts it.

### Live sink settings

Factories return `log.Sink` objects with native-backed properties.
Assigning a sink object aliases the same reference; there is no script constructor.
Creation settings remain ordinary script objects: editing them does not update a live sink.

| Property | Access |
| --- | --- |
| `sink.level:log.Level` | Read/write |
| `sink.active:bool` | Read/write |
| `sink.rotationMode:log.FileRotationMode` | Read/write; file sinks only |
| `sink.rotateAtStartup:bool` | Read/write; file sinks only |
| `sink.maxSizeBytes:int` | Read-only; file sinks only |
| `sink.maxFiles:int` | Read-only; file sinks only |

Use `log.setFileRotationLimits sink maxSizeBytes maxFiles` to change both limits
atomically. `log.getFilePath sink` returns the active path; create a replacement
to change the path or name. The former setting get/set functions are no longer available.

File-specific properties and functions fail for console or other sink kinds. Changing the startup
flag after startup does not rotate again or undo rotation. Other rotation changes
apply to subsequent writes.

The logger owns sinks, not configuration objects or script handles. Discarding a
handle does not remove its sink. Accessing a removed/cleared sink or removing it
again fails explicitly. Null references and references belonging to a different
logger are rejected by sink properties and functions. Directory,
file, rotation, and delivery errors also propagate as script execution errors.
Memory sinks and custom script sink callbacks are not exposed.

Analysis mode exposes types and signatures without configuring the host logger
or creating files. Configuration mutations, factory calls, and flushing require
application mode. Constructing configuration objects alone has no logging or
filesystem effects.

## C++ sink configuration

C++ hosts can create logger-owned console, file, and memory sinks, configure their
activation and minimum levels, flush and replace all sinks, and select buffered
or synchronous delivery. Factories return strong `LogSinkId` handles; removed
handles cannot refer to a newly created sink. See
[C++ logging](embedding.md#c-logging) for the public API and ownership contracts.

File sinks default to size-based rotation at 10 MiB, rotation at startup, and
retention of ten archives.
The configurable base name defaults to `application`, producing
`application.log`, `application.1.log` (newest archive), and so on.
`rotateAtStartup` is independent of the rotation mode and defaults to `true`.
`None` disables ongoing size-based rotation; also disable `rotateAtStartup` for
unlimited append across runs. See
[File sinks](embedding.md#file-sinks) for configuration and failure behavior.

Project logging settings in `nomad.toml` are not yet supported. Loading
`nomad-log` currently preserves the host's existing sinks and settings until
explicitly changed programmatically.

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
