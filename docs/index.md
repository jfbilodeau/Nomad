# Getting started

Nomad combines a strongly typed scripting language with a 2D game engine.
The SDK provides three tools:

- `nomad`: create, check, run, and package game projects.
- `nomadc`: compile-check scripts, inspect instructions, and generate API documentation.
- `nomad-runtime`: execute game scripts; included in packaged games.

## Install the SDK

Copy and paste the following command to download and run the installer for
GitHub's latest stable release.

**Windows x64 (PowerShell):**

```powershell
& ([scriptblock]::Create((Invoke-RestMethod https://github.com/jfbilodeau/Nomad/releases/latest/download/install.ps1 -ErrorAction Stop)))
```

**Linux x64 (Bash):**

```bash
(installer="$(curl -fsSL https://github.com/jfbilodeau/Nomad/releases/latest/download/install.sh)" && bash -c "$installer")
```

These commands execute code downloaded from GitHub. Only run them if you trust
the repository. To inspect the script first, download `install.ps1` or
`install.sh` from [GitHub Releases](https://github.com/jfbilodeau/Nomad/releases),
review it, and run the saved file instead. Both commands download the complete
script successfully before executing it; neither writes a temporary script
or changes your system's execution policy.

### Select a version or installation directory

With a downloaded installer:

```powershell
.\install.ps1
.\install.ps1 -Version 0.1.0 -Destination 'C:\Tools\Nomad'
```

```bash
bash install.sh
bash install.sh 0.1.0 "$HOME/tools/nomad"
```

The version defaults to `latest`, meaning GitHub's latest **stable** release.
Explicit versions may select prereleases. If no stable release exists, specify
a version; installers never silently fall back to a prerelease.
The default destination is `~/.nomad/sdks/<version>/<platform>`.
Installers require Windows x64 or Linux x64; Linux requires `curl`, `unzip`,
`zipinfo`, and `sha256sum`. They verify SHA-256 before extraction, refuse to
overwrite an existing installation, and print PATH instructions without
changing your shell configuration. No administrator privileges are needed
for the default destination.

### Manual installation

Download the Windows x64 or Linux x64 SDK ZIP from
[GitHub Releases](https://github.com/jfbilodeau/Nomad/releases).
Extract the entire ZIP into one directory, keeping its libraries, templates,
and `runtime` directory together. No C++ compiler is required.

Verify the archive against the release's `SHA256SUMS.txt` before extraction:

```powershell
Get-FileHash .\nomad-sdk-windows-x64-0.1.0.zip -Algorithm SHA256
```

On Linux, place the downloaded SDK ZIP and checksum file in the same directory:

```bash
sha256sum --check --ignore-missing SHA256SUMS.txt
```

Compare the Windows hash to the matching checksum entry. Archive names vary
with the release version.

Add the extracted SDK directory to `PATH`, or invoke its executables by path.
On Linux, preserve executable permissions when extracting the archive.

## Create and run a game

With the SDK directory on `PATH`:

```console
nomad --version
nomad init my-game
nomad check my-game
nomad run my-game
```

The generated project contains `nomad.toml` and `res/scripts/init.nomad`.
Edit the script to change your game. Start with the
[language guide](language.md) and [engine API](engine-api.md).

## Package your game

```console
nomad package my-game --dry-run
nomad package my-game
```

The ZIP appears in the project's configured output directory, `dist` by
default. Extract it and run the game executable from the extraction directory.
The packaged game does not require an installed Nomad SDK.

Packaging targets the runtime bundled with your SDK: use the Windows SDK
for Windows packages and the Linux SDK for Linux packages.
Use `--force` to replace an existing package.

See the [CLI reference](cli.md) and
[project configuration reference](configuration.md) for details.
