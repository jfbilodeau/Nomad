// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <span>
#include <vector>

namespace nomad {

class Compiler;
class Runtime;
class Tokenizer;

enum class DiagnosticSeverity {
    Warning = 1,
    Error,
};

// Function: source files compiled to bytecode. Interpreter: single console lines evaluated from the AST.
enum class CompilerMode {
    Function = 1,
    Interpreter,
};

struct Diagnostic {
    DiagnosticSeverity severity;
    NomadString message;
    NomadString sourceName;
    NomadIndex line;
    NomadIndex column;
};

// Formats as `source:line:column: error: message`, omitting unknown location parts.
[[nodiscard]] NomadString formatDiagnostic(const Diagnostic& diagnostic);

// Collects errors and warnings while parsing and resolving source code. A single context can span many functions:
// each diagnostic records the source (file path) it was reported against.
class CompilerContext {
public:
    explicit CompilerContext(Compiler* compiler, CompilerMode mode = CompilerMode::Function);
    CompilerContext(const CompilerContext&) = delete;
    CompilerContext& operator=(const CompilerContext&) = delete;
    ~CompilerContext();

    [[nodiscard]] Compiler* getCompiler() const;
    [[nodiscard]] Runtime* getRuntime() const;
    [[nodiscard]] CompilerMode getMode() const;

    void reportError(NomadStringView message);
    void reportError(NomadStringView message, NomadStringView sourceName, NomadIndex line, NomadIndex column);
    // Reports at the tokenizer's source, line and column.
    void reportError(NomadStringView message, const Tokenizer* tokens);
    void reportWarning(NomadStringView message);
    void reportWarning(NomadStringView message, NomadStringView sourceName, NomadIndex line, NomadIndex column);
    void reportWarning(NomadStringView message, const Tokenizer* tokens);

    [[nodiscard]] NomadBoolean hasError() const;
    [[nodiscard]] NomadBoolean hasWarning() const;
    [[nodiscard]] NomadIndex getErrorCount() const;
    [[nodiscard]] NomadIndex getWarningCount() const;
    [[nodiscard]] std::span<const Diagnostic> getDiagnostics() const;
    // Returns nullptr when no error has been reported.
    [[nodiscard]] const Diagnostic* getFirstError() const;
    void clearDiagnostics();

private:
    void report(
        DiagnosticSeverity severity,
        NomadStringView message,
        NomadStringView sourceName,
        NomadIndex line,
        NomadIndex column
    );

    Compiler* m_compiler;
    CompilerMode m_mode;
    std::vector<Diagnostic> m_diagnostics;
    NomadIndex m_errorCount = 0;
    NomadIndex m_warningCount = 0;
};

} // namespace nomad
