// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compiler.hpp>

#include <nomad/compiler/Argument.hpp>
#include <nomad/compiler/CallNode.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/script/VirtualMachine.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/Function.hpp>

namespace nomad {

///////////////////////////////////////////////////////////////////////////////
/// Argument
///////////////////////////////////////////////////////////////////////////////
Argument::Argument(const NomadIndex line, const NomadIndex column, const Type* type):
    AstNode(line, column),
    m_type(type)
{
}

const Type* Argument::getType() const {
    return m_type;
}

void Argument::setType(const Type* type) {
    m_type = type;
}

const Type* Argument::resolveOverloadType(CompilerContext* /*context*/, Function* /*function*/) {
    // Most arguments are built by the parser with a fixed type and cannot tell overloads apart.
    return nullptr;
}

const Type* Argument::getPushedType() const {
    return m_type;
}

TypedValue Argument::evaluate(Interpreter& context) const {
    return onEvaluate(context);
}

TypedValue Argument::onEvaluate(Interpreter& /*context*/) const {
    throw AstException("Argument kind is not supported by the interpreter", getLine(), getColumn());
}

NomadBoolean Argument::resolve(CompilerContext* context, Function* function) {
    const auto errorCount = context->getErrorCount();

    onResolve(context, function);

    return context->getErrorCount() == errorCount;
}

void Argument::onResolve(CompilerContext* /*context*/, Function* /*function*/) {
    // Most arguments have nothing to resolve.
}

void Argument::generateCode(Compiler* compiler, Function* function) {
    onCompile(compiler, function);
}

/////////////////////////////////////////////////////////////////////////////
/// ArgumentList
/////////////////////////////////////////////////////////////////////////////
ArgumentList::ArgumentList() = default;

void ArgumentList::addExpressionArgument(
    const NomadIndex line,
    const NomadIndex column,
    const Type* type,
    std::unique_ptr<Expression> expression)
{
    auto argument = createExpressionArgument(line, column, type, std::move(expression));

    m_arguments.push_back(std::move(argument));
}

void ArgumentList::addPredicateArgument(NomadIndex line, NomadIndex column, NomadId predicateFunctionId) {
    m_arguments.emplace_back(createPredicateArgument(line, column, predicateFunctionId));
}

void ArgumentList::addCallbackArgument(NomadIndex line, NomadIndex column, const Type* type, NomadId functionId) {
    m_arguments.emplace_back(createCallbackArgument(line, column, type, functionId));
}

void ArgumentList::addEventCallbackArgument(
    NomadIndex line,
    NomadIndex column,
    const Type* type,
    NomadId eventId,
    NomadId functionId
) {
    m_arguments.emplace_back(createEventCallbackArgument(line, column, type, eventId, functionId));
}

void ArgumentList::addEventDispatchArgument(
    NomadIndex line,
    NomadIndex column,
    const Type* type,
    const EventDefinition& eventDefinition
) {
    m_arguments.emplace_back(createEventDispatchArgument(line, column, type, eventDefinition));
}

void ArgumentList::addFileNameArgument(
    NomadIndex line,
    NomadIndex column,
    const Type* type,
    NomadString fileName
) {
    m_arguments.emplace_back(createFileNameArgument(line, column, type, std::move(fileName)));
}

void ArgumentList::addLineNumberArgument(
    NomadIndex line,
    NomadIndex column,
    const Type* type
) {
    m_arguments.emplace_back(createLineNumberArgument(line,  column, type));
}

void ArgumentList::add(std::unique_ptr<Argument>&& argument) {
    m_arguments.push_back(std::move(argument));
}

void ArgumentList::addAll(std::vector<std::unique_ptr<Argument>>&& arguments) {
    for (auto& argument : arguments) {
        m_arguments.push_back(std::move(argument));
    }
}

NomadBoolean ArgumentList::resolve(CompilerContext* context, Function* function) const {
    NomadBoolean resolved = true;

    for (const auto& argument : m_arguments) {
        if (!argument->resolve(context, function)) {
            resolved = false;
        }
    }

    return resolved;
}

void ArgumentList::compile(Compiler* compiler, Function* function) const {
    // Push arguments right-to-left
    for (auto argument = m_arguments.rbegin(); argument != m_arguments.rend(); ++argument) {
        (*argument)->generateCode(compiler, function);
    }
}

void ArgumentList::evaluate(
    Interpreter& context,
    std::vector<TypedValue>& evaluatedArguments,
    std::vector<RuntimeValue>& values
) const {
    evaluatedArguments.reserve(m_arguments.size());
    values.reserve(m_arguments.size());

    for (const auto& argument : m_arguments) {
        auto value = argument->evaluate(context);
        if (context.hasError()) {
            return;
        }

        values.push_back(value.getValue());
        evaluatedArguments.push_back(std::move(value));
    }
}

NomadIndex ArgumentList::getArgumentCount() const {
    NomadIndex total = 0;

    for (const auto& argument : m_arguments) {
        total += argument->getStackValueCount();
    }

    return total;
}

std::vector<std::unique_ptr<Argument>>& ArgumentList::getArguments() {
    return m_arguments;
}

const std::vector<std::unique_ptr<Argument>>& ArgumentList::getArguments() const {
    return m_arguments;
}

///////////////////////////////////////////////////////////////////////////////
// CallStatementNode
CallStatementNode::CallStatementNode(const NomadIndex line, const NomadIndex column):
    Statement(line, column)
{
}

void CallStatementNode::addArgument(std::unique_ptr<Argument> argument) {
    m_arguments.add(std::move(argument));
}

ArgumentList* CallStatementNode::getArguments() {
    return &m_arguments;
}

const ArgumentList* CallStatementNode::getArguments() const {
    return &m_arguments;
}

void CallStatementNode::onResolve(CompilerContext* context, Function* function) {
    (void)m_arguments.resolve(context, function);
}

CallableId selectCallableOverload(
    CompilerContext* context,
    Function* function,
    const NomadString& name,
    ArgumentList* arguments,
    NomadString& error
) {
    const auto* runtime = context->getRuntime();
    auto& argumentNodes = arguments->getArguments();

    std::vector<const Type*> argumentTypes;
    argumentTypes.reserve(argumentNodes.size());

    for (const auto& argument : argumentNodes) {
        argumentTypes.push_back(argument->resolveOverloadType(context, function));
    }

    const auto selected = runtime->resolveCallableOverload(name, argumentTypes, error);

    if (!selected.isValid()) {
        return NOMAD_INVALID_CALLABLE_ID;
    }

    // Only overloadable shapes can reach this point, so each parameter produced exactly one argument.
    for (NomadIndex index = 0; index < argumentNodes.size(); ++index) {
        if (argumentTypes[index] != nullptr) {
            argumentNodes[index]->setType(runtime->getCallableParameterType(selected, index));
        }
    }

    return selected;
}

///////////////////////////////////////////////////////////////////////////////
// NativeFunctionStatementNode
NativeFunctionStatementNode::NativeFunctionStatementNode(const NomadIndex line, const NomadIndex column, const NativeFunctionDefinition& definition):
    CallStatementNode(line, column),
    m_definition(definition)
{
}

void NativeFunctionStatementNode::onResolve(CompilerContext* context, Function* function) {
    auto* runtime = context->getRuntime();

    // The parser picked the first overload to shape the argument list; now that the arguments exist, pick the one
    // whose parameter types they actually match.
    if (runtime->getCallableOverloadCount(m_definition.name) > 1) {
        NomadString error;
        const auto selected = selectCallableOverload(context, function, m_definition.name, getArguments(), error);

        if (!selected.isValid()) {
            reportError(context, function, error);

            return;
        }

        runtime->getNativeFunctionDefinition(selected.id, m_definition);
    }

    CallStatementNode::onResolve(context, function);
}

void NativeFunctionStatementNode::onCompile(Compiler* compiler, Function* function) {
    compiler->addNativeFunctionCall(m_definition.id, function, getArguments());

    // The statement discards the nativeFunction's return value.
    compiler->addFreeValue(m_definition.returnType->getTypeOpCodes()->freeResult);
}

void NativeFunctionStatementNode::onEvaluate(Interpreter& context) const {
    std::vector<TypedValue> evaluatedArguments;
    std::vector<RuntimeValue> values;
    getArguments()->evaluate(context, evaluatedArguments, values);
    if (context.hasError()) {
        return;
    }

    context.getRuntime()->executeNativeFunction(m_definition.id, values, context);
    if (!context.hasError()) {
        context.setVoidResult();
    }
}

///////////////////////////////////////////////////////////////////////////////
// FunctionCallStatementNode
FunctionCallStatementNode::FunctionCallStatementNode(const NomadIndex line, const NomadIndex column, NomadString name):
    CallStatementNode(line, column),
    m_name(std::move(name)) {
}

void FunctionCallStatementNode::onResolve(CompilerContext* context, Function* function) {
    const auto* runtime = context->getRuntime();
    m_functionId = runtime->getFunctionId(m_name);

    if (m_functionId == NOMAD_INVALID_ID) {
        reportError(context, function, "Unknown function '" + m_name + "'");
        return;
    }

    if (context->getMode() == CompilerMode::Interpreter &&
        runtime->getFunction(m_functionId)->getFunctionStart() == NOMAD_INVALID_INDEX) {
        reportError(context, function, "Function '" + m_name + "' has not been compiled");
        return;
    }

    CallStatementNode::onResolve(context, function);
}

void FunctionCallStatementNode::onCompile(Compiler* compiler, Function* function) {
    const auto targetFunctionId = compiler->getRuntime()->getFunctionId(m_name);

    compiler->addFunctionCall(targetFunctionId, function, getArguments());

    // The statement discards the function's return value.
    if (const auto* targetFunction = compiler->getRuntime()->getFunction(targetFunctionId);
        targetFunction != nullptr && targetFunction->getReturnType() != nullptr) {
        compiler->addFreeValue(targetFunction->getReturnType()->getTypeOpCodes()->freeResult);
    }
}

void FunctionCallStatementNode::onEvaluate(Interpreter& context) const {
    if (m_functionId == NOMAD_INVALID_ID) {
        raiseException("Function call to '" + m_name + "' has not been resolved");
    }

    std::vector<TypedValue> evaluatedArguments;
    std::vector<RuntimeValue> values;
    getArguments()->evaluate(context, evaluatedArguments, values);
    if (context.hasError()) {
        return;
    }

    context.getRuntime()->executeFunction(m_functionId, values);
    context.setVoidResult();
}

} // namespace nomad
