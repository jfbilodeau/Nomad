# Operating system

Select `nomad-os` in `nomad.toml` to access startup information through the `os`
hub. Its `nomad-collections` dependency is loaded automatically.

```toml
[nomad]
version = "0.1.8"
modules = ["nomad-os"]
```

## Startup snapshots

| Binding | Type | Value |
| --- | --- | --- |
| `os.argv` | `Array<string>` | Application arguments following the first `--`, in order. |
| `os.env` | `Map<string string>` | Process environment variables, keyed by name. |
| `os.platform` | `os.Platform` | The operating system running the application. |
| `os.architecture` | `os.Architecture` | The architecture of the running executable, not necessarily the physical CPU. |
| `os.currentDirectory` | `string` | Working directory at startup. |
| `os.executablePath` | `string` | Absolute path to the running executable. |
| `os.tempDirectory` | `string` | The operating system's temporary directory at startup. |

These bindings are read-only: scripts cannot assign a different array, map,
enum, or string to them. The contents of `os.argv` and `os.env` remain mutable.
Changing an array element or map entry changes only that application's snapshot;
it does not modify the real command line or process environment. Environment
keys use the map's normal case-sensitive matching, including on Windows.
Windows' hidden drive-directory entries are not included.

Arguments, environment text, and paths use UTF-8. Empty argument strings and
environment values are preserved. Snapshots are captured when the module loads,
before scripts run, and do not refresh when the host environment or working
directory changes.

Compiler checks, generated documentation, and editor analysis expose the same
types and enum members without capturing command-line arguments, environment
variables, or paths: `os.argv` and `os.env` are empty and all three paths are
empty strings. Platform and architecture still identify the host tool.

## Platform and architecture

`os.Platform` has exactly these members:

- `os.Platform.Windows`
- `os.Platform.Linux`
- `os.Platform.MacOS`

`os.Architecture` has exactly these members:

- `os.Architecture.X86`
- `os.Architecture.X64`
- `os.Architecture.Arm32`
- `os.Architecture.Arm64`

Use enum comparisons rather than text comparisons:

```nomad
if os.platform == os.Platform.Windows
    # Windows-specific application behavior
end

if os.architecture == os.Architecture.Arm64
    # This executable runs as Arm64
end
```

## Application arguments

Separate application arguments from launcher and runtime options with `--`:

```text
nomad run --debug -- profile "Player One" --help
nomad-runtime --project-path MyProject -- profile "Player One" --help
MyPackagedApp -- profile "Player One" --help
```

In all three examples `os.argv` contains `"profile"`, `"Player One"`, and
`"--help"`. It never includes the executable name, project directory, launcher
options, runtime options, or the separating `--`. Without a separator the array
is empty. Subsequent `--` strings are ordinary application arguments.

```nomad
if os.argv.count > 0
    firstArgument = os.argv[0]
    os.argv[0] = "processed"
end

if os.env.contains "HOME"
    homeDirectory = os.env["HOME"]
end
os.env["APPLICATION_MODE"] = "preview"
```

Packaged applications retain `nomad-os` and its collections dependency and
capture fresh snapshots when launched. Packaging itself does not run scripts or
capture the packager's environment.
