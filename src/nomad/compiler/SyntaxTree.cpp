// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/SyntaxTree.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/script/Function.hpp>

#include <nomad/script/VirtualMachine.hpp>
#include <nomad/script/Interpreter.hpp>

#include <algorithm>
#include <utility>

namespace nomad {

namespace {

NomadString getSourceName(const Function* function) {
    return function != nullptr ? function->getPath() : NomadString{};
}

} // namespace

///////////////////////////////////////////////////////////////////////////////
// AstException
AstException::AstException(const NomadString& message, NomadIndex line, NomadIndex column):
    NomadException(message),
    m_line(line),
    m_column(column) {
}

AstException::AstException(const NomadString& message, NomadIndex startLine, NomadIndex startColumn, NomadIndex endLine, NomadIndex endColumn):
    NomadException(message),
    m_line(startLine),
    m_column(startColumn) {
    (void)endLine; (void)endColumn; // currently AstException exposes only start position; keep values for future use
}

NomadIndex AstException::getLine() const {
    return m_line;
}

NomadIndex AstException::getColumn() const {
    return m_column;
}


///////////////////////////////////////////////////////////////////////////////
// AstNode
AstNode::AstNode(const NomadIndex line, const NomadIndex column) {
    m_span.startLine = line;
    m_span.startColumn = column;
    m_span.endLine = line;
    m_span.endColumn = column;
}

void AstNode::raiseException(const NomadString& message) const {
    throw AstException(message, m_span.startLine, m_span.startColumn, m_span.endLine, m_span.endColumn);
}

void AstNode::reportError(CompilerContext* context, const Function* function, const NomadString& message) const {
    context->reportError(message, getSourceName(function), m_span.startLine, m_span.startColumn);
}

void AstNode::reportWarning(CompilerContext* context, const Function* function, const NomadString& message) const {
    context->reportWarning(message, getSourceName(function), m_span.startLine, m_span.startColumn);
}

NomadIndex AstNode::getLine() const {
    return m_span.startLine;
}

NomadIndex AstNode::getColumn() const {
    return m_span.startColumn;
}

NomadIndex AstNode::getEndLine() const {
    return m_span.endLine;
}

NomadIndex AstNode::getEndColumn() const {
    return m_span.endColumn;
}

void AstNode::setEndSpan(const NomadIndex endLine, const NomadIndex endColumn) {
    m_span.endLine = endLine;
    m_span.endColumn = endColumn;
}

///////////////////////////////////////////////////////////////////////////////
// StatementNode
Statement::Statement(const NomadIndex line, const NomadIndex column):
    AstNode(line, column) {
}

NomadBoolean Statement::resolve(CompilerContext* context, Function* function) {
    const auto errorCount = context->getErrorCount();

    onResolve(context, function);

    return context->getErrorCount() == errorCount;
}

void Statement::compile(Compiler* compiler, Function* function) {
    onCompile(compiler, function);
}

NomadBoolean Statement::alwaysReturns() const {
    return false;
}

NomadBoolean Statement::isDeclaration() const {
    return false;
}

void Statement::checkReachability(CompilerContext* /*context*/, const Function* /*function*/) const {
}

void Statement::evaluate(Interpreter& context) const {
    context.clearResult();
    if (context.hasError()) {
        return;
    }

    try {
        onEvaluate(context);
    } catch (...) {
        context.clearResult();
        throw;
    }
}

void Statement::onResolve(CompilerContext* /*context*/, Function* /*function*/) {
    // Default implementation does nothing
}

void Statement::onCompile(Compiler* /*compiler*/, Function* /*function*/) {
    // Default implementation does nothing
}

void Statement::onEvaluate(Interpreter& /*context*/) const {
    raiseException("Statement evaluation is not supported by the interpreter");
}

///////////////////////////////////////////////////////////////////////////////
// NullStatementNode
NullStatement::NullStatement(NomadIndex line, NomadIndex column):
    Statement(line, column) {

}

void NullStatement::onCompile(nomad::Compiler* /*compiler*/, Function* /*function*/) {
    // Nothing to do...
}

void NullStatement::onEvaluate(Interpreter& context) const {
    context.setVoidResult();
}

///////////////////////////////////////////////////////////////////////////////
// AssignmentStatementNode
AssignmentStatement::AssignmentStatement(
    const NomadIndex line,
    const NomadIndex column,
    NomadString identifier,
    std::unique_ptr<Expression> expression
):
    Statement(line, column),
    m_identifier(std::move(identifier)),
    m_expression(std::move(expression)) {

}

void AssignmentStatement::onResolve(CompilerContext* context, Function* function) {
    if (!m_expression->resolve(context, function)) {
        return;
    }

    auto* compiler = context->getCompiler();
    const auto expressionType = m_expression->getType();

    IdentifierDefinition identifier;
    compiler->getIdentifierDefinition(m_identifier, function, identifier);

    if (context->getMode() == CompilerMode::Interpreter) {
        auto* interpreterRuntime = context->getRuntime();

        switch (identifier.identifierType) {
        case IdentifierType::DynamicVariable: {
            if (!interpreterRuntime->canSetDynamicVariable(identifier.variableId)) {
                reportError(context, function, "Dynamic variable '" + m_identifier + "' is read-only");
                return;
            }

            const auto variableType = interpreterRuntime->getDynamicVariableType(identifier.variableId);
            if (expressionType != nullptr && variableType != nullptr && variableType != expressionType) {
                reportError(
                    context,
                    function,
                    "Cannot assign value of type '" + expressionType->getTypeName() +
                    "' to dynamic variable '" + m_identifier + "' of type '" + variableType->getTypeName() + "'"
                );
                return;
            }
            break;
        }

        case IdentifierType::ContextVariable: {
            const auto variableType = interpreterRuntime->getContextVariableType(identifier.variableId);

            if (variableType != nullptr && expressionType != nullptr && variableType != expressionType) {
                reportError(
                    context,
                    function,
                    "Cannot assign value of type '" + expressionType->getTypeName() +
                    "' to variable '" + m_identifier + "' of type '" + variableType->getTypeName() + "'"
                );
                return;
            }

            if (variableType == nullptr && expressionType != nullptr) {
                interpreterRuntime->registerOrUpdateContextVariable(m_identifier, expressionType);
                context->getCompiler()->getIdentifierDefinition(m_identifier, function, identifier);
            }
            break;
        }

        default: {
            // Console variables are defined when assigned and may change type, so only the target kind is checked.
            const auto isNewConsoleVariable = identifier.identifierType == IdentifierType::Unknown &&
                interpreterRuntime->getVariableContextIdByPrefix(m_identifier) == NOMAD_INVALID_ID;

            if (identifier.identifierType != IdentifierType::FunctionVariable && !isNewConsoleVariable) {
                reportError(context, function, "Cannot assign value to '" + m_identifier + "' in the interpreter");
                return;
            }
            break;
        }
        }

        m_identifierDefinition = identifier;
        return;
    }

    if (expressionType == nullptr) {
        reportError(context, function, "Cannot determine type of expression assigned to '" + m_identifier + "'");
        return;
    }

    auto* runtime = context->getRuntime();

    switch (identifier.identifierType) {
    case IdentifierType::FunctionVariable: {
        // Make sure type is unknown or the same as the expression.
        const auto variableType = function->getVariableType(identifier.variableId);

        if (variableType != nullptr && variableType != expressionType) {
            reportError(context, function, "Cannot assign value of type '" + expressionType->getTypeName() + "' to variable '" + m_identifier + "' of type '" + variableType->getTypeName() + "'");
            return;
        }

        function->setVariableType(identifier.variableId, expressionType);
        break;
    }
    case IdentifierType::DynamicVariable: {
        const auto variableType = runtime->getDynamicVariableType(identifier.variableId);

        if (variableType != expressionType) {
            const auto& variableTypeName = variableType ? variableType->getTypeName() : "<unknown>";

            reportError(context, function, "Cannot assign value of type '" + expressionType->getTypeName() + "' to dynamic variable '" + m_identifier + "' of type '" + variableTypeName + "'");
        }
        break;
    }
    case IdentifierType::ContextVariable: {
        // Make sure type is unknown or the same as the expression.
        const auto variableType = runtime->getContextVariableType(identifier.variableId);

        if (variableType != nullptr && variableType != expressionType) {
            reportError(context, function, "Cannot assign value of type '" + expressionType->getTypeName() + "' to variable '" + m_identifier + "' of type '" + variableType->getTypeName() + "'");
            return;
        }

        if (variableType == nullptr) {
            runtime->registerOrUpdateContextVariable(m_identifier, expressionType);
        }
        break;
    }
    case IdentifierType::Constant:
        reportError(context, function, "Cannot assign value to constant '" + m_identifier + "'");
        break;
    case IdentifierType::Keyword:
        reportError(context, function, "Cannot assign value to keyword '" + m_identifier + "'");
        break;
    case IdentifierType::Statement:
        reportError(context, function, "Cannot assign value to statement '" + m_identifier + "'");
        break;
    case IdentifierType::NativeFunction:
        reportError(context, function, "Cannot assign value to native function '" + m_identifier + "'");
        break;
    case IdentifierType::Function:
        reportError(context, function, "Cannot assign value to function '" + m_identifier + "'");
        break;
    case IdentifierType::Event:
        reportError(context, function, "Cannot assign value to event type '" + m_identifier + "'");
        break;
    case IdentifierType::Parameter:
        reportError(
            context,
            function,
            "Cannot assign value to parameter '" + m_identifier + "': parameters are read-only. Copy it to a local variable first"
        );
        break;
    default:
        reportError(context, function, "Cannot assign value to unknown identifier '" + m_identifier + "'");
        break;
    }
}

void AssignmentStatement::onCompile(Compiler* compiler, Function* function) {
    m_expression->compile(compiler, function, ExpressionTarget::Result);

    IdentifierDefinition identifier;

    compiler->getIdentifierDefinition(m_identifier, function, identifier);

    // Resolve validated the target and the expression type.
    const auto* typeOpCodes = m_expression->getType()->getTypeOpCodes();

    switch (identifier.identifierType) {
    case IdentifierType::FunctionVariable:
        // `r` is moved into the variable, so its previous value is released first.
        compiler->addFreeValue(typeOpCodes->freeFunctionVariable, toNomadIndex(identifier.variableId));
        compiler->addOpCode(typeOpCodes->setFunctionVariable);
        compiler->addId(identifier.variableId);
        break;

    case IdentifierType::DynamicVariable:
        // The setter copies what it keeps.
        compiler->addOpCode(typeOpCodes->setDynamicVariable);
        compiler->addId(identifier.variableId);
        compiler->addFreeValue(typeOpCodes->freeResult);
        break;

    case IdentifierType::ContextVariable: {
        // The variable context copies what it keeps.
        compiler->addOpCode(typeOpCodes->setContextVariable);
        compiler->addId(identifier.variableId);
        compiler->addFreeValue(typeOpCodes->freeResult);

        compiler->getRuntime()->setContextVariableWritten(identifier.variableId, true);
        break;
    }
    default:
        compiler->reportInternalError("Cannot assign value to '" + m_identifier + "'");
    }
}

void AssignmentStatement::onEvaluate(Interpreter& context) const {
    m_expression->evaluate(context);
    const auto* value = context.getResult();
    if (value == nullptr) {
        if (context.hasError()) {
            return;
        }

        raiseException("Assignment expression did not produce a value");
    }

    const TypedValue assignedValue(*value);
    const auto* type = assignedValue.getType();

    switch (m_identifierDefinition.identifierType) {
    case IdentifierType::DynamicVariable: {
        auto* runtime = context.getRuntime();
        if (type->isString()) {
            runtime->setStringDynamicVariable(
                context.getVirtualMachine(),
                m_identifierDefinition.variableId,
                assignedValue.getValue()
            );
        } else {
            runtime->setDynamicVariable(
                context.getVirtualMachine(),
                m_identifierDefinition.variableId,
                assignedValue.getValue()
            );
        }
        break;
    }

    case IdentifierType::ContextVariable: {
        auto* runtime = context.getRuntime();

        // The variable context copies what it keeps.
        runtime->setContextVariableValue(m_identifierDefinition.variableId, assignedValue.getValue());
        runtime->setContextVariableWritten(m_identifierDefinition.variableId, true);
        break;
    }

    default:
        context.setVariable(m_identifier, type, assignedValue.getValue());
        break;
    }

    context.setResult(assignedValue);
}

ExpressionStatement::ExpressionStatement(
    const NomadIndex line,
    const NomadIndex column,
    std::unique_ptr<Expression> expression
):
    Statement(line, column),
    m_expression(std::move(expression))
{
}

void ExpressionStatement::onResolve(CompilerContext* context, Function* function) {
    (void)m_expression->resolve(context, function);
}

void ExpressionStatement::onCompile(Compiler* /*compiler*/, Function* /*function*/) {
    raiseException("Expression statements are only supported by the interpreter");
}

void ExpressionStatement::onEvaluate(Interpreter& context) const {
    m_expression->evaluate(context);
}

///////////////////////////////////////////////////////////////////////////////
// StatementList
NomadBoolean StatementList::resolve(CompilerContext* context, Function* function) const {
    NomadBoolean resolved = true;

    for (auto& statement: m_statements) {
        if (!statement->resolve(context, function)) {
            resolved = false;
        }
    }

    return resolved;
}

void StatementList::compile(Compiler* compiler, Function* function) const {
    for (auto& statement: m_statements) {
        statement->compile(compiler, function);
    }
}

void StatementList::evaluate(Interpreter& context) const {
    if (context.hasError()) {
        context.clearResult();
        return;
    }

    context.setVoidResult();
    for (const auto& statement: m_statements) {
        if (context.hasError()) {
            context.clearResult();
            return;
        }
        statement->evaluate(context);
    }
}

void StatementList::addStatement(std::unique_ptr<Statement> statement) {
    if (statement == nullptr) {
        // Skip null statements
        return;
    }

    m_statements.push_back(std::move(statement));
}

NomadIndex StatementList::getStatementCount() const {
    return m_statements.size();
}

NomadIndex StatementList::isEmpty() const {
    return m_statements.empty();
}

NomadBoolean StatementList::alwaysReturns() const {
    // Statements after one that always returns are unreachable, so any such statement is enough.
    return std::ranges::any_of(m_statements, [](const auto& statement) {
        return statement->alwaysReturns();
    });
}

void StatementList::checkReachability(CompilerContext* context, const Function* function) const {
    auto returned = false;

    for (const auto& statement: m_statements) {
        if (returned && !statement->isDeclaration()) {
            context->reportError(
                "Unreachable code: every path before this statement ends with a `return`",
                function != nullptr ? function->getPath() : NomadString{},
                statement->getLine(),
                statement->getColumn()
            );

            // One report per statement list is enough.
            return;
        }

        statement->checkReachability(context, function);

        returned = returned || statement->alwaysReturns();
    }
}

///////////////////////////////////////////////////////////////////////////////
// FunctionNode
FunctionNode::FunctionNode(const NomadIndex line, const NomadIndex row):
    AstNode(line, row)
{}

NomadBoolean FunctionNode::resolve(CompilerContext* context, Function* function) const {
    return m_statements.resolve(context, function);
}

void FunctionNode::compile(Compiler* compiler, Function* function) const {
    m_statements.compile(compiler, function);
}

NomadBoolean FunctionNode::alwaysReturns() const {
    return m_statements.alwaysReturns();
}

void FunctionNode::checkReachability(CompilerContext* context, const Function* function) const {
    m_statements.checkReachability(context, function);
}

void FunctionNode::addStatement(std::unique_ptr<Statement> statement) {
    m_statements.addStatement(std::move(statement));
}

StatementList* FunctionNode::getStatements() {
    return &m_statements;
}

///////////////////////////////////////////////////////////////////////////////
// ParamDecl
ParameterDeclaration::ParameterDeclaration(NomadIndex line, NomadIndex column, NomadString name, const Type* type):
    AstNode(line, column),
    m_name(std::move(name)),
    m_type(type) {
}

const NomadString& ParameterDeclaration::getName() const {
    return m_name;
}

const Type* ParameterDeclaration::getType() const {
    return m_type;
}

///////////////////////////////////////////////////////////////////////////////
// FunctionDecl
FunctionDeclaration::FunctionDeclaration(NomadIndex line, NomadIndex column, NomadString name):
    Statement(line, column),
    m_name(std::move(name)) {
}

void FunctionDeclaration::addParam(std::unique_ptr<ParameterDeclaration> param) {
    m_params.push_back(std::move(param));
}

void FunctionDeclaration::setFunctionId(NomadId id) {
    m_functionId = id;
}

NomadId FunctionDeclaration::getFunctionId() const {
    return m_functionId;
}

void FunctionDeclaration::setBody(std::unique_ptr<FunctionNode> body) {
    m_body = std::move(body);
}

const NomadString& FunctionDeclaration::getName() const {
    return m_name;
}

const std::vector<std::unique_ptr<ParameterDeclaration>>& FunctionDeclaration::getParams() const {
    return m_params;
}

FunctionNode* FunctionDeclaration::getBody() const {
    return m_body.get();
}

NomadBoolean FunctionDeclaration::isDeclaration() const {
    return true;
}

void FunctionDeclaration::onResolve(CompilerContext* context, Function* function) {
    // Default: resolve body if present
    if (m_body) {
        (void)m_body->resolve(context, function);
    }
}

void FunctionDeclaration::onCompile(Compiler* /*compiler*/, Function* /*function*/) {
    // Functions compile as functions; by default do nothing here. Runtime registration handled elsewhere.
}

///////////////////////////////////////////////////////////////////////////////
// EventDecl
EventDeclaration::EventDeclaration(NomadIndex line, NomadIndex column, NomadString name):
    Statement(line, column),
    m_name(std::move(name)) {
}

void EventDeclaration::addParam(std::unique_ptr<ParameterDeclaration> param) {
    m_params.push_back(std::move(param));
}

const NomadString& EventDeclaration::getName() const {
    return m_name;
}

const std::vector<std::unique_ptr<ParameterDeclaration>>& EventDeclaration::getParams() const {
    return m_params;
}

NomadBoolean EventDeclaration::isDeclaration() const {
    return true;
}

void EventDeclaration::onResolve(CompilerContext* /*context*/, Function* /*function*/) {
    // Nothing by default; runtime registration happens in pre-parse stage.
}

///////////////////////////////////////////////////////////////////////////////
// ConstDecl
ConstDeclaration::ConstDeclaration(NomadIndex line, NomadIndex column, NomadString name, std::unique_ptr<Expression> initializer):
    Statement(line, column),
    m_name(std::move(name)),
    m_initializer(std::move(initializer)) {
}

const NomadString& ConstDeclaration::getName() const {
    return m_name;
}

const Expression* ConstDeclaration::getInitializer() const {
    return m_initializer.get();
}

NomadBoolean ConstDeclaration::isDeclaration() const {
    return true;
}

void ConstDeclaration::onResolve(CompilerContext* context, Function* function) {
    if (m_initializer && m_initializer->resolve(context, function)) {
        m_type = m_initializer->getType();
    }
}

///////////////////////////////////////////////////////////////////////////////
// OnHandlerDecl
EventHandlerDeclaration::EventHandlerDeclaration(NomadIndex line, NomadIndex column, NomadString eventName, std::unique_ptr<FunctionDeclaration> handler):
    Statement(line, column),
    m_eventName(std::move(eventName)),
    m_handler(std::move(handler)) {
}

const NomadString& EventHandlerDeclaration::getEventName() const {
    return m_eventName;
}

const FunctionDeclaration* EventHandlerDeclaration::getHandler() const {
    return m_handler.get();
}

NomadBoolean EventHandlerDeclaration::isDeclaration() const {
    return true;
}

void EventHandlerDeclaration::onResolve(CompilerContext* context, Function* function) {
    if (m_handler) {
        (void)m_handler->resolve(context, function);
    }
}

void EventHandlerDeclaration::onCompile(Compiler* /*compiler*/, Function* /*function*/) {
    // Handler registration and compilation are handled by the compiler's event system.
}

} // namespace nomad
