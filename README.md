# Nomad

[![CI](https://github.com/jfbilodeau/Nomad/actions/workflows/ci.yml/badge.svg)](https://github.com/jfbilodeau/Nomad/actions/workflows/ci.yml)

Nomad is a compiled, strongly typed scripting language
designed for embedding in C++ applications, and a 2D game engine built around it.

Nomad is a bespoke language and engine built to suit my very specific brain. I don’t expect it to make sense to most people—maybe two others, tops.

The current baseline builds the engine library, generic runtime, and tests.

## Build

Nomad requires CMake 3.30 or newer and a C++20 compiler.

```console
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

On Linux, install Ninja and use the corresponding `linux-debug` preset. The
`windows-release` and `linux-release` presets create optimized builds.
The `linux-sanitizers` preset enables AddressSanitizer and
UndefinedBehaviorSanitizer for Linux diagnostics.

For faster repeat builds, install `ccache`; CMake uses it automatically when
it is available:

```console
sudo apt install ninja-build ccache
```

## Debugging in VS Code

Launch configurations are provided for both platforms. The
`Nomad Runtime - MSVC *` and `Nomad Tests - MSVC` configurations debug the
Windows preset output, while `Nomad Runtime - WSL/Linux *` and
`Nomad Tests - WSL/Linux *` attach GDB to the matching Linux preset output.
Each configuration reads from its own preset build directory, so Windows
and Linux builds never share output.

To debug under WSL, reopen the folder in WSL using the
[Remote - WSL](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-wsl)
extension. Keep the repository in the WSL filesystem (for example,
`~/src/Nomad`) rather than under `/mnt/c`; CMake and compiler filesystem
operations are substantially slower on Windows-mounted directories.

Then choose a `WSL/Linux` configuration. Because the pre-launch
tasks build the *active* CMake preset, select the preset that matches the
configuration you are launching — for example `linux-sanitizers` before
starting `Nomad Tests - WSL/Linux Sanitizers (GDB)`.

Ubuntu 24.04 ships CMake 3.28 through `apt`, which is too old to read
`CMakePresets.json`. Install a newer CMake inside the distribution and make
sure it precedes `/usr/bin/cmake` on `PATH`:

```console
sudo snap install cmake --classic
sudo ln -s /snap/bin/cmake /usr/local/bin/cmake
```

See [the language documentation](docs/language.md) for the current Nomad
language reference.
