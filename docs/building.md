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

Assertions that require live-string allocation tracking run only in Debug
builds. Release builds still run the corresponding behavioral and
reference-count assertions.

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

## Lookup and registration benchmarks

Build the `nomad-lookup-benchmark` target in the selected CMake configuration.
It is excluded from normal builds and is not a CTest entry. Run its executable
from the build's `tests` directory (`tests/Debug` or `tests/Release` for
Visual Studio).

The benchmark measures native-function registration and first, middle, last,
and missing free-function name lookups after adding 100, 1,000, and 5,000
functions to a fresh Runtime. It checks registration order and lookup results
and reports five-sample medians with minimum/maximum values. Registration
excludes Runtime construction and name/parameter preparation.

An independent section measures complete method and constructor resolution on
a fixed owner with one method and two constructor overloads, while adding
0, 100, 1,000, or 5,000 unrelated free functions. Hit cases select the expected
member; miss cases use a registered empty owner. The benchmark checks status,
selected ID, candidate order, and argument types before timing, then verifies
timed result checksums. These timings include resolution result allocation and
overload matching, not just a name lookup.

A string section measures unique registration, repeated registration of the last
interned string, and first/middle/last/missing `getStringId` lookups at 100, 1,000, and 5,000
strings. An empty-text primer is registered outside timing. Checks cover ID order,
text, stable object identity/reference counts, missing lookups, and timed result
checksums. Debug builds also check live string-object counts after cleanup.

An active-callable section queries `VirtualMachine::getCurrentCallable()` from
native probes inside a compiled caller, a nested callee, and the restored caller,
with 0, 100, 1,000, or 5,000 unrelated native functions. Each timed query must
return the expected descriptor. Compilation, script initialization, and frame
transitions are outside timing; this measures lookup cost, not an entire frame
or formatting operation.

A script-call section measures a prepared scalar caller invoking one compiled
leaf, with the same unrelated-function counts. Each sample performs 50,000
complete invocations after warmup. Timing includes two script frames, execution,
return, and a result checksum, but no current-callable queries. Setup and state
checks are outside timing. This captures call overhead separately from lookup
gains; it does not isolate individual frame-entry instructions.

A native-property section compares borrowed descriptor lookup, copied metadata,
and complete VM getter/setter access. Three fixed cases use small callback
captures, large captures, or long property names/documentation. Each sample
performs 50,000 operations after warmup and reports the callback object sizes.
Checks cover metadata contents, results and setter side effects, callback counts,
borrowed receiver identity/reference counts, VM-state restoration, and receiver
cleanup. Setup and ownership/state checks are outside timing. These scalar
workloads do not measure reference-valued or unresolved properties.
After descriptor-based execution, copied metadata remains a separate control;
compare VM getter/setter timings without treating snapshot-copy cost as removed.

A host-call section compares prepared zero-argument execution, raw script calls
with zero/one/four integers or two string arguments, the raw capture-vector path,
and Closure calls with empty/scalar/captured arguments. Each of five fresh
Runtime samples performs 50,000 calls after warmup. Argument vectors and a
source-created scalar/string closure are prepared outside timing. Checks cover
results, capture/reference ownership, borrowed enclosing state, and final cleanup.

On MSVC Debug, a separate untimed pass counts current-thread CRT allocation and
reallocation requests over 1,000 calls per case, after validating a one-allocation
control. The hook is restored after the pass and is not installed during latency
measurement. Other builds explicitly report allocation counts as unavailable;
Release timings must not be interpreted as measured Release allocation counts.
Separate Debug probes measure parameter classification, scalar call layout,
closure-frame preparation, and a raw call with an explicitly prepared empty
capture vector. Counts include Debug STL instrumentation and all work in the
selected path, not just argument-storage capacity allocations.

Run the same target, configuration, and workload before and after an optimization.
Compare optimized Release results separately from Debug results; timing values
are observations, not pass/fail thresholds.
