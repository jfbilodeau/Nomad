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
    evaluation.clearError();
    evaluation.clearResult();
    evaluation.validateFrame();
    auto compiler = evaluation.m_runtime->createCompiler();
    CompilerContext context(compiler.get(), CompilerMode::Interpreter, &evaluation);
    auto* function = evaluation.m_function.get();
    Tokenizer tokens(&context, function->getPath(), source);
    if (const auto* error = context.getFirstError()) {
        evaluation.setDiagnostic(*error);
        return false;
    }
    if (tokens.getLineCount() != 1 || !tokens.nextLine()) {
        evaluation.setError("Interpreter requires one non-empty expression");
        return false;
    }
    auto expression = parser::parseExpression(&context, function, &tokens);
    if (expression != nullptr && parser::expectEndOfLine(&context, function, &tokens)) {
        (void)expression->resolve(&context, function);
    }
    if (const auto* error = context.getFirstError()) {
        evaluation.setDiagnostic(*error);
        return false;
    }
    if (expression == nullptr || expression->getType() == nullptr || expression->getType()->isVoid()) {
        evaluation.setError("Interpreter expression must return a value");
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
    evaluation.validateFrame();
    evaluation.clearError();
    evaluation.clearResult();
    const RegisterRestoration registers(*evaluation.getVirtualMachine());
    try {
        auto compiler = evaluation.m_runtime->createCompiler();
        CompilerContext compilerContext(compiler.get(), CompilerMode::Interpreter, &evaluation);
        Tokenizer tokenizer(&compilerContext, evaluation.m_function->getPath(), source);
        if (tokenizer.getLineCount() > 1) {
            evaluation.setError("Interpreter accepts a single line of input");
            return false;
        }
        if (const auto* lexingError = compilerContext.getFirstError(); lexingError != nullptr) {
            evaluation.setError(lexingError->message);
            return false;
        }
        if (tokenizer.endOfLine()) {
            evaluation.setVoidResult();
            return true;
        }

        const auto& firstToken = tokenizer.currentToken();
        const auto isAssignment = tokenizer.getTokenCount() >= 3 &&
                                  tokenizer.getTokenAt(1) == "=";
        const auto isStatement = firstToken.type == TokenType::Identifier &&
                                 compiler->isStatement(firstToken.textValue);
        NativeFunctionDefinition nativeFunction;
        const auto isNativeFunction = firstToken.type == TokenType::Identifier &&
                               evaluation.m_runtime->getNativeFunctionDefinition(firstToken.textValue, nativeFunction);
        const auto functionId = firstToken.type == TokenType::Identifier
            ? evaluation.m_runtime->getFunctionId(firstToken.textValue)
            : NOMAD_INVALID_ID;
        const auto* function = functionId == NOMAD_INVALID_ID ? nullptr : evaluation.m_runtime->getFunction(functionId);
        const auto isValueReturningCall =
            (isNativeFunction && nativeFunction.returnType != nullptr && !nativeFunction.returnType->isVoid()) ||
            (function != nullptr && function->getReturnType() != nullptr && !function->getReturnType()->isVoid());
        const auto line = tokenizer.getLineIndex();
        const auto column = tokenizer.getColumnIndex();
        const auto parseAsExpression = !isAssignment && !isStatement &&
                                       (isValueReturningCall || (!isNativeFunction && function == nullptr));

        std::unique_ptr<Statement> statement;
        if (parseAsExpression) {
            auto expression = parser::parseExpression(&compilerContext, evaluation.m_function.get(), &tokenizer);
            if (expression != nullptr &&
                parser::expectEndOfLine(&compilerContext, evaluation.m_function.get(), &tokenizer)) {
                if (auto* member = expression->asIntrinsicExpression()) {
                    member->setAllowVoid(true);
                }
                statement = std::make_unique<ExpressionStatement>(
                    line,
                    column,
                    std::move(expression)
                );
            }
        } else if (isStatement && !isAssignment) {
            evaluation.setError("Statement '" + firstToken.textValue + "' is not supported by the interpreter");
            return false;
        } else {
            statement = parser::parseLine(&compilerContext, evaluation.m_function.get(), &tokenizer);
        }

        if (const auto* parseError = compilerContext.getFirstError(); parseError != nullptr) {
            evaluation.setError(parseError->message);
            return false;
        }

        if (statement == nullptr) {
            evaluation.setVoidResult();
            return true;
        }

        if (!statement->resolve(&compilerContext, evaluation.m_function.get())) {
            const auto* firstError = compilerContext.getFirstError();
            evaluation.setError(firstError != nullptr ? firstError->message : "Failed to resolve interpreter input");
            return false;
        }

        statement->evaluate(evaluation);
        return !evaluation.hasError();
    } catch (const NomadException& exception) {
        evaluation.clearResult();
        evaluation.setError(exception.what());
        return false;
    } catch (const std::exception& exception) {
        evaluation.clearResult();
        evaluation.setError(exception.what());
        return false;
    }
}

} // namespace nomad
