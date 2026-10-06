// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Argument.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/script/VirtualMachine.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>

namespace nomad {


//////////////////////////////////////////////////////////////////////////////
/// Declare specialized argument types.
//////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// ExpressionArgument
///////////////////////////////////////////////////////////////////////////////
class ExpressionArgument : public Argument {
public:
    ExpressionArgument(
        NomadIndex line,
        NomadIndex column,
        const Type* type,
        std::unique_ptr<Expression> expression,
        ParameterAccess parameterAccess
    );

    [[nodiscard]] const Type* getPushedType() const override;
    [[nodiscard]] const Type* resolveOverloadType(CompilerContext* context, Function* function) override;

protected:
    void onResolve(CompilerContext* context, Function* function) override;
    void onCompile(Compiler* compiler, Function* function) override;
    [[nodiscard]] TypedValue onEvaluate(Interpreter& context) const override;

private:
    // Resolves `m_expression`, so overload resolution and the regular resolve pass can both ask for its type.
    bool resolveExpression(CompilerContext* context, Function* function);

    std::unique_ptr<Expression> m_expression;
    ParameterAccess m_parameterAccess;
    // Pass a borrowed pointer rather than an owned copy (`$stringref` or read-only parameter with a borrowable
    // expression).
    bool m_borrow = false;
    const Type* m_pushedType = nullptr;
};

///////////////////////////////////////////////////////////////////////////////
// PredicateArgument
///////////////////////////////////////////////////////////////////////////////
class PredicateArgument : public Argument {
public:
    PredicateArgument(NomadIndex line, NomadIndex column, NomadId predicateFunctionId);

protected:
    void onCompile(Compiler* compiler, Function* function) override;

private:
    NomadId m_predicateFunctionId;
};

///////////////////////////////////////////////////////////////////////////////
// CallbackArgument
///////////////////////////////////////////////////////////////////////////////
class CallbackArgument : public Argument {
public:
    CallbackArgument(NomadIndex line, NomadIndex column, const Type* type, NomadId functionId);

    [[nodiscard]]
    NomadId getFunctionId() const;

protected:
    void onResolve(CompilerContext* context, Function* function) override;
    void onCompile(Compiler* compiler, Function* function) override;
    [[nodiscard]] TypedValue onEvaluate(Interpreter& context) const override;

private:
    NomadId m_functionId;
};

///////////////////////////////////////////////////////////////////////////////
// EventCallbackArgument
///////////////////////////////////////////////////////////////////////////////
class EventCallbackArgument : public Argument {
public:
    EventCallbackArgument(NomadIndex line, NomadIndex column, const Type* type, NomadId eventId, NomadId functionId);

