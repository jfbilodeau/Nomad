# Project configuration

Nomad uses `nomad.toml` at the project root. Paths below are relative to that
root, except exclusion patterns, which are relative to the resource directory.

```toml
schema = 1

[project]
name = "Example Game"
identifier = "com.example.game"
version = "0.1.0"
executable = "example-game"
entry = "init"

[nomad]
version = "0.1.0"

[resources]
directory = "res"

[package]
output = "dist"
exclude = [
    "**/*.psd",
    "development/**",
]
```

All fields shown are required except `project.entry`, which defaults to `init`.
`project.executable` is an extensionless file name.
`nomad.version` uses canonical `major.minor.patch` form and identifies the
required SDK version. Packaging requires that exact runtime bundle version.

## Packaged configuration

Game packages contain a reduced `nomad.toml`. It retains the schema,
project identity and version, entry function, and required Nomad version.
It omits `project.executable`, `[resources]`, and `[package]`.
Packaged resources always live in `res`.

## Runtime bundle manifest

The SDK's `runtime/runtime.json` is separate from the game's configuration.
It declares schema 1, the exact Nomad version, the target platform, and
files with `path` and `role` fields. Roles are `runtime`, `library`, and `license`.

Paths are relative to the bundle, use `/` separators, cannot escape it, and
cannot be symbolic links. The inventory requires exactly one root-level
runtime executable and at least one license. Declared files must exist;
undeclared files are ignored.

Packaging renames the runtime to the game executable name and preserves
the relative paths of libraries and licenses. The internal `runtime.json`
is not included in the game package.
