# Nomad

> [!IMPORTANT]
> Nomad is a work in progress. APIs, tools, and project formats may change.

[![CI](https://github.com/jfbilodeau/Nomad/actions/workflows/ci.yml/badge.svg)](https://github.com/jfbilodeau/Nomad/actions/workflows/ci.yml)

Nomad is a 2D game engine with its own compiled, strongly typed scripting
language. It is designed for building games with a small native runtime while
keeping gameplay code in readable `.nomad` scripts.

The engine uses C++20, SDL3, and Box2D. The SDK includes:

- `nomad` to create, check, run, and package projects.
- `nomadc` to compile scripts and inspect generated instructions.
- `nomad-lsp` to provide compiler diagnostics in editors over LSP.
- `nomad-runtime` to run games during development and in packaged builds.

Windows x64 and Linux x64 SDK releases are currently available.

The [VS Code extension](editor/README.md) provides syntax highlighting and
diagnostics, including unsaved changes to scripts in a project.

## Quick start

### 1. Install the SDK

Install the latest stable release:

**Windows x64 (PowerShell)**

```powershell
& ([scriptblock]::Create((Invoke-RestMethod https://github.com/jfbilodeau/Nomad/releases/latest/download/install.ps1 -ErrorAction Stop)))
```

**Linux x64 (Bash)**

```bash
(installer="$(curl -fsSL https://github.com/jfbilodeau/Nomad/releases/latest/download/install.sh)" && bash -c "$installer")
```

These commands download and execute the installer from GitHub. To review it
first, download `install.ps1` or `install.sh` from
[GitHub Releases](https://github.com/jfbilodeau/Nomad/releases), inspect it,
and run the saved file.

The installer verifies the SDK checksum and prints the command needed to add
Nomad to `PATH`. You can also download an SDK ZIP from
[GitHub Releases](https://github.com/jfbilodeau/Nomad/releases) and extract it
manually.

### 2. Create and run a game

Once the SDK is on `PATH`:

```console
nomad --version
nomad init my-game
nomad check my-game
nomad run my-game
```

The generated project includes:

```text
my-game/
├── nomad.toml
├── nomadw / nomadw.cmd
└── res/
    └── scripts/
        └── main.nomad
```

Edit `res/scripts/main.nomad` to start building your game. The project-local
`nomadw` and `nomadw.cmd` wrappers select the SDK version declared in
`nomad.toml`, making builds reproducible across machines. If that SDK is
missing, the wrapper follows `NOMAD_DOWNLOAD_POLICY`: `prompt` (the default)
asks for permission, `allow` downloads without prompting, and `never` prohibits
downloads. Invalid policy values are rejected.

### 3. Package the game

Preview the package contents, then create a standalone ZIP:

```console
nomad package my-game --dry-run
nomad package my-game
```

Packages are written to `dist` by default. The extracted game includes its
runtime and does not require a separate Nomad SDK installation.

## Common commands

| Command | Purpose |
| --- | --- |
| `nomad init [directory]` | Create a Nomad project. |
| `nomad check [directory]` | Validate scripts and report automatic sprite packing. |
| `nomad run [directory] [--debug]` | Run the game. |
| `nomad package [directory] [--dry-run] [--force]` | Build a standalone game ZIP. |
| `nomadc check [path]` | Compile a script or directory without running it. |
| `nomadc dump [path] [--function <name>]` | Print generated VM instructions. |
| `nomadc docs [path] --output <file>` | Generate language and engine API documentation. |

Run `nomad help`, `nomad <command> --help`, or `nomadc <command> --help` for
command-specific usage.

## Documentation

- [Getting started](https://jfbilodeau.github.io/Nomad/)
- [Language guide](docs/language.md)
- [Sprites](docs/sprites.md)
- [Sound and music](docs/sound.md)
- [Debugging](docs/debugging.md)
- [CLI reference](docs/cli.md)
- [Project configuration](docs/configuration.md)
- [Runtime modules](docs/modules.md)
- [Building from source](docs/building.md)
- [Release and installation details](docs/releases.md)