    [[nodiscard]]
    NomadIndex getStackValueCount() const override { return 2; }

protected:
    void onCompile(Compiler* compiler, Function* function) override;

private:
    NomadId m_eventId;
    NomadId m_functionId;
};

///////////////////////////////////////////////////////////////////////////////
// EventDispatchArgument
///////////////////////////////////////////////////////////////////////////////
class EventDispatchArgument : public Argument {
public:
    EventDispatchArgument(NomadIndex line, NomadIndex column, const Type* type, const EventDefinition& eventDefinition);

protected:
    void onCompile(Compiler* compiler, Function* function) override;

private:
    const EventDefinition m_eventDefinition;
};

///////////////////////////////////////////////////////////////////////////////
// FileNameArgument
///////////////////////////////////////////////////////////////////////////////
class FileNameArgument: public Argument {
public:
    FileNameArgument(NomadIndex line, NomadIndex column, const Type* type, NomadString fileName);

protected:
    void onCompile(Compiler* compiler, Function* function) override;
    [[nodiscard]] TypedValue onEvaluate(Interpreter& context) const override;

private:
    NomadString m_fileName;
};

///////////////////////////////////////////////////////////////////////////////
// FunctionNameArgument
///////////////////////////////////////////////////////////////////////////////
class FunctionNameArgument: public Argument {
public:
    FunctionNameArgument(NomadIndex line, NomadIndex column, const Type* type, NomadString functionName);

protected:
    void onCompile(Compiler* compiler, Function* function) override;
    [[nodiscard]] TypedValue onEvaluate(Interpreter& context) const override;

private:
    NomadString m_functionName;
};

///////////////////////////////////////////////////////////////////////////////
// LineNumberArgument
///////////////////////////////////////////////////////////////////////////////
class LineNumberArgument: public Argument {
public:
    LineNumberArgument(NomadIndex line, NomadIndex column, const Type* type);

protected:
    void onCompile(Compiler* compiler, Function* function) override;
    [[nodiscard]] TypedValue onEvaluate(Interpreter& context) const override;

private:

};

///////////////////////////////////////////////////////////////////////////////
/// Argument implementations.
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// ExpressionArgument
///////////////////////////////////////////////////////////////////////////////
ExpressionArgument::ExpressionArgument(
    const NomadIndex line,
    const NomadIndex column,
    const Type* type,
    std::unique_ptr<Expression> expression,
    const ParameterAccess parameterAccess
):
    Argument(line, column, type),
    m_expression(std::move(expression)),
    m_parameterAccess(parameterAccess),
    m_pushedType(type)
{
}

bool ExpressionArgument::resolveExpression(CompilerContext* context, Function* function) {
    // Deliberately not cached: type inference resolves a function more than once, and the expression's type may only
    // settle on a later pass.
    return m_expression->resolve(context, function);
}

const Type* ExpressionArgument::resolveOverloadType(CompilerContext* context, Function* function) {
    if (!resolveExpression(context, function)) {
        return nullptr;
    }

    return m_expression->getType();
}

void ExpressionArgument::onResolve(CompilerContext* context, Function* function) {
    if (!resolveExpression(context, function)) {
        return;
    }

    const auto* argumentType = getType();
    const auto* expressionType = m_expression->getType();

    if (argumentType == nullptr || expressionType == nullptr) {
        reportError(context, function, "Cannot determine type of argument");
        return;
    }

    if (!argumentType->acceptsArgument(expressionType)) {
        reportError(
            context,
            function,
            "Argument type mismatch. Expected " + argumentType->getTypeName() + " but got " + expressionType->getTypeName()
        );
        return;
    }

    const auto lendable = argumentType->isReference() || m_parameterAccess == ParameterAccess::ReadOnly;
    m_borrow = lendable && m_expression->canBorrow();

    if (m_borrow) {
        // Only strings can be borrowed (`Expression::canBorrow`). A lent string is pushed as a reference: not freed.
        m_pushedType = argumentType->isReference() ? argumentType : context->getRuntime()->getStringRefType();
    } else {
        // A reference parameter given an owned temporary still has to free that temporary.
        m_pushedType = argumentType->isReference() ? expressionType : argumentType;
    }
}

const Type* ExpressionArgument::getPushedType() const {
    return m_pushedType;
}

void ExpressionArgument::onCompile(Compiler* compiler, Function* function) {
    if (m_borrow) {
        m_expression->compileBorrowed(compiler, function, ExpressionTarget::Stack);
    } else {
        m_expression->compile(compiler, function, ExpressionTarget::Stack);
    }
}

TypedValue ExpressionArgument::onEvaluate(Interpreter& context) const {
    m_expression->evaluate(context);
    const auto* value = context.getResult();

    if (value == nullptr) {
        if (context.hasError()) {
            // The caller stops on the reported error; the returned value is never used.
            return TypedValue(context.getRuntime()->getVoidType(), RuntimeValue{});
        }

        throw AstException("Argument expression did not produce a value", getLine(), getColumn());
    }

    return *value;
}

///////////////////////////////////////////////////////////////////////////////
// PredicateArgument
///////////////////////////////////////////////////////////////////////////////
PredicateArgument::PredicateArgument(
    const NomadIndex line,
    const NomadIndex column,
    const NomadId predicateFunctionId
):
    Argument(line, column, nullptr),
    m_predicateFunctionId(predicateFunctionId)
{
}

void PredicateArgument::onCompile(Compiler* compiler, Function* /*function*/) {
    compiler->addIndex(m_predicateFunctionId);
}

///////////////////////////////////////////////////////////////////////////////
// FunCallbackArgument
///////////////////////////////////////////////////////////////////////////////
CallbackArgument::CallbackArgument(NomadIndex line, NomadIndex column, const Type* type, const NomadId functionId):
    Argument(line, column, type),
    m_functionId(functionId)
{

}

NomadId CallbackArgument::getFunctionId() const {
    return m_functionId;
}

void CallbackArgument::onResolve(CompilerContext* context, Function* function) {
    const auto* callbackType = getType() == nullptr ? nullptr : getType()->asCallback();
    const auto* callbackFunction = context->getRuntime()->getFunction(m_functionId);

    if (callbackType == nullptr || callbackFunction == nullptr) {
        return;
    }

    const auto* expectedReturnType = callbackType->getReturnType();
    const auto* actualReturnType = callbackFunction->getReturnType();

    // The return type is unknown until type inference has seen the callback's `return` statements.
    if (expectedReturnType == nullptr || actualReturnType == nullptr) {
        return;
    }

    if (!expectedReturnType->sameType(actualReturnType)) {
        reportError(
            context,
            function,
            "Callback return type mismatch. Expected " + expectedReturnType->getTypeName() + " but got " +
                actualReturnType->getTypeName()
        );
    }
}

void CallbackArgument::onCompile(Compiler* compiler, Function* /*function*/) {
    compiler->addOpCode(OpCodes::op_id_push_v);
    compiler->addIndex(m_functionId);
}

TypedValue CallbackArgument::onEvaluate(Interpreter& context) const {
    const auto* runtime = context.getRuntime();
    const auto* function = runtime->getFunction(m_functionId);
    const auto* callbackType = getType() == nullptr ? nullptr : getType()->asCallback();

    if (function == nullptr) {
        throw AstException("Unknown callback function", getLine(), getColumn());
    }
    if (function->getFunctionStart() == NOMAD_INVALID_INDEX) {
        throw AstException(
            "Callback function '" + function->getName() +
            "' is not compiled; interpreter callbacks must reference compiled named functions",
            getLine(),
            getColumn()
        );
    }
    const auto* expectedReturnType = callbackType == nullptr ? nullptr : callbackType->getReturnType();
    const auto* actualReturnType = function->getReturnType();
    if (callbackType == nullptr ||
        function->getParameterCount() != callbackType->getParameterCount() ||
        expectedReturnType == nullptr || actualReturnType == nullptr ||
        !expectedReturnType->sameType(actualReturnType)) {
        throw AstException("Callback function signature does not match its parameter", getLine(), getColumn());
    }

    for (NomadIndex index = 0; index < callbackType->getParameterCount(); ++index) {
        const auto* expectedType = callbackType->getParameterType(index);
        const auto* actualType = function->getParameterType(toNomadId(index));
        if (expectedType == nullptr || actualType == nullptr || !expectedType->sameType(actualType)) {
            throw AstException("Callback function signature does not match its parameter", getLine(), getColumn());
        }
    }

    return TypedValue(getType(), RuntimeValue{m_functionId});
}

///////////////////////////////////////////////////////////////////////////////
// EventCallbackArgument
///////////////////////////////////////////////////////////////////////////////
EventCallbackArgument::EventCallbackArgument(
    NomadIndex line,
    NomadIndex column,
    const Type* type,
    NomadId eventId,
    NomadId functionId
):
    Argument(line, column, type),
    m_eventId(eventId),
    m_functionId(functionId)
{
}

void EventCallbackArgument::onCompile(Compiler* compiler, Function* /*function*/) {
    compiler->addOpCode(OpCodes::op_id_push_v);
    compiler->addIndex(m_functionId);
    compiler->addOpCode(OpCodes::op_id_push_v);
    compiler->addIndex(m_eventId);
}

///////////////////////////////////////////////////////////////////////////////
// EventCallbackArgument
///////////////////////////////////////////////////////////////////////////////
EventDispatchArgument::EventDispatchArgument(
    const NomadIndex line,
    const NomadIndex column,
    const Type* type,
    const EventDefinition& eventDefinition
):
    Argument(line, column, type),
    m_eventDefinition(eventDefinition)
{
}

void EventDispatchArgument::onCompile(Compiler* compiler, Function* /*function*/) {
    compiler->addOpCode(OpCodes::op_id_push_v);
    compiler->addIndex(m_eventDefinition.id);
}

///////////////////////////////////////////////////////////////////////////////
// FileNameArgument
///////////////////////////////////////////////////////////////////////////////
FileNameArgument::FileNameArgument(
    const NomadIndex line,
    const NomadIndex column,
    const Type* type,
    NomadString fileName
):
    Argument(line, column, type),
    m_fileName(std::move(fileName))
{
}

void FileNameArgument::onCompile(Compiler* compiler, Function* /*function*/) {
    compiler->addLoadStringReference(m_fileName, ExpressionTarget::Stack);
}

TypedValue FileNameArgument::onEvaluate(Interpreter& /*context*/) const {
    // Borrows `m_fileName`, which outlives the nativeFunction call.
    RuntimeValue value;
    value.setStringRefValue(m_fileName.c_str());
    return TypedValue(getType(), value);
}

///////////////////////////////////////////////////////////////////////////////
// FunctionNameArgument
///////////////////////////////////////////////////////////////////////////////
FunctionNameArgument::FunctionNameArgument(
    const NomadIndex line,
    const NomadIndex column,
    const Type* type,
    NomadString  functionName
):
    Argument(line, column, type),
    m_functionName(std::move(functionName))
{
}

void FunctionNameArgument::onCompile(Compiler* compiler, Function* /*function*/) {
    compiler->addLoadStringReference(m_functionName, ExpressionTarget::Stack);
}

TypedValue FunctionNameArgument::onEvaluate(Interpreter& /*context*/) const {
    // Borrows `m_functionName`, which outlives the nativeFunction call.
    RuntimeValue value;
    value.setStringRefValue(m_functionName.c_str());
    return TypedValue(getType(), value);
}

LineNumberArgument::LineNumberArgument(const NomadIndex line, const NomadIndex column, const Type* type):
    Argument(line, column, type)
{
}

void LineNumberArgument::onCompile(Compiler* compiler, Function* /*function*/) {
    compiler->addLoadIntegerValue(static_cast<NomadInteger>(getLine()), ExpressionTarget::Stack);
}

TypedValue LineNumberArgument::onEvaluate(Interpreter& /*context*/) const {
    return TypedValue(getType(), RuntimeValue{static_cast<NomadInteger>(getLine())});
}

///////////////////////////////////////////////////////////////////////////////
// Factories
///////////////////////////////////////////////////////////////////////////////
std::unique_ptr<Argument> createExpressionArgument(
    NomadIndex line,
    NomadIndex column,
    const Type* type,
    std::unique_ptr<Expression> expression,
    const ParameterAccess parameterAccess
) {
    return std::make_unique<ExpressionArgument>(
        line,
        column,
        type,
        std::move(expression),
        parameterAccess
    );
}

std::unique_ptr<Argument> createPredicateArgument(
    NomadIndex line,
    NomadIndex column,
    NomadId predicateFunctionId
) {
    return std::make_unique<PredicateArgument>(line, column, predicateFunctionId);
}

std::unique_ptr<Argument> createCallbackArgument(
    NomadIndex line,
    NomadIndex column,
    const Type* type,
    NomadId functionId
) {
    return std::make_unique<CallbackArgument>(line, column, type, functionId);
}

std::unique_ptr<Argument> createEventCallbackArgument(
    NomadIndex line,
    NomadIndex column,
    const Type* type,
    NomadId eventId,
    NomadId functionId
) {
    return std::make_unique<EventCallbackArgument>(line, column, type, eventId, functionId);
}

std::unique_ptr<Argument> createEventDispatchArgument(
    NomadIndex line,
    NomadIndex column,
    const Type* type,
    const EventDefinition& eventDefinition
) {
    return std::make_unique<EventDispatchArgument>(line, column, type, eventDefinition);
}

std::unique_ptr<Argument> createFileNameArgument(
    NomadIndex line,
    NomadIndex column,
    const Type* type,
    NomadString fileName
) {
    return std::make_unique<FileNameArgument>(line, column, type,std::move(fileName));
}

std::unique_ptr<Argument> createFunctionNameArgument(
    NomadIndex line,
    NomadIndex column,
    const Type* type,
    NomadString functionName
) {
    return std::make_unique<FunctionNameArgument>(line, column, type, std::move(functionName));
}

std::unique_ptr<Argument> createLineNumberArgument(
    NomadIndex line,
    NomadIndex column,
    const Type* type
) {
    return std::make_unique<LineNumberArgument>(line, column, type);
}
} // namespace nomad
