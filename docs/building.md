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

## Incremental builds

Keep using the same build directory and configuration to reuse compiled objects.
Source changes rebuild the affected files; header changes rebuild files that
include those headers, directly or indirectly. Changing compiler options can
also require recompilation.

Nomad disables C++20 named-module dependency scanning for its own targets because
they use headers rather than named modules. Ordinary header dependency tracking
remains enabled. Dependency targets retain their own scanning settings.

Build output can still list targets, check file globs, and run DLL export checks
when no source needs recompilation. Those messages alone do not indicate a full
rebuild.

The standard build presets use the `nomad-development` target to build the
enabled project manager, language server, runtime, compiler, and unit tests
together. On Visual Studio, this avoids separate MSBuild invocations that repeat
shared dependency checks and bundle steps for each program. Individual targets
remain available for focused builds; the sanitizer preset retains its focused
target selection.

## Running tests

Test presets allow up to four concurrent test processes. Unit tests are split
into `nomad_test_audio`, `nomad_test_language`, `nomad_test_tooling`, and
`nomad_test_remaining`; together they run the full unit suite. Each group uses
the same `nomad_test` executable. Tests not assigned to a named group run in the
remaining group, including newly added tests.

In VS Code, select the relevant CTest group for focused validation, or run all
tests for full validation. The `unit` label selects all unit-test groups without
the separate integration tests. Suite and case timings are included in the unit
test output to help identify slow tests. Test directories have unique names so
concurrent processes do not overwrite one another's temporary files.

When running tests in CLion, rebuild the `nomad_test` target in the selected
CMake profile after source or project-template changes. On Windows, wrapper
tests check that the wrapper files are created; Unix executable permissions
are checked only on non-Windows hosts.

Packaging and distribution integration tests retain their existing serial
execution requirements. Parallel jobs do not override those restrictions.

The `nomad-lsp` target builds the standalone stdio language server and is included
in SDK distributions. It is enabled by default; configure with
`-DNOMAD_BUILD_LSP=OFF` to omit the executable. See the
[extension instructions](https://github.com/jfbilodeau/Nomad/blob/main/editor/README.md)
to connect a local build to VS Code.
