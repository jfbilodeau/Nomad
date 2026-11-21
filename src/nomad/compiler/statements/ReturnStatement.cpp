// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/statements/ReturnStatement.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Expression.hpp>
#include <nomad/compiler/SyntaxTree.hpp>
#include <nomad/compiler/Tokenizer.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>
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

const Expression* ReturnStatementNode::getExpression() const {
    return m_expression.get();
}

VariableId ReturnStatementNode::getStagingVariable() const {
    return m_stagingVariable;
}

void ReturnStatementNode::onResolve(CompilerContext* context, ScriptCallableBase* function) {
    if (context->getMode() == CompilerMode::Interpreter) {
        reportError(context, function, "`return` is not supported by the interpreter");
        return;
    }

    if (function == nullptr) {
        reportError(context, function, "`return` can only be used in a function");
        return;
    }

    if (context->getRuntime()->getConstructor(function->getId()) != nullptr) {
        if (m_expression != nullptr) {
            reportError(context, function, "Script constructors cannot return a value");
        }
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
        return;
    }
    m_forwardReturn = returnType->isStruct() && m_expression->supportsReturnDestination();
    if (returnType->isStruct() && !m_forwardReturn && m_stagingVariable.isInvalid()) {
        m_stagingVariable = function->registerVariable(
            "$return" + std::to_string(function->getVariableCount()), returnType);
    }
}

void ReturnStatementNode::onCompile(Compiler* compiler, ScriptCallableBase* function) {
    const auto candidate = function->getNamedReturnVariable();
    const auto* identifier = m_expression != nullptr ? m_expression->asIdentifierExpression() : nullptr;
    if (candidate.isValid() && identifier != nullptr &&
        identifier->getIdentifier() == function->getVariableName(candidate)) {
        compiler->addFunctionReturn(function);
        return;
    }
    if (m_expression != nullptr) {
        if (m_forwardReturn) {
            m_expression->compileIntoReturn(compiler, function);
        } else if (m_stagingVariable.isValid()) {
            m_expression->compileIntoLocal(compiler, function, m_stagingVariable);
            const auto layout = function->getCallStorageLayout();
            if (layout.returnOffset.has_value()) {
                m_expression->getType()->emitCopyFunctionVariableToStorageParameter(
                    *compiler, function->getVariableOffset(m_stagingVariable), *layout.returnOffset, 0);
            }
        } else {
            m_expression->compile(compiler, function, ExpressionTarget::Result);
        }
    }

    compiler->addFunctionReturn(function);
}

std::unique_ptr<Statement> parseReturnStatement(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens) {
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
