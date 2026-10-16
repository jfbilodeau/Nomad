# Building from source

Building Nomad requires:

- CMake 3.30 or newer.
- A C++20 compiler.
- Visual Studio 2022 with C++ tools on Windows, or Ninja and GCC/Clang on Linux.

Dependencies are pinned and downloaded by CMake during configuration.

## Windows

```console
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

## Linux

```console
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

Optimized `windows-release` and `linux-release` presets are also available.
Linux developers can use `linux-sanitizers` for AddressSanitizer and
UndefinedBehaviorSanitizer builds.

VS Code tasks and launch configurations are included. Select the matching
CMake preset before building or debugging the runtime and tests.

The `nomad-lsp` target builds the standalone stdio language server and is included
in SDK distributions. It is enabled by default; configure with
`-DNOMAD_BUILD_LSP=OFF` to omit the executable. See the
[extension instructions](https://github.com/jfbilodeau/Nomad/blob/main/editor/README.md)
to connect a local build to VS Code.
