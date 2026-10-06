// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/statements/ReturnStatement.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/SyntaxTree.hpp>
#include <nomad/compiler/Tokenizer.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/Function.hpp>
#include <nomad/script/Type.hpp>

namespace nomad {

ReturnStatementNode::ReturnStatementNode(
    const NomadIndex line,
    const NomadIndex column,
    std::unique_ptr<Expression> expression
):
    Statement(line, column),
    m_expression(std::move(expression))
{
}

NomadBoolean ReturnStatementNode::alwaysReturns() const {
    return true;
}

void ReturnStatementNode::onResolve(CompilerContext* context, Function* function) {
    if (context->getMode() == CompilerMode::Interpreter) {
        reportError(context, function, "`return` is not supported by the interpreter");
        return;
    }

    if (function == nullptr) {
        reportError(context, function, "`return` can only be used in a function");
        return;
    }

    context->getCompiler()->addReturningFunction(function->getId());

    const Type* returnType = context->getRuntime()->getVoidType();

    if (m_expression != nullptr) {
        if (!m_expression->resolve(context, function)) {
            return;
        }

        returnType = m_expression->getType();

        if (returnType == nullptr) {
            reportError(context, function, "Cannot determine type of return expression");
            return;
        }
    }

    // The function return type is inferred from its first return statement.
    const auto* functionReturnType = function->getReturnType();

    if (functionReturnType == nullptr) {
        function->setReturnType(returnType);
    } else if (!functionReturnType->sameType(returnType)) {
        reportError(
            context,
            function,
            "Invalid return statement. Attempting to return `" + returnType->getTypeName() + "` but function returns `" +
                functionReturnType->getTypeName() + "`"
        );
    }
}

void ReturnStatementNode::onCompile(Compiler* compiler, Function* function) {
    if (m_expression != nullptr) {
        m_expression->compile(compiler, function, ExpressionTarget::Result);
    }

    compiler->addFunctionReturn(function);
}

std::unique_ptr<Statement> parseReturnStatement(CompilerContext* context, Function* function, Tokenizer* tokens) {
    if (tokens->endOfLine() == false) {
        auto expression = parser::parseExpression(context, function, tokens);

        if (expression == nullptr) {
            return nullptr;
        }

        return std::make_unique<ReturnStatementNode>(tokens->getLineIndex(), tokens->getColumnIndex(), std::move(expression));
    } else {
        return std::make_unique<ReturnStatementNode>(tokens->getLineIndex(), tokens->getColumnIndex(), nullptr);
    }
}

} // namespace nomad
