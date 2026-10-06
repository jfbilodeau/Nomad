// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/Argument.hpp>
#include <nomad/compiler/CallNode.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Expression.hpp>
#include <nomad/compiler/FormatStringCompiler.hpp>
#include <nomad/compiler/SyntaxTree.hpp>

#include <nomad/script/VirtualMachine.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/Function.hpp>

#include <algorithm>
#include <any>
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

NomadBoolean Expression::resolve(CompilerContext* context, Function* function) {
    if (isParsed()) {
        return true;
    }

    const auto errorCount = context->getErrorCount();

    onResolve(context, function);

    return context->getErrorCount() == errorCount;
}

void Expression::compile(Compiler* compiler, Function* function, const ExpressionTarget target) {
    if (isParsed()) {
        compiler->addLoadValue(m_type, m_value, target);
    } else {
        onCompile(compiler, function, target);
    }
}

bool Expression::canBorrow() const {
    return isParsed() && m_type->isString();
}

void Expression::compileBorrowed(Compiler* compiler, Function* function, const ExpressionTarget target) {
    if (!canBorrow()) {
        compiler->reportInternalError("BUG: Expression cannot lend a string reference");
    }

    if (isParsed()) {
        compiler->addLoadStringReference(m_value.getStringValue(), target);
    } else {
        onCompileBorrowed(compiler, function, target);
    }
}

void Expression::onCompileBorrowed(Compiler* compiler, Function* /*function*/, ExpressionTarget /*target*/) {
    compiler->reportInternalError("BUG: Expression does not implement borrowed compilation");
}

