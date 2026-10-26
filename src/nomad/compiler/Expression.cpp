// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Expression.hpp>

#include <nomad/compiler/Argument.hpp>
#include <nomad/compiler/CallNode.hpp>
#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/FormatStringCompiler.hpp>
#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/SyntaxTree.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/compiler/statements/ReturnStatement.hpp>

#include <nomad/script/FormatString.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/String.hpp>

#include <algorithm>
#include <any>
#include <cctype>
#include <ranges>

namespace nomad {

///////////////////////////////////////////////////////////////////////////////
// Expressions
Expression::Expression(const Expression* parent, const NomadIndex line, const NomadIndex column, const Type* type):
    AstNode(line, column),
    m_parent(parent),
    m_type(type)
{ }

Expression::~Expression() {
    if (m_type != nullptr) {
        m_type->freeValue(m_value);
    }
}

NomadBoolean Expression::resolve(CompilerContext* context, ScriptCallableBase* function) {
    if (isParsed()) {
        return true;
    }

    const auto errorCount = context->getErrorCount();

    onResolve(context, function);

    return context->getErrorCount() == errorCount;
}

void Expression::compile(Compiler* compiler, ScriptCallableBase* function, const ExpressionTarget target) {
    if (isParsed()) {
        compiler->addLoadValue(m_type, m_value, target);
    } else {
        onCompile(compiler, function, target);
    }
}

bool Expression::isWritable() const {
    return false;
}

IdentifierExpression* Expression::asIdentifierExpression() {
    return nullptr;
}

const IdentifierExpression* Expression::asIdentifierExpression() const {
    return nullptr;
}

NativeMemberExpression* Expression::asNativeExpression() {
    return nullptr;
}

const NativeMemberExpression* Expression::asNativeExpression() const {
    return nullptr;
}

bool Expression::resolveAssignmentTarget(CompilerContext* context, ScriptCallableBase* function, const Type*) {
    reportError(context, function, "Assignment target must be an identifier, native property, or indexer");
    return false;
}

void Expression::compileAssignment(Compiler* compiler, ScriptCallableBase*, Expression*) {
    compiler->reportInternalError("Invalid assignment target");
}

void Expression::evaluateAssignment(InterpreterContext&, const Expression*) const {
    raiseException("Invalid assignment target");
}

void Expression::evaluate(InterpreterContext& context) const {
    context.clearResult();
    if (context.hasError()) {
        return;
    }

    try {
        if (isParsed()) {
            context.setResult(m_type, m_value);
            return;
        }

        if (m_type == nullptr) {
            raiseException("Expression has not been resolved");
        }

        onEvaluate(context);
        if (context.hasError()) {
            context.clearResult();
            return;
        }
        const auto* value = context.getResult();

        if (value == nullptr || !value->getType()->sameType(m_type)) {
            raiseException("VirtualMachine expression produced a value with an unexpected type");
        }
    } catch (...) {
        context.clearResult();
        throw;
    }
}

void Expression::onEvaluate(InterpreterContext& /*context*/) const {
    raiseException("Expression is not supported by the interpreter");
}

const Type* Expression::getType() const {
    return m_type;
}

const RuntimeValue& Expression::getValue() const {
    return m_value;
}

bool Expression::hasValue() const {
    return m_hasValue;
}

bool Expression::hasType() const {
    return m_type != nullptr;
}

bool Expression::isParsed() const {
    return hasType() && hasValue();
}

std::unique_ptr<TypedValue> Expression::getInitialValue(Compiler* /*compiler*/) const {
    return isParsed() ? std::make_unique<TypedValue>(getType(), getValue()) : nullptr;
}

bool Expression::isTerminal() const {
    return true;
}

const Statement* Expression::getStatement() const {
    return onGetStatement();
}

void Expression::setResolved(const RuntimeValue& value, const Type* type) {
    setType(type);
    setValue(value);
}

void Expression::setType(const Type* type) {
    m_type = type;
}

void Expression::setValue(const RuntimeValue& value) {
    m_type->copyValue(value, m_value);

    m_hasValue = true;
}

const Statement* Expression::onGetStatement() const {
    if (m_parent) {
        return m_parent->getStatement();
    }

    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////
// UnaryExpression
UnaryExpression::UnaryExpression(
    const Expression* parent,
    const NomadIndex line,
    const NomadIndex column,
    const UnaryOperator unaryOperator,
    std::unique_ptr<Expression> expression
):
    Expression(parent, line, column),
    m_unaryOperator(unaryOperator),
    m_expression(std::move(expression)) {
}

void UnaryExpression::onResolve(CompilerContext* context, ScriptCallableBase* function) {
    if (!m_expression->resolve(context, function)) {
        return;
    }

    const auto* expressionType = m_expression->getType();

    if (expressionType == nullptr) {
        // Not inferred (yet): resolved again once type inference completes; the enclosing node reports the error.
        return;
    }

    auto* compiler = context->getCompiler();
    const auto* resultType = compiler->getUnaryOperatorResultType(m_unaryOperator, expressionType);

    if (resultType == nullptr) {
        reportError(
            context,
            function,
            "Invalid unary operator '" + toString(m_unaryOperator) + "' for " + expressionType->getTypeName()
        );
        return;
    }

    if (m_expression->isParsed()) {
        RuntimeValue result;

        if (!compiler->foldUnary(m_unaryOperator, expressionType, m_expression->getValue(), result)) {
            reportError(context, function, "Cannot fold unary operator");
            return;
        }

        setResolved(result, resultType);
    } else {
        setType(resultType);
    }
}

void UnaryExpression::onCompile(Compiler* compiler, ScriptCallableBase* function, ExpressionTarget target) {
    m_expression->compile(compiler, function, target);

    auto opcode = compiler->getUnaryOperatorOpCodeId(m_unaryOperator, m_expression->getType());

    compiler->addOpCode(opcode);
}

std::unique_ptr<TypedValue> UnaryExpression::getInitialValue(Compiler* compiler) const {
    if (isParsed()) {
        return Expression::getInitialValue(compiler);
    }
    const auto operand = m_expression->getInitialValue(compiler);
    if (operand == nullptr || getType() == nullptr) {
        return nullptr;
    }
    RuntimeValue value;
    if (!compiler->foldUnary(m_unaryOperator, operand->getType(), operand->getValue(), value)) {
        return nullptr;
    }
    auto result = std::make_unique<TypedValue>(getType(), value);
    getType()->freeValue(value);
    return result;
}

void UnaryExpression::onEvaluate(InterpreterContext& context) const {
    m_expression->evaluate(context);
    const auto* evaluatedOperand = context.getResult();
    if (evaluatedOperand == nullptr) {
        return;
    }

    const TypedValue operand(*evaluatedOperand);
    RuntimeValue result;
    if (!context.getRuntime()->foldUnary(m_unaryOperator, operand.getType(), operand.getValue(), result)) {
        raiseException("Cannot evaluate unary operator '" + toString(m_unaryOperator) + "'");
    }

    context.setResult(getType(), result);
}

DynamicFormatExpression::DynamicFormatExpression(const Expression* parent, const NomadIndex line,
    const NomadIndex column, std::unique_ptr<Expression> expression):
    Expression(parent, line, column), m_expression(std::move(expression)) {}

bool DynamicFormatExpression::isTerminal() const {
    return false;
}

void DynamicFormatExpression::onResolve(CompilerContext* context, ScriptCallableBase* function) {
    if (!m_expression->resolve(context, function) || m_expression->getType() == nullptr) {
        return;
    }
    if (!m_expression->getType()->isString()) {
        reportError(context, function, "Dynamic format operator requires a string operand");
        return;
    }
    setType(context->getRuntime()->getStringType());
}

void DynamicFormatExpression::onCompile(Compiler* compiler, ScriptCallableBase* function, const ExpressionTarget target) {
    m_expression->compile(compiler, function, ExpressionTarget::Result);
    compiler->addOpCode(op_dynamic_format_execute);
    compiler->addIndex(getLine());
    compiler->addIndex(getColumn());
    compiler->addMoveResult(getType(), target);
}

void DynamicFormatExpression::onEvaluate(InterpreterContext& context) const {
    m_expression->evaluate(context);
    const auto* value = context.getResult();
    if (value == nullptr) {
        return;
    }
    const auto format = value->getType()->toString(value->getValue());
    const auto result = formatDynamicString(format, context, context.getFunction()->getPath(), getLine(), getColumn());
    RuntimeValue formatted;
    formatted.setStringValue(result.text);
    try {
        context.setResult(getType(), formatted);
    } catch (...) {
        getType()->freeValue(formatted);
        throw;
    }
    getType()->freeValue(formatted);
}

///////////////////////////////////////////////////////////////////////////////
// BinaryExpression
BinaryExpression::BinaryExpression(
    const Expression* parent,
    const NomadIndex line,
    const NomadIndex column,
    const BinaryOperator binaryOperator,
    std::unique_ptr<Expression> left, std::unique_ptr<Expression> right
):
    Expression(parent, line, column),
    m_binaryOperator(binaryOperator),
    m_left(std::move(left)),
    m_right(std::move(right)) {
}

bool BinaryExpression::isTerminal() const {
    return false;
}

std::unique_ptr<TypedValue> BinaryExpression::getInitialValue(Compiler* compiler) const {
    if (isParsed()) {
        return Expression::getInitialValue(compiler);
    }
    const auto left = m_left->getInitialValue(compiler);
    const auto right = m_right->getInitialValue(compiler);
    if (left == nullptr || right == nullptr || getType() == nullptr) {
        return nullptr;
    }
    RuntimeValue value;
    if (!compiler->foldBinary(
        m_binaryOperator, left->getType(), left->getValue(), right->getType(), right->getValue(), value
    )) {
        return nullptr;
    }
    auto result = std::make_unique<TypedValue>(getType(), value);
    getType()->freeValue(value);
    return result;
}

void BinaryExpression::onResolve(CompilerContext* context, ScriptCallableBase* function) {
    const auto leftResolved = m_left->resolve(context, function);
    const auto rightResolved = m_right->resolve(context, function);

    if (!leftResolved || !rightResolved) {
        return;
    }

    auto* compiler = context->getCompiler();
    const auto* lhsType = m_left->getType();
    const auto* rhsType = m_right->getType();

    if (lhsType == nullptr || rhsType == nullptr) {
        // Not inferred (yet): resolved again once type inference completes; the enclosing node reports the error.
        return;
    }

    const auto* resultType = compiler->getBinaryOperatorResultType(m_binaryOperator, lhsType, rhsType);

    if (resultType == nullptr) {
        reportError(
            context,
            function,
            "Invalid binary operator '" + toString(m_binaryOperator) + "' for " +
                getTypeDisplayName(lhsType) + " and " + getTypeDisplayName(rhsType)
        );
        return;
    }

    if (m_left->isParsed() && m_right->isParsed()) {
        RuntimeValue value;

        if (!compiler->foldBinary(m_binaryOperator, lhsType, m_left->getValue(), rhsType, m_right->getValue(), value)) {
            reportError(context, function, "Cannot fold binary operator");
            return;
        }

        setResolved(value, resultType);
    } else {
        setType(resultType);
    }
}

void BinaryExpression::onCompile(Compiler* compiler, ScriptCallableBase* function, const ExpressionTarget target) {
    if (isParsed()) {
        compiler->addLoadValue(getType(), getValue(), target);
        return;
    }

    if (m_binaryOperator == BinaryOperator::AndAnd || m_binaryOperator == BinaryOperator::PipePipe) {
        // Short-circuit: the left operand's value is the result when it decides the outcome.
        m_left->compile(compiler, function, ExpressionTarget::Result);

        compiler->addOpCode(m_binaryOperator == BinaryOperator::AndAnd ? OpCodes::op_jump_if_false : OpCodes::op_jump_if);
        const auto skipRightIndex = compiler->addIndex(NOMAD_INVALID_INDEX);

        m_right->compile(compiler, function, ExpressionTarget::Result);

        compiler->setIndex(skipRightIndex, compiler->getOpCodeSize());

        if (target == ExpressionTarget::Intermediate) {
            compiler->addOpCode(OpCodes::op_copy_r_to_i);
        }

        if (target == ExpressionTarget::Stack) {
            compiler->addOpCode(OpCodes::op_push_r);
        }

        return;
    }

    m_left->compile(compiler, function, ExpressionTarget::Stack);
    m_right->compile(compiler, function, ExpressionTarget::Intermediate);
    compiler->addPopResult(m_left->getType());

    const auto leftType = m_left->getType();
    const auto rightType = m_right->getType();

    const auto opCodeId = compiler->getBinaryOperatorOpCodeId(
        m_binaryOperator,
        leftType,
        rightType
    );

    if (opCodeId.isInvalid()) {
        raiseException("Invalid binary operator for " + leftType->getTypeName() + " and " + rightType->getTypeName());
    }

    compiler->addOpCode(opCodeId);

    compiler->addMoveResult(getType(), target);
}

void BinaryExpression::onEvaluate(InterpreterContext& context) const {
    m_left->evaluate(context);
    const auto* evaluatedLhs = context.getResult();
    if (evaluatedLhs == nullptr) {
        return;
    }

    const TypedValue lhs(*evaluatedLhs);

    // Resolution guarantees logical operators have boolean operands.
    if (m_binaryOperator == BinaryOperator::AndAnd || m_binaryOperator == BinaryOperator::PipePipe) {
        const auto lhsValue = lhs.getValue().getBooleanValue();
        if ((m_binaryOperator == BinaryOperator::AndAnd && !lhsValue) ||
            (m_binaryOperator == BinaryOperator::PipePipe && lhsValue)) {
            context.setResult(getType(), RuntimeValue{lhsValue});
            return;
        }
    }

    m_right->evaluate(context);
    const auto* evaluatedRhs = context.getResult();
    if (evaluatedRhs == nullptr) {
        return;
    }

    const TypedValue rhs(*evaluatedRhs);
    RuntimeValue result;
    if (!context.getRuntime()->foldBinary(
        m_binaryOperator,
        lhs.getType(),
        lhs.getValue(),
        rhs.getType(),
        rhs.getValue(),
        result
    )) {
        raiseException("Cannot evaluate binary operator '" + toString(m_binaryOperator) + "'");
    }

    context.setResult(getType(), result);
}

///////////////////////////////////////////////////////////////////////////////
// CallFunctionExpression
FunctionCallExpression::FunctionCallExpression(
    const Expression* parent,
    const NomadIndex line,
    const NomadIndex column,
    NomadString name
):
    Expression(parent, line, column),
    m_name(std::move(name))
{
}

bool FunctionCallExpression::isTerminal() const {
    return false;
}

void FunctionCallExpression::onResolve(CompilerContext* context, ScriptCallableBase* function) {
    NomadString error;
    m_function = resolveFunctionCallArguments(context, function, m_name, &m_arguments, error);
    if (m_function == nullptr) {
        reportError(context, function, error);
        return;
    }

    const auto* script = m_function->asScriptFunction();
    const auto* returnType = m_function->getReturnType();

    if (context->getMode() != CompilerMode::Script && script != nullptr &&
        script->getFunctionStart() == NOMAD_INVALID_INDEX) {
        reportError(context, function, "Function '" + m_name + "' has not been compiled");
        return;
    }

    auto* compiler = context->getCompiler();

    if (context->getMode() == CompilerMode::Script && function != nullptr && script != nullptr) {
        compiler->addFunctionCallDependency(function->getId(), script->getId());
    }

    if (returnType == nullptr) {
        if (script != nullptr && function != nullptr &&
            compiler->isRecursiveFunctionCall(function->getId(), script->getId())) {
            reportError(
                context,
                function,
                "Cannot infer return type of recursive function '" + m_name +
                    "': it needs a `return` whose type does not depend on a recursive call"
            );
        } else {
            reportError(context, function, "Cannot determine return type of function '" + m_name + "'");
        }
        return;
    }

    if (returnType->isVoid()) {
        reportError(context, function, "Function '" + m_name + "' does not return a value");
        return;
    }

    setType(returnType);

    (void)m_arguments.resolve(context, function);
}

void FunctionCallExpression::onCompile(Compiler* compiler, ScriptCallableBase* function, const ExpressionTarget target) {
    if (m_function == nullptr) {
        raiseException("Function call to '" + m_name + "' has not been resolved");
    }
    if (const auto* native = m_function->asNativeFunction()) {
        compiler->addNativeFunctionCall(native->getId(), function, &m_arguments);
    } else if (const auto* script = m_function->asScriptFunction()) {
        compiler->addFunctionCall(script->getId(), function, &m_arguments);
    } else {
        raiseException("Unsupported implementation of function '" + m_name + "'");
    }

    compiler->addMoveResult(getType(), target);
}

void FunctionCallExpression::addArgument(std::unique_ptr<Argument> argument) {
    m_arguments.add(std::move(argument));
}

ArgumentList* FunctionCallExpression::getArguments() {
    return &m_arguments;
}

const ArgumentList* FunctionCallExpression::getArguments() const {
    return &m_arguments;
}

NomadIndex FunctionCallExpression::getArgumentCount() const {
    return m_arguments.getArgumentCount();
}

void FunctionCallExpression::onEvaluate(InterpreterContext& context) const {
    if (m_function == nullptr) {
        raiseException("Function call to '" + m_name + "' has not been resolved");
    }
    std::vector<TypedValue> evaluatedArguments;
    std::vector<RuntimeValue> values;
    m_arguments.evaluate(context, evaluatedArguments, values);
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
}

///////////////////////////////////////////////////////////////////////////////
// PrimaryExpression
PrimaryExpression::PrimaryExpression(const Expression* parent, const NomadIndex line, const NomadIndex column)
    : Expression(parent, line, column) {
}

///////////////////////////////////////////////////////////////////////////////
// NativeMemberExpression
NativeMemberExpression::NativeMemberExpression(const Expression* parent, const NomadIndex line,
                                                     const NomadIndex column, NomadString qualifiedName,
                                                     NomadString memberName, std::unique_ptr<Expression> receiver,
                                                     ArgumentList arguments, const bool hasExplicitArguments)
    : Expression(parent, line, column), m_qualifiedName(std::move(qualifiedName)), m_memberName(std::move(memberName)),
      m_receiver(std::move(receiver)), m_arguments(std::move(arguments)), m_hasExplicitArguments(hasExplicitArguments) {
}

bool NativeMemberExpression::isTerminal() const {
    return false;
}

bool NativeMemberExpression::isWritable() const {
    return false;
}

NativeMemberExpression* NativeMemberExpression::asNativeExpression() {
    return this;
}

const NativeMemberExpression* NativeMemberExpression::asNativeExpression() const {
    return this;
}

void NativeMemberExpression::setAllowVoid(const bool allowVoid) {
    m_allowVoid = allowVoid;
}

bool NativeMemberExpression::resolveMember(
    CompilerContext* context,
    ScriptCallableBase* function,
    const Type* valueType,
    const bool assignment
) {
    m_memberId = PropertyId::INVALID;
    m_memberType = MemberType::Unknown;
    m_method = nullptr;
    if (!m_receiver->resolve(context, function)) {
        return false;
    }

    auto* runtime = context->getRuntime();
    m_memberId = runtime->getNativeMemberId(m_receiver->getType(), m_memberName);

    if (runtime->getMemberType(m_memberId) == MemberType::Unknown) {
        const auto* receiverType = m_receiver->getType();
        const auto* owner = receiverType != nullptr ? receiverType->asObjectType() : nullptr;
        if (owner != nullptr) {
            const auto* property = owner->getProperty(m_memberName);
            if (assignment && context->getMode() == CompilerMode::Script) {
                property = context->getCompiler()->resolveObjectField(
                    context, function, owner, m_memberName, valueType, owner->isOpen(), getLine(), getColumn()
                );
            }
            if (property != nullptr) {
                if (m_hasExplicitArguments && m_arguments.getArgumentCount() != 0) {
                    reportError(context, function, "Class field '" + m_qualifiedName + "' does not take arguments");
                    return false;
                }
                m_memberId = property->getId();
                m_memberType = MemberType::Property;
                return true;
            }
            if (!assignment || context->getMode() != CompilerMode::Script) {
                reportError(context, function, "Unknown class field '" + m_memberName +
                    "' on type '" + owner->getTypeName() + "'");
            }
            return false;
        }
        reportError(context, function,
                    "Type '" + getTypeDisplayName(m_receiver->getType()) + "' has no native member '" +
                        m_memberName + "'");
        return false;
    }

    m_memberType = runtime->getMemberType(m_memberId);
    if (m_memberType == MemberType::Method) {
        m_method = runtime->getNativeMemberMethod(m_memberId);
        if (m_method == nullptr) {
            context->getCompiler()->reportInternalError("Invalid native method: " + m_qualifiedName);
        }
    }
    return true;
}

bool NativeMemberExpression::resolveAssignmentTarget(
    CompilerContext* context,
    ScriptCallableBase* function,
    const Type* valueType
) {
    if (!resolveMember(context, function, valueType, true)) {
        return false;
    }

    auto* runtime = context->getRuntime();
    if (m_memberType == MemberType::Method) {
        reportError(context, function, "Cannot assign to native method '" + m_qualifiedName + "'");
        return false;
    }

    const auto info = runtime->getNativePropertyInfo(m_memberId);
    if (!info) {
        const auto* property = runtime->getProperty(std::get<PropertyId>(m_memberId));
        if (property == nullptr) {
            context->getCompiler()->reportInternalError("Invalid property: " + m_qualifiedName);
        }
        setType(property->getValueType());
        return true;
    }
    if (!info->setFn) {
        reportError(context, function, "Native property '" + m_qualifiedName + "' is read-only");
        return false;
    }
    setType(info->valueType);
    return true;
}

void NativeMemberExpression::onResolve(CompilerContext* context, ScriptCallableBase* function) {
    if (!resolveMember(context, function)) {
        return;
    }

    auto* runtime = context->getRuntime();
    switch (m_memberType) {
    case MemberType::Property: {
        const auto info = runtime->getNativePropertyInfo(m_memberId);
        if (!info) {
            const auto id = std::get<PropertyId>(m_memberId);
            const auto* property = runtime->getProperty(id);
            if (property == nullptr) {
                context->getCompiler()->reportInternalError("Invalid property: " + m_qualifiedName);
            }
            setType(property->getValueType());
            context->getCompiler()->recordFieldRead(id);
            return;
        }

        if (!info->getFn) {
            reportError(context, function, "Native property '" + m_qualifiedName + "' is write-only");
            return;
        }

        setType(info->valueType);
        return;
    }

    case MemberType::Method: {
        const auto* method = m_method;

        auto& arguments = m_arguments.getArguments();
        if (arguments.size() != method->getParameterCount()) {
            reportError(context, function, "Incorrect argument count for native method '" + m_qualifiedName + "'");
            return;
        }

        for (NomadIndex index = 0; index < arguments.size(); ++index) {
            arguments[index]->setType(method->getParameters()[index].type);
        }

        if (!m_arguments.resolve(context, function)) {
            return;
        }

        if (method->getReturnType()->isVoid() && !m_allowVoid) {
            reportError(context, function,
                        "Cannot use native method '" + m_qualifiedName +
                            "' in an expression because it does not return a value");
            return;
        }

        setType(method->getReturnType());
        return;
    }

    case MemberType::Unknown:
        context->getCompiler()->reportInternalError("Invalid native member: " + m_qualifiedName);
    }
}

void NativeMemberExpression::onCompile(Compiler* compiler, ScriptCallableBase* function, const ExpressionTarget target) {
    if (m_memberType == MemberType::Unknown) {
        compiler->reportInternalError("Native member has not been resolved: " + m_qualifiedName);
    }
    const auto* receiverType = m_receiver->getType();

    m_receiver->compile(compiler, function, ExpressionTarget::Intermediate);

    if (m_memberType == MemberType::Property) {
        compiler->addOpCode(op_native_property_get);
        compiler->addId(std::get<PropertyId>(m_memberId).id);
        compiler->addFreeValue(receiverType->getTypeOpCodes()->freeIntermediate);
        compiler->addMoveResult(getType(), target);
        return;
    }

    const auto* method = m_method != nullptr ? m_method->asNativeMethod() : nullptr;
    if (method == nullptr) {
        compiler->reportInternalError("Invalid native member: " + m_qualifiedName);
    }

    m_arguments.setReceiverType(receiverType);
    compiler->addNativeMethodCall(std::get<CallableId>(m_memberId), function, &m_arguments);

    compiler->addFreeValue(receiverType->getTypeOpCodes()->freeIntermediate);

    if (!method->getReturnType()->isVoid()) {
        compiler->addMoveResult(method->getReturnType(), target);
    }
}

void NativeMemberExpression::onEvaluate(InterpreterContext& context) const {
    if (m_memberType == MemberType::Unknown) {
        raiseException("Native member has not been resolved: " + m_qualifiedName);
    }
    m_receiver->evaluate(context);
    const auto* evaluatedReceiver = context.getResult();
    if (evaluatedReceiver == nullptr) {
        if (!context.hasError()) {
            context.setError("Native member receiver did not produce a value");
        }
        return;
    }

    const auto* receiverType = m_receiver->getType();
    RuntimeValue receiver;
    receiverType->copyValue(evaluatedReceiver->getValue(), receiver);

    auto* runtime = context.getRuntime();
    if (m_memberType == MemberType::Property) {
        const auto* property = runtime->getProperty(std::get<PropertyId>(m_memberId));
        if (property == nullptr) {
            receiverType->freeValue(receiver);
            raiseException("Invalid property getter: " + m_qualifiedName);
        }
        const auto* valueType = property->getValueType();
        RuntimeValue result;

        try {
            context.getVirtualMachine()->getProperty(std::get<PropertyId>(m_memberId), receiver, result);
            context.setResult(valueType, result);
        } catch (...) {
            valueType->freeValue(result);
            receiverType->freeValue(receiver);
            throw;
        }
        valueType->freeValue(result);
        receiverType->freeValue(receiver);
        return;
    }

    const auto* method = m_method != nullptr ? m_method->asNativeMethod() : nullptr;
    if (method == nullptr) {
        receiverType->freeValue(receiver);
        raiseException("Invalid native method: " + m_qualifiedName);
    }
    const auto* returnType = method->getReturnType();

    std::vector<TypedValue> evaluatedArguments;
    std::vector<RuntimeValue> values;
    m_arguments.evaluate(context, evaluatedArguments, values);
    if (context.hasError()) {
        receiverType->freeValue(receiver);
        return;
    }

    auto* virtualMachine = context.getVirtualMachine();
    try {
        virtualMachine->runNativeMethod(std::get<CallableId>(m_memberId), values, receiver);


        if (returnType->isVoid()) {
            context.setVoidResult();
        } else {
            context.setResult(returnType, virtualMachine->getResult());
            returnType->freeValue(virtualMachine->getResult());
        }
    } catch (...) {
        if (!returnType->isVoid()) {
            returnType->freeValue(virtualMachine->getResult());
        }
        receiverType->freeValue(receiver);
        throw;
    }

    receiverType->freeValue(receiver);
}

void NativeMemberExpression::compileAssignment(Compiler* compiler, ScriptCallableBase* function, Expression* expression) {
    const auto* receiverType = m_receiver->getType();
    const auto* propertyType = getType();
    m_receiver->compile(compiler, function, ExpressionTarget::Intermediate);
    compiler->addPushIntermediate(receiverType);
    expression->compile(compiler, function, ExpressionTarget::Result);
    compiler->addPopIntermediate(receiverType);
    compiler->addOpCode(op_native_property_set);
    compiler->addId(std::get<PropertyId>(m_memberId).id);
    compiler->addFreeValue(propertyType->getTypeOpCodes()->freeResult);
    compiler->addFreeValue(receiverType->getTypeOpCodes()->freeIntermediate);
}

void NativeMemberExpression::evaluateAssignment(InterpreterContext& context, const Expression* expression) const {
    m_receiver->evaluate(context);
    const auto* evaluatedReceiver = context.getResult();
    if (evaluatedReceiver == nullptr) {
        if (!context.hasError()) {
            context.setError("Native property receiver did not produce a value");
        }
        return;
    }

    const auto* receiverType = m_receiver->getType();
    RuntimeValue receiver;
    receiverType->copyValue(evaluatedReceiver->getValue(), receiver);

    expression->evaluate(context);
    const auto* assignedValue = context.getResult();
    if (assignedValue == nullptr) {
        receiverType->freeValue(receiver);
        if (!context.hasError()) {
            context.setError("Native property assignment did not produce a value");
        }
        return;
    }

    const auto* property = context.getRuntime()->getProperty(std::get<PropertyId>(m_memberId));
    if (property == nullptr) {
        receiverType->freeValue(receiver);
        raiseException("Invalid property setter: " + m_qualifiedName);
    }

    try {
        context.getVirtualMachine()->setProperty(std::get<PropertyId>(m_memberId), receiver, assignedValue->getValue());
        context.setResult(property->getValueType(), assignedValue->getValue());
    } catch (...) {
        receiverType->freeValue(receiver);
        throw;
    }

    receiverType->freeValue(receiver);
}

///////////////////////////////////////////////////////////////////////////////
// IndexerExpression
IndexerExpression::IndexerExpression(
    const Expression* parent,
    const NomadIndex line,
    const NomadIndex column,
    std::unique_ptr<Expression> receiver,
    std::vector<std::unique_ptr<Expression>> indexes
):
    Expression(parent, line, column),
    m_receiver(std::move(receiver)),
    m_indexes(std::move(indexes))
{}

bool IndexerExpression::isTerminal() const {
    return false;
}

bool IndexerExpression::resolveIndexer(CompilerContext* context, ScriptCallableBase* function) {
    if (!m_receiver->resolve(context, function)) {
        return false;
    }
    for (const auto& index : m_indexes) {
        if (!index->resolve(context, function)) {
            return false;
        }
    }
    auto* runtime = context->getRuntime();
    m_indexerId = runtime->getNativeIndexerId(m_receiver->getType(), m_indexes.size());
    const auto info = runtime->getNativeIndexerInfo(m_indexerId);
    if (!info) {
        reportError(context, function, "Type '" + getTypeDisplayName(m_receiver->getType()) +
            "' has no indexer with " + toString(m_indexes.size()) + " indices");
        return false;
    }
    for (auto index = NomadIndex{0}; index < m_indexes.size(); ++index) {
        if (!info->indexTypes[index]->acceptsArgument(m_indexes[index]->getType())) {
            reportError(context, function, "Indexer parameter " + toString(index + 1) + " requires type '" +
                getTypeDisplayName(info->indexTypes[index]) + "', not '" +
                getTypeDisplayName(m_indexes[index]->getType()) + "'");
            return false;
        }
    }
    setType(info->valueType);
    m_canRead = static_cast<bool>(info->getFn);
    m_canWrite = static_cast<bool>(info->setFn);
    return true;
}

bool IndexerExpression::resolveAssignmentTarget(CompilerContext* context, ScriptCallableBase* function, const Type*) {
    if (!resolveIndexer(context, function)) {
        return false;
    }
    if (!m_canWrite) {
        reportError(context, function, "Indexer for type '" + getTypeDisplayName(m_receiver->getType()) + "' is read-only");
        return false;
    }
    return true;
}

void IndexerExpression::onResolve(CompilerContext* context, ScriptCallableBase* function) {
    if (!resolveIndexer(context, function)) {
        return;
    }
    if (!m_canRead) {
        reportError(context, function, "Indexer for type '" + getTypeDisplayName(m_receiver->getType()) + "' is write-only");
    }
}

void IndexerExpression::compileOperands(Compiler* compiler, ScriptCallableBase* function, Expression* assigned) {
    m_receiver->compile(compiler, function, ExpressionTarget::Stack);
    if (assigned != nullptr) {
        assigned->compile(compiler, function, ExpressionTarget::Stack);
    }
    for (const auto& index : std::ranges::reverse_view(m_indexes)) {
        index->compile(compiler, function, ExpressionTarget::Stack);
    }
}

void IndexerExpression::onCompile(Compiler* compiler, ScriptCallableBase* function, const ExpressionTarget target) {
    compileOperands(compiler, function);
    compiler->addOpCode(op_native_index_get);
    compiler->addId(m_indexerId.id);
    compiler->addMoveResult(getType(), target);
}

void IndexerExpression::compileAssignment(Compiler* compiler, ScriptCallableBase* function, Expression* expression) {
    compileOperands(compiler, function, expression);
    compiler->addOpCode(op_native_index_set);
    compiler->addId(m_indexerId.id);
}

void IndexerExpression::onEvaluate(InterpreterContext& context) const {
    evaluateIndexer(context);
}

void IndexerExpression::evaluateAssignment(InterpreterContext& context, const Expression* expression) const {
    evaluateIndexer(context, expression);
}

void IndexerExpression::evaluateIndexer(InterpreterContext& context, const Expression* assigned) const {
    m_receiver->evaluate(context);
    if (context.hasError()) {
        return;
    }
    const TypedValue receiver(*context.getResult());
    std::vector<TypedValue> arguments;
    arguments.reserve(m_indexes.size() + (assigned != nullptr ? 1 : 0));
    if (assigned != nullptr) {
        assigned->evaluate(context);
        if (context.hasError()) {
            return;
        }
        arguments.emplace_back(*context.getResult());
    }
    for (const auto& index : std::ranges::reverse_view(m_indexes)) {
        index->evaluate(context);
        if (context.hasError()) {
            return;
        }
        arguments.emplace_back(*context.getResult());
    }
    std::vector<RuntimeValue> values;
    values.reserve(arguments.size());
    for (const auto& argument : std::ranges::reverse_view(arguments)) {
        values.push_back(argument.getValue());
    }
    auto* machine = context.getVirtualMachine();
    try {
        machine->runNativeIndexer(m_indexerId, values, receiver.getValue(), assigned != nullptr);
        if (assigned != nullptr) {
            context.setResult(arguments.front());
        } else {
            context.setResult(getType(), machine->getResult());
        }
    } catch (...) {
        if (assigned == nullptr) {
            getType()->freeValue(machine->getResult());
        }
        throw;
    }
    if (assigned == nullptr) {
        getType()->freeValue(machine->getResult());
    }
}

///////////////////////////////////////////////////////////////////////////////
// BooleanLiteral
BooleanLiteral::BooleanLiteral(
    const Expression* parent,
    const NomadIndex line,
    const NomadIndex column,
    const NomadBoolean m_value
):
    PrimaryExpression(parent, line, column),
    m_value(m_value) {
}

void BooleanLiteral::onResolve(CompilerContext* context, ScriptCallableBase* /*function*/) {
    setResolved(RuntimeValue{m_value}, context->getRuntime()->getBooleanType());
}

void BooleanLiteral::onCompile(Compiler* compiler, ScriptCallableBase* /*function*/, ExpressionTarget target) {
    compiler->addLoadBooleanValue(m_value, target);
}

///////////////////////////////////////////////////////////////////////////////
// IntegerExpression

IntegerLiteral::IntegerLiteral(
    const Expression* parent,
    const NomadIndex line,
    const NomadIndex column,
    const NomadInteger value
):
    PrimaryExpression(parent, line, column),
    m_value(value) {
}

void IntegerLiteral::onResolve(CompilerContext* context, ScriptCallableBase* /*function*/) {
    RuntimeValue value{m_value};

    setResolved(value, context->getRuntime()->getIntegerType());
}

void IntegerLiteral::onCompile(Compiler* compiler, ScriptCallableBase* /*function*/, ExpressionTarget target) {
    compiler->addLoadIntegerValue(m_value, target);
}

///////////////////////////////////////////////////////////////////////////////
// FloatLiteralExpression
FloatLiteral::FloatLiteral(
    const Expression* parent,
    const NomadIndex line,
    const NomadIndex column,
    const NomadFloat value
):
    PrimaryExpression(parent, line, column),
    m_value(value) {
}

void FloatLiteral::onResolve(CompilerContext* context, ScriptCallableBase* /*function*/) {
    RuntimeValue value{m_value};

    setResolved(value, context->getRuntime()->getFloatType());
}

void FloatLiteral::onCompile(Compiler* compiler, ScriptCallableBase* /*function*/, ExpressionTarget target) {
    compiler->addLoadFloatValue(m_value, target);
}

///////////////////////////////////////////////////////////////////////////////
// Object construction

ConstructorExpression::ConstructorExpression(
    const Expression* parent,
    const NomadIndex line,
    const NomadIndex column,
    NomadString constructorName,
    const Type* objectType,
    ArgumentList arguments
):
    Expression(parent, line, column),
    m_constructorName(std::move(constructorName)),
    m_objectType(objectType),
    m_arguments(std::move(arguments))
{
}

void ConstructorExpression::onResolve(CompilerContext* context, ScriptCallableBase* function) {
    m_constructor = nullptr;
    auto* runtime = context->getRuntime();
    auto& arguments = m_arguments.getArguments();

    // Arguments without a type of their own (such as source arguments) match any parameter.
    std::vector<const Type*> argumentTypes;
    argumentTypes.reserve(arguments.size());
    const auto errorCount = context->getErrorCount();
    for (const auto& argument : arguments) {
        argumentTypes.push_back(argument->resolveOverloadType(context, function));
    }
    if (m_objectType == nullptr && context->getErrorCount() != errorCount) {
        return;
    }

    const auto* objectType = m_objectType;
    if (objectType == nullptr) {
        const auto inference = runtime->inferConstructorTypeArguments(m_constructorName, argumentTypes);
        if (!inference.isResolved()) {
            reportError(context, function, inference.error);
            return;
        }
        objectType = runtime->getParameterizedType(m_constructorName, inference.typeArguments);
        if (objectType == nullptr) {
            reportError(context, function, "Cannot specialize inferred constructor '" + m_constructorName + "'");
            return;
        }
    }
    const auto resolution = runtime->resolveConstructor(objectType, argumentTypes);
    if (resolution.status != ResolveStatus::Resolved) {
        reportError(context, function, runtime->describeResolveFailure(m_constructorName, resolution));
        return;
    }
    const auto* selected = runtime->getConstructor(resolution.id);
    if (selected == nullptr) {
        reportError(context, function, "Invalid constructor for '" + m_constructorName + "'");
        return;
    }
    for (auto index = NomadIndex{0}; index < arguments.size(); ++index) {
        arguments[index]->setType(selected->getParameters()[index].type);
    }
    if (m_arguments.resolve(context, function)) {
        m_constructor = selected;
        setType(selected->getReturnType());
    }
}

bool ConstructorExpression::isTerminal() const {
    return false;
}

void ConstructorExpression::onCompile(Compiler* compiler, ScriptCallableBase* function, const ExpressionTarget target) {
    if (m_constructor == nullptr) {
        raiseException("Constructor call to '" + m_constructorName + "' has not been resolved");
    }
    compiler->addConstructorCall(m_constructor->getId(), function, &m_arguments);
    compiler->addMoveResult(getType(), target);
}

void ConstructorExpression::onEvaluate(InterpreterContext& context) const {
    if (m_constructor == nullptr) {
        raiseException("Constructor call to '" + m_constructorName + "' has not been resolved");
    }
    std::vector<TypedValue> evaluatedArguments;
    std::vector<RuntimeValue> values;
    m_arguments.evaluate(context, evaluatedArguments, values);
    if (context.hasError()) {
        return;
    }
    const auto release = [](NomadObject* object) { object->releaseRef(); };
    const std::unique_ptr<NomadObject, decltype(release)> object(
        context.getVirtualMachine()->constructObject(m_constructor->getId(), values), release
    );
    RuntimeValue value;
    value.setObjectValue(object.get());
    context.setResult(getType(), value);
}

///////////////////////////////////////////////////////////////////////////////
// StringLiteralExpression

StringLiteral::StringLiteral(
    const Expression* parent,
    const NomadIndex line,
    const NomadIndex column,
    const NomadString& value
):
    PrimaryExpression(parent, line, column),
    m_value(std::move(value)) {
}

void StringLiteral::onResolve(CompilerContext* context, ScriptCallableBase* /*function*/) {
    const auto* stringType = context->getRuntime()->getStringType();
    RuntimeValue value{m_value};

    setResolved(value, stringType);
    stringType->freeValue(value);
}

void StringLiteral::onCompile(Compiler* compiler, ScriptCallableBase* /*function*/, ExpressionTarget /*target*/) {
    const auto stringValue = getValue().getStringValue();

    const auto stringId = compiler->getRuntime()->registerString(stringValue);

    compiler->addOpCode(OpCodes::op_string_load_r);
    compiler->addId(stringId.id);
}

///////////////////////////////////////////////////////////////////////////////
// ConstantValueExpression
ConstantValueExpression::ConstantValueExpression(
    const Expression* parent,
    const NomadIndex line,
    const NomadIndex column,
    const NomadString& constantName
):
    PrimaryExpression(parent, line, column),
    m_constantName(constantName) {
}

void ConstantValueExpression::onResolve(CompilerContext* context, ScriptCallableBase* function) {
    auto* runtime = context->getRuntime();
    m_constantId = runtime->getConstantId(m_constantName);

    if (m_constantId.isInvalid()) {
        reportError(context, function, "Unknown constant: " + m_constantName);
        return;
    }

    RuntimeValue constantValue;
    runtime->getConstantValue(m_constantId, constantValue);
    auto constantType = runtime->getConstantType(m_constantId);

    if (constantType->needsFree() && !constantType->isString()) {
        setType(constantType);
    } else {
        setResolved(constantValue, constantType);
    }
}

void ConstantValueExpression::onCompile(Compiler* compiler, ScriptCallableBase* /*function*/, ExpressionTarget target) {
    if (m_constantId.isInvalid()) {
        raiseException("Unknown constant: " + m_constantName);
    }

    compiler->addLoadConstant(m_constantId, target);
}

void ConstantValueExpression::onEvaluate(InterpreterContext& context) const {
    RuntimeValue value;
    context.getRuntime()->getConstantValue(m_constantId, value);
    context.setResult(getType(), value);
}

///////////////////////////////////////////////////////////////////////////////
// FormatStringLiteral
FormatStringLiteral::FormatStringLiteral(
    const Expression* parent,
    const NomadIndex line,
    const NomadIndex column,
    const NomadString& formatString
):
    Expression(parent, line, column),
    m_formatString(formatString),
    m_variableFormat(formatString) {
}

bool FormatStringLiteral::parsePlaceholders(
    CompilerContext* context,
    ScriptCallableBase* function,
    const NomadStringView sourceContent
) {
    if (context->getMode() != CompilerMode::Script || function == nullptr) {
        return true;
    }

    const auto parsed = parseFormatTemplate(sourceContent);
    for (const auto& diagnostic : parsed.diagnostics) {
        context->reportError(diagnostic.message, function->getPath(), getLine(), getColumn() + 2 + diagnostic.offset);
    }
    if (!parsed.diagnostics.empty()) {
        return false;
    }

    m_segments = parsed.segments;
    m_variableFormat.clear();
    for (auto segmentIndex = NomadIndex{0}; segmentIndex < m_segments.size(); ++segmentIndex) {
        auto& segment = m_segments[segmentIndex];
        if (segment.type == FormatTemplateSegmentType::Literal) {
            segment.text = decodeFormatStringLiteral(segment.text);
            m_variableFormat += segment.text;
            continue;
        }

        auto identifier = !segment.text.empty();
        auto needsFirstCharacter = true;
        for (const auto character : segment.text) {
            const auto byte = static_cast<unsigned char>(character);
            if (needsFirstCharacter) {
                if (!std::isalpha(byte) && character != '_') {
                    identifier = false;
                    break;
                }
                needsFirstCharacter = false;
            } else if (character == '.') {
                needsFirstCharacter = true;
            } else if (!std::isalnum(byte) && character != '_') {
                identifier = false;
                break;
            }
        }
        if (identifier && !needsFirstCharacter &&
            !parser::isNativeMemberCandidate(context->getCompiler(), function, segment.text)) {
            IdentifierDefinition definition;
            context->getCompiler()->getIdentifierDefinition(segment.text, function, definition);
            if (context->isHubInitializer() &&
                (definition.identifierType == IdentifierType::FunctionVariable ||
                 definition.identifierType == IdentifierType::Parameter)) {
                reportError(context, function, "Hub initializers cannot read script local '" + segment.text + "'");
                return false;
            }
            m_variableFormat += "{" + segment.text + "}";
            continue;
        }

        auto* compiler = context->getCompiler();
        const auto name = compiler->generateFunctionName("formatExpression", function, getLine());
        const auto functionId = compiler->createFunction(
            context, compiler->getScriptId(function->getId()), name, context->isHubInitializer()
        );
        if (functionId.isInvalid()) {
            return false;
        }
        auto* expressionFunction = context->getRuntime()->getScriptCallable(functionId);
        expressionFunction->setParentId(
            context->isHubInitializer() && function->getParentId().isInvalid()
                ? context->getRuntime()->getFunctionId("$init")
                : function->getId()
        );
        expressionFunction->setHub(context->getHub().empty() ? function->getHub() : context->getHub());

        const auto column = getColumn() + 3 + segment.offset;
        // Padding keeps every child AST node and nested format literal in host-source coordinates.
        const auto source = NomadString(getLine() - 1, '\n') + NomadString(column - 1, ' ') + segment.text;
        Tokenizer tokens(context, function->getPath(), source);
        if (context->hasError()) {
            return false;
        }
        if (!tokens.nextLine()) {
            context->reportError("Empty expression in format string", function->getPath(), getLine(), column);
            return false;
        }
        auto expression = parser::parseExpression(context, expressionFunction, &tokens);
        if (expression == nullptr || !parser::expectEndOfLine(context, expressionFunction, &tokens)) {
            return false;
        }

        auto node = std::make_unique<FunctionNode>(getLine(), column);
        node->addStatement(std::make_unique<ReturnStatementNode>(getLine(), column, std::move(expression)));
        node->setEndSpan(getLine(), column + segment.text.size());
        compiler->setFunctionNode(functionId, std::move(node));
        m_expressionPlaceholders.push_back({functionId, column, segmentIndex});
    }

    return true;
}

void FormatStringLiteral::onResolve(CompilerContext* context, ScriptCallableBase* function) {
    if (context->getMode() != CompilerMode::Script) {
        setType(context->getRuntime()->getStringType());
        return;
    }

    if (const auto error = compileFormatString(context->getCompiler(), function, m_variableFormat, nullptr)) {
        reportError(context, function, *error);
        return;
    }

    for (const auto& placeholder : m_expressionPlaceholders) {
        const auto* expressionFunction = context->getRuntime()->getScriptCallable(placeholder.functionId);
        const auto* returnType = expressionFunction->getReturnType();
        if (returnType != nullptr && returnType->isVoid()) {
            context->reportError("Format string expression must return a value", function->getPath(),
                getLine(), placeholder.column);
        }
    }

    setType(context->getRuntime()->getStringType());
}

void FormatStringLiteral::onCompile(Compiler* compiler, ScriptCallableBase* function, ExpressionTarget target) {
    const auto functionId = function->getId();

    auto formatKey = m_variableFormat;
    for (const auto& placeholder : m_expressionPlaceholders) {
        formatKey += "@" + std::to_string(placeholder.functionId.id);
    }
    auto formatStringId = compiler->getRuntime()->getFormatStringId(formatKey, functionId);

    if (formatStringId.isInvalid()) {
        formatStringId = compiler->getRuntime()->registerFormatString(formatKey, functionId);

        const auto format_string = compiler->getRuntime()->getFormatString(formatStringId);

        auto expressionIndex = NomadIndex{0};
        for (auto segmentIndex = NomadIndex{0}; segmentIndex < m_segments.size(); ++segmentIndex) {
            const auto& segment = m_segments[segmentIndex];
            if (segment.type == FormatTemplateSegmentType::Literal) {
                format_string->addLiteral(segment.text);
            } else if (expressionIndex < m_expressionPlaceholders.size() &&
                       m_expressionPlaceholders[expressionIndex].segmentIndex == segmentIndex) {
                const auto id = m_expressionPlaceholders[expressionIndex++].functionId;
                const auto* expressionFunction = compiler->getRuntime()->getScriptCallable(id);
                const auto* returnType = expressionFunction->getReturnType();
                if (returnType == nullptr || returnType->isVoid()) {
                    compiler->reportInternalError("Format string expression has no resolved value type");
                }
                format_string->addExpression(returnType, id);
            } else if (const auto error = compileFormatString(compiler, function, "{" + segment.text + "}", format_string)) {
                compiler->reportInternalError("Format string placeholder failed to compile after resolving: " + *error);
            }
        }
    }

    compiler->addOpCode(OpCodes::op_format_string_execute);
    compiler->addId(formatStringId.id);

    compiler->addMoveResult(getType(), target);
}

void FormatStringLiteral::onEvaluate(InterpreterContext& context) const {
    const auto result = formatDynamicString(m_formatString, context, context.getFunction()->getPath(),
        getLine(), getColumn());
    RuntimeValue value;
    value.setStringValue(result.text);
    try {
        context.setResult(getType(), value);
    } catch (...) {
        getType()->freeValue(value);
        throw;
    }
    getType()->freeValue(value);
}

///////////////////////////////////////////////////////////////////////////////
// IdentifierExpression
IdentifierExpression::IdentifierExpression(
    const Expression* parent,
    const NomadIndex line,
    const NomadIndex column,
    const NomadString& identifier
):
    PrimaryExpression(parent, line, column),
    m_identifier(identifier)
{
}

const NomadString& IdentifierExpression::getIdentifier() const {
    return m_identifier;
}

void IdentifierExpression::onResolve(CompilerContext* context, ScriptCallableBase* function) {
    if (context->getCompiler()->getReceiverType(function) != nullptr && m_identifier.starts_with("this.")) {
        const auto* owner = context->getCompiler()->getReceiverType(function)->asObjectType();
        const auto* property = owner->getProperty(m_identifier.substr(5));
        if (property == nullptr) {
            reportError(context, function, "Unknown class field '" + m_identifier.substr(5) + "'");
            return;
        }
        IdentifierDefinition receiver;
        if (!resolveIdentifier("this", context->getCompiler(), function, receiver)) {
            reportError(context, function, "Cannot capture the class receiver 'this'");
            return;
        }
        setType(property->getValueType());
        context->getCompiler()->recordFieldRead(property->getId());
        return;
    }
    auto* compiler = context->getCompiler();

    compiler->getIdentifierDefinition(m_identifier, function, m_identifierDefinition);

    // Check for invalid identifier type.
    switch (m_identifierDefinition.identifierType) {
        case IdentifierType::Unknown:
            if (context->getMode() == CompilerMode::Interpreter ||
                function == nullptr || !parseCapture(compiler, function)) {
                reportError(context, function, "Unknown identifier: " + m_identifier);
                return;
            }

            // Reload the identifier definition.
            compiler->getIdentifierDefinition(m_identifier, function, m_identifierDefinition);

            if (m_identifierDefinition.identifierType != IdentifierType::FunctionVariable) {
                compiler->reportInternalError("Capture definition should exist.");
            }
            break;

        case IdentifierType::Function:
            reportError(context, function, "Cannot use function as identifier: " + m_identifier);
            return;

        case IdentifierType::NativeFunction:
            reportError(context, function, "Cannot use native function as identifier: " + m_identifier);
            return;

        case IdentifierType::Event:
            reportError(context, function, "Cannot use event as identifier: " + m_identifier);
            return;

        case IdentifierType::Keyword:
            reportError(context, function, "Cannot use keyword as identifier: " + m_identifier);
            return;

        case IdentifierType::Statement:
            reportError(context, function, "Cannot use statement as identifier: " + m_identifier);
            return;

        default:
            // Valid identifier type. Ignore
            break;
    }

    if (context->getMode() != CompilerMode::Script &&
        m_identifierDefinition.identifierType != IdentifierType::FunctionVariable &&
        m_identifierDefinition.identifierType != IdentifierType::Constant &&
        m_identifierDefinition.identifierType != IdentifierType::DynamicVariable &&
        m_identifierDefinition.identifierType != IdentifierType::ContextVariable &&
        m_identifierDefinition.identifierType != IdentifierType::Parameter) {
        reportError(context, function, "Identifier '" + m_identifier + "' is not supported by the interpreter");
        return;
    }

    if (context->getMode() != CompilerMode::Script &&
        m_identifierDefinition.identifierType == IdentifierType::DynamicVariable &&
        !context->getRuntime()->canGetDynamicVariable(m_identifierDefinition.dynamicVariableId)) {
        reportError(context, function, "Dynamic variable '" + m_identifier + "' cannot be read");
        return;
    }

    if (context->isHubInitializer() &&
        (m_identifierDefinition.identifierType == IdentifierType::FunctionVariable ||
         m_identifierDefinition.identifierType == IdentifierType::Parameter)) {
        reportError(context, function, "Hub initializers cannot read script local '" + m_identifier + "'");
        return;
    }

    if (m_identifierDefinition.identifierType == IdentifierType::Constant) {
        RuntimeValue constantValue;
        context->getRuntime()->getConstantValue(m_identifierDefinition.constantId, constantValue);
        const auto* type = m_identifierDefinition.valueType;
        if (type->needsFree() && !type->isString()) {
            setType(type);
        } else {
            setResolved(constantValue, type);
        }
        return;
    }

    if (m_identifierDefinition.identifierType == IdentifierType::FunctionVariable &&
        context->getMode() == CompilerMode::Script) {
        function->registerVariable(m_identifier, m_identifierDefinition.valueType);
    }

    if (m_identifierDefinition.identifierType == IdentifierType::Parameter &&
        m_identifierDefinition.valueType == nullptr) {
        reportError(context, function, "Unknown type for parameter: " + m_identifier);
        return;
    }

    switch (m_identifierDefinition.identifierType) {
    case IdentifierType::FunctionVariable:
        m_writable = true;
        break;
    case IdentifierType::ContextVariable:
        m_writable = true;
        break;

    case IdentifierType::DynamicVariable:
        m_writable = context->getRuntime()->canSetDynamicVariable(m_identifierDefinition.dynamicVariableId);
        break;

    default:
        m_writable = false;
        break;
    }

    setType(m_identifierDefinition.valueType);
}

void IdentifierExpression::onCompile(Compiler* compiler, ScriptCallableBase* /*function*/, const ExpressionTarget target) {
    switch (m_identifierDefinition.identifierType) {
        case IdentifierType::Constant:
            compiler->addLoadConstant(m_identifierDefinition.constantId, target);
            break;

        case IdentifierType::FunctionVariable:
            compiler->addLoadFunctionVariable(m_identifierDefinition.variableId, m_identifierDefinition.valueType, target);
            break;

        case IdentifierType::DynamicVariable:
            compiler->addLoadDynamicVariable(m_identifierDefinition.dynamicVariableId, m_identifierDefinition.valueType, target);
        break;

        case IdentifierType::ContextVariable:
            compiler->addLoadContextVariable(
                m_identifierDefinition.contextVariableId,
                m_identifierDefinition.valueType,
                target
            );

            compiler->getRuntime()->setContextVariableRead(m_identifierDefinition.contextVariableId, true);
            break;

        case IdentifierType::Parameter:
            compiler->addLoadParameter(m_identifierDefinition.parameterId, m_identifierDefinition.valueType, target);
            break;

        default:
            raiseException("Invalid identifier type");
    }
}

bool IdentifierExpression::isWritable() const {
    return m_writable;
}

std::unique_ptr<TypedValue> IdentifierExpression::getInitialValue(Compiler* compiler) const {
    if (isParsed()) {
        return Expression::getInitialValue(compiler);
    }
    const auto* value = compiler->getHubInitialValue(m_identifier);
    return value != nullptr ? std::make_unique<TypedValue>(*value) : nullptr;
}

IdentifierExpression* IdentifierExpression::asIdentifierExpression() {
    return this;
}

const IdentifierExpression* IdentifierExpression::asIdentifierExpression() const {
    return this;
}

void IdentifierExpression::compileStore(Compiler* compiler) const {
    const auto* typeOpCodes = m_identifierDefinition.valueType->getTypeOpCodes();

    switch (m_identifierDefinition.identifierType) {
    case IdentifierType::FunctionVariable:
        compiler->addFreeValue(typeOpCodes->freeFunctionVariable, toNomadIndex(m_identifierDefinition.variableId.id));
        compiler->addOpCode(typeOpCodes->setFunctionVariable);
        compiler->addId(m_identifierDefinition.variableId.id);
        break;

    case IdentifierType::DynamicVariable:
        compiler->addOpCode(typeOpCodes->setDynamicVariable);
        compiler->addId(m_identifierDefinition.dynamicVariableId.id);
        compiler->addFreeValue(typeOpCodes->freeResult);
        break;

    case IdentifierType::ContextVariable:
        compiler->addOpCode(typeOpCodes->setContextVariable);
        compiler->addId(m_identifierDefinition.contextVariableId.id);
        compiler->addFreeValue(typeOpCodes->freeResult);
        compiler->getRuntime()->setContextVariableWritten(m_identifierDefinition.contextVariableId, true);
        break;

    default:
        compiler->reportInternalError("Cannot store native receiver '" + m_identifier + "'");
    }
}

void IdentifierExpression::evaluateStore(InterpreterContext& context, const RuntimeValue& value) const {
    auto* runtime = context.getRuntime();
    const auto* type = m_identifierDefinition.valueType;

    switch (m_identifierDefinition.identifierType) {
    case IdentifierType::FunctionVariable:
        context.setVariable(m_identifier, type, value);
        break;

    case IdentifierType::DynamicVariable:
        runtime->setDynamicVariable(context.getVirtualMachine(), m_identifierDefinition.dynamicVariableId, value);
        break;

    case IdentifierType::ContextVariable:
        runtime->setContextVariableValue(m_identifierDefinition.contextVariableId, value);
        runtime->setContextVariableWritten(m_identifierDefinition.contextVariableId, true);
        break;

    default:
        raiseException("Cannot store native receiver '" + m_identifier + "'");
    }
}

void IdentifierExpression::onEvaluate(InterpreterContext& context) const {
    switch (m_identifierDefinition.identifierType) {
    case IdentifierType::Constant: {
        RuntimeValue value;
        context.getRuntime()->getConstantValue(m_identifierDefinition.constantId, value);
        context.setResult(getType(), value);
        break;
    }
    case IdentifierType::Parameter:
        context.setParameterResult(m_identifierDefinition.parameterId);
        break;
    case IdentifierType::FunctionVariable: {
        const auto* value = context.getVariableValueById(m_identifierDefinition.variableId);
        if (value == nullptr) {
            raiseException("Variable '" + m_identifier + "' has not been assigned a value");
        }
        const auto* actualType = context.getVariableTypeById(m_identifierDefinition.variableId);
        if (actualType != getType()) {
            raiseException("Variable '" + m_identifier + "' changed type after this expression was resolved");
        }
        context.setResult(actualType, *value);
        break;
    }

    case IdentifierType::DynamicVariable: {
        auto* runtime = context.getRuntime();
        const auto* type = runtime->getDynamicVariableType(m_identifierDefinition.dynamicVariableId);
        if (type == nullptr) {
            raiseException("Dynamic variable '" + m_identifier + "' has no type");
        }

        RuntimeValue value;
        runtime->getDynamicVariableValue(
            context.getVirtualMachine(),
            m_identifierDefinition.dynamicVariableId,
            value
        );

        // Getters may leave a string untouched when the value is unavailable.
        if (type->isString() && value.getObjectValue() == nullptr) {
            value.initStringValue();
        }

        // `setResult` copies the value, so the temporary is released here.
        context.setResult(type, value);
        type->freeValue(value);
        break;
    }

    case IdentifierType::ContextVariable: {
        auto* runtime = context.getRuntime();
        const auto contextId = runtime->getVariableContextIdByPrefix(m_identifier);
        if (contextId.isInvalid()) {
            raiseException("Unknown context for variable '" + m_identifier + "'");
        }

        const auto* type = m_identifierDefinition.valueType;
        if (type == nullptr) {
            raiseException("Variable '" + m_identifier + "' has not been assigned a value");
        }

        // The context owns the value, so it is read without taking ownership.
        RuntimeValue value;
        runtime->getContextVariableValue(m_identifierDefinition.contextVariableId, value);

        if (type->isString() && value.getObjectValue() == nullptr) {
            RuntimeValue emptyValue;
            emptyValue.initStringValue();
            context.setResult(type, emptyValue);
            emptyValue.freeObjectValue();
            break;
        }

        context.setResult(type, value);
        break;
    }

    default:
        raiseException("Identifier '" + m_identifier + "' is not supported by the interpreter");
    }
}

bool IdentifierExpression::parseCapture(const Compiler* compiler, ScriptCallableBase* function) {
    return resolveIdentifier(m_identifier, compiler, function, m_identifierDefinition);
}

bool getCapture(const NomadString& identifier, const ScriptCallableBase* function, Capture& capture) {
    const auto variableId = function->getVariableId(identifier);
    if (variableId.isValid()) {
        capture.variableId = variableId;
        capture.source = CaptureSource::Variable;
        capture.type = function->getVariableType(variableId);
        return true;
    }

    const auto parameterId = function->getParameterId(identifier);
    if (parameterId != NOMAD_INVALID_ID) {
        capture.parameterId = parameterId;
        capture.source = CaptureSource::Parameter;
        capture.type = function->getParameterType(parameterId);
        return true;
    }

    return false;
}

// Resolve captures in nested functions.
// This function assumes the identifier is not already a variable or identifier of the function.
bool parseCapture(const NomadString& identifier, const Compiler* compiler, ScriptCallableBase* function) {
    if (function->isNested() == false) {
        return false;
    }

    ScriptCallableBase* currentFunction = function;
    const Type* captureType;
    std::vector<ScriptCallableBase*> intermediateFunctions = { function };

    // Walk up the nested functions until the identifier is found.
    for (;;) {
        const auto parentId = currentFunction->getParentId();

        if (parentId.isInvalid()) {
            // We've gone past the root function and haven't found the identifier.
            return false;
        }

        auto parentFunction = compiler->getRuntime()->getScriptCallable(parentId);

        if (parentFunction == nullptr) {
            compiler->reportInternalError("Failed to retrieve parent function during capture resolution");
        }

        Capture capture;

        if (getCapture(identifier, parentFunction, capture)) {
            // Found a capturable parameter or variable.
            captureType = capture.type;

            break;
        }

        // Capture not found. Add to intermediate functions.
        intermediateFunctions.push_back(parentFunction);

        currentFunction = parentFunction;
    }

    // A later inference pass may establish the source type; do not permanently register an untyped capture.
    if (captureType == nullptr) {
        return false;
    }

    // We've found the capture. Walk back up and ensure the capture is propagated through all intermediate functions.
    for (const auto intermediateFunction : std::ranges::reverse_view(intermediateFunctions)) {
        const auto parentId = intermediateFunction->getParentId();

        if (parentId.isInvalid()) {
            throw NomadBug("Failed to retrieve parent function during capture propagation");
        }

        const auto parentFunction = compiler->getRuntime()->getScriptCallable(parentId);

        if (parentFunction == nullptr) {
            throw NomadBug("Failed to retrieve parent function during capture propagation");
        }

        Capture capture;

        if (getCapture(identifier, parentFunction, capture)) {
            if (capture.source == CaptureSource::Variable) {
                intermediateFunction->createCapture(capture.variableId, identifier, captureType);
            } else {
                intermediateFunction->createCapture(capture.parameterId, identifier, captureType);
            }
        } else {
            compiler->reportInternalError("Failed to retrieve capture propagation");
        }
    }

    return true;
}

bool resolveIdentifier(const NomadString& identifier, const Compiler* compiler, ScriptCallableBase* function, IdentifierDefinition& definition) {
    compiler->getIdentifierDefinition(identifier, function, definition);

    // Check for invalid identifier type.
    if (definition.identifierType == IdentifierType::Unknown) {
        if (parseCapture(identifier, compiler, function)) {
            compiler->getIdentifierDefinition(identifier, function, definition);

            return true;
        } else {
            return false;
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
// FileNameExpression
FileNameExpression::FileNameExpression(
    const Expression* parent,
    const NomadIndex line,
    const NomadIndex column,
    NomadString fileName
):
    PrimaryExpression(parent, line, column),
    m_fileName(std::move(fileName))
{
}

void FileNameExpression::onResolve(CompilerContext* /*context*/, ScriptCallableBase* /*function*/) {
    // Nothing to do...
}

void FileNameExpression::onCompile(Compiler* compiler, ScriptCallableBase* /*function*/, const ExpressionTarget target) {
    compiler->addLoadStringValue(m_fileName, target);
}

///////////////////////////////////////////////////////////////////////////////
// FunctionNameExpression
FunctionNameExpression::FunctionNameExpression(
    const Expression* parent,
    const NomadIndex line,
    const NomadIndex column,
    NomadString functionName
):
    PrimaryExpression(parent, line, column),
    m_functionName(std::move(functionName))
{
}

void FunctionNameExpression::onResolve(CompilerContext* /*context*/, ScriptCallableBase* /*function*/) {
    // Nothing to do...
}

void FunctionNameExpression::onCompile(Compiler* compiler, ScriptCallableBase* /*function*/, ExpressionTarget target) {
    compiler->addLoadStringValue(m_functionName, target);
}

///////////////////////////////////////////////////////////////////////////////
// LineNumberExpression
LineNumberExpression::LineNumberExpression(
    const Expression* parent,
    const NomadIndex line,
    const NomadIndex column
):
    PrimaryExpression(parent, line, column)
{
}

void LineNumberExpression::onResolve(CompilerContext* /*context*/, ScriptCallableBase* /*function*/) {
    // Nothing to do...
}

void LineNumberExpression::onCompile(Compiler* compiler, ScriptCallableBase* /*function*/, ExpressionTarget target) {
    const auto lineNumber = static_cast<NomadInteger>(getLine());

    compiler->addLoadIntegerValue(lineNumber, target);
}

} // namespace nomad
