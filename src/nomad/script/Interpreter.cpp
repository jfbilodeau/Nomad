// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Interpreter.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Expression.hpp>
#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/SyntaxTree.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/script/NativeFunction.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptFunction.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

namespace nomad {

namespace {

void retainDiagnostics(InterpreterContext& evaluation, std::span<const Diagnostic> diagnostics,
    const NomadString& sourceName) {
    for (auto diagnostic : diagnostics) {
        diagnostic.sourceName = sourceName;
        evaluation.addDiagnostic(diagnostic);
    }
}

void reportInputError(InterpreterContext& evaluation, const SourceText& source, const NomadString& message) {
    evaluation.addDiagnostic({DiagnosticSeverity::Error, message, source.sourceName,
        NOMAD_INVALID_INDEX, NOMAD_INVALID_INDEX});
}

class RegisterRestoration {
public:
    explicit RegisterRestoration(VirtualMachine& machine):
        m_machine(machine), m_result(machine.getResult()), m_intermediate(machine.getIntermediate()) {}

    ~RegisterRestoration() {
        m_machine.setResult(m_result);
        m_machine.setIntermediate(m_intermediate);
    }

private:
    VirtualMachine& m_machine;
    RuntimeValue m_result;
    RuntimeValue m_intermediate;
};

} // namespace

bool Interpreter::evaluateExpression(const NomadString& source, InterpreterContext& evaluation) const {
    return evaluateExpression(SourceText{evaluation.m_function->getPath(), source}, evaluation);
}

bool Interpreter::evaluateExpression(const SourceText& source, InterpreterContext& evaluation) const {
    evaluation.clearError();
    evaluation.clearResult();
    evaluation.validateFrame();
    auto compiler = evaluation.m_runtime->createCompiler();
    CompilerContext context(compiler.get(), CompilerMode::Interpreter, &evaluation);
    auto* function = evaluation.m_function.get();
    Tokenizer tokens(&context, source.sourceName, source.text);
    retainDiagnostics(evaluation, context.getDiagnostics(), source.sourceName);
    if (context.hasError()) {
        return false;
    }
    if (tokens.getLineCount() != 1 || !tokens.nextLine()) {
        reportInputError(evaluation, source, "Interpreter requires one non-empty expression");
        return false;
    }
    auto expression = parser::parseExpression(&context, function, &tokens);
    if (expression != nullptr && parser::expectEndOfLine(&context, function, &tokens)) {
        (void)expression->resolve(&context, function);
    }
    // Tokenization diagnostics were already retained above.
    const auto retainedDiagnostics = evaluation.getDiagnostics().size();
    retainDiagnostics(evaluation, context.getDiagnostics().subspan(retainedDiagnostics), source.sourceName);
    if (context.hasError()) {
        return false;
    }
    if (expression == nullptr || expression->getType() == nullptr || expression->getType()->isVoid()) {
        reportInputError(evaluation, source, "Interpreter expression must return a value");
        return false;
    }
    auto* machine = evaluation.getVirtualMachine();
    const RegisterRestoration registers(*machine);
    try {
        expression->evaluate(evaluation);
    } catch (...) {
        evaluation.clearResult();
        throw;
    }
    return !evaluation.hasError();
}

NomadBoolean Interpreter::execute(const NomadString& source, InterpreterContext& evaluation) const {
    return execute(SourceText{evaluation.m_function->getPath(), source}, evaluation);
}

NomadBoolean Interpreter::execute(const SourceText& source, InterpreterContext& evaluation) const {
    evaluation.validateFrame();
    evaluation.clearError();
    evaluation.clearResult();
    const RegisterRestoration registers(*evaluation.getVirtualMachine());
    try {
        auto compiler = evaluation.m_runtime->createCompiler();
        CompilerContext compilerContext(compiler.get(), CompilerMode::Interpreter, &evaluation);
        Tokenizer tokenizer(&compilerContext, source.sourceName, source.text);
        if (tokenizer.getLineCount() > 1) {
            reportInputError(evaluation, source, "Interpreter accepts a single line of input");
            return false;
        }
        if (compilerContext.hasError()) {
            retainDiagnostics(evaluation, compilerContext.getDiagnostics(), source.sourceName);
            return false;
        }
        if (!tokenizer.nextLine()) {
            evaluation.setVoidResult();
            return true;
        }

        const auto& firstToken = tokenizer.currentToken();
        const auto isAssignment = parser::hasAssignmentOperator(&tokenizer);
        const auto isStatement = firstToken.type == TokenType::Identifier &&
                                 compiler->isStatement(firstToken.textValue);
        const auto* nativeFunction = firstToken.type == TokenType::Identifier ?
            evaluation.m_runtime->getNativeFunction(firstToken.textValue) : nullptr;
        const auto functionId = firstToken.type == TokenType::Identifier
            ? evaluation.m_runtime->getFunctionId(firstToken.textValue)
            : NOMAD_INVALID_ID;
        const auto* function = functionId == NOMAD_INVALID_ID ? nullptr : evaluation.m_runtime->getFunction(functionId);
        const auto isValueReturningCall =
            (nativeFunction != nullptr && nativeFunction->getReturnType() != nullptr && !nativeFunction->getReturnType()->isVoid()) ||
            (function != nullptr && function->getReturnType() != nullptr && !function->getReturnType()->isVoid());
        const auto line = tokenizer.getLineIndex();
        const auto column = tokenizer.getColumnIndex();
        const auto parseAsExpression = !isAssignment && !isStatement &&
                                       (isValueReturningCall || (nativeFunction == nullptr && function == nullptr));

        std::unique_ptr<Statement> statement;
        if (parseAsExpression) {
            auto expression = parser::parseExpression(&compilerContext, evaluation.m_function.get(), &tokenizer);
            if (expression != nullptr &&
                parser::expectEndOfLine(&compilerContext, evaluation.m_function.get(), &tokenizer)) {
                if (auto* member = expression->asNativeExpression()) {
                    member->setAllowVoid(true);
                }
                statement = std::make_unique<ExpressionStatement>(
                    line,
                    column,
                    std::move(expression)
                );
            }
        } else if (isStatement && !isAssignment) {
            reportInputError(evaluation, source, "Statement '" + firstToken.textValue + "' is not supported by the interpreter");
            return false;
        } else {
            statement = parser::parseLine(&compilerContext, evaluation.m_function.get(), &tokenizer);
        }

        if (compilerContext.hasError()) {
            retainDiagnostics(evaluation, compilerContext.getDiagnostics(), source.sourceName);
            return false;
        }

        if (statement == nullptr) {
            evaluation.setVoidResult();
            return true;
        }

        if (!statement->resolve(&compilerContext, evaluation.m_function.get())) {
            retainDiagnostics(evaluation, compilerContext.getDiagnostics(), source.sourceName);
            if (!evaluation.hasError()) {
                reportInputError(evaluation, source, "Failed to resolve interpreter input");
            }
            return false;
        }

        retainDiagnostics(evaluation, compilerContext.getDiagnostics(), source.sourceName);
        statement->evaluate(evaluation);
        return !evaluation.hasError();
    } catch (const NomadException& exception) {
        evaluation.clearResult();
        reportInputError(evaluation, source, exception.what());
        return false;
    } catch (const std::exception& exception) {
        evaluation.clearResult();
        reportInputError(evaluation, source, exception.what());
        return false;
    }
}

} // namespace nomad
