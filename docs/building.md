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

## Lookup and registration benchmarks

Build the `nomad-lookup-benchmark` target in the selected CMake configuration.
It is excluded from normal builds and is not a CTest entry. Run its executable
from the build's `tests` directory (`tests/Debug` or `tests/Release` for
Visual Studio).

It covers registration, name lookup, member resolution, strings, property access,
and script/host calls. Workloads and checks are defined in
[RuntimeLookupBenchmark.cpp](https://github.com/jfbilodeau/Nomad/blob/main/tests/RuntimeLookupBenchmark.cpp).
Compare the same workload and configuration before and after a change. Treat
Debug and Release separately; timings are not pass/fail thresholds. Allocation
counts are available only on MSVC Debug and include Debug-library overhead.

Host-call workloads include an eight-argument heap-fallback case. The report
also gives cleanup-container object sizes, not complete native-stack frame sizes.

A separate frame-result workload checks void, scalar, reference, empty-struct,
two-slot, and eight-slot results without executing bodies or copying snapshots.
It reports preparation/check/destruction costs and the frame object's size.
