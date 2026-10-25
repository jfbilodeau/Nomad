// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compiler.hpp>

#include <nomad/compiler/Argument.hpp>
#include <nomad/compiler/CallNode.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/VirtualMachine.hpp>

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

const Type* Argument::resolveOverloadType(CompilerContext* /*context*/, ScriptCallableBase* /*function*/) {
    // Most arguments are built by the parser with a fixed type and cannot tell overloads apart.
    return nullptr;
}

TypedValue Argument::evaluate(InterpreterContext& context) const {
    return onEvaluate(context);
}

TypedValue Argument::onEvaluate(InterpreterContext& /*context*/) const {
    throw AstException("Argument kind is not supported by the interpreter", getLine(), getColumn());
}

NomadBoolean Argument::resolve(CompilerContext* context, ScriptCallableBase* function) {
    const auto errorCount = context->getErrorCount();

    onResolve(context, function);

    return context->getErrorCount() == errorCount;
}

void Argument::onResolve(CompilerContext* /*context*/, ScriptCallableBase* /*function*/) {
    // Most arguments have nothing to resolve.
}

void Argument::generateCode(Compiler* compiler, ScriptCallableBase* function) {
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

void ArgumentList::addPredicateArgument(NomadIndex line, NomadIndex column, CallableId predicateFunctionId) {
    m_arguments.emplace_back(createPredicateArgument(line, column, predicateFunctionId));
}

void ArgumentList::addCallbackArgument(NomadIndex line, NomadIndex column, const Type* type, CallableId functionId) {
    m_arguments.emplace_back(createCallbackArgument(line, column, type, functionId));
}

void ArgumentList::addEventCallbackArgument(
    NomadIndex line,
    NomadIndex column,
    const Type* type,
    EventId eventId,
    CallableId functionId
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

NomadBoolean ArgumentList::resolve(CompilerContext* context, ScriptCallableBase* function) const {
    NomadBoolean resolved = true;

    for (const auto& argument : m_arguments) {
        if (!argument->resolve(context, function)) {
            resolved = false;
        }
    }

    return resolved;
}

void ArgumentList::compile(Compiler* compiler, ScriptCallableBase* function) const {
    if (m_receiverType != nullptr) {
        compiler->addPushIntermediate(m_receiverType);
    }

    // Push arguments right-to-left
    for (auto argument = m_arguments.rbegin(); argument != m_arguments.rend(); ++argument) {
        (*argument)->generateCode(compiler, function);
    }
}

void ArgumentList::evaluate(
    InterpreterContext& context,
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

void ArgumentList::setReceiverType(const Type* receiverType) {
    m_receiverType = receiverType;
}

const Type* ArgumentList::getReceiverType() const {
    return m_receiverType;
}

NomadIndex ArgumentList::getStackCount() const {
    return getArgumentCount() + (m_receiverType != nullptr ? 1 : 0);
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

void CallStatementNode::onResolve(CompilerContext* context, ScriptCallableBase* function) {
    (void)m_arguments.resolve(context, function);
}

const Function* resolveFunctionCallArguments(
    CompilerContext* context,
    ScriptCallableBase* function,
    const NomadString& name,
    ArgumentList* arguments,
    NomadString& error
) {
    const auto* runtime = context->getRuntime();
    if (runtime->getCallableOverloadCount(name) <= 1) {
        const auto* selected = runtime->getFunction(runtime->getCallableId(name));
        if (selected == nullptr) {
            error = "Unknown function '" + name + "'";
        }
        return selected;
    }
    auto& argumentNodes = arguments->getArguments();

    std::vector<const Type*> argumentTypes;
    argumentTypes.reserve(argumentNodes.size());

    for (const auto& argument : argumentNodes) {
        argumentTypes.push_back(argument->resolveOverloadType(context, function));
    }

    const auto selected = runtime->resolveFunction(name, argumentTypes);

    if (!selected.isResolved()) {
        error = runtime->describeResolveFailure(name, selected);
        return nullptr;
    }
    const auto* selectedFunction = runtime->getFunction(selected.id);

    // Only overloadable shapes can reach this point, so each parameter produced exactly one argument.
    for (NomadIndex index = 0; index < argumentNodes.size(); ++index) {
        if (argumentTypes[index] != nullptr) {
            argumentNodes[index]->setType(selectedFunction->getParameters()[index].type);
        }
    }

    return selectedFunction;
}

///////////////////////////////////////////////////////////////////////////////
// FunctionCallStatementNode
FunctionCallStatementNode::FunctionCallStatementNode(const NomadIndex line, const NomadIndex column, NomadString name):
    CallStatementNode(line, column),
    m_name(std::move(name)) {
}

void FunctionCallStatementNode::onResolve(CompilerContext* context, ScriptCallableBase* function) {
    NomadString error;
    m_function = resolveFunctionCallArguments(context, function, m_name, getArguments(), error);
    if (m_function == nullptr) {
        reportError(context, function, error);
        return;
    }

    const auto* script = m_function->asScriptFunction();
    if (context->getMode() != CompilerMode::Script && script != nullptr &&
        script->getFunctionStart() == NOMAD_INVALID_INDEX) {
        reportError(context, function, "Function '" + m_name + "' has not been compiled");
        return;
    }

    CallStatementNode::onResolve(context, function);
}

void FunctionCallStatementNode::onCompile(Compiler* compiler, ScriptCallableBase* function) {
    if (m_function == nullptr) {
        raiseException("Function call to '" + m_name + "' has not been resolved");
    }
    if (const auto* native = m_function->asNativeFunction()) {
        compiler->addNativeFunctionCall(native->getId(), function, getArguments());
    } else if (const auto* script = m_function->asScriptFunction()) {
        compiler->addFunctionCall(script->getId(), function, getArguments());
    } else {
        raiseException("Unsupported implementation of function '" + m_name + "'");
    }

    // The statement discards the function's return value.
    if (const auto* returnType = m_function->getReturnType()) {
        compiler->addFreeValue(returnType->getTypeOpCodes()->freeResult);
    }
}

void FunctionCallStatementNode::onEvaluate(InterpreterContext& context) const {
    if (m_function == nullptr) {
        raiseException("Function call to '" + m_name + "' has not been resolved");
    }

    std::vector<TypedValue> evaluatedArguments;
    std::vector<RuntimeValue> values;
    getArguments()->evaluate(context, evaluatedArguments, values);
    if (context.hasError()) {
        return;
    }

    if (const auto* native = m_function->asNativeFunction()) {
        context.evaluateNativeFunctionCall(native->getId(), values);
    } else if (const auto* script = m_function->asScriptFunction()) {
        context.evaluateFunctionCall(script->getId(), values);
    } else {
        raiseException("Unsupported implementation of function '" + m_name + "'");
    }
    if (!context.hasError()) {
        context.setVoidResult();
    }
}

} // namespace nomad
