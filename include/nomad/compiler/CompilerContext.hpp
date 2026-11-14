// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/compiler/CompilerDiagnostic.hpp>

#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace nomad {

class Compiler;
class InterpreterContext;
class LoopStatement;
class Runtime;
class ScriptCallableBase;
class Tokenizer;

// Script: compiled source. Interpreter: AST evaluation without changing compiled function definitions.
enum class CompilerMode {
    Script = 1,
    Interpreter,
};

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

    void addDiagnostic(const CompilerDiagnostic& diagnostic);
    void addDiagnostics(std::span<const CompilerDiagnostic> diagnostics);

    void addError(NomadStringView message);
    void addError(NomadStringView message, NomadStringView sourceName, NomadIndex line, NomadIndex column);
    // Reports at the tokenizer's source, line and column.
    void addError(NomadStringView message, const Tokenizer* tokens);
    void addWarning(NomadStringView message);
    void addWarning(NomadStringView message, NomadStringView sourceName, NomadIndex line, NomadIndex column);
    void addWarning(NomadStringView message, const Tokenizer* tokens);

    [[nodiscard]] NomadBoolean hasError() const;
    [[nodiscard]] NomadBoolean hasWarning() const;
    [[nodiscard]] NomadIndex getErrorCount() const;
    [[nodiscard]] NomadIndex getWarningCount() const;
    [[nodiscard]] std::span<const CompilerDiagnostic> getDiagnostics() const;
    // Returns nullptr when no error has been reported.
    [[nodiscard]] std::optional<CompilerDiagnostic> getFirstError() const;
    void clearDiagnostics();

    struct Block {
        const ScriptCallableBase* function;
        LoopStatement* loop;
        bool functionRoot;
        bool endsAtEnd;
    };
    void enterBlock(
        const ScriptCallableBase* function,
        LoopStatement* loop = nullptr,
        bool functionRoot = false,
        bool endsAtEnd = false
    );
    void leaveBlock();
    [[nodiscard]] bool isFunctionRoot(const ScriptCallableBase* function) const;
    [[nodiscard]] LoopStatement* getEnclosingLoop(const ScriptCallableBase* function) const;
    [[nodiscard]] bool currentBodyEndsAtEnd() const;

private:
    void report(
        CompilerDiagnosticSeverity severity,
        NomadStringView message,
        NomadStringView sourceName,
        NomadIndex line,
        NomadIndex column
    );

    Compiler* m_compiler;
    CompilerMode m_mode;
    const InterpreterContext* m_interpreterContext;
    bool m_hubInitializer;
    std::vector<CompilerDiagnostic> m_diagnostics;
    NomadIndex m_errorCount = 0;
    NomadIndex m_warningCount = 0;
    std::vector<Block> m_blocks;
    NomadString m_hub;
};

class ParserBlockScope {
public:
    ParserBlockScope(
        CompilerContext& context,
        const ScriptCallableBase* function,
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
