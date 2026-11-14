// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/Tokenizer.hpp>

namespace nomad {

ParserBlockScope::ParserBlockScope(
    CompilerContext& context,
    const ScriptCallableBase* function,
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
    const ScriptCallableBase* function,
    LoopStatement* loop,
    bool functionRoot,
    bool endsAtEnd
) {
    m_blocks.push_back({function, loop, functionRoot, endsAtEnd});
}

void CompilerContext::leaveBlock() {
    m_blocks.pop_back();
}

bool CompilerContext::isFunctionRoot(const ScriptCallableBase* function) const {
    return m_blocks.empty() || m_blocks.back().function != function || m_blocks.back().functionRoot;
}

bool CompilerContext::currentBodyEndsAtEnd() const {
    return !m_blocks.empty() && m_blocks.back().endsAtEnd;
}

LoopStatement* CompilerContext::getEnclosingLoop(const ScriptCallableBase* function) const {
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

CompilerContext::CompilerContext(Compiler* compiler, const CompilerMode mode,
    const InterpreterContext* interpreterContext, const bool hubInitializer):
    m_compiler(compiler),
    m_mode(mode),
    m_interpreterContext(interpreterContext),
    m_hubInitializer(hubInitializer)
{
    if (m_compiler == nullptr) {
        throw NomadBug("CompilerContext requires a compiler");
    }
}

CompilerContext::~CompilerContext() = default;

Compiler* CompilerContext::getCompiler() const {
    return m_compiler;
}

bool CompilerContext::isHubInitializer() const {
    return m_hubInitializer;
}

Runtime* CompilerContext::getRuntime() const {
    return m_compiler->getRuntime();
}

const NomadString& CompilerContext::getHub() const {
    return m_hub;
}

void CompilerContext::setHub(NomadString hub) {
    m_hub = std::move(hub);
}

NomadString CompilerContext::qualifyDeclaration(const NomadString& name) const {
    return m_hub.empty() ? name : m_hub + "." + name;
}

CompilerMode CompilerContext::getMode() const {
    return m_mode;
}

const InterpreterContext* CompilerContext::getInterpreterContext() const {
    return m_interpreterContext;
}

void CompilerContext::addDiagnostic(const CompilerDiagnostic& diagnostic) {
    m_diagnostics.push_back(diagnostic);

    if (diagnostic.severity == CompilerDiagnosticSeverity::Error) {
        ++m_errorCount;
    } else {
        ++m_warningCount;
    }

}

void CompilerContext::addDiagnostics(std::span<const CompilerDiagnostic> diagnostics) {
    for (const auto& diagnostic : diagnostics) {
        addDiagnostic(diagnostic);
    }
}

void CompilerContext::addError(const NomadStringView message) {
    report(
        CompilerDiagnosticSeverity::Error,
         message, 
         {}, 
         NOMAD_INVALID_INDEX, 
         NOMAD_INVALID_INDEX
    );
}

void CompilerContext::addError(
    const NomadStringView message,
    const NomadStringView sourceName,
    const NomadIndex line,
    const NomadIndex column
) {
    report(CompilerDiagnosticSeverity::Error, message, sourceName, line, column);
}

void CompilerContext::addError(const NomadStringView message, const Tokenizer* tokens) {
    report(
        CompilerDiagnosticSeverity::Error,
        message,
        tokens->getSourceName(),
        tokens->getLineIndex(),
        tokens->getColumnIndex()
    );
}

void CompilerContext::addWarning(const NomadStringView message) {
    report(
        CompilerDiagnosticSeverity::Warning,
        message,
        {},
        NOMAD_INVALID_INDEX,
        NOMAD_INVALID_INDEX
    );
}

void CompilerContext::addWarning(
    const NomadStringView message,
    const NomadStringView sourceName,
    const NomadIndex line,
    const NomadIndex column
) {
    report(
        CompilerDiagnosticSeverity::Warning,
        message,
        sourceName,
        line,
        column
    );
}

void CompilerContext::addWarning(const NomadStringView message, const Tokenizer* tokens) {
    report(
        CompilerDiagnosticSeverity::Warning,
        message,
        tokens->getSourceName(),
        tokens->getLineIndex(),
        tokens->getColumnIndex()
    );
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

std::span<const CompilerDiagnostic> CompilerContext::getDiagnostics() const {
    return m_diagnostics;
}

std::optional<CompilerDiagnostic> CompilerContext::getFirstError() const {
    for (const auto& diagnostic : m_diagnostics) {
        if (diagnostic.severity == CompilerDiagnosticSeverity::Error) {
            return diagnostic;
        }
    }

    return std::nullopt;
}

void CompilerContext::clearDiagnostics() {
    m_diagnostics.clear();
    m_errorCount = 0;
    m_warningCount = 0;
}

void CompilerContext::report(
    const CompilerDiagnosticSeverity severity,
    const NomadStringView message,
    const NomadStringView sourceName,
    const NomadIndex line,
    const NomadIndex column
) {
    addDiagnostic({
        severity,
        NomadString(message),
        NomadString(sourceName),
        line,
        column
    });

}

} // namespace nomad
