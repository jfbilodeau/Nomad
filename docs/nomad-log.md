# Logging module

Select `log` in `nomad.toml` for structured logging:

```toml
[nomad]
modules = ["log"]
```

Keep the project's existing `nomad.version`. The module is independent of
`system`, `game`, and `os`; none loads it automatically.
For a game using logging, select `["game", "log"]`.

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
- `log.ConsoleSink` and `log.FileSink`: distinct reference-semantic objects returned
  by their respective factories, not integers.
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
The module supplies these declarations in its `scripts/` directory; hosts must
compile the module scripts before using its configuration types or factories.
An inactive sink (`active = false`) is owned but receives no entries and creates
no files until started. Enable it with `sink.active = true`; an active
flush, including an empty one, starts it.

### Live sink settings

`log.createConsoleSink` returns `log.ConsoleSink`; `log.createFileSink` returns
`log.FileSink`. Both are independent types with native-backed properties.
Assigning a sink object aliases the same reference; there is no script constructor.
Creation settings remain ordinary script objects: editing them does not update a live sink.

| Property | Access |
| --- | --- |
| `sink.level:log.Level` | Read/write; both sink types |
| `sink.active:bool` | Read/write; both sink types |
| `sink.rotationMode:log.FileRotationMode` | Read/write; file sinks only |
| `sink.rotateAtStartup:bool` | Read/write; file sinks only |
| `sink.maxSizeBytes:int` | Read-only; file sinks only |
| `sink.maxFiles:int` | Read-only; file sinks only |

Use `log.setFileRotationLimits sink maxSizeBytes maxFiles` to change both limits
atomically. `log.getFilePath sink` returns the active path; create a replacement
to change the path or name. The former setting get/set functions are no longer available.

The compiler rejects file-specific properties and functions on console sinks.
`log.removeSink` accepts either sink type. Changing the startup
flag after startup does not rotate again or undo rotation. Other rotation changes
apply to subsequent writes.

The logger owns sinks, not configuration objects or script handles. Discarding a
handle does not remove its sink. Accessing a removed/cleared sink or removing it
again fails explicitly. References belonging to a different
logger are rejected by sink properties and functions. Directory,
file, rotation, and delivery errors also propagate as script execution errors.
Memory sinks and custom script sink callbacks are not exposed.

C++ hosts invoking logging functions or properties receive logger, sink, binding,
and filesystem exceptions unchanged. The module does not wrap those failures in
`VirtualMachineException`; application-mode restrictions still raise VM faults.
Native callbacks assume non-null receivers and arguments of the declared types.
C++ hosts calling them directly must supply valid values; null arguments and
type mismatches are not supported error cases.

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

## Project logging schema

The selected `log` module applies these settings in `nomad.toml` when loaded in
application mode. Analysis and packaging validate settings without changing the
host logger or creating log files or directories.

```toml
[log]
level = "debug"
delivery-mode = "synchronous"

[[log.sinks]]
type = "console"
level = "info"

[[log.sinks]]
type = "file"
level = "debug"
directory = "logs"
name = "application"
rotation-mode = "size"
rotate-at-startup = true
max-size-bytes = 10485760
max-files = 10
```

| Field | Default / accepted values |
| --- | --- |
| `log.level` | `"info"`; `debug`, `info`, `warning`, `error`, `critical` |
| `log.delivery-mode` | `"synchronous"`; `synchronous`, `buffered` |
| Sink `type` | Required; `"console"` or `"file"` |
| Sink `active` | `true` |
| Sink `level` | `"info"` independently of the global level |
| File `directory` | `"logs"`; relative paths resolve beside `nomad.toml` |
| File `name` | `"application"`; a non-empty base name without path separators |
| File `rotation-mode` | `"size"` or `"none"` |
| File `rotate-at-startup` | `true`, independent of the rotation mode |
| File `max-size-bytes` | `10485760`; positive integer |
| File `max-files` | `10`; nonnegative integer, excluding the active file |

Omitting `log.sinks` produces one console sink in the parsed settings. An explicit
list supplies exactly those sinks without adding a console. An explicit empty
list, `[log]` followed by `sinks = []`, produces no sinks.
Both logger and sink level thresholds apply when logging is configured.
Unknown keys, incorrect types, unsupported values, and file-only options on
console sinks are rejected. Values are case-sensitive.
Projects without the `log` module retain these settings without interpreting them.

Application startup prepares the new sinks before flushing and removing all
existing sinks. Preparation or old-sink flushing failures abort startup without
detaching existing sinks or changing level/delivery settings. Preparing an active
file sink can create files or rotate existing logs; these filesystem effects are
not rolled back.
The module registers the prepared sinks, then applies level and delivery mode.
Failures after clearing the old sinks abort startup without restoring them.
Inactive configured sinks remain inactive.

Subsequent programmatic changes override startup settings. Sinks registered after
the module loads remain attached until removed or session teardown. Application
session shutdown flushes and clears all sinks, releasing file handles and custom
sinks before module libraries unload. An explicit `log.level` is not overwritten
by game debug startup, regardless of module order.

C++ hosts can call `readLogConfiguration(configuration, projectDirectory)` from
`<nomad/log/LogConfiguration.hpp>` to obtain the validated settings without
applying them. File constraints use the same validation as native file sinks.

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

Existing projects using `log.info` through `system` must either select
`log` explicitly or use `print` when plain output is sufficient.
