// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <span>
#include <utility>
#include <vector>

namespace nomad {

class Compiler;
class InterpreterContext;
class LoopStatement;
class Runtime;
class ScriptFunction;
class Tokenizer;

enum class DiagnosticSeverity {
    Warning = 1,
    Error,
};

// Script: compiled source. Interpreter: AST evaluation without changing compiled function definitions.
enum class CompilerMode {
    Script = 1,
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

// Collects loading, registration, parsing and resolution diagnostics. A single context can span many scripts:
// each diagnostic records the source name it was reported against.
class CompilerContext {
public:
    explicit CompilerContext(Compiler* compiler, CompilerMode mode = CompilerMode::Script,
        const InterpreterContext* interpreterContext = nullptr, bool hubInitializer = false);
    CompilerContext(const CompilerContext&) = delete;
    CompilerContext& operator=(const CompilerContext&) = delete;
    ~CompilerContext();

    [[nodiscard]] Compiler* getCompiler() const;
    [[nodiscard]] Runtime* getRuntime() const;
    [[nodiscard]] CompilerMode getMode() const;
    [[nodiscard]] const InterpreterContext* getInterpreterContext() const;
    [[nodiscard]] bool isHubInitializer() const;
    [[nodiscard]] const NomadString& getHub() const;
    void setHub(NomadString hub);
    [[nodiscard]] NomadString qualifyDeclaration(const NomadString& name) const;
    [[nodiscard]] NomadString resolveName(const NomadString& name, const ScriptFunction* function = nullptr) const;

    void reportDiagnostics(std::span<const Diagnostic> diagnostics);

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

    struct Block {
        const ScriptFunction* function;
        LoopStatement* loop;
        bool functionRoot;
        bool endsAtEnd;
    };
    void enterBlock(
        const ScriptFunction* function,
        LoopStatement* loop = nullptr,
        bool functionRoot = false,
        bool endsAtEnd = false
    );
    void leaveBlock();
    [[nodiscard]] bool isFunctionRoot(const ScriptFunction* function) const;
    [[nodiscard]] LoopStatement* getEnclosingLoop(const ScriptFunction* function) const;
    [[nodiscard]] bool currentBodyEndsAtEnd() const;

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
    const InterpreterContext* m_interpreterContext;
    bool m_hubInitializer;
    std::vector<Diagnostic> m_diagnostics;
    NomadIndex m_errorCount = 0;
    NomadIndex m_warningCount = 0;
    std::vector<Block> m_blocks;
    NomadString m_hub;
};

class ParserBlockScope {
public:
    ParserBlockScope(
        CompilerContext& context,
        const ScriptFunction* function,
        LoopStatement* loop = nullptr,
        bool functionRoot = false,
        bool endsAtEnd = false
    );
    ParserBlockScope(const ParserBlockScope&) = delete;
    ParserBlockScope& operator=(const ParserBlockScope&) = delete;
    ~ParserBlockScope();

private:
    CompilerContext& m_context;
};

class CompilerHubScope {
public:
    CompilerHubScope(CompilerContext& context, NomadString hub):
        m_context(context),
        m_previous(context.getHub())
    {
        m_context.setHub(std::move(hub));
    }
    CompilerHubScope(const CompilerHubScope&) = delete;
    CompilerHubScope& operator=(const CompilerHubScope&) = delete;
    ~CompilerHubScope() { m_context.setHub(std::move(m_previous)); }

private:
    CompilerContext& m_context;
    NomadString m_previous;
};

} // namespace nomad
