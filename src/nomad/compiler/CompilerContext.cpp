// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/Tokenizer.hpp>

namespace nomad {

ParserBlockScope::ParserBlockScope(
    CompilerContext& context,
    const ScriptFunction* function,
    LoopStatement* loop,
    bool functionRoot,
    bool endsAtEnd
):
    m_context(context)
{
    m_context.enterBlock(function, loop, functionRoot, endsAtEnd);
}

ParserBlockScope::~ParserBlockScope() {
    m_context.leaveBlock();
}

void CompilerContext::enterBlock(
    const ScriptFunction* function,
    LoopStatement* loop,
    bool functionRoot,
    bool endsAtEnd
) {
    m_blocks.push_back({function, loop, functionRoot, endsAtEnd});
}

void CompilerContext::leaveBlock() {
    m_blocks.pop_back();
}

bool CompilerContext::isFunctionRoot(const ScriptFunction* function) const {
    return m_blocks.empty() || m_blocks.back().function != function || m_blocks.back().functionRoot;
}

bool CompilerContext::currentBodyEndsAtEnd() const {
    return !m_blocks.empty() && m_blocks.back().endsAtEnd;
}

LoopStatement* CompilerContext::getEnclosingLoop(const ScriptFunction* function) const {
    for (auto block = m_blocks.rbegin(); block != m_blocks.rend(); ++block) {
        if (block->function != function || block->functionRoot) {
            break;
        }
        if (block->loop != nullptr) {
            return block->loop;
        }
    }
    return nullptr;
}

NomadString formatDiagnostic(const Diagnostic& diagnostic) {
    NomadString location = diagnostic.sourceName;

    if (diagnostic.line != NOMAD_INVALID_INDEX) {
        location += ":" + std::to_string(diagnostic.line);

        if (diagnostic.column != NOMAD_INVALID_INDEX) {
            location += ":" + std::to_string(diagnostic.column);
        }
    }

    const NomadString severity = diagnostic.severity == DiagnosticSeverity::Error ? "error" : "warning";

    if (location.empty()) {
        return severity + ": " + diagnostic.message;
    }

    return location + ": " + severity + ": " + diagnostic.message;
}

CompilerContext::CompilerContext(Compiler* compiler, const CompilerMode mode,
    const InterpreterContext* interpreterContext):
    m_compiler(compiler),
    m_mode(mode),
    m_interpreterContext(interpreterContext)
{
    if (m_compiler == nullptr) {
        throw NomadBug("CompilerContext requires a compiler");
    }
}

CompilerContext::~CompilerContext() = default;

Compiler* CompilerContext::getCompiler() const {
    return m_compiler;
}

Runtime* CompilerContext::getRuntime() const {
    return m_compiler->getRuntime();
}

CompilerMode CompilerContext::getMode() const {
    return m_mode;
}

const InterpreterContext* CompilerContext::getInterpreterContext() const {
    return m_interpreterContext;
}

void CompilerContext::reportDiagnostics(std::span<const Diagnostic> diagnostics) {
    for (const auto& diagnostic : diagnostics) {
        report(diagnostic.severity, diagnostic.message, diagnostic.sourceName, diagnostic.line, diagnostic.column);
    }
}

void CompilerContext::reportError(const NomadStringView message) {
    report(DiagnosticSeverity::Error, message, {}, NOMAD_INVALID_INDEX, NOMAD_INVALID_INDEX);
}

void CompilerContext::reportError(
    const NomadStringView message,
    const NomadStringView sourceName,
    const NomadIndex line,
    const NomadIndex column
) {
    report(DiagnosticSeverity::Error, message, sourceName, line, column);
}

void CompilerContext::reportError(const NomadStringView message, const Tokenizer* tokens) {
    report(DiagnosticSeverity::Error, message, tokens->getSourceName(), tokens->getLineIndex(), tokens->getColumnIndex());
}

void CompilerContext::reportWarning(const NomadStringView message) {
    report(DiagnosticSeverity::Warning, message, {}, NOMAD_INVALID_INDEX, NOMAD_INVALID_INDEX);
}

void CompilerContext::reportWarning(
    const NomadStringView message,
    const NomadStringView sourceName,
    const NomadIndex line,
    const NomadIndex column
) {
    report(DiagnosticSeverity::Warning, message, sourceName, line, column);
}

void CompilerContext::reportWarning(const NomadStringView message, const Tokenizer* tokens) {
    report(DiagnosticSeverity::Warning, message, tokens->getSourceName(), tokens->getLineIndex(), tokens->getColumnIndex());
}

NomadBoolean CompilerContext::hasError() const {
    return m_errorCount > 0;
}

NomadBoolean CompilerContext::hasWarning() const {
    return m_warningCount > 0;
}

NomadIndex CompilerContext::getErrorCount() const {
    return m_errorCount;
}

NomadIndex CompilerContext::getWarningCount() const {
    return m_warningCount;
}

std::span<const Diagnostic> CompilerContext::getDiagnostics() const {
    return m_diagnostics;
}

const Diagnostic* CompilerContext::getFirstError() const {
    for (const auto& diagnostic : m_diagnostics) {
        if (diagnostic.severity == DiagnosticSeverity::Error) {
            return &diagnostic;
        }
    }

    return nullptr;
}

void CompilerContext::clearDiagnostics() {
    m_diagnostics.clear();
    m_errorCount = 0;
    m_warningCount = 0;
}

void CompilerContext::report(
    const DiagnosticSeverity severity,
    const NomadStringView message,
    const NomadStringView sourceName,
    const NomadIndex line,
    const NomadIndex column
) {
    m_diagnostics.push_back(Diagnostic{
        severity,
        NomadString(message),
        NomadString(sourceName),
        line,
        column
    });

    if (severity == DiagnosticSeverity::Error) {
        ++m_errorCount;
    } else {
        ++m_warningCount;
    }
}

} // namespace nomad
