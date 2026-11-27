# Building from source

Building Nomad requires:

- CMake 3.30 or newer.
- A C++20 compiler.
- Visual Studio 2022 with C++ tools on Windows, or Ninja and GCC/Clang on Linux.

Dependencies are pinned and downloaded by CMake during configuration.

CI temporarily uses CMake 4.4.5, which is also recommended for local builds.
CMake 4.5.0 cannot configure the bundled Boost.Locale release.

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

The standard build presets build the enabled tools and tests together through
`nomad-development`. To build one target, add `--target <name>` to the build
command. Reuse the same preset and build directory for incremental builds.

## Running tests

Use the matching CTest preset. To select a unit-test group:

```console
ctest --preset windows-debug -R "^nomad_test_language$"
ctest --preset linux-debug -L unit
```

The unit groups are `nomad_test_audio`, `nomad_test_language`,
`nomad_test_tooling`, and `nomad_test_remaining`. The `unit` label selects all
four without the separate integration tests. Test presets allow up to four
concurrent processes; packaging and distribution tests remain serial.

The workspace CTest integration builds the required targets before testing.
Live-string allocation assertions require a Debug build.

For a local language-server build, use the `nomad-lsp` target and follow the
[extension instructions](https://github.com/jfbilodeau/Nomad/blob/main/editor/README.md).
Configure with `-DNOMAD_BUILD_LSP=OFF` to omit it.
