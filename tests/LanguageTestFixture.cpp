// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/script/Function.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

namespace nomad::test {

LanguageTestFixture::LanguageTestFixture() :
    m_compiler(m_runtime.createCompiler()) {
}

LanguageTestFixture::~LanguageTestFixture() = default;

Runtime& LanguageTestFixture::getRuntime() {
    return m_runtime;
}

Compiler& LanguageTestFixture::getCompiler() {
    return *m_compiler;
}

void LanguageTestFixture::addFunction(const NomadString& name, const NomadString& source) {
    (void)m_compiler->registerScriptFile(name, name + ".nomad", source);
}

bool LanguageTestFixture::compile() {
    m_context = std::make_unique<CompilerContext>(m_compiler.get());
    m_compiled = m_compiler->compileFunctions(m_context.get()) && !m_context->hasError();

    return m_compiled;
}

NomadString LanguageTestFixture::getDiagnostics() const {
    NomadString text;

    if (m_context == nullptr) {
        return text;
    }

    for (const auto& diagnostic: m_context->getDiagnostics()) {
        text += formatDiagnostic(diagnostic) + "\n";
    }

    return text;
}

NomadIndex LanguageTestFixture::getErrorCount() const {
    return m_context == nullptr ? 0 : m_context->getErrorCount();
}

const Diagnostic* LanguageTestFixture::findError(const NomadString& messageFragment) const {
    if (m_context == nullptr) {
        return nullptr;
    }

    for (const auto& diagnostic: m_context->getDiagnostics()) {
        if (diagnostic.severity == DiagnosticSeverity::Error &&
            diagnostic.message.find(messageFragment) != NomadString::npos) {
            return &diagnostic;
        }
    }

    return nullptr;
}

bool LanguageTestFixture::hasError(const NomadString& messageFragment) const {
    return findError(messageFragment) != nullptr;
}

FunctionOutcome LanguageTestFixture::execute(const NomadString& functionName, const std::vector<RuntimeValue>& arguments) {
    FunctionOutcome outcome;
    outcome.compiled = m_compiled;
    outcome.diagnostics = getDiagnostics();

    if (!m_compiled) {
        return outcome;
    }

    const auto functionId = m_runtime.getFunctionId(functionName);
    const auto function = m_runtime.getFunction(functionId);

    if (function == nullptr) {
        outcome.fault = "Unknown function '" + functionName + "'";

        return outcome;
    }

    RuntimeValue result;
    const auto liveStringsBefore = RuntimeValue::getLiveStringCount();

    try {
        m_runtime.executeFunction(functionId, arguments, result);
    } catch (const VirtualMachineException& exception) {
        outcome.fault = exception.what();

        return outcome;
    }

    const auto returnType = function->getReturnType();

    if (returnType == nullptr || returnType->isVoid()) {
        outcome.returnTypeName = m_runtime.getVoidType()->getTypeName();
        outcome.leakedStrings = RuntimeValue::getLiveStringCount() - liveStringsBefore;

        return outcome;
    }

    outcome.returnTypeName = returnType->getTypeName();
    outcome.text = returnType->toString(result);

    if (returnType == m_runtime.getIntegerType()) {
        outcome.integerValue = result.getIntegerValue();
    } else if (returnType == m_runtime.getFloatType()) {
        outcome.floatValue = result.getFloatValue();
    } else if (returnType == m_runtime.getBooleanType()) {
        outcome.booleanValue = result.getBooleanValue();
    }

    returnType->freeValue(result);
    outcome.leakedStrings = RuntimeValue::getLiveStringCount() - liveStringsBefore;

    return outcome;
}

FunctionOutcome runSource(const NomadString& source) {
    LanguageTestFixture fixture;
    fixture.addFunction("main", source);

    if (!fixture.compile()) {
        FunctionOutcome outcome;
        outcome.diagnostics = fixture.getDiagnostics();

        return outcome;
    }

    return fixture.execute("main");
}

FunctionOutcome runExpression(const NomadString& expression) {
    return runSource("return " + expression);
}

NomadString compileErrors(const NomadString& source) {
    LanguageTestFixture fixture;
    fixture.addFunction("main", source);

    if (fixture.compile()) {
        return {};
    }

    const auto diagnostics = fixture.getDiagnostics();

    // A failed compilation must always explain itself.
    return diagnostics.empty() ? NomadString{"<compilation failed without diagnostics>"} : diagnostics;
}

} // namespace nomad::test
