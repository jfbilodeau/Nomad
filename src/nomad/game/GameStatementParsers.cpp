// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/SyntaxTree.hpp>

#include <nomad/compiler/statements/ReturnStatement.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>

#include <memory>

namespace nomad {

class SelectStatement : public Statement {
public:
    SelectStatement(NomadIndex line, NomadIndex column, CallableId predicateFunctionId);

protected:
    void onResolve(CompilerContext* context, ScriptCallableBase* function) override;

private:
    CallableId m_predicateFunctionId;

};

SelectStatement::SelectStatement(
    const NomadIndex line,
    const NomadIndex column,
    const CallableId predicateFunctionId
):
    Statement(line, column),
    m_predicateFunctionId(predicateFunctionId)
{
}

void SelectStatement::onResolve(CompilerContext* context, ScriptCallableBase* function) {
    // The predicate is resolved with its own function, which infers the function return type.
    const auto* predicateFunction = context->getRuntime()->getScriptCallable(m_predicateFunctionId);

    if (predicateFunction->getReturnType() != context->getRuntime()->getBooleanType()) {
        reportError(context, function, "`select` expression must be of type boolean");
    }
}

std::unique_ptr<Statement> parseSelectStatement(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens) {
    const auto functionName = context->getCompiler()->generateFunctionName("select", function, tokens->getLineIndex());

    auto* compiler = context->getCompiler();
    const auto functionId = compiler->createFunction(
        context, compiler->getScriptId(function->getId()), functionName);
    if (functionId.isInvalid()) {
        return nullptr;
    }

    auto predicate = parser::parseExpression(context, function, tokens);

    if (predicate == nullptr) {
        return nullptr;
    }

    // Wrap predicate in return statement.
    auto return_statement = std::make_unique<ReturnStatementNode>(tokens->getLineIndex(), tokens->getColumnIndex(), std::move(predicate));

    // Wrap statement in statement list.
    auto function_node = std::make_unique<FunctionNode>(tokens->getLineIndex(), tokens->getColumnIndex());
    function_node->addStatement(std::move(return_statement));

    // Assign statement list to function
    context->getCompiler()->setFunctionBody(functionId, std::move(function_node));

    auto select_statement = std::make_unique<SelectStatement>(
        tokens->getLineIndex(),
        tokens->getColumnIndex(),
        functionId
    );

    return select_statement;
}

} // namespace nomad
