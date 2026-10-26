// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/SyntaxTree.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/script/ScriptCallable.hpp>

#include <nomad/script/Interpreter.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <algorithm>
#include <utility>

namespace nomad {

namespace {

NomadString getSourceName(const ScriptCallableBase* function) {
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

void AstNode::reportError(CompilerContext* context, const ScriptCallableBase* function, const NomadString& message) const {
    context->reportError(message, getSourceName(function), m_span.startLine, m_span.startColumn);
}

void AstNode::reportWarning(CompilerContext* context, const ScriptCallableBase* function, const NomadString& message) const {
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

NomadBoolean Statement::resolve(CompilerContext* context, ScriptCallableBase* function) {
    const auto errorCount = context->getErrorCount();

    onResolve(context, function);

    return context->getErrorCount() == errorCount;
}

void Statement::compile(Compiler* compiler, ScriptCallableBase* function) {
    onCompile(compiler, function);
}

NomadBoolean Statement::alwaysReturns() const {
    return false;
}

NomadBoolean Statement::isDeclaration() const {
    return false;
}

void Statement::checkReachability(CompilerContext* /*context*/, const ScriptCallableBase* /*function*/) const {
}

void Statement::evaluate(InterpreterContext& context) const {
    context.clearResult();
    if (context.hasError() || context.getLoopControl() != LoopControl::None) {
        return;
    }

    try {
        onEvaluate(context);
    } catch (...) {
        context.clearResult();
        throw;
    }
}

void Statement::onResolve(CompilerContext* /*context*/, ScriptCallableBase* /*function*/) {
    // Default implementation does nothing
}

void Statement::onCompile(Compiler* /*compiler*/, ScriptCallableBase* /*function*/) {
    // Default implementation does nothing
}

void Statement::onEvaluate(InterpreterContext& /*context*/) const {
    raiseException("Statement evaluation is not supported by the interpreter");
}

///////////////////////////////////////////////////////////////////////////////
// NullStatementNode
NullStatement::NullStatement(NomadIndex line, NomadIndex column):
    Statement(line, column) {

}

void NullStatement::onCompile(nomad::Compiler* /*compiler*/, ScriptCallableBase* /*function*/) {
    // Nothing to do...
}

void NullStatement::onEvaluate(InterpreterContext& context) const {
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

const NomadString& AssignmentStatement::getIdentifier() const {
    return m_identifier;
}

const Expression* AssignmentStatement::getExpression() const {
    return m_expression.get();
}

HubInitializerStatement::HubInitializerStatement(
    const NomadIndex line,
    const NomadIndex column,
    NomadString identifier,
    std::unique_ptr<Expression> expression,
    ScriptCallableBase* sourceFunction,
    NomadString hub
):
    AssignmentStatement(line, column, std::move(identifier), std::move(expression)),
    m_sourceFunction(sourceFunction),
    m_hub(std::move(hub))
{}

HubInitializerStatement::~HubInitializerStatement() = default;

const NomadString& HubInitializerStatement::getSourceName() const {
    return m_sourceFunction->getPath();
}

const TypedValue* HubInitializerStatement::getInitialValue() const {
    return m_initialValue.get();
}

void HubInitializerStatement::setInitialValue(std::unique_ptr<TypedValue> value) {
    m_initialValue = std::move(value);
}

void HubInitializerStatement::onResolve(CompilerContext* context, ScriptCallableBase* /*function*/) {
    CompilerContext initializerContext(context->getCompiler(), context->getMode(), nullptr, true);
    initializerContext.setHub(m_hub);
    try {
        AssignmentStatement::onResolve(&initializerContext, m_sourceFunction);
    } catch (const AstException& exception) {
        initializerContext.reportError(exception.what(), getSourceName(),
            exception.getLine(), exception.getColumn());
    }
    context->reportDiagnostics(initializerContext.getDiagnostics());
}

void HubInitializerStatement::onCompile(Compiler* compiler, ScriptCallableBase* function) {
    if (m_initialValue == nullptr) {
        AssignmentStatement::onCompile(compiler, function);
    } else {
        compiler->getRuntime()->setContextVariableWritten(
            compiler->getRuntime()->getContextVariableId(getIdentifier()), true
        );
    }
}

void AssignmentStatement::onResolve(CompilerContext* context, ScriptCallableBase* function) {
    if (context->getCompiler()->isFieldInitializer(function) &&
        (m_identifier == "this" || m_identifier.starts_with("this."))) {
        reportError(context, function, "Field initializers cannot use or capture 'this'");
        return;
    }
    if (!m_expression->resolve(context, function)) {
        return;
    }

    auto* compiler = context->getCompiler();
    const auto expressionType = m_expression->getType();

    if (compiler->getReceiverType(function) != nullptr && m_identifier.starts_with("this.")) {
        (void)compiler->resolveClassField(
            context, function, m_identifier.substr(5), expressionType,
            context->getRuntime()->getConstructor(function->getId()) != nullptr, getLine(), getColumn()
        );
        return;
    }

    IdentifierDefinition identifier;
    compiler->getIdentifierDefinition(m_identifier, function, identifier);

    if (expressionType != nullptr) {
        NomadString conflictingIdentifier;
        if (compiler->getNativeMemberConflict(m_identifier, expressionType, function, conflictingIdentifier)) {
            reportError(
                context,
                function,
                "Assigning type '" + getTypeDisplayName(expressionType) + "' to variable '" + m_identifier +
                    "' introduces native member '" + conflictingIdentifier +
                    "', which conflicts with an existing identifier"
            );
            return;
        }
    }

    if (context->getMode() == CompilerMode::Interpreter) {
        auto* interpreterRuntime = context->getRuntime();

        switch (identifier.identifierType) {
        case IdentifierType::Parameter:
            if (identifier.valueType != expressionType) {
                reportError(context, function, "Cannot change type of parameter '" + m_identifier + "'");
                return;
            }
            break;
        case IdentifierType::FunctionVariable: {
            const auto* interpreterContext = context->getInterpreterContext();
            if (interpreterContext != nullptr && interpreterContext->isFrameVariable(identifier.variableId) &&
                function->getVariableType(identifier.variableId) != expressionType) {
                reportError(context, function, "Cannot change type of frame variable '" + m_identifier + "'");
                return;
            }
            function->setVariableType(identifier.variableId, expressionType);
            break;
        }
        case IdentifierType::DynamicVariable: {
            if (!interpreterRuntime->canSetDynamicVariable(identifier.dynamicVariableId)) {
                reportError(context, function, "Dynamic variable '" + m_identifier + "' is read-only");
                return;
            }

            const auto variableType = interpreterRuntime->getDynamicVariableType(identifier.dynamicVariableId);
            if (expressionType != nullptr && variableType != nullptr && variableType != expressionType) {
                reportError(
                    context,
                    function,
                    "Cannot assign value of type '" + getTypeDisplayName(expressionType) +
                    "' to dynamic variable '" + m_identifier + "' of type '" + getTypeDisplayName(variableType) + "'"
                );
                return;
            }
            break;
        }

        case IdentifierType::ContextVariable: {
            const auto variableType = interpreterRuntime->getContextVariableType(identifier.contextVariableId);

            if (variableType != nullptr && expressionType != nullptr && variableType != expressionType) {
                reportError(
                    context,
                    function,
                    "Cannot assign value of type '" + getTypeDisplayName(expressionType) +
                    "' to variable '" + m_identifier + "' of type '" + getTypeDisplayName(variableType) + "'"
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
            // New unqualified names belong to the interpreter context, not the script frame.
            const auto isNewConsoleVariable = identifier.identifierType == IdentifierType::Unknown &&
                m_identifier.find('.') == NomadString::npos &&
                interpreterRuntime->getVariableContextIdByPrefix(m_identifier).isInvalid();

            if (identifier.identifierType != IdentifierType::FunctionVariable && !isNewConsoleVariable) {
                reportError(context, function, "Cannot assign value to '" + m_identifier + "' in the interpreter");
                return;
            }
            if (isNewConsoleVariable && function != nullptr) {
                identifier.identifierType = IdentifierType::FunctionVariable;
                identifier.variableId = function->registerVariable(m_identifier, expressionType);
                identifier.valueType = expressionType;
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
    if (expressionType->isVoid()) {
        reportError(context, function, "Cannot assign a void expression to '" + m_identifier + "'");
        return;
    }

    auto* runtime = context->getRuntime();

    switch (identifier.identifierType) {
    case IdentifierType::FunctionVariable: {
        // Make sure type is unknown or the same as the expression.
        const auto variableType = function->getVariableType(identifier.variableId);

        if (variableType != nullptr && variableType != expressionType) {
            reportError(
                context,
                function,
                "Cannot assign value of type '" + getTypeDisplayName(expressionType) + "' to variable '" +
                    m_identifier + "' of type '" + getTypeDisplayName(variableType) + "'"
            );
            return;
        }

        function->setVariableType(identifier.variableId, expressionType);
        break;
    }
    case IdentifierType::DynamicVariable: {
        const auto variableType = runtime->getDynamicVariableType(identifier.dynamicVariableId);

        if (variableType != expressionType) {
            reportError(
                context,
                function,
                "Cannot assign value of type '" + getTypeDisplayName(expressionType) + "' to dynamic variable '" +
                    m_identifier + "' of type '" + getTypeDisplayName(variableType) + "'"
            );
        }
        break;
    }
    case IdentifierType::ContextVariable: {
        // Make sure type is unknown or the same as the expression.
        const auto variableType = runtime->getContextVariableType(identifier.contextVariableId);

        if (variableType != nullptr && variableType != expressionType) {
            reportError(
                context,
                function,
                "Cannot assign value of type '" + getTypeDisplayName(expressionType) + "' to variable '" +
                    m_identifier + "' of type '" + getTypeDisplayName(variableType) + "'"
            );
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
        if (identifier.valueType != expressionType) {
            reportError(context, function, "Cannot change type of parameter '" + m_identifier + "'");
        }
        break;
    default:
        reportError(context, function, "Cannot assign value to unknown identifier '" + m_identifier + "'");
        break;
    }
}

void AssignmentStatement::onCompile(Compiler* compiler, ScriptCallableBase* function) {
    m_expression->compile(compiler, function, ExpressionTarget::Result);

    if (const auto* receiverType = compiler->getReceiverType(function);
        receiverType != nullptr && m_identifier.starts_with("this.")) {
        const auto* property = receiverType->asObjectType()->getProperty(m_identifier.substr(5));
        if (property == nullptr) {
            compiler->reportInternalError("Unresolved class field: " + m_identifier);
        }
        compiler->addLoadReceiver(function, ExpressionTarget::Intermediate);
        compiler->addOpCode(op_native_property_set);
        compiler->addId(property->getId().id);
        compiler->addFreeValue(m_expression->getType()->getTypeOpCodes()->freeResult);
        compiler->addFreeValue(receiverType->getTypeOpCodes()->freeIntermediate);
        return;
    }

    IdentifierDefinition identifier;

    compiler->getIdentifierDefinition(m_identifier, function, identifier);

    // Resolve validated the target and the expression type.
    const auto* typeOpCodes = m_expression->getType()->getTypeOpCodes();

    switch (identifier.identifierType) {
    case IdentifierType::Parameter:
        compiler->addStoreParameter(identifier.parameterId, m_expression->getType());
        break;
    case IdentifierType::FunctionVariable:
        // `r` is moved into the variable, so its previous value is released first.
        compiler->addFreeValue(typeOpCodes->freeFunctionVariable, toNomadIndex(identifier.variableId.id));
        compiler->addOpCode(typeOpCodes->setFunctionVariable);
        compiler->addId(identifier.variableId.id);
        break;

    case IdentifierType::DynamicVariable:
        // The setter copies what it keeps.
        compiler->addOpCode(typeOpCodes->setDynamicVariable);
        compiler->addId(identifier.dynamicVariableId.id);
        compiler->addFreeValue(typeOpCodes->freeResult);
        break;

    case IdentifierType::ContextVariable: {
        // The variable context copies what it keeps.
        compiler->addOpCode(typeOpCodes->setContextVariable);
        compiler->addId(identifier.contextVariableId.id);
        compiler->addFreeValue(typeOpCodes->freeResult);

        compiler->getRuntime()->setContextVariableWritten(identifier.contextVariableId, true);
        break;
    }
    default:
        compiler->reportInternalError("Cannot assign value to '" + m_identifier + "'");
    }
}

void AssignmentStatement::onEvaluate(InterpreterContext& context) const {
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
    case IdentifierType::Parameter:
        context.setParameter(m_identifierDefinition.parameterId, assignedValue.getValue());
        break;
    case IdentifierType::DynamicVariable: {
        auto* runtime = context.getRuntime();
        runtime->setDynamicVariable(
            context.getVirtualMachine(),
            m_identifierDefinition.dynamicVariableId,
            assignedValue.getValue()
        );
        break;
    }

    case IdentifierType::ContextVariable: {
        auto* runtime = context.getRuntime();

        // The variable context copies what it keeps.
        runtime->setContextVariableValue(m_identifierDefinition.contextVariableId, assignedValue.getValue());
        runtime->setContextVariableWritten(m_identifierDefinition.contextVariableId, true);
        break;
    }

    default:
        context.setVariable(m_identifier, type, assignedValue.getValue());
        break;
    }

    context.setResult(assignedValue);
}

///////////////////////////////////////////////////////////////////////////////
// MemberAssignmentStatement
MemberAssignmentStatement::MemberAssignmentStatement(const NomadIndex line, const NomadIndex column,
                                                     std::unique_ptr<Expression> target,
                                                     std::unique_ptr<Expression> expression)
    : Statement(line, column), m_target(std::move(target)), m_expression(std::move(expression)) {
}

void MemberAssignmentStatement::onResolve(CompilerContext* context, ScriptCallableBase* function) {
    auto* target = m_target.get();
    if (!m_expression->resolve(context, function) ||
        !target->resolveAssignmentTarget(context, function, m_expression->getType())) {
        return;
    }

    const auto* targetType = m_target->getType();
    const auto* expressionType = m_expression->getType();

    if (targetType == nullptr || expressionType == nullptr) {
        reportError(context, function, "Cannot determine member assignment type");
        return;
    }

    if (targetType != expressionType) {
        reportError(context, function,
                    "Cannot assign value of type '" + getTypeDisplayName(expressionType) +
                        "' to member of type '" + getTypeDisplayName(targetType) + "'");
    }
}

void MemberAssignmentStatement::onCompile(Compiler* compiler, ScriptCallableBase* function) {
    m_target->compileAssignment(compiler, function, m_expression.get());
}

void MemberAssignmentStatement::onEvaluate(InterpreterContext& context) const {
    m_target->evaluateAssignment(context, m_expression.get());
}

///////////////////////////////////////////////////////////////////////////////
// ExpressionStatement
ExpressionStatement::ExpressionStatement(const NomadIndex line, const NomadIndex column,
                                         std::unique_ptr<Expression> expression)
    : Statement(line, column), m_expression(std::move(expression)) {
}

void ExpressionStatement::onResolve(CompilerContext* context, ScriptCallableBase* function) {
    (void)m_expression->resolve(context, function);
}

void ExpressionStatement::onCompile(Compiler* compiler, ScriptCallableBase* function) {
    m_expression->compile(compiler, function, ExpressionTarget::Result);
    if (!m_expression->getType()->isVoid()) {
        compiler->addFreeValue(m_expression->getType()->getTypeOpCodes()->freeResult);
    }
}

void ExpressionStatement::onEvaluate(InterpreterContext& context) const {
    m_expression->evaluate(context);
}

///////////////////////////////////////////////////////////////////////////////
// StatementList
NomadBoolean StatementList::resolve(CompilerContext* context, ScriptCallableBase* function) const {
    NomadBoolean resolved = true;

    for (auto& statement: m_statements) {
        if (!statement->resolve(context, function)) {
            resolved = false;
        }
    }

    return resolved;
}

void StatementList::compile(Compiler* compiler, ScriptCallableBase* function) const {
    for (auto& statement: m_statements) {
        statement->compile(compiler, function);
    }
}

void StatementList::evaluate(InterpreterContext& context) const {
    if (context.hasError()) {
        context.clearResult();
        return;
    }

    context.setVoidResult();
    for (const auto& statement: m_statements) {
        if (context.hasError() || context.getLoopControl() != LoopControl::None) {
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

void StatementList::checkReachability(CompilerContext* context, const ScriptCallableBase* function) const {
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

NomadBoolean FunctionNode::resolve(CompilerContext* context, ScriptCallableBase* function) const {
    return m_statements.resolve(context, function);
}

void FunctionNode::compile(Compiler* compiler, ScriptCallableBase* function) const {
    m_statements.compile(compiler, function);
}

NomadBoolean FunctionNode::alwaysReturns() const {
    return m_statements.alwaysReturns();
}

void FunctionNode::checkReachability(CompilerContext* context, const ScriptCallableBase* function) const {
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

void FunctionDeclaration::setFunctionId(CallableId id) {
    m_functionId = id;
}

CallableId FunctionDeclaration::getFunctionId() const {
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

void FunctionDeclaration::onResolve(CompilerContext* context, ScriptCallableBase* function) {
    // Default: resolve body if present
    if (m_body) {
        (void)m_body->resolve(context, function);
    }
}

void FunctionDeclaration::onCompile(Compiler* /*compiler*/, ScriptCallableBase* /*function*/) {
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

void EventDeclaration::onResolve(CompilerContext* /*context*/, ScriptCallableBase* /*function*/) {
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

void ConstDeclaration::onResolve(CompilerContext* context, ScriptCallableBase* function) {
    if (m_initializer && m_initializer->resolve(context, function)) {
        m_type = m_initializer->getType();
    }
}

///////////////////////////////////////////////////////////////////////////////
// EnumDecl
EnumDeclaration::EnumDeclaration(
    const NomadIndex line,
    const NomadIndex column,
    NomadString name,
    std::vector<NomadString> values
):
    Statement(line, column),
    m_name(std::move(name)),
    m_values(std::move(values))
{
}

const NomadString& EnumDeclaration::getName() const {
    return m_name;
}

const std::vector<NomadString>& EnumDeclaration::getValues() const {
    return m_values;
}

NomadBoolean EnumDeclaration::isDeclaration() const {
    return true;
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

void EventHandlerDeclaration::onResolve(CompilerContext* context, ScriptCallableBase* function) {
    if (m_handler) {
        (void)m_handler->resolve(context, function);
    }
}

void EventHandlerDeclaration::onCompile(Compiler* /*compiler*/, ScriptCallableBase* /*function*/) {
    // Handler registration and compilation are handled by the compiler's event system.
}

} // namespace nomad