void Expression::evaluate(Interpreter& context) const {
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

void Expression::onEvaluate(Interpreter& /*context*/) const {
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


void UnaryExpression::onResolve(CompilerContext* context, Function* function) {
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

void UnaryExpression::onCompile(Compiler* compiler, Function* function, ExpressionTarget target) {
    m_expression->compile(compiler, function, target);

    auto opcode = compiler->getUnaryOperatorOpCodeId(m_unaryOperator, m_expression->getType());

    compiler->addOpCode(opcode);
}

void UnaryExpression::onEvaluate(Interpreter& context) const {
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

void BinaryExpression::onResolve(CompilerContext* context, Function* function) {
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
                lhsType->getTypeName() + " and " + rhsType->getTypeName()
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

void BinaryExpression::onCompile(Compiler* compiler, Function* function, const ExpressionTarget target) {
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

    const auto* stringRefType = compiler->getRuntime()->getStringRefType();

    if (stringRefType->acceptsArgument(m_left->getType()) && stringRefType->acceptsArgument(m_right->getType())) {
        // String operators borrow their operands. Operands that cannot be borrowed are evaluated into owned copies
        // on the stack, then released once the operator has run.
        const auto borrowLeft = m_left->canBorrow();
        const auto borrowRight = m_right->canBorrow();

        if (!borrowRight) {
            m_right->compile(compiler, function, ExpressionTarget::Stack);
        }

        if (!borrowLeft) {
            m_left->compile(compiler, function, ExpressionTarget::Stack);
        }

        const NomadIndex leftOffset = 0;
        const NomadIndex rightOffset = borrowLeft ? 0 : 1;

        if (borrowRight) {
            m_right->compileBorrowed(compiler, function, ExpressionTarget::Intermediate);
        } else {
            compiler->addOpCode(op_stringref_peek_i);
            compiler->addIndex(rightOffset);
        }

        if (borrowLeft) {
            m_left->compileBorrowed(compiler, function, ExpressionTarget::Result);
        } else {
            compiler->addOpCode(op_stringref_peek_r);
            compiler->addIndex(leftOffset);
        }

        const auto opCodeId = compiler->getBinaryOperatorOpCodeId(m_binaryOperator, m_left->getType(), m_right->getType());

        if (opCodeId == NOMAD_INVALID_ID) {
            raiseException("Invalid binary operator for strings");
        }

        compiler->addOpCode(opCodeId);

        const NomadIndex ownedCount = (borrowLeft ? 0 : 1) + (borrowRight ? 0 : 1);

        if (!borrowLeft) {
            compiler->addFreeValue(m_left->getType()->getTypeOpCodes()->freeStack, leftOffset);
        }

        if (!borrowRight) {
            compiler->addFreeValue(m_right->getType()->getTypeOpCodes()->freeStack, rightOffset);
        }

        if (ownedCount != 0) {
            compiler->addOpCode(OpCodes::op_pop_n);
            compiler->addIndex(ownedCount);
        }

        compiler->addMoveResult(getType(), target);

        return;
    }

    m_right->compile(compiler, function, ExpressionTarget::Intermediate);
    if (m_left->isTerminal() == false) {
        compiler->addPushIntermediate(m_right->getType());
    }
    m_left->compile(compiler, function, ExpressionTarget::Result);
    if (m_left->isTerminal() == false) {
        compiler->addPopIntermediate(m_right->getType());
    }

    const auto leftType = m_left->getType();
    const auto rightType = m_right->getType();

    const auto opCodeId = compiler->getBinaryOperatorOpCodeId(
        m_binaryOperator,
        leftType,
        rightType
    );

    if (opCodeId == NOMAD_INVALID_ID) {
        raiseException("Invalid binary operator for " + leftType->getTypeName() + " and " + rightType->getTypeName());
    }

    compiler->addOpCode(opCodeId);

    compiler->addMoveResult(getType(), target);
}

void BinaryExpression::onEvaluate(Interpreter& context) const {
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
// CallNativeFunctionExpression
CallNativeFunctionExpression::CallNativeFunctionExpression(
    const Expression* parent,
    const NomadIndex line,
    const NomadIndex column,
    NomadString name
):
    Expression(parent, line, column),
    m_name(std::move(name))
{
}

bool CallNativeFunctionExpression::isTerminal() const {
    return false;
}

void CallNativeFunctionExpression::onResolve(CompilerContext* context, Function* function) {
    auto* runtime = context->getRuntime();

    if (runtime->getCallableOverloadCount(m_name) > 1) {
        NomadString error;
        const auto selected = selectCallableOverload(context, function, m_name, &m_arguments, error);

        if (!selected.isValid()) {
            reportError(context, function, error);
            return;
        }

        m_nativeFunctionId = selected.id;
    } else {
        m_nativeFunctionId = runtime->getNativeFunctionId(m_name);
    }

    if (m_nativeFunctionId == NOMAD_INVALID_ID) {
        reportError(context, function, "Unknown native function '" + m_name + "'");
        return;
    }

    NativeFunctionDefinition nativeFunctionDefinition;
    runtime->getNativeFunctionDefinition(m_nativeFunctionId, nativeFunctionDefinition);

    if (nativeFunctionDefinition.returnType == nullptr || nativeFunctionDefinition.returnType->isVoid()) {
        reportError(context, function, "NativeFunction '" + m_name + "' does not return a value");
        return;
    }

    setType(nativeFunctionDefinition.returnType);

    (void)m_arguments.resolve(context, function);
}

void CallNativeFunctionExpression::onCompile(Compiler* compiler, Function* function, const ExpressionTarget target) {
    if (m_nativeFunctionId == NOMAD_INVALID_ID) {
        raiseException("Unknown native function '" + m_name + "'");
    }

    compiler->addNativeFunctionCall(m_nativeFunctionId, function, &m_arguments);

    compiler->addMoveResult(getType(), target);
}

void CallNativeFunctionExpression::onEvaluate(Interpreter& context) const {
    std::vector<TypedValue> evaluatedArguments;
    std::vector<RuntimeValue> values;
    m_arguments.evaluate(context, evaluatedArguments, values);
    if (context.hasError()) {
        return;
    }

    context.getRuntime()->executeNativeFunction(m_nativeFunctionId, values, context);
}

void CallNativeFunctionExpression::addArgument(std::unique_ptr<Argument> argument) {
    m_arguments.add(std::move(argument));
}

ArgumentList* CallNativeFunctionExpression::getArguments() {
    return &m_arguments;
}

const ArgumentList* CallNativeFunctionExpression::getArguments() const {
    return &m_arguments;
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

void FunctionCallExpression::onResolve(CompilerContext* context, Function* function) {
    auto* runtime = context->getRuntime();
    m_functionId = runtime->getFunctionId(m_name);

    if (m_functionId == NOMAD_INVALID_ID) {
        reportError(context, function, "Unknown function '" + m_name + "'");
        return;
    }

    const auto* callFunction = runtime->getFunction(m_functionId);
    const auto* returnType = callFunction->getReturnType();

    if (context->getMode() == CompilerMode::Interpreter && callFunction->getFunctionStart() == NOMAD_INVALID_INDEX) {
        reportError(context, function, "Function '" + m_name + "' has not been compiled");
        return;
    }

    auto* compiler = context->getCompiler();

    if (context->getMode() == CompilerMode::Function && function != nullptr) {
        compiler->addFunctionCallDependency(function->getId(), m_functionId);
    }

    if (returnType == nullptr) {
        if (function != nullptr && compiler->isRecursiveFunctionCall(function->getId(), m_functionId)) {
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

void FunctionCallExpression::onCompile(Compiler* compiler, Function* function, const ExpressionTarget target) {
    compiler->addFunctionCall(m_functionId, function, &m_arguments);

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

void FunctionCallExpression::onEvaluate(Interpreter& context) const {
    std::vector<TypedValue> evaluatedArguments;
    std::vector<RuntimeValue> values;
    m_arguments.evaluate(context, evaluatedArguments, values);
    if (context.hasError()) {
        return;
    }

    const auto* returnType = getType();
    RuntimeValue result;
    returnType->initValue(result);
    try {
        context.getRuntime()->executeFunction(m_functionId, values, result);
        context.setResult(returnType, result);
        returnType->freeValue(result);
    } catch (...) {
        returnType->freeValue(result);
        throw;
    }
}

///////////////////////////////////////////////////////////////////////////////
// PrimaryExpression
PrimaryExpression::PrimaryExpression(
    const Expression* parent,
    const NomadIndex line,
    const NomadIndex column
):
    Expression(parent, line, column) {
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

void BooleanLiteral::onResolve(CompilerContext* context, Function* /*function*/) {
    setResolved(RuntimeValue{m_value}, context->getRuntime()->getBooleanType());
}

void BooleanLiteral::onCompile(Compiler* compiler, Function* /*function*/, ExpressionTarget target) {
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

void IntegerLiteral::onResolve(CompilerContext* context, Function* /*function*/) {
    RuntimeValue value{m_value};

    setResolved(value, context->getRuntime()->getIntegerType());
}

void IntegerLiteral::onCompile(Compiler* compiler, Function* /*function*/, ExpressionTarget target) {
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

void FloatLiteral::onResolve(CompilerContext* context, Function* /*function*/) {
    RuntimeValue value{m_value};

    setResolved(value, context->getRuntime()->getFloatType());
}

void FloatLiteral::onCompile(Compiler* compiler, Function* /*function*/, ExpressionTarget target) {
    compiler->addLoadFloatValue(m_value, target);
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

void StringLiteral::onResolve(CompilerContext* context, Function* /*function*/) {
    const auto* stringType = context->getRuntime()->getStringType();
    RuntimeValue value{m_value};

    setResolved(value, stringType);
    stringType->freeValue(value);
}

void StringLiteral::onCompile(Compiler* compiler, Function* /*function*/, ExpressionTarget /*target*/) {
    const auto stringValue = getValue().getStringValue();

    const auto stringId = compiler->getRuntime()->registerString(stringValue);

    compiler->addOpCode(OpCodes::op_string_load_r);
    compiler->addId(stringId);
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

void ConstantValueExpression::onResolve(CompilerContext* context, Function* function) {
    auto* runtime = context->getRuntime();
    m_constantId = runtime->getConstantId(m_constantName);

    if (m_constantId == NOMAD_INVALID_ID) {
        reportError(context, function, "Unknown constant: " + m_constantName);
        return;
    }

    RuntimeValue constantValue;
    runtime->getConstantValue(m_constantId, constantValue);
    auto constantType = runtime->getConstantType(m_constantId);

    setResolved(constantValue, constantType);
}

void ConstantValueExpression::onCompile(Compiler* compiler, Function* /*function*/, ExpressionTarget target) {
    if (!isParsed()) {
        raiseException("Unknown constant: " + m_constantName);
    }

    compiler->addLoadValue(getType(), getValue(), target);
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
    m_formatString(formatString) {
}

void FormatStringLiteral::onResolve(CompilerContext* context, Function* function) {
    if (context->getMode() == CompilerMode::Interpreter) {
        reportError(context, function, "Format strings are not supported by the interpreter");
        return;
    }

    if (const auto error = compileFormatString(context->getCompiler(), function, m_formatString, nullptr)) {
        reportError(context, function, *error);
        return;
    }

    setType(context->getRuntime()->getStringType());
}

void FormatStringLiteral::onCompile(Compiler* compiler, Function* function, ExpressionTarget target) {
    const NomadId functionId = function->getId();

    auto formatStringId = compiler->getRuntime()->getFormatStringId(m_formatString, functionId);

    if (formatStringId == NOMAD_INVALID_ID) {
        formatStringId = compiler->getRuntime()->registerFormatString(m_formatString, functionId);

        const auto format_string = compiler->getRuntime()->getFormatString(formatStringId);

        if (const auto error = compileFormatString(compiler, function, m_formatString, format_string)) {
            compiler->reportInternalError("Format string failed to compile after resolving: " + *error);
        }
    }

    compiler->addOpCode(OpCodes::op_format_string_execute);
    compiler->addId(formatStringId);

    compiler->addMoveResult(getType(), target);
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

void IdentifierExpression::onResolve(CompilerContext* context, Function* function) {
    auto* compiler = context->getCompiler();

    compiler->getIdentifierDefinition(m_identifier, function, m_identifierDefinition);

    // Check for invalid identifier type.
    switch (m_identifierDefinition.identifierType) {
        case IdentifierType::Unknown:
            if (function == nullptr || !parseCapture(compiler, function)) {
                reportError(context, function, "Unknown identifier: " + m_identifier);
                return;
            }

            // Reload the identifier definition.
            compiler->getIdentifierDefinition(m_identifier, function, m_identifierDefinition);

            if (m_identifierDefinition.identifierType != IdentifierType::Parameter) {
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

    if (context->getMode() == CompilerMode::Interpreter &&
        m_identifierDefinition.identifierType != IdentifierType::FunctionVariable &&
        m_identifierDefinition.identifierType != IdentifierType::Constant &&
        m_identifierDefinition.identifierType != IdentifierType::DynamicVariable &&
        m_identifierDefinition.identifierType != IdentifierType::ContextVariable) {
        reportError(context, function, "Identifier '" + m_identifier + "' is not supported by the interpreter");
        return;
    }

    if (context->getMode() == CompilerMode::Interpreter &&
        m_identifierDefinition.identifierType == IdentifierType::DynamicVariable &&
        !context->getRuntime()->canGetDynamicVariable(m_identifierDefinition.variableId)) {
        reportError(context, function, "Dynamic variable '" + m_identifier + "' cannot be read");
        return;
    }

    if (m_identifierDefinition.identifierType == IdentifierType::Constant) {
        RuntimeValue constantValue;
        context->getRuntime()->getConstantValue(m_identifierDefinition.variableId, constantValue);
        setResolved(constantValue, m_identifierDefinition.valueType);
        return;
    }

    if (m_identifierDefinition.identifierType == IdentifierType::FunctionVariable) {
        function->registerVariable(m_identifier, m_identifierDefinition.valueType);
    }

    if (m_identifierDefinition.identifierType == IdentifierType::Parameter &&
        m_identifierDefinition.valueType == nullptr) {
        reportError(context, function, "Unknown type for parameter: " + m_identifier);
        return;
    }

    setType(m_identifierDefinition.valueType);
}

void IdentifierExpression::onCompile(Compiler* compiler, Function* /*function*/, const ExpressionTarget target) {
    switch (m_identifierDefinition.identifierType) {
        case IdentifierType::FunctionVariable:
            compiler->addLoadFunctionVariable(m_identifierDefinition.variableId, m_identifierDefinition.valueType, target);
            break;

        case IdentifierType::DynamicVariable:
            compiler->addLoadDynamicVariable(m_identifierDefinition.variableId, m_identifierDefinition.valueType, target);
        break;

        case IdentifierType::ContextVariable:
            compiler->addLoadContextVariable(
                m_identifierDefinition.variableId,
                m_identifierDefinition.valueType,
                target
            );

            compiler->getRuntime()->setContextVariableRead(m_identifierDefinition.variableId, true);
            break;

        case IdentifierType::Parameter:
            compiler->addLoadParameter(m_identifierDefinition.variableId, m_identifierDefinition.valueType, target);
            break;

        default:
            raiseException("Invalid identifier type");
    }
}

bool IdentifierExpression::canBorrow() const {
    if (Expression::canBorrow()) {
        return true;
    }

    // Literals, the caller's locals and the caller's (read-only) parameters cannot be changed by the nativeFunction or function
    // being called. Dynamic and context variables are read through getters that allocate, and their storage can be
    // replaced by the callee, so they are always copied.
    const auto identifierType = m_identifierDefinition.identifierType;

    return hasType() && getType()->isString() &&
        (identifierType == IdentifierType::FunctionVariable || identifierType == IdentifierType::Parameter);
}

void IdentifierExpression::onCompileBorrowed(Compiler* compiler, Function* /*function*/, const ExpressionTarget target) {
    // Borrowing is a bitwise load of the pointer: the non-string load opcodes do exactly that.
    const auto* referenceType = compiler->getRuntime()->getStringRefType();

    switch (m_identifierDefinition.identifierType) {
        case IdentifierType::FunctionVariable:
            compiler->addLoadFunctionVariable(m_identifierDefinition.variableId, referenceType, target);
            break;

        case IdentifierType::Parameter:
            compiler->addLoadParameter(m_identifierDefinition.variableId, referenceType, target);
            break;

        default:
            compiler->reportInternalError("BUG: Identifier cannot lend a string reference: " + m_identifier);
    }
}

void IdentifierExpression::onEvaluate(Interpreter& context) const {
    switch (m_identifierDefinition.identifierType) {
    case IdentifierType::FunctionVariable: {
        const auto* value = context.getVariableValueById(m_identifierDefinition.variableId);
        if (value == nullptr) {
            raiseException("Variable '" + m_identifier + "' has not been assigned a value");
        }

        context.setResult(context.getVariableTypeById(m_identifierDefinition.variableId), *value);
        break;
    }

    case IdentifierType::DynamicVariable: {
        auto* runtime = context.getRuntime();
        const auto* type = runtime->getDynamicVariableType(m_identifierDefinition.variableId);
        if (type == nullptr) {
            raiseException("Dynamic variable '" + m_identifier + "' has no type");
        }

        RuntimeValue value;
        type->initValue(value);
        runtime->getDynamicVariableValue(
            context.getVirtualMachine(),
            m_identifierDefinition.variableId,
            value
        );

        // Getters may leave a string untouched when the value is unavailable.
        if (type->isString() && value.getStringValue() == nullptr) {
            value.setStringValue("");
        }

        // `setResult` copies the value, so the temporary is released here.
        context.setResult(type, value);
        type->freeValue(value);
        break;
    }

    case IdentifierType::ContextVariable: {
        auto* runtime = context.getRuntime();
        const auto contextId = runtime->getVariableContextIdByPrefix(m_identifier);
        if (contextId == NOMAD_INVALID_ID) {
            raiseException("Unknown context for variable '" + m_identifier + "'");
        }

        const auto* type = m_identifierDefinition.valueType;
        if (type == nullptr) {
            raiseException("Variable '" + m_identifier + "' has not been assigned a value");
        }

        // The context owns the value, so it is read without taking ownership.
        RuntimeValue value;
        runtime->getVariableContext(contextId)->getValue(m_identifierDefinition.variableId, value);

        if (type->isString() && value.getStringValue() == nullptr) {
            RuntimeValue emptyValue;
            emptyValue.setStringValue("");
            context.setResult(type, emptyValue);
            emptyValue.freeStringValue();
            break;
        }

        context.setResult(type, value);
        break;
    }

    default:
        raiseException("Identifier '" + m_identifier + "' is not supported by the interpreter");
    }
}

bool IdentifierExpression::parseCapture(const Compiler* compiler, Function* function) {
    return resolveIdentifier(m_identifier, compiler, function, m_identifierDefinition);
}

bool getCapture(const NomadString& identifier, const Function* function, NomadId& id, CaptureSource& captureSource) {
    const NomadId parameterId = function->getParameterId(identifier);
    if (parameterId != NOMAD_INVALID_ID) {
        id = parameterId;
        captureSource = CaptureSource::Parameter;
        return true;
    }

    const NomadId variableId = function->getVariableId(identifier);
    if (variableId != NOMAD_INVALID_ID) {
        id = variableId;
        captureSource = CaptureSource::Variable;
        return true;
    }

    return false;
}

// Resolve captures in nested functions.
// This function assumes the identifier is not already a variable or identifier of the function.
bool parseCapture(const NomadString& identifier, const Compiler* compiler, Function* function) {
    if (function->isNested() == false) {
        return false;
    }

    Function* currentFunction = function;
    const Type* captureType;
    std::vector<Function*> intermediateFunctions = { function };

    // Walk up the nested functions until the identifier is found.
    for (;;) {
        const auto parentId = currentFunction->getParentId();

        if (parentId == NOMAD_INVALID_ID) {
            // We've gone past the root function and haven't found the identifier.
            return false;
        }

        auto parentFunction = compiler->getRuntime()->getFunction(parentId);

        if (parentFunction == nullptr) {
            compiler->reportInternalError("Failed to retrieve parent function during capture resolution");
        }

        NomadId id;
        CaptureSource source;

        if (getCapture(identifier, parentFunction, id, source)) {
            // Found a capturable parameter or variable.
            // Need to determine the type.
            if (source == CaptureSource::Variable) {
                captureType = parentFunction->getVariableType(id);
            } else if (source == CaptureSource::Parameter) {
                captureType = parentFunction->getParameterType(id);
            } else {
                compiler->reportInternalError("Unexpected capture source type");
            }

            break;
        }

        // Capture not found. Add to intermediate functions.
        intermediateFunctions.push_back(parentFunction);

        currentFunction = parentFunction;
    }

    // We've found the capture. Walk back up and ensure the capture is propagated through all intermediate functions.
    for (const auto intermediateFunction : std::ranges::reverse_view(intermediateFunctions)) {
        const auto parentId = intermediateFunction->getParentId();

        if (parentId == NOMAD_INVALID_ID) {
            throw NomadBug("Failed to retrieve parent function during capture propagation");
        }

        const auto parentFunction = compiler->getRuntime()->getFunction(parentId);

        if (parentFunction == nullptr) {
            throw NomadBug("Failed to retrieve parent function during capture propagation");
        }

        NomadId id;
        CaptureSource source;

        if (getCapture(identifier, parentFunction, id, source)) {
            intermediateFunction->createCapture(id, identifier, source, captureType);
        } else {
            compiler->reportInternalError("Failed to retrieve capture propagation");
        }
    }

    return true;
}

bool resolveIdentifier(const NomadString& identifier, const Compiler* compiler, Function* function, IdentifierDefinition& definition) {
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

void FileNameExpression::onResolve(CompilerContext* /*context*/, Function* /*function*/) {
    // Nothing to do...
}

void FileNameExpression::onCompile(Compiler* compiler, Function* /*function*/, const ExpressionTarget target) {
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

void FunctionNameExpression::onResolve(CompilerContext* /*context*/, Function* /*function*/) {
    // Nothing to do...
}

void FunctionNameExpression::onCompile(Compiler* compiler, Function* /*function*/, ExpressionTarget target) {
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

void LineNumberExpression::onResolve(CompilerContext* /*context*/, Function* /*function*/) {
    // Nothing to do...
}

void LineNumberExpression::onCompile(Compiler* compiler, Function* /*function*/, ExpressionTarget target) {
    const auto lineNumber = static_cast<NomadInteger>(getLine());

    compiler->addLoadIntegerValue(lineNumber, target);
}

} // namespace nomad
