# Compiler Improvement Plan

This plan tracks focused improvements identified while reviewing
`Compiler.hpp` and `Compiler.cpp`. Implement one step at a time, validate the
affected behavior, and stop for review before proceeding to the next step.

## 1. Stabilize phase-hook storage (completed)

- Replace the phase-hook vector with storage that keeps callback references
  stable when a hook registers another hook.
- Preserve callback identity/state, registration order, and the current behavior
  that newly registered hooks begin with the next phase.
- Add regression coverage for registration during invocation and stateful hooks
  in `CompilerHooksTest.cpp`.
- Validate with the focused compiler-hook CTest, then build the workspace
  targets as required.

Verified with the Windows Debug `nomad_test_remaining` CTest group, including
the new compiler-hook regression, and the workspace CMake build.

## 2. Correct inferred-type collection (completed)

- Collect externally introduced fields once, independently of variable-context
  enumeration in `collectInferredTypes`.
- Add focused coverage showing external field inference does not depend on how
  many variable contexts are registered.
- Validate the relevant language tests and build.

Verified with the Windows Debug `nomad_test_language` CTest group and the
workspace CMake build.

## 3. Clarify identifier lookup contracts (completed)

- Add characterization coverage for events, context prefixes/members,
  declarations, and lexical `this`.
- Share lookup logic only where classification and full identifier resolution
  have the same contract; preserve intentional differences used by parsing and
  declaration validation.
- Preserve lexical class/struct receiver precedence over runtime `this.*`
  contexts, including closures and format expressions.
- Validate compiler, parser, event, context-variable, class, and struct tests.

Added event classification and tests that distinguish context-prefix
classification from exact member resolution. Existing class and callback tests
continue to cover lexical `this` precedence and closure/format-expression use.
Verified with the Windows Debug `nomad_test_language` CTest group and the
workspace CMake build.

## 4. Define completion-phase semantics (completed)

- Decide whether `Completed` callbacks run before or after script initial values
  are installed.
- Keep analysis-mode behavior explicit and make the module-host failure state
  agree with the chosen contract.
- Add coverage for successful initialization, initialization failure, callback
  failure, and analysis compilation, including game startup behavior.
- Validate compiler-hook, hub-initialization, module-host, and game-startup
  tests.

Contract chosen: executable `Completed` callbacks run only after static hub
values and the script initializer have been installed; installation failures
skip completion callbacks. Analysis compilation retains its existing completion
behavior without installing executable state. Verified with the Windows Debug
`nomad_test_remaining` and `nomad_test_language` CTest groups and workspace
CMake build.

## 5. Reduce call-emission duplication (completed)

- Extract only small emission operations that are genuinely shared by local
  and return-destination call paths.
- Keep free-function, object-method, struct-method, and constructor behavior
  specialized where receiver layout or ownership differs.
- Validate script/native dispatch, local/return destinations, slot cleanup,
  empty and multi-slot structs, and runtime results.

Added shared script/native dispatch emitters for functions and methods and
replaced the repeated dispatch blocks in local/return function calls,
struct/object method calls, and object setters. Receiver binding, argument
compilation, and cleanup remain in their specialized call paths. Verified with
the Windows Debug `nomad_test_remaining` and `nomad_test_language` CTest groups,
the workspace CMake build, and `git diff --check`.

## 6. Define duplicate statement-registration behavior (completed)

- Decide whether duplicate statement names are errors or intentional no-ops.
- Make the behavior explicit in `registerParseStatementFn` and add a focused
  regression test.
- Validate compiler and parser tests.

Contract chosen: registering an existing statement name throws `NomadBug`
instead of silently retaining the original registration. Added a regression
test confirming the duplicate is rejected and the original metadata remains
unchanged. Verified with the Windows Debug `nomad_test_remaining` CTest group,
the workspace CMake build, and `git diff --check`.

## 7. Clean up the compiler interface

- Remove only declarations confirmed unused across the repository; preserve
  externally consumed public APIs unless compatibility is addressed.
- Remove the duplicate `[[noreturn]]`, correct misleading diagnostics, and
  clarify that `ArgumentList::getArgumentCount` counts stack slots.
- Consider changing load-emitter return types only after checking all API users.
- Validate the complete C++ build and relevant compiler tests.

## Validation policy

Run focused CTest tests for each step, then build the workspace targets after
code changes. Expand to broader test groups when behavior crosses compiler,
runtime, or host boundaries. Do not combine unrelated cleanup with a step.
