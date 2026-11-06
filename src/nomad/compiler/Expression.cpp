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
#include <nomad/script/ScriptObject.hpp>
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
    if (m_type != nullptr && m_hasValue) {
        m_type->freeValue(&m_value);
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

void Expression::compileIntoLocal(
    Compiler* compiler,
    ScriptCallableBase* function,
    const VariableId destination
) {
    if (m_type == nullptr || m_type->isVoid() || function->getVariableType(destination) != m_type) {
        compiler->reportInternalError("Expression local destination requires a resolved matching value type");
    }
    onCompileIntoLocal(compiler, function, destination);
}

void Expression::onCompileIntoLocal(
    Compiler* compiler,
    ScriptCallableBase* function,
    const VariableId destination
) {
    if (m_type->isStruct()) {
        compiler->reportError("Struct expression local destination is not supported for this expression");
    }
    compile(compiler, function, ExpressionTarget::Result);
    const auto destinationOffset = function->getVariableOffset(destination);
    m_type->emitFreeFunctionVariable(*compiler, destinationOffset);
    m_type->emitStoreFunctionVariable(*compiler, destinationOffset);
}

bool Expression::supportsReturnDestination() const {
    return false;
}

void Expression::compileIntoReturn(Compiler* compiler, ScriptCallableBase* function) {
    if (function == nullptr || !supportsReturnDestination() || m_type == nullptr ||
        !m_type->isStruct() || function->getReturnType() != m_type) {
        compiler->reportInternalError("Expression return destination requires a supported matching struct result");
    }
    onCompileIntoReturn(compiler, function);
}

void Expression::onCompileIntoReturn(Compiler* compiler, ScriptCallableBase*) {
    compiler->reportInternalError("Expression does not support a struct return destination");
}

bool Expression::isWritable() const {
    return false;
}

std::optional<StorageLocation> Expression::getStorageLocation() const {
    return std::nullopt;
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

InterpreterValue Expression::evaluateAssignment(InterpreterContext&, const Expression*) const {
    raiseException("Invalid assignment target");
}

InterpreterValue Expression::evaluate(InterpreterContext& context) const {
    if (context.hasError()) {
        raiseException(*context.getError());
    }
    if (isParsed()) {
        return InterpreterValue(m_type, m_value);
    }
    if (m_type == nullptr) {
        raiseException("Expression has not been resolved");
    }
    auto value = onEvaluate(context);
    if (context.hasError()) {
        raiseException(*context.getError());
    }
    if (!value.getType()->sameType(m_type)) {
        raiseException("Interpreter expression produced a value with an unexpected type");
    }
    return value;
}

InterpreterValue Expression::onEvaluate(InterpreterContext& /*context*/) const {
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

std::unique_ptr<InterpreterValue> Expression::getInitialValue(Compiler* /*compiler*/) const {
    return isParsed() ? std::make_unique<InterpreterValue>(getType(), getValue()) : nullptr;
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
    m_type->copyValue(&value, &m_value);

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

std::unique_ptr<InterpreterValue> UnaryExpression::getInitialValue(Compiler* compiler) const {
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
    auto result = std::make_unique<InterpreterValue>(getType(), value);
    getType()->freeValue(&value);
    return result;
}

InterpreterValue UnaryExpression::onEvaluate(InterpreterContext& context) const {
    const auto operand = m_expression->evaluate(context);
    RuntimeValue result;
    if (!context.getRuntime()->foldUnary(m_unaryOperator, operand.getType(), operand.getValue(), result)) {
        raiseException("Cannot evaluate unary operator '" + toString(m_unaryOperator) + "'");
    }

    return InterpreterValue(getType(), result);
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

InterpreterValue DynamicFormatExpression::onEvaluate(InterpreterContext& context) const {
    const auto value = m_expression->evaluate(context);
    const auto format = value.getType()->toString(value.getValue());
    const auto result = formatDynamicString(format, context, context.getCallable()->getPath(), getLine(), getColumn());
    RuntimeValue formatted;
    formatted.setStringValue(result.text);
    try {
        InterpreterValue formattedValue(getType(), formatted);
        getType()->freeValue(&formatted);
        return formattedValue;
    } catch (...) {
        getType()->freeValue(&formatted);
        throw;
    }
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

std::unique_ptr<InterpreterValue> BinaryExpression::getInitialValue(Compiler* compiler) const {
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
    auto result = std::make_unique<InterpreterValue>(getType(), value);
    getType()->freeValue(&value);
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

InterpreterValue BinaryExpression::onEvaluate(InterpreterContext& context) const {
    const auto lhs = m_left->evaluate(context);

    // Resolution guarantees logical operators have boolean operands.
    if (m_binaryOperator == BinaryOperator::AndAnd || m_binaryOperator == BinaryOperator::PipePipe) {
        const auto lhsValue = lhs.getValue().getBooleanValue();
        if ((m_binaryOperator == BinaryOperator::AndAnd && !lhsValue) ||
            (m_binaryOperator == BinaryOperator::PipePipe && lhsValue)) {
            return InterpreterValue(getType(), RuntimeValue{lhsValue});
        }
    }

    const auto rhs = m_right->evaluate(context);
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

    return InterpreterValue(getType(), result);
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
    if (getType()->isStruct()) {
        compiler->reportError("Struct function calls require a local destination");
    }
    if (const auto* native = m_function->asNativeFunction()) {
        compiler->addNativeFunctionCall(native->getId(), function, &m_arguments);
    } else if (const auto* script = m_function->asScriptFunction()) {
        compiler->addScriptFunctionCall(script->getId(), function, &m_arguments);
    } else {
        raiseException("Unsupported implementation of function '" + m_name + "'");
    }

    compiler->addMoveResult(getType(), target);
}

void FunctionCallExpression::onCompileIntoLocal(
    Compiler* compiler,
    ScriptCallableBase* function,
    const VariableId destination
) {
    if (!getType()->isStruct()) {
        Expression::onCompileIntoLocal(compiler, function, destination);
        return;
    }
    if (m_function == nullptr) {
        raiseException("Function call to '" + m_name + "' has not been resolved");
    }
    compiler->addFunctionCallIntoLocal(m_function->getId(), function, &m_arguments, destination);
}

void FunctionCallExpression::addArgument(std::unique_ptr<Argument> argument) {
    m_arguments.add(std::move(argument));
}

bool FunctionCallExpression::supportsReturnDestination() const {
    return getType() != nullptr && getType()->isStruct();
}

void FunctionCallExpression::onCompileIntoReturn(Compiler* compiler, ScriptCallableBase* function) {
    if (m_function == nullptr) {
        raiseException("Function call to '" + m_name + "' has not been resolved");
    }
    compiler->addFunctionCallIntoReturn(m_function->getId(), function, &m_arguments);
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

InterpreterValue FunctionCallExpression::onEvaluate(InterpreterContext& context) const {
    if (m_function == nullptr) {
        raiseException("Function call to '" + m_name + "' has not been resolved");
    }
    const auto evaluatedArguments = m_arguments.evaluate(context);
    return context.getInterpreter().callFunction(
        m_function->getId(),
        evaluatedArguments
    );
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
    return m_storage.has_value();
}

std::optional<StorageLocation> NativeMemberExpression::getStorageLocation() const {
    return m_storage;
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
    m_storage.reset();
    m_structReceiver.reset();
    if (!m_receiver->resolve(context, function)) {
        return false;
    }
    if (m_receiver->getType() == nullptr) {
        reportError(context, function, "Cannot determine receiver type for member '" + m_qualifiedName + "'");
        return false;
    }

    auto* runtime = context->getRuntime();
    m_memberId = runtime->getNativeMemberId(m_receiver->getType(), m_memberName);

    const auto* owner = m_receiver->getType()->asClassType();
    const auto* composite = m_receiver->getType()->asCompositeType();
    if (runtime->getMemberType(m_memberId) == MemberType::Method ||
        (composite != nullptr && std::ranges::any_of(composite->getMethods(),
            [&](const auto* method) { return method->getName() == m_memberName; }))) {
        m_memberType = MemberType::Method;
        return true;
    }

    if (runtime->getMemberType(m_memberId) == MemberType::Unknown) {
        if (m_receiver->getType()->isStruct()) {
            const auto* property = composite->getProperty(m_memberName);
            if (assignment && context->getMode() == CompilerMode::Script) {
                const auto* identifier = dynamic_cast<const IdentifierExpression*>(m_receiver.get());
                if (identifier != nullptr && identifier->getIdentifier() == "this" &&
                    context->getCompiler()->getReceiverType(function) == composite) {
                    property = context->getCompiler()->resolveStructField(
                        context, function, m_memberName, valueType,
                        runtime->getConstructor(function->getId()) != nullptr, getLine(), getColumn());
                    if (property == nullptr) {
                        return false;
                    }
                }
            }
            if (property == nullptr) {
                reportError(context, function, "Unknown struct property '" + m_memberName +
                    "' on type '" + composite->getTypeName() + "'");
                return false;
            }
            if (m_hasExplicitArguments && m_arguments.getArgumentCount() != 0) {
                reportError(context, function, "Struct property '" + m_qualifiedName + "' does not take arguments");
                return false;
            }
            if (context->getMode() == CompilerMode::Script && function != nullptr) {
                auto storage = m_receiver->getStorageLocation();
                if (!storage.has_value()) {
                    if (m_receiverVariable.isInvalid()) {
                        m_receiverVariable = function->registerVariable(
                            "$propertyReceiver" + std::to_string(function->getVariableCount()), m_receiver->getType());
                    }
                    storage = StorageLocation(m_receiver.get(), m_receiverVariable);
                }
                m_storage = storage->getProperty(*property);
            } else if (assignment) {
                reportError(context, function, "Struct property assignments are not supported by the interpreter");
                return false;
            }
            m_memberId = property->getId();
            m_memberType = MemberType::Property;
            return true;
        }
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
                if (context->getMode() == CompilerMode::Script && function != nullptr &&
                    property->getValueType() != nullptr && property->getValueType()->isStruct() &&
                    property->asNativeProperty() == nullptr) {
                    m_storage = StorageLocation(m_receiver.get(), property);
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
        reportError(context, function, "Cannot assign to method '" + m_qualifiedName + "'");
        return false;
    }

    const auto info = runtime->getNativePropertyInfo(m_memberId);
    if (!info) {
        const auto* property = runtime->getProperty(std::get<PropertyId>(m_memberId));
        if (property == nullptr) {
            context->getCompiler()->reportInternalError("Invalid property: " + m_qualifiedName);
        }
        setType(property->getValueType());
        if (m_storage.has_value() && getType()->isStruct() && m_stagingVariable.isInvalid()) {
            m_stagingVariable = function->registerVariable(
                "$property" + std::to_string(function->getVariableCount()), getType());
        }
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
        auto& arguments = m_arguments.getArguments();
        std::vector<const Type*> argumentTypes;
        argumentTypes.reserve(arguments.size());
        for (const auto& argument : arguments) {
            argumentTypes.push_back(argument->resolveOverloadType(context, function));
        }
        const auto resolution = runtime->resolveMethod(m_receiver->getType(), m_memberName, argumentTypes);
        if (resolution.status != ResolveStatus::Resolved) {
            reportError(context, function, runtime->describeResolveFailure(m_qualifiedName, resolution));
            return;
        }
        m_method = runtime->getMethod(resolution.id);
        m_memberId = resolution.id;
        const auto* script = m_method->asScriptCallable();
        const auto* returnType = m_method->getReturnType();
        auto* compiler = context->getCompiler();
        if (script != nullptr && context->getMode() == CompilerMode::Script && function != nullptr) {
            compiler->addFunctionCallDependency(function->getId(), script->getId());
        }
        if (returnType == nullptr) {
            const auto recursive = script != nullptr && function != nullptr &&
                compiler->isRecursiveFunctionCall(function->getId(), script->getId());
            reportError(context, function, recursive
                ? "Cannot infer return type of recursive method '" + m_qualifiedName +
                    "': it needs a `return` whose type does not depend on a recursive call"
                : "Cannot determine return type of method '" + m_qualifiedName + "'");
            return;
        }
        if (script != nullptr && context->getMode() != CompilerMode::Script &&
            script->getFunctionStart() == NOMAD_INVALID_INDEX) {
            reportError(context, function, "Method '" + m_qualifiedName + "' has not been compiled");
            return;
        }
        if (m_receiver->getType()->isStruct()) {
            if (context->getMode() != CompilerMode::Script || function == nullptr) {
                reportError(context, function, "Struct method calls are not supported by the interpreter");
                return;
            }
            m_structReceiver = m_receiver->getStorageLocation();
            if (!m_structReceiver.has_value()) {
                if (m_receiverVariable.isInvalid()) {
                    m_receiverVariable = function->registerVariable(
                        "$methodReceiver" + std::to_string(function->getVariableCount()), m_receiver->getType());
                }
                IdentifierDefinition receiver;
                receiver.identifierType = IdentifierType::FunctionVariable;
                receiver.variableId = m_receiverVariable;
                receiver.valueType = m_receiver->getType();
                m_structReceiver = StorageLocation(receiver);
            }
        }
        for (NomadIndex index = 0; index < arguments.size(); ++index) {
            arguments[index]->setType(m_method->getParameters()[index].type);
        }

        if (!m_arguments.resolve(context, function)) {
            return;
        }

        if (returnType->isVoid() && !m_allowVoid) {
            reportError(context, function,
                        "Cannot use method '" + m_qualifiedName +
                            "' in an expression because it does not return a value");
            return;
        }

        setType(returnType);
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
    if (getType()->isStruct() && (!m_storage.has_value() || target != ExpressionTarget::Stack)) {
        compiler->reportError("Struct member values require a local destination or stack copies");
    }
    if (m_storage.has_value()) {
        m_storage->emitLoad(*compiler, function, target);
        return;
    }
    if (m_memberType == MemberType::Method && receiverType->isStruct()) {
        if (m_receiverVariable.isValid()) {
            m_receiver->compileIntoLocal(compiler, function, m_receiverVariable);
        }
        compiler->addStructMethodCall(m_method->getId(), function, &m_arguments, *m_structReceiver);
        if (m_receiverVariable.isValid()) {
            receiverType->emitFreeFunctionVariable(*compiler, function->getVariableOffset(m_receiverVariable));
        }
        if (!getType()->isVoid()) {
            compiler->addMoveResult(getType(), target);
        }
        return;
    }

    if (m_memberType != MemberType::Property) {
        compiler->addOpCode(OpCodes::op_push_this);
    }
    m_receiver->compile(compiler, function, ExpressionTarget::Intermediate);

    if (m_memberType == MemberType::Property) {
        compiler->addLoadProperty(std::get<PropertyId>(m_memberId));
        receiverType->emitFreeIntermediate(*compiler);
        compiler->addMoveResult(getType(), target);
        return;
    }

    m_arguments.setReceiverType(receiverType);
    if (m_method->asScriptMethod() != nullptr) {
        compiler->addScriptMethodCall(std::get<CallableId>(m_memberId), function, &m_arguments);
    } else {
        compiler->addNativeMethodCall(std::get<CallableId>(m_memberId), function, &m_arguments);
    }

    if (!m_method->getReturnType()->isVoid()) {
        compiler->addMoveResult(m_method->getReturnType(), target);
    }
}

void NativeMemberExpression::onCompileIntoLocal(
    Compiler* compiler,
    ScriptCallableBase* function,
    const VariableId destination
) {
    if (!getType()->isStruct()) {
        Expression::onCompileIntoLocal(compiler, function, destination);
        return;
    }
    if (m_storage.has_value()) {
        m_storage->emitCopyToLocal(*compiler, function, destination);
        return;
    }
    if (m_memberType != MemberType::Method || !m_receiver->getType()->isStruct()) {
        compiler->reportError("Struct local destinations currently require a struct method receiver");
    }
    if (m_receiverVariable.isValid()) {
        m_receiver->compileIntoLocal(compiler, function, m_receiverVariable);
    }
    compiler->addStructMethodCall(m_method->getId(), function, &m_arguments, *m_structReceiver, destination);
    if (m_receiverVariable.isValid()) {
        m_receiver->getType()->emitFreeFunctionVariable(*compiler, function->getVariableOffset(m_receiverVariable));
    }
}

bool NativeMemberExpression::supportsReturnDestination() const {
    return m_memberType == MemberType::Method && m_structReceiver.has_value() &&
        getType() != nullptr && getType()->isStruct();
}

void NativeMemberExpression::onCompileIntoReturn(Compiler* compiler, ScriptCallableBase* function) {
    if (m_receiverVariable.isValid()) {
        m_receiver->compileIntoLocal(compiler, function, m_receiverVariable);
    }
    compiler->addStructMethodCallIntoReturn(m_method->getId(), function, &m_arguments, *m_structReceiver);
    if (m_receiverVariable.isValid()) {
        m_receiver->getType()->emitFreeFunctionVariable(*compiler, function->getVariableOffset(m_receiverVariable));
    }
}

InterpreterValue NativeMemberExpression::onEvaluate(InterpreterContext& context) const {
    if (m_memberType == MemberType::Unknown) {
        raiseException("Native member has not been resolved: " + m_qualifiedName);
    }
    if (m_memberType == MemberType::Property &&
        (m_receiver->getType()->isStruct() || getType()->isStruct())) {
        const auto* property = context.getRuntime()->getProperty(std::get<PropertyId>(m_memberId));
        if (property == nullptr) {
            raiseException("Invalid property getter: " + m_qualifiedName);
        }
        if (property->asNativeProperty() == nullptr) {
            const auto receiver = m_receiver->evaluate(context);
            if (receiver.getType()->isStruct()) {
                return context.getInterpreter().getProperty(receiver, property->getId());
            }
            const auto* object = dynamic_cast<const ScriptObject*>(receiver.getValue().getObjectValue());
            if (object == nullptr || object->getClassType() != property->getOwnerType()) {
                context.getVirtualMachine()->fault("Stored property receiver has no matching field storage");
            }
            return InterpreterValue(getType(), object->getFieldStorage(
                property->getOwnerType()->getFieldIndex(property->getId())));
        }
    }
    if (m_receiver->getType()->isStruct() || getType()->isStruct()) {
        raiseException("Struct member values are not supported by the interpreter");
    }
    const auto receiver = m_receiver->evaluate(context);
    if (m_memberType == MemberType::Method) {
        const auto evaluatedArguments = m_arguments.evaluate(context);
        return context.getInterpreter().callMethod(
            receiver,
            m_method->getId(),
            evaluatedArguments
        );
    }

    auto* runtime = context.getRuntime();
    if (m_memberType == MemberType::Property) {
        const auto* property = runtime->getProperty(std::get<PropertyId>(m_memberId));
        if (property == nullptr) {
            raiseException("Invalid property getter: " + m_qualifiedName);
        }
        const auto* valueType = property->getValueType();
        RuntimeValue result;

        try {
            context.getVirtualMachine()->getProperty(std::get<PropertyId>(m_memberId), receiver.getValue(), result);
            InterpreterValue value(valueType, result);
            valueType->freeValue(&result);
            return value;
        } catch (...) {
            valueType->freeValue(&result);
            throw;
        }
    }

    raiseException("Unsupported interpreter member: " + m_qualifiedName);
}

void NativeMemberExpression::compileAssignment(Compiler* compiler, ScriptCallableBase* function, Expression* expression) {
    const auto* receiverType = m_receiver->getType();
    const auto* propertyType = getType();
    if (m_storage.has_value()) {
        m_storage->emitAssignment(*compiler, function, *expression, m_stagingVariable);
        return;
    }
    m_receiver->compile(compiler, function, ExpressionTarget::Intermediate);
    compiler->addPushIntermediate(receiverType);
    expression->compile(compiler, function, ExpressionTarget::Result);
    compiler->addPopIntermediate(receiverType);
    compiler->addStoreProperty(std::get<PropertyId>(m_memberId));
    propertyType->emitFreeResult(*compiler);
    receiverType->emitFreeIntermediate(*compiler);
}

InterpreterValue NativeMemberExpression::evaluateAssignment(
    InterpreterContext& context,
    const Expression* expression
) const {
    if (m_receiver->getType()->isStruct() || getType()->isStruct()) {
        raiseException("Struct member values are not supported by the interpreter");
    }
    const auto receiver = m_receiver->evaluate(context);
    auto assignedValue = expression->evaluate(context);

    const auto* property = context.getRuntime()->getProperty(std::get<PropertyId>(m_memberId));
    if (property == nullptr) {
        raiseException("Invalid property setter: " + m_qualifiedName);
    }
    context.getVirtualMachine()->setProperty(std::get<PropertyId>(m_memberId), receiver.getValue(),
        assignedValue.getValue());
    return InterpreterValue(property->getValueType(), assignedValue.getValue());
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
    compiler->addOpCode(OpCodes::op_push_this);
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

InterpreterValue IndexerExpression::onEvaluate(InterpreterContext& context) const {
    return evaluateIndexer(context);
}

InterpreterValue IndexerExpression::evaluateAssignment(InterpreterContext& context, const Expression* expression) const {
    return evaluateIndexer(context, expression);
}

InterpreterValue IndexerExpression::evaluateIndexer(InterpreterContext& context, const Expression* assigned) const {
    const auto receiver = m_receiver->evaluate(context);
    std::vector<InterpreterValue> arguments;
    arguments.reserve(m_indexes.size() + (assigned != nullptr ? 1 : 0));
    if (assigned != nullptr) {
        arguments.push_back(assigned->evaluate(context));
    }
    for (const auto& index : std::ranges::reverse_view(m_indexes)) {
        arguments.push_back(index->evaluate(context));
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
            return arguments.front();
        }
        InterpreterValue result(getType(), machine->getResult());
        getType()->freeValue(&machine->getResult());
        return result;
    } catch (...) {
        if (assigned == nullptr) {
            getType()->freeValue(&machine->getResult());
        }
        throw;
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
    const Type* classType,
    ArgumentList arguments
):
    Expression(parent, line, column),
    m_constructorName(std::move(constructorName)),
    m_classType(classType),
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
    if (m_classType == nullptr && context->getErrorCount() != errorCount) {
        return;
    }

    const auto* classType = m_classType;
    if (classType == nullptr) {
        const auto inference = runtime->getTypeManager().inferConstructorTypeArguments(m_constructorName, argumentTypes);
        if (!inference.isResolved()) {
            reportError(context, function, inference.error);
            return;
        }
        for (const auto* argument : inference.typeArguments) {
            if (argument->isStruct()) {
                reportError(context, function, "Struct type argument '" + argument->getTypeName() +
                    "' is not supported for constructor '" + m_constructorName + "'");
                return;
            }
        }
        classType = runtime->getTypeManager().getParameterizedType(m_constructorName, inference.typeArguments);
        if (classType == nullptr) {
            reportError(context, function, "Cannot specialize inferred constructor '" + m_constructorName + "'");
            return;
        }
    }
    const auto resolution = runtime->resolveConstructor(classType, argumentTypes);
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
        setType(selected->getConstructedType());
    }
}

bool ConstructorExpression::isTerminal() const {
    return false;
}

void ConstructorExpression::onCompile(Compiler* compiler, ScriptCallableBase* function, const ExpressionTarget target) {
    if (m_constructor == nullptr) {
        raiseException("Constructor call to '" + m_constructorName + "' has not been resolved");
    }
    if (getType()->isStruct()) {
        compiler->reportError("Struct construction requires a local destination");
    }
    if (m_constructor->asNativeConstructor() != nullptr) {
        compiler->addNativeObjectCreation(m_constructor->getId(), function, &m_arguments);
    } else {
        compiler->addScriptObjectCreation(m_constructor->getId(), function, &m_arguments);
    }
    compiler->addMoveResult(getType(), target);
}

void ConstructorExpression::onCompileIntoLocal(
    Compiler* compiler,
    ScriptCallableBase* function,
    const VariableId destination
) {
    if (!getType()->isStruct()) {
        Expression::onCompileIntoLocal(compiler, function, destination);
        return;
    }
    if (m_constructor == nullptr) {
        raiseException("Constructor call to '" + m_constructorName + "' has not been resolved");
    }
    compiler->addStructCreationIntoLocal(m_constructor->getId(), function, &m_arguments, destination);
}

bool ConstructorExpression::supportsReturnDestination() const {
    return getType() != nullptr && getType()->isStruct();
}

void ConstructorExpression::onCompileIntoReturn(Compiler* compiler, ScriptCallableBase* function) {
    if (m_constructor == nullptr) {
        raiseException("Constructor call to '" + m_constructorName + "' has not been resolved");
    }
    compiler->addStructCreationIntoReturn(m_constructor->getId(), function, &m_arguments);
}

InterpreterValue ConstructorExpression::onEvaluate(InterpreterContext& context) const {
    if (m_constructor == nullptr) {
        raiseException("Constructor call to '" + m_constructorName + "' has not been resolved");
    }
    const auto evaluatedArguments = m_arguments.evaluate(context);
    return context.getInterpreter().construct(
        m_constructor->getId(),
        evaluatedArguments
    );
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
    stringType->freeValue(&value);
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

InterpreterValue ConstantValueExpression::onEvaluate(InterpreterContext& context) const {
    RuntimeValue value;
    context.getRuntime()->getConstantValue(m_constantId, value);
    return InterpreterValue(getType(), value);
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
        if (context->getCompiler()->isFieldInitializer(function) &&
            (segment.text == "this" || segment.text.starts_with("this."))) {
            reportError(context, function, "Field initializers cannot use or capture 'this'");
            return false;
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
            context, compiler->getScriptId(function->getId()), name,
            context->isHubInitializer() ? CompilationCallableTarget::HubInitializer : CompilationCallableTarget::Function
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
        compiler->setFunctionBody(functionId, std::move(node));
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

InterpreterValue FormatStringLiteral::onEvaluate(InterpreterContext& context) const {
    const auto result = formatDynamicString(m_formatString, context, context.getCallable()->getPath(),
        getLine(), getColumn());
    RuntimeValue value;
    value.setStringValue(result.text);
    try {
        InterpreterValue formattedValue(getType(), value);
        getType()->freeValue(&value);
        return formattedValue;
    } catch (...) {
        getType()->freeValue(&value);
        throw;
    }
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
    if (context->getCompiler()->isFieldInitializer(function) &&
        (m_identifier == "this" || m_identifier.starts_with("this."))) {
        reportError(context, function, "Field initializers cannot use or capture 'this'");
        return;
    }
    if (context->getCompiler()->getReceiverType(function) != nullptr && m_identifier.starts_with("this.")) {
        const auto* owner = context->getCompiler()->getReceiverType(function)->asClassType();
        if (owner == nullptr) {
            reportError(context, function, "Struct property access is not yet supported");
            return;
        }
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

            if (m_identifierDefinition.identifierType != IdentifierType::Parameter) {
                compiler->reportInternalError("Capture definition should exist.");
            }
            break;

        case IdentifierType::ScriptFunction:
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
        m_identifierDefinition.identifierType != IdentifierType::Receiver &&
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
    case IdentifierType::Parameter:
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

void IdentifierExpression::onCompile(Compiler* compiler, ScriptCallableBase* function, const ExpressionTarget target) {
    switch (m_identifierDefinition.identifierType) {
        case IdentifierType::Receiver:
            compiler->addLoadReceiver(function, target);
            break;
        case IdentifierType::Constant:
            compiler->addLoadConstant(m_identifierDefinition.constantId, target);
            break;

        case IdentifierType::FunctionVariable:
            compiler->addLoadFunctionVariable(
                function->getVariableOffset(m_identifierDefinition.variableId), m_identifierDefinition.valueType, target);
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
            compiler->addLoadParameter(function->getParameterOffset(m_identifierDefinition.parameterId),
                m_identifierDefinition.valueType, target);
            break;

        default:
            raiseException("Invalid identifier type");
    }
}

bool IdentifierExpression::isWritable() const {
    return m_writable;
}

std::optional<StorageLocation> IdentifierExpression::getStorageLocation() const {
    if (getType() == nullptr ||
        (m_identifierDefinition.identifierType != IdentifierType::FunctionVariable &&
         m_identifierDefinition.identifierType != IdentifierType::Parameter &&
         m_identifierDefinition.identifierType != IdentifierType::Receiver)) {
        return std::nullopt;
    }
    return StorageLocation(m_identifierDefinition);
}

void IdentifierExpression::onCompileIntoLocal(
    Compiler* compiler,
    ScriptCallableBase* function,
    const VariableId destination
) {
    if (!getType()->isStruct()) {
        Expression::onCompileIntoLocal(compiler, function, destination);
        return;
    }
    const auto storage = getStorageLocation();
    if (!storage.has_value()) {
        compiler->reportError("Struct local destinations currently support only local variable or parameter sources");
    }
    storage->emitCopyToLocal(*compiler, function, destination);
}

std::unique_ptr<InterpreterValue> IdentifierExpression::getInitialValue(Compiler* compiler) const {
    if (isParsed()) {
        return Expression::getInitialValue(compiler);
    }
    const auto* value = compiler->getHubInitialValue(m_identifier);
    return value != nullptr ? std::make_unique<InterpreterValue>(*value) : nullptr;
}

IdentifierExpression* IdentifierExpression::asIdentifierExpression() {
    return this;
}

const IdentifierExpression* IdentifierExpression::asIdentifierExpression() const {
    return this;
}

void IdentifierExpression::compileStore(Compiler* compiler, const ScriptCallableBase* function) const {
    const auto* type = m_identifierDefinition.valueType;

    switch (m_identifierDefinition.identifierType) {
    case IdentifierType::Parameter:
        compiler->addStoreParameter(function->getParameterOffset(m_identifierDefinition.parameterId),
            m_identifierDefinition.valueType);
        break;
    case IdentifierType::FunctionVariable:
        m_identifierDefinition.valueType->emitFreeFunctionVariable(
            *compiler,
            function->getVariableOffset(m_identifierDefinition.variableId)
        );
        type->emitStoreFunctionVariable(*compiler, function->getVariableOffset(m_identifierDefinition.variableId));
        break;

    case IdentifierType::DynamicVariable:
        type->emitStoreDynamicVariable(*compiler, m_identifierDefinition.dynamicVariableId.id);
        m_identifierDefinition.valueType->emitFreeResult(*compiler);
        break;

    case IdentifierType::ContextVariable:
        type->emitStoreContextVariable(*compiler, m_identifierDefinition.contextVariableId.id);
        m_identifierDefinition.valueType->emitFreeResult(*compiler);
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
    case IdentifierType::Parameter:
        context.setParameter(m_identifierDefinition.parameterId, value);
        break;
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

InterpreterValue IdentifierExpression::onEvaluate(InterpreterContext& context) const {
    switch (m_identifierDefinition.identifierType) {
    case IdentifierType::Receiver: {
        if (getType()->isStruct()) {
            return InterpreterValue(getType(), context.getReceiverStorage());
        }
        RuntimeValue receiver;
        receiver.setObjectValue(context.getVirtualMachine()->getThis());
        return InterpreterValue(getType(), receiver);
    }
    case IdentifierType::Constant: {
        RuntimeValue value;
        context.getRuntime()->getConstantValue(m_identifierDefinition.constantId, value);
        return InterpreterValue(getType(), value);
    }
    case IdentifierType::Parameter:
        return InterpreterValue(getType(), context.getParameterStorage(m_identifierDefinition.parameterId));
    case IdentifierType::FunctionVariable: {
        const auto* actualType = context.getVariableTypeById(m_identifierDefinition.variableId);
        if (actualType == nullptr) {
            raiseException("Variable '" + m_identifier + "' has not been assigned a value");
        }
        if (actualType != getType()) {
            raiseException("Variable '" + m_identifier + "' changed type after this expression was resolved");
        }
        return InterpreterValue(actualType, context.getVariableStorageById(m_identifierDefinition.variableId));
    }

    case IdentifierType::DynamicVariable: {
        auto* runtime = context.getRuntime();
        const auto* type = runtime->getDynamicVariableType(m_identifierDefinition.dynamicVariableId);
        if (type == nullptr) {
            raiseException("Dynamic variable '" + m_identifier + "' has no type");
        }

        RuntimeValue value;
        try {
            runtime->getDynamicVariableValue(
                context.getVirtualMachine(),
                m_identifierDefinition.dynamicVariableId,
                value
            );
            // Getters may leave a string untouched when the value is unavailable.
            if (type->isString() && value.getObjectValue() == nullptr) {
                value.initStringValue();
            }
            InterpreterValue result(type, value);
            type->freeValue(&value);
            return result;
        } catch (...) {
            type->freeValue(&value);
            throw;
        }
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
            try {
                InterpreterValue result(type, emptyValue);
                emptyValue.freeObjectValue();
                return result;
            } catch (...) {
                emptyValue.freeObjectValue();
                throw;
            }
        }

        return InterpreterValue(type, value);
    }

    default:
        raiseException("Identifier '" + m_identifier + "' is not supported by the interpreter");
    }
}

bool IdentifierExpression::parseCapture(const Compiler* compiler, ScriptCallableBase* function) {
    return resolveIdentifier(m_identifier, compiler, function, m_identifierDefinition);
}

bool getCapture(const NomadString& identifier, const ScriptCallableBase* function, Capture& capture) {
    if (identifier == "this" && function->getClassType() != nullptr) {
        capture.source = CaptureSource::Receiver;
        capture.type = function->getClassType();
        return true;
    }
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
    if (captureType->isStruct()) {
        compiler->reportError("Struct captures are not yet supported");
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
            } else if (capture.source == CaptureSource::Receiver) {
                intermediateFunction->createReceiverCapture(captureType);
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
