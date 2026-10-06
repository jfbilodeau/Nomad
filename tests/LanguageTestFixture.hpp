// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/RuntimeValue.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace nomad::test {

// Snapshot of a function execution. Values are copied so the outcome outlives the runtime that produced it.
struct FunctionOutcome {
    bool compiled = false;
    NomadString diagnostics;
    NomadString returnTypeName;
    NomadString text;
    NomadInteger integerValue = 0;
    NomadFloat floatValue = 0.0f;
    NomadBoolean booleanValue = false;
    std::optional<NomadString> fault;
    // Owned strings still allocated after the function returned and its result was released (debug builds only).
    NomadInteger leakedStrings = 0;
};

// Compiles and executes Nomad source code against a fresh runtime. One instance per test.
class LanguageTestFixture {
public:
    LanguageTestFixture();
    LanguageTestFixture(const LanguageTestFixture&) = delete;
    LanguageTestFixture& operator=(const LanguageTestFixture&) = delete;
    ~LanguageTestFixture();

    [[nodiscard]] Runtime& getRuntime();
    [[nodiscard]] Compiler& getCompiler();

    void addFunction(const NomadString& name, const NomadString& source);
    [[nodiscard]] bool compile();

    [[nodiscard]] NomadString getDiagnostics() const;
    [[nodiscard]] NomadIndex getErrorCount() const;
    [[nodiscard]] const Diagnostic* findError(const NomadString& messageFragment) const;
    [[nodiscard]] bool hasError(const NomadString& messageFragment) const;

    // Executes a compiled function. VM faults are captured in `FunctionOutcome::fault`.
    [[nodiscard]] FunctionOutcome execute(const NomadString& functionName, const std::vector<RuntimeValue>& arguments = {});

private:
    Runtime m_runtime;
    std::unique_ptr<Compiler> m_compiler;
    std::unique_ptr<CompilerContext> m_context;
    bool m_compiled = false;
};

// Compiles `source` as a function named `main` and executes it.
[[nodiscard]] FunctionOutcome runSource(const NomadString& source);

// Compiles `return <expression>` and executes it.
[[nodiscard]] FunctionOutcome runExpression(const NomadString& expression);

// Returns the diagnostics produced by compiling `source`, or an empty string when it compiles.
[[nodiscard]] NomadString compileErrors(const NomadString& source);

} // namespace nomad::test
