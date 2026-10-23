// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compiler.hpp>

#include <nomad/compiler/CallNode.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/CompilerException.hpp>
#include <nomad/compiler/LoopStatement.hpp>
#include <nomad/compiler/StatementParsers.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/compiler/statements/EventStatement.hpp>
#include <nomad/compiler/statements/ReturnStatement.hpp>

#include <nomad/script/InterpreterContext.hpp>
#include <nomad/script/Runtime.hpp>

#include <nomad/system/String.hpp>

namespace nomad {

Compiler::Compiler(Runtime *runtime, std::vector<Instruction>& instructions):
    m_runtime(runtime),
    m_instructions(instructions)
{
    if (m_instructions.empty()) {
        // Add op_stop instruction so that the return of a function is a stop instruction
        addOpCode(OpCodes::op_stop);
    }

    registerParseStatementFn("const", parseConstStatement, preParseConstStatement);
    registerParseStatementFn("enum", parseEnumStatement, preParseEnumStatement);
    registerParseStatementFn("event", parseEventStatement, preParseEventStatement);
    registerParseStatementFn("fun", parseFunStatement, preParseFunStatement);
    registerParseStatementFn("hub", parseHubStatement);
    registerParseStatementFn("assert", parseAssertStatement);
    registerParseStatementFn("if", parseIfStatement);
    registerParseStatementFn("params", nullptr, preParseParamsStatement);
    registerParseStatementFn("return", parseReturnStatement);
    registerParseStatementFn("while", parseWhileStatement);
    registerParseStatementFn("until", parseUntilStatement);
    registerParseStatementFn("do", parseDoStatement);
    registerParseStatementFn("break", parseBreakStatement);
    registerParseStatementFn("continue", parseContinueStatement);
    registerParseStatementFn("for", parseForStatement);

}

Compiler::~Compiler() = default;

Runtime* Compiler::getRuntime() const {
    return m_runtime;
}

void Compiler::reportError(const NomadString& message) const {
    throw CompilerException(message);
}

void Compiler::reportError(const NomadString& message, const ScriptFunction* function, const Tokenizer* tokenizer) const {
    reportError(message, function->getName(), tokenizer->getLineIndex(), tokenizer->getColumnIndex());
}

void Compiler::reportError(const NomadString& message, const NomadString& functionName, const NomadIndex line, const NomadIndex column) const {
    const auto errorMessage = message + ": '" + functionName + "' at line " + toString(line) + ", column " + toString(column);

    reportError(errorMessage);
}

void Compiler::reportInternalError(const NomadString& message) const {
    throw NomadBug("Internal error: " + message);
}

void Compiler::registerParseStatementFn(
    const NomadString& name,
    ParseStatementFn fn,
    PreParseStatementFn preFn
) {
    m_statements.emplace(name, ParseStatementFnRegistration{ preFn, fn });
}

bool Compiler::isStatement(const NomadString& name) const {
    return m_statements.find(name) != m_statements.end();
}

bool Compiler::getParseStatementFn(const NomadString& name, ParseStatementFn& fn) const {
    const auto it = m_statements.find(name);

    if (it != m_statements.end()) {
        fn = it->second.fn;

        return true;
    }

    return false;
}

bool Compiler::getGetPreParseStatementFn(const NomadString& name, PreParseStatementFn& fn) const {
    const auto it = m_statements.find(name);

    if (it != m_statements.end()) {
        fn = it->second.preFn;

        return true;
    }

    return false;
}

void Compiler::getRegisteredStatements(std::vector<NomadString>& parsers) const {
    for (auto& statement: m_statements) {
        parsers.push_back(statement.first);
    }
}

void Compiler::registerUnaryOperator(
    UnaryOperator op,
    const Type* operand,
    const Type* result,
    const NomadString& opCodeName,
    UnaryFoldingFn fn
) {
    m_runtime->registerUnaryOperator(op, operand, result, opCodeName, fn);
}

void Compiler::registerBinaryOperator(
    BinaryOperator op,
    const Type* lhs,
    const Type* rhs,
    const Type* result,
    const NomadString& opCodeName,
    BinaryFoldingFn fn
) {
    m_runtime->registerBinaryOperator(op, lhs, rhs, result, opCodeName, fn);
}

const Type* Compiler::getUnaryOperatorResultType(UnaryOperator op, const Type* operandType) const {
    return m_runtime->getUnaryOperatorResultType(op, operandType);
}

const Type* Compiler::getBinaryOperatorResultType(BinaryOperator op, const Type* lhsType, const Type* rhsType) const {
    return m_runtime->getBinaryOperatorResultType(op, lhsType, rhsType);
}

OpCodeId Compiler::getUnaryOperatorOpCodeId(UnaryOperator op, const Type* operand) const {
    return m_runtime->getUnaryOperatorOpCodeId(op, operand);
}

OpCodeId Compiler::getBinaryOperatorOpCodeId(const BinaryOperator op, const Type* lhs, const Type* rhs) const {
    return m_runtime->getBinaryOperatorOpCodeId(op, lhs, rhs);
}

bool Compiler::foldUnary(
    UnaryOperator op,
    const Type* operandType,
    const RuntimeValue& value,
    RuntimeValue& result
) const {
    return m_runtime->foldUnary(op, operandType, value, result);
}

bool Compiler::foldBinary(
    BinaryOperator op,
    const Type* lhsType,
    const RuntimeValue& lhs,
    const Type* rhsType,
    const RuntimeValue& rhs,
    RuntimeValue& result
) const {
    return m_runtime->foldBinary(op, lhsType, lhs, rhsType, rhs, result);
}

NomadString Compiler::resolveName(
    const NomadString& name,
    const ScriptFunction* function,
    NomadString hub
) const {
    const auto exists = [this](const NomadString& candidate) {
        if (m_runtime->isHubVariable(candidate)) {
            return false;
        }
        return getIdentifierType(candidate, nullptr) != IdentifierType::Unknown ||
            m_runtime->getTypeByName(candidate) != nullptr ||
            m_runtime->getEventId(candidate).isValid() ||
            m_runtime->isHub(candidate) ||
            m_runtime->getParameterizedTypeArgumentCount(candidate).has_value() ||
            m_runtime->getParameterizedConstructorArgumentCount(candidate).has_value();
    };
    auto receiver = name;
    while (!receiver.empty()) {
        for (auto* scope = function; scope != nullptr;
             scope = m_runtime->getScriptFunction(scope->getParentId())) {
            if (scope->getVariableId(receiver) != NOMAD_INVALID_ID ||
                scope->getParameterId(receiver) != NOMAD_INVALID_ID) {
                return name;
            }
        }
        const auto separator = receiver.rfind('.');
        if (separator == NomadString::npos) {
            break;
        }
        receiver.resize(separator);
    }
    if (hub.empty() && function != nullptr) {
        hub = function->getHub();
    }
    while (!hub.empty()) {
        const auto candidate = hub + "." + name;
        auto prefix = candidate;
        while (prefix.size() > hub.size() + 1) {
            if (exists(prefix)) {
                return candidate;
            }
            const auto separator = prefix.rfind('.');
            if (separator == NomadString::npos) {
                break;
            }
            prefix.resize(separator);
        }
        const auto separator = hub.rfind('.');
        if (separator == NomadString::npos) {
            break;
        }
        hub.resize(separator);
    }
    return name;
}

IdentifierType Compiler::getIdentifierType(const NomadString& name, const ScriptFunction* function) const {
    if (m_runtime->getKeywordId(name).isValid()) {
        return IdentifierType::Keyword;
    }

    if (isStatement(name)) {
        return IdentifierType::Statement;
    }

    const auto* callable = m_runtime->getFunction(m_runtime->getCallableId(name));

    if (callable != nullptr && callable->asNativeFunction() != nullptr) {
        return IdentifierType::NativeFunction;
    }

    if (m_runtime->getConstantId(name).isValid()) {
        return IdentifierType::Constant;
    }

    if (callable != nullptr && callable->asScriptFunction() != nullptr) {
        return IdentifierType::Function;
    }

    if (m_runtime->getDynamicVariableId(name) != NOMAD_INVALID_ID) {
        return IdentifierType::DynamicVariable;
    }

    if (m_runtime->getVariableContextIdByPrefix(name) != NOMAD_INVALID_ID) {
        return IdentifierType::ContextVariable;
    }

    if (function == nullptr) {
        return IdentifierType::Unknown;
    }

    if (function->getVariableId(name) != NOMAD_INVALID_ID) {
        return IdentifierType::FunctionVariable;
    }

    if (function->getParameterId(name) != NOMAD_INVALID_ID) {
        return IdentifierType::Parameter;
    }
    return IdentifierType::Unknown;
}

void Compiler::getIdentifierDefinition(const NomadString& name, const ScriptFunction* function, IdentifierDefinition& definition) const {
    if (name.empty()) {
        definition.identifierType = IdentifierType::Unknown;

        return;
    }

    const auto firstCharacter = static_cast<unsigned char>(name[0]);
    if (!(std::isalpha(firstCharacter) || firstCharacter == '_')) {
        definition.identifierType = IdentifierType::Unknown;

        return;
    }

    definition.identifierType = getIdentifierType(name, function);

    if (definition.identifierType == IdentifierType::Keyword || definition.identifierType == IdentifierType::Statement) {
        return;
    }

    const auto* callable = m_runtime->getFunction(m_runtime->getCallableId(name));

    if (callable != nullptr) {
        if (callable->asNativeFunction() != nullptr) {
            definition.identifierType = IdentifierType::NativeFunction;
            definition.nativeFunctionId = callable->getId();
        } else {
            definition.identifierType = IdentifierType::Function;
            definition.functionId = callable->getId();
        }

        definition.valueType = callable->getReturnType();

        return;
    }

    const auto eventId = m_runtime->getEventId(name);

    if (eventId.isValid()) {
        definition.identifierType = IdentifierType::Event;
        definition.eventId = eventId;

        return;
    }

    const auto constantId = m_runtime->getConstantId(name);

    if (constantId.isValid()) {
        definition.identifierType = IdentifierType::Constant;
        definition.constantId = constantId;
        definition.valueType = m_runtime->getConstantType(constantId);

        return;
    }

    const NomadId dynamicVariableId = m_runtime->getDynamicVariableId(name);

    if (dynamicVariableId != NOMAD_INVALID_ID) {
        definition.identifierType = IdentifierType::DynamicVariable;
        definition.variableId = dynamicVariableId;
        definition.valueType = m_runtime->getDynamicVariableType(dynamicVariableId);

        return;
    }

    const NomadId contextId = m_runtime->getVariableContextIdByPrefix(name);

    if (contextId != NOMAD_INVALID_ID) {
        const auto variableId = m_runtime->getContextVariableId(name);

        if (variableId == NOMAD_INVALID_ID) {
            definition.identifierType = IdentifierType::Unknown;

            return;
        }

        definition.variableId = variableId;
        definition.valueType = m_runtime->getContextVariableType(variableId);

        return;
    }

    if (function == nullptr) {
        definition.identifierType = IdentifierType::Unknown;

        return;
    }

    // Check if this is a local variable
    const NomadId variableId = function->getVariableId(name);

    if (variableId != NOMAD_INVALID_ID) {
        definition.identifierType = IdentifierType::FunctionVariable;
        definition.variableId = variableId;
        definition.valueType = function->getVariableType(variableId);

        return;
    }

    const auto parameterId = function->getParameterId(name);
    if (parameterId != NOMAD_INVALID_ID) {
        definition.identifierType = IdentifierType::Parameter;
        definition.variableId = parameterId;
        definition.valueType = function->getParameterType(parameterId);
        return;
    }

    definition.identifierType = IdentifierType::Unknown;
}

bool Compiler::getNativeMemberConflict(const NomadString& identifier, const Type* type, const ScriptFunction* function,
                                          NomadString& conflictingIdentifier) const {
    if (identifier.empty() || type == nullptr) {
        return false;
    }

    std::vector<MemberReference> members;
    m_runtime->getNativeMembers(type, members);

    for (const auto memberId : members) {
        NomadString memberName;

        switch (m_runtime->getMemberType(memberId)) {
        case MemberType::Property: {
            NativePropertyDefinition definition;
            if (!m_runtime->getNativePropertyDefinition(memberId, definition)) {
                reportInternalError("Invalid native property id: " + toString(std::get<PropertyId>(memberId).id));
            }
            memberName = definition.name;
            break;
        }

        case MemberType::Method: {
            const auto* method = m_runtime->getNativeMemberMethod(memberId);
            if (method == nullptr) {
                reportInternalError("Invalid native method id: " + toString(std::get<CallableId>(memberId).id));
            }
            memberName = method->getName();
            break;
        }

        case MemberType::Unknown:
            reportInternalError("Invalid native member id: " +
                std::visit([](const auto id) { return toString(id.id); }, memberId));
        }

        const auto candidate = identifier + "." + memberName;
        IdentifierDefinition existingIdentifier;
        getIdentifierDefinition(candidate, function, existingIdentifier);

        if (existingIdentifier.identifierType != IdentifierType::Unknown) {
            conflictingIdentifier = candidate;
            return true;
        }
    }

    return false;
}


NomadIndex Compiler::getOpCodeSize() const {
    return m_instructions.size();
}

NomadIndex Compiler::addOpCode(OpCodeId opCode) {
    auto fn = m_runtime->getInstructionFn(opCode);

    if (fn == nullptr) {
        reportInternalError("Unknown op code: " + toString(opCode.id));
    }

    m_instructions.emplace_back(fn);

    return m_instructions.size() - 1;
}

NomadIndex Compiler::addOpCode(const NomadString& opCodeName) {
    const auto opCodeId = m_runtime->getInstructionId(opCodeName);

    if (opCodeId.isInvalid()) {
        reportError("Unknown instruction: " + opCodeName);
    }

    return addOpCode(opCodeId);
}

NomadIndex Compiler::addOpCode(const InstructionFn opCode) {
    if (opCode == nullptr || m_runtime->getInstructionId(opCode).isInvalid()) {
        reportInternalError("BUG: Unregistered op code function");
    }

    m_instructions.emplace_back(opCode);

    return m_instructions.size() - 1;
}

NomadIndex Compiler::addId(const NomadId id) {
    const auto index = m_instructions.size();

    const RuntimeValue functionValue(id);

    m_instructions.emplace_back(functionValue);

    return index;
}

NomadIndex Compiler::addIndex(const NomadIndex index) {
    const auto instruction = m_instructions.size();

    m_instructions.emplace_back(RuntimeValue(index));

    return instruction;
}

NomadIndex Compiler::addInteger(NomadInteger value) {
    const auto instruction = m_instructions.size();

    m_instructions.emplace_back(RuntimeValue(value));

    return instruction;
}

NomadIndex Compiler::addFloat(NomadFloat value) {
    const auto instruction = m_instructions.size();

    m_instructions.emplace_back(RuntimeValue(value));

    return instruction;
}

NomadIndex Compiler::addLoadConstant(const ConstantId constantId, const ExpressionTarget target) {
    return addLoadTarget({op_constant_get_r, op_constant_get_i, op_constant_push}, constantId.id, target);
}

NomadIndex Compiler::addLoadValue(const Type* type, const RuntimeValue& value, ExpressionTarget target) {
    if (type == getRuntime()->getBooleanType()) {
        return addLoadBooleanValue(value.getBooleanValue(), target);
    } else if (type == getRuntime()->getIntegerType() || type->asEnum() != nullptr) {
        return addLoadIntegerValue(value.getIntegerValue(), target);
    } else if (type == getRuntime()->getFloatType()) {
        return addLoadFloatValue(value.getFloatValue(), target);
    } else if (type->isString()) {
        return addLoadStringValue(value.getStringValue(), target);
    }

    reportInternalError("Unknown type: " + type->getTypeName());
}

NomadIndex Compiler::addLoadBooleanValue(const bool value, const ExpressionTarget target) {
    switch (target) {
    case ExpressionTarget::Result:
        if (value) {
            return addOpCode(OpCodes::op_boolean_load_true_r);
        } else {
            return addOpCode(OpCodes::op_boolean_load_false_r);
        }
    case ExpressionTarget::Intermediate:
        if (value) {
            return addOpCode(OpCodes::op_boolean_load_true_i);
        } else {
            return addOpCode(OpCodes::op_boolean_load_false_i);
        }
    case ExpressionTarget::Stack:
        if (value) {
            return addOpCode(OpCodes::op_boolean_push_true);
        } else {
            return addOpCode(OpCodes::op_boolean_push_false);
        }
    }

    this->reportInternalError("BUG: Invalid expression target for boolean value");
}

NomadIndex Compiler::addLoadIntegerValue(const NomadInteger value, const ExpressionTarget target) {
    switch (target) {
    case ExpressionTarget::Result:
        if (value == 0) {
            return addOpCode(OpCodes::op_integer_load_zero_r);
        }

        if (value == 1) {
            return addOpCode(OpCodes::op_integer_load_one_r);
        }

        addOpCode(OpCodes::op_integer_load_r);
        return addInteger(value);

    case ExpressionTarget::Intermediate:
        if (value == 0) {
            return addOpCode(OpCodes::op_integer_load_zero_i);
        }
        if (value == 1) {
            return addOpCode(OpCodes::op_integer_load_one_i);
        }

        addOpCode(OpCodes::op_integer_load_i);
        return addInteger(value);

    case ExpressionTarget::Stack:
        if (value == 0) {
            return addOpCode(OpCodes::op_integer_push_zero);
        }

        if (value == 1) {
            return addOpCode(OpCodes::op_integer_push_one);
        }

        addOpCode(OpCodes::op_integer_push_n);
        return addInteger(value);
    }

    this->reportInternalError("BUG: Invalid expression target for float value");
}

NomadIndex Compiler::addLoadFloatValue(NomadFloat value, const ExpressionTarget target) {
    switch (target) {
    case ExpressionTarget::Result:
        if (value == 0) {
            return addOpCode(OpCodes::op_float_load_zero_r);
        }
        if (value == 1) {
            return addOpCode(OpCodes::op_float_load_one_r);
        }

        addOpCode(OpCodes::op_float_load_r);
        return addFloat(value);

    case ExpressionTarget::Intermediate:
        if (value == 0) {
            return addOpCode(OpCodes::op_float_load_zero_i);
        }
        if (value == 1) {
            return addOpCode(OpCodes::op_float_load_one_i);
        }

        addOpCode(OpCodes::op_float_load_i);
        return addFloat(value);

    case ExpressionTarget::Stack:
        if (value == 0) {
            return addOpCode(OpCodes::op_float_push_zero);
        }
        if (value == 1) {
            return addOpCode(OpCodes::op_float_push_one);
        }
        addOpCode(OpCodes::op_float_push_n);
        return addFloat(value);
    }

    this->reportInternalError("BUG: Invalid expression target for float value");
}

NomadIndex Compiler::addLoadStringValue(const NomadString& value, const ExpressionTarget target) {
    const auto stringId = getRuntime()->registerString(value);

    switch (target) {
    case ExpressionTarget::Result:
        addOpCode(OpCodes::op_string_load_r);
        return addId(stringId);

    case ExpressionTarget::Intermediate:
        addOpCode(OpCodes::op_string_load_i);
        return addId(stringId);

    case ExpressionTarget::Stack:
        addOpCode(OpCodes::op_string_push_n);
        return addId(stringId);
    }

    this->reportInternalError("BUG: Invalid expression target for string value");
}

NomadIndex Compiler::addLoadTarget(const TargetOpCodes& opCodes, const NomadId operand, const ExpressionTarget target) {
    switch (target) {
    case ExpressionTarget::Result:
        addOpCode(opCodes.result);
        return addId(operand);

    case ExpressionTarget::Intermediate:
        addOpCode(opCodes.intermediate);
        return addId(operand);

    case ExpressionTarget::Stack:
        addOpCode(opCodes.stack);
        return addId(operand);
    }

    reportInternalError("BUG: Invalid expression target");
}

NomadIndex Compiler::addLoadFunctionVariable(const NomadId variableId, const Type* type, const ExpressionTarget target) {
    return addLoadTarget(type->getTypeOpCodes()->loadFunctionVariable, variableId, target);
}

NomadIndex Compiler::addLoadDynamicVariable(const NomadId variableId, const Type* type, const ExpressionTarget target) {
    return addLoadTarget(type->getTypeOpCodes()->loadDynamicVariable, variableId, target);
}

NomadIndex Compiler::addLoadContextVariable(const NomadId contextVariableId, const Type* type, const ExpressionTarget target) {
    return addLoadTarget(type->getTypeOpCodes()->loadContextVariable, contextVariableId, target);
}

NomadIndex Compiler::addLoadParameter(const NomadIndex parameterId, const Type* type, const ExpressionTarget target) {
    return addLoadTarget(type->getTypeOpCodes()->loadParameter, toNomadId(parameterId), target);
}

NomadIndex Compiler::addPushResult(const Type* type) {
    return addOpCode(type->getTypeOpCodes()->copyResultToStack);
}

void Compiler::addMoveResult(const Type* type, const ExpressionTarget target) {
    switch (target) {
    case ExpressionTarget::Result:
        break;

    case ExpressionTarget::Intermediate:
        addOpCode(type->getTypeOpCodes()->moveResultToIntermediate);
        break;

    case ExpressionTarget::Stack:
        addOpCode(type->getTypeOpCodes()->moveResultToStack);
        break;
    }
}

void Compiler::addMoveIntermediateToResult(const Type* type) {
    addOpCode(type->getTypeOpCodes()->moveIntermediateToResult);
}

NomadIndex Compiler::addPushIntermediate(const Type* type) {
    return addOpCode(type->getTypeOpCodes()->pushIntermediate);
}

NomadIndex Compiler::addPopResult(const Type* type) {
    return addOpCode(type->getTypeOpCodes()->popResult);
}

NomadIndex Compiler::addPopIntermediate(const Type* type) {
    return addOpCode(type->getTypeOpCodes()->popIntermediate);
}

void Compiler::addFreeValue(const InstructionFn freeOpCode) {
    if (freeOpCode != nullptr) {
        addOpCode(freeOpCode);
    }
}

void Compiler::addFreeValue(const InstructionFn freeOpCode, const NomadIndex operand) {
    if (freeOpCode != nullptr) {
        addOpCode(freeOpCode);
        addIndex(operand);
    }
}

NomadIndex Compiler::addFunctionCall(const CallableId targetFunctionId, ScriptFunction* function, const ArgumentList* arguments) {
    const auto targetFunction = getRuntime()->getScriptFunction(targetFunctionId);

    if (targetFunction == nullptr) {
        reportInternalError("Unknown function id: " + toString(targetFunctionId.id));
    }

    const auto argumentCount = arguments->getArgumentCount();

    if (argumentCount != targetFunction->getParameterCount()) {
        reportInternalError("BUG: Argument count does not match parameters of function '" + targetFunction->getName() + "'");
    }

    arguments->compile(this, function);

    addOpCode(OpCodes::op_call_function);

    if (targetFunction->getFunctionStart() == NOMAD_INVALID_INDEX) {
        addFunctionLink(targetFunctionId, m_instructions.size());

        addIndex(NOMAD_INVALID_INDEX); // Placeholder for function jump index
    } else {
        addIndex(targetFunction->getFunctionStart());
    }

    addFreeArguments(arguments);

    return m_instructions.size() - 1;
}

void Compiler::addFreeArguments(const ArgumentList* arguments) {
    const auto stackCount = arguments->getStackCount();

    if (stackCount == 0) {
        return;
    }

    // Arguments are pushed right-to-left, so the first argument is at the top of the stack. Offsets account for
    // arguments occupying more than one slot. The optional `this` is below the arguments and is popped, not freed.
    NomadIndex stackOffset = 0;

    for (const auto& argument : arguments->getArguments()) {
        if (const auto* argumentType = argument->getType()) {
            addFreeValue(argumentType->getTypeOpCodes()->freeStack, stackOffset);
        }

        stackOffset += argument->getStackValueCount();
    }

    switch (stackCount) {
    case 1:
        addOpCode(OpCodes::op_pop_1);
        break;

    case 2:
        addOpCode(OpCodes::op_pop_2);
        break;

    case 3:
        addOpCode(OpCodes::op_pop_3);
        break;

    default:
        addOpCode(OpCodes::op_pop_n);
        addIndex(stackCount);
    }
}

NomadIndex Compiler::addNativeFunctionCall(const CallableId nativeFunctionId, ScriptFunction* function, const ArgumentList* arguments) {
    arguments->compile(this, function);

    if (m_runtime->getNativeFunction(nativeFunctionId) == nullptr) {
        reportInternalError("Unknown native function: " + toString(nativeFunctionId.id));
    }

    addOpCode(OpCodes::op_call_native_function);
    addIndex(toNomadIndex(nativeFunctionId.id));

    addFreeArguments(arguments);

    // if (!nativeFunction.parameters.empty()) {
    //     for (auto i = 0; i < nativeFunction.parameters.size(); i++) {
    //         const auto& parameter = nativeFunction.parameters[i];
    //
    //         if (parameter.type == getRuntime()->getStringType()) {
    //             addOpCode(OpCodes::op_string_free_stack);
    //             addIndex(i);
    //         }
    //     }
    //
    //     addOpCode(OpCodes::op_pop_n);
    //     addIndex(arguments->getArgumentCount());
    // }

    return m_instructions.size() - 1;
}

NomadIndex Compiler::addConstructorCall(const CallableId constructorId, ScriptFunction* function, const ArgumentList* arguments) {
    arguments->compile(this, function);

    addOpCode(OpCodes::op_object_new);
    addId(constructorId.id);

    addFreeArguments(arguments);

    return m_instructions.size() - 1;
}

NomadIndex Compiler::addNativeMethodCall(const CallableId memberId, ScriptFunction* function,
                                            const ArgumentList* arguments) {
    arguments->compile(this, function);

    const auto argumentCount = arguments->getArgumentCount();
    addOpCode(op_native_method_call);
    addId(memberId.id);
    addIndex(argumentCount);

    addFreeArguments(arguments);

    return m_instructions.size() - 1;
}

void Compiler::setOpCode(const NomadIndex index, const NomadString& opCodeName) {
    const auto opCodeId = m_runtime->getInstructionId(opCodeName);

    if (opCodeId.isInvalid()) {
        reportInternalError("Unknown op code: " + opCodeName);
    }

    setOpCode(index, opCodeId);
}

void Compiler::setOpCode(NomadIndex index, OpCodeId opCodeId) {
    auto fn = m_runtime->getInstructionFn(opCodeId);

    if (fn == nullptr) {
        reportInternalError("Unknown op code: " + toString(opCodeId.id));
    }

    m_instructions[index].fn = fn;
}


void Compiler::setId(NomadIndex index, NomadId id) {
    m_instructions[index].value.setIdValue(id);
}

void Compiler::setIndex(NomadIndex index, NomadIndex value) {
    m_instructions[index].value.setIndexValue(value);
}

void Compiler::preParseFunction(CompilerContext* context, ScriptCompilation& scriptFile, const bool hubsOnly) {
    const CompilerHubScope scriptScope(*context, {});
    auto function = m_runtime->getScriptFunction(scriptFile.functionId);
    auto& tokens = *scriptFile.tokens;

    tokens.reset();

    struct DeclarationBlock {
        NomadString hub;
        bool isHub;
    };
    std::vector<DeclarationBlock> blocks;
    while (tokens.nextLine()) {
        const auto statement = tokens.next().textValue;
        if (statement == "end") {
            if (!blocks.empty()) {
                context->setHub(std::move(blocks.back().hub));
                blocks.pop_back();
            }
            continue;
        }
        if (statement == "hub") {
            NomadString name;
            if (!tokens.nextIdentifier(name) || !tokens.endOfLine()) {
                context->reportError("Expected a hub name followed by end of line", &tokens);
                continue;
            }
            if (blocks.size() >= 64) {
                context->reportError("Hub nesting exceeds the maximum depth of 64", &tokens);
                continue;
            }
            const auto qualifiedName = context->qualifyDeclaration(name);
            blocks.push_back({context->getHub(), true});
            if (m_runtime->registerHub(qualifiedName).isInvalid()) {
                context->reportError("Cannot declare hub '" + qualifiedName + "': invalid or conflicting name", &tokens);
            }
            context->setHub(qualifiedName);
            continue;
        }

        PreParseStatementFn preParseStatementFn;

        if (hubsOnly && statement == "enum") {
            while (tokens.nextLine() && !tokens.is("end")) {}
            continue;
        }
        if (hubsOnly && !blocks.empty() && blocks.back().isHub &&
            tokens.getTokenCount() >= 3 && tokens.getTokenTypeAt(0) == TokenType::Identifier &&
            tokens.getTokenAt(1) == "=") {
            const auto name = context->qualifyDeclaration(statement);
            if (statement.find('.') != NomadString::npos) {
                context->reportError("Hub variable declarations require an unqualified member name", &tokens);
            } else if (!m_hubVariableDeclarations.insert(name).second) {
                context->reportError("Hub variable '" + name + "' has more than one initializer", &tokens);
            } else if (m_runtime->registerHubVariable(name) == NOMAD_INVALID_ID) {
                context->reportError("Hub member '" + name + "' is already declared or conflicts with an identifier", &tokens);
            }
        }
        if (!hubsOnly && getGetPreParseStatementFn(statement, preParseStatementFn) && preParseStatementFn) {
            // Errors are reported to the context; keep going to collect errors from the following lines.
            (void)preParseStatementFn(context, function, &tokens);
        }
        if (statement == "enum") {
            continue; // Enum pre-parsing consumes its own closing end.
        }
        auto opensBlock = statement == "fun" || statement == "while" || statement == "until" ||
            statement == "do" || statement == "for" || statement == "if";
        for (NomadIndex index = 1; index < tokens.getTokenCount(); ++index) {
            const auto& token = tokens.getTokenAt(index);
            if (token == "fun") {
                opensBlock = true;
            }
            if (statement == "if" && (token == "break" || token == "continue")) {
                opensBlock = false;
            }
        }
        if (opensBlock) {
            blocks.push_back({context->getHub(), false});
        }
    }
    context->setHub({});
}


void Compiler::parseFunction(CompilerContext* context, ScriptCompilation& file) {
    const CompilerHubScope scriptScope(*context, {});
    auto ast = std::make_unique<FunctionNode>(0, 0);
    auto& tokens = *file.tokens;

    tokens.reset();

    // Statements with errors are skipped so the following lines are still parsed.
    while (tokens.nextLine()) {
        const auto function = m_runtime->getScriptFunction(file.functionId);

        auto statement = parser::parseLine(context, function, &tokens);

        if (statement) {
            ast->addStatement(std::move(statement));
        }
    }

    ast->setEndSpan(tokens.getLineCount(), 0);

    setFunctionNode(file.functionId, std::move(ast));
}

bool Compiler::resolveFunction(CompilerContext* context, const FunctionCompilation& source) {
    if (source.hubInitializer) {
        CompilerContext initializerContext(this, context->getMode(), nullptr, true);
        initializerContext.setHub(source.function->getHub());
        const auto resolved = source.ast->resolve(&initializerContext, source.function);
        context->reportDiagnostics(initializerContext.getDiagnostics());
        return resolved;
    }
    return source.ast->resolve(context, source.function);
}

void Compiler::checkReturnPaths(CompilerContext* context, const FunctionCompilation& source) const {
    const auto* returnType = source.function->getReturnType();

    if (returnType == nullptr || returnType->isVoid() || source.ast->alwaysReturns()) {
        return;
    }

    context->reportError(
        "Missing `return` at the end of function '" + source.function->getName() + "'. It returns `" +
            returnType->getTypeName() + "`, so every path must end with a `return`",
        source.function->getPath(),
        source.ast->getEndLine(),
        source.ast->getEndColumn()
    );
}

void Compiler::inferTypes(CompilerContext* context) {
    // Return and variable types are inferred while resolving, so a function can depend on a type that is only discovered
    // by a function resolved after it. Resolve until no new type is discovered and discard the diagnostics: the final
    // resolve pass reports errors once, when every type that can be inferred is known.
    auto inferredTypes = collectInferredTypes();

    for (NomadIndex pass = 0; pass <= m_functions.size(); ++pass) {
        CompilerContext inferenceContext(this, context->getMode());

        for (const auto& source: m_functions) {
            try {
                (void)resolveFunction(&inferenceContext, source);
            } catch (const NomadBug&) {
                throw;
            } catch (const NomadException&) {
                // The final resolve pass reports it.
            }
        }

        auto updatedTypes = collectInferredTypes();

        if (updatedTypes == inferredTypes) {
            break;
        }

        inferredTypes = std::move(updatedTypes);
    }

    // Functions without a `return` statement return void.
    for (const auto& source: m_functions) {
        if (source.function->getReturnType() == nullptr && !m_returningFunctions.contains(source.function->getId())) {
            source.function->setReturnType(m_runtime->getVoidType());
        }
    }
}

std::vector<const Type*> Compiler::collectInferredTypes() const {
    std::vector<const Type*> types;

    for (const auto& source: m_functions) {
        types.push_back(source.function->getReturnType());

        for (NomadIndex variableIndex = 0; variableIndex < source.function->getVariableCount(); ++variableIndex) {
            types.push_back(source.function->getVariableType(toNomadId(variableIndex)));
        }
    }

    for (auto contextId = m_runtime->getFirstVariableContextId(); contextId != NOMAD_INVALID_ID;
         contextId = m_runtime->getNextVariableContextId(contextId)) {
        const auto* variableContext = m_runtime->getVariableContext(contextId);

        for (auto variableId = variableContext->getFirstVariableId(); variableId != NOMAD_INVALID_ID;
             variableId = variableContext->getNextVariableId(variableId)) {
            types.push_back(variableContext->getVariableType(variableId));
        }
    }

    return types;
}

void Compiler::compileFunction(const FunctionCompilation& source) {
    ScriptFunction* function = source.function;

    function->setFunctionStart(m_instructions.size());

    source.ast->compile(this, function);

    // Unreachable for valid programs: `checkReturnPaths` rejects functions returning a value that can reach their end.
    // Kept as a safety net so a caller never takes ownership of whatever `r` last held.
    if (const auto* returnType = function->getReturnType(); returnType != nullptr && returnType->isString()) {
        addLoadStringValue("", ExpressionTarget::Result);
    }

    addFunctionReturn(function);

    function->setFunctionEnd(m_instructions.size());
}

void Compiler::addFunctionReturn(const ScriptFunction* function) {
    const NomadIndex variableCount = function->getVariableCount();

    if (variableCount == 0) {
        addOpCode(OpCodes::op_return);

        return;
    }

    // Free owned variables before releasing the function's variable slots.
    for (auto i = NomadIndex{0}; i < variableCount; ++i) {
        addFreeValue(function->getVariableType(toNomadId(i))->getTypeOpCodes()->freeFunctionVariable, i);
    }

    addOpCode(OpCodes::op_return_n);
    addIndex(variableCount);
}

void Compiler::registerScript(CompilerContext* context, const ScriptSource& script) {
    if (m_scripts.size() >= toNomadIndex(NOMAD_ID_MAX)) {
        context->reportError("Maximum number of scripts reached", script.source.sourceName,
            NOMAD_INVALID_INDEX, NOMAD_INVALID_INDEX);
        return;
    }
    const auto scriptId = toNomadId(m_scripts.size());
    m_scripts.push_back({script, nullptr, NOMAD_INVALID_ID});
    const auto functionId = createFunction(context, scriptId, script.scriptName);
    if (functionId.isInvalid()) {
        m_scripts.pop_back();
        return;
    }
    m_scripts.back().functionId = functionId;
}

CallableId Compiler::createFunction(
    CompilerContext* context,
    const NomadId scriptId,
    const NomadString& functionName,
    const bool hubInitializer
) {
    if (context == nullptr || context->getCompiler() != this) {
        throw NomadBug("Function creation requires this compiler's context");
    }
    const auto& source = getScript(scriptId).source;
    const auto existingFunctionId = m_runtime->getFunctionId(functionName);
    if (existingFunctionId != CallableId{}) {
        const auto* existingFunction = m_runtime->getScriptFunction(existingFunctionId);
        context->reportError(
            "Function name '" + functionName + "' is already used by '" + existingFunction->getPath() + "'",
            source.sourceName, NOMAD_INVALID_INDEX, NOMAD_INVALID_INDEX);
        return CallableId::INVALID;
    }

    const auto functionId = m_runtime->registerScriptFunction(functionName, source.sourceName, source.text);
    if (functionId == CallableId{}) {
        context->reportError("Could not register function '" + functionName + "'",
            source.sourceName, NOMAD_INVALID_INDEX, NOMAD_INVALID_INDEX);
        return CallableId::INVALID;
    }

    auto function = m_runtime->getScriptFunction(functionId);
    function->setHub(context->getHub());

    m_functions.emplace_back(FunctionCompilation {
        scriptId,
        {},
        function,
        hubInitializer
    });

    return functionId;
}

NomadId Compiler::getScriptId(const CallableId functionId) const {
    for (const auto& function : m_functions) {
        if (function.function->getId() == functionId) {
            return function.scriptId;
        }
    }
    throw NomadBug("Function does not belong to a registered script: " + toString(functionId.id));
}

const ScriptSource& Compiler::getScript(const NomadId scriptId) const {
    if (isIdOutOfRange(scriptId, m_scripts.size())) {
        throw NomadBug("Unknown script id");
    }
    return m_scripts[toNomadIndex(scriptId)].script;
}

void Compiler::setFunctionNode(CallableId functionId, std::unique_ptr<FunctionNode> ast) {
    for (auto& source: m_functions) {
        if (source.function->getId() == functionId) {
            source.function->setSourceLineRange(
                ast ? ast->getLine() : NOMAD_INVALID_INDEX,
                ast ? ast->getEndLine() : NOMAD_INVALID_INDEX
            );
            source.ast = std::move(ast);

            return;
        }
    }

    reportInternalError("Unknown function id: " + toString(functionId.id));
}

void Compiler::registerSources(CompilerContext* context, const SourceBatch& batch) {
    if (context == nullptr || context->getCompiler() != this) {
        throw NomadBug("Source registration requires this compiler's context");
    }

    context->reportDiagnostics(batch.diagnostics);
    for (const auto& script : batch.sources) {
        registerScript(context, script);
    }
}

void Compiler::deferFormatString(
    FormatStringLiteral* literal,
    const CallableId functionId,
    NomadString sourceContent,
    NomadString hub,
    const bool hubInitializer
) {
    m_pendingFormatStrings.push_back({
        literal, functionId, std::move(sourceContent), std::move(hub), hubInitializer
    });
}

void Compiler::registerPhaseHook(std::function<void(CompilerContext*, CompilationPhase)> hook) {
    if (hook) {
        m_phaseHooks.push_back(std::move(hook));
    }
}

void Compiler::registerScriptInitializer(
    CompilerContext* context,
    const NomadId scriptId,
    std::unique_ptr<HubInitializerStatement> initializer
) {
    if (m_scriptInitializerId.isInvalid()) {
        m_scriptInitializerId = createFunction(context, scriptId, "$init");
        if (m_scriptInitializerId.isInvalid()) {
            return;
        }
        m_runtime->getScriptFunction(m_scriptInitializerId)->setHub({});
        setFunctionNode(m_scriptInitializerId, std::make_unique<FunctionNode>(0, 0));
    }
    for (auto& source : m_functions) {
        if (source.function->getId() == m_scriptInitializerId) {
            m_scriptInitializers.push_back(initializer.get());
            source.ast->addStatement(std::move(initializer));
            return;
        }
    }
    throw NomadBug("Missing script initialization function");
}

const TypedValue* Compiler::getHubInitialValue(const NomadString& name) const {
    if (m_allowInitialValueLookup) {
        for (const auto* initializer : m_scriptInitializers) {
            if (initializer->getIdentifier() == name) {
                return initializer->getInitialValue();
            }
        }
    }
    return nullptr;
}

void Compiler::prepareScriptInitialization(CompilerContext* context) {
    for (auto* initializer : m_scriptInitializers) {
        if (initializer->getExpression()->isParsed()) {
            initializer->setInitialValue(initializer->getExpression()->getInitialValue(this));
        }
    }
    m_allowInitialValueLookup = true;
    for (auto* initializer : m_scriptInitializers) {
        try {
            if (initializer->getInitialValue() == nullptr) {
                initializer->setInitialValue(initializer->getExpression()->getInitialValue(this));
                if (initializer->getInitialValue() == nullptr) {
                    m_allowInitialValueLookup = false;
                }
            }
        } catch (const NomadBug&) {
            throw;
        } catch (const NomadException& exception) {
            context->reportError(exception.what(), initializer->getSourceName(),
                initializer->getLine(), initializer->getColumn());
        }
    }
    m_allowInitialValueLookup = false;
}

void Compiler::installScriptInitialValues() {
    for (const auto* initializer : m_scriptInitializers) {
        const auto id = m_runtime->getContextVariableId(initializer->getIdentifier());
        const auto* type = m_runtime->getContextVariableType(id);
        if (const auto* initialValue = initializer->getInitialValue()) {
            m_runtime->setContextVariableValue(id, initialValue->getValue());
        } else {
            RuntimeValue value;
            type->initValue(value);
            try {
                m_runtime->setContextVariableValue(id, value);
            } catch (...) {
                type->freeValue(value);
                throw;
            }
            type->freeValue(value);
        }
    }
}

bool Compiler::compileFunctions(CompilerContext* context) {
    return compileScripts(context);
}

bool Compiler::compileScripts(CompilerContext* context, const CompilationTarget target) {
    if (context == nullptr || context->getCompiler() != this) {
        throw NomadBug("Compilation requires this compiler's context");
    }
    if (context->hasError()) {
        return false;
    }

    const auto errorCount = context->getErrorCount();
    const auto hasNewErrors = [context, errorCount] {
        return context->getErrorCount() != errorCount;
    };

    // A few user errors are still thrown (e.g. function registration failures); report them as diagnostics. Internal bugs
    // keep propagating.
    const auto reportExceptions = [context](const NomadString& sourceName, const auto& action) {
        try {
            action();
        } catch (const NomadBug&) {
            throw;
        } catch (const AstException& exception) {
            context->reportError(exception.what(), sourceName, exception.getLine(), exception.getColumn());
        } catch (const NomadException& exception) {
            context->reportError(exception.what(), sourceName, NOMAD_INVALID_INDEX, NOMAD_INVALID_INDEX);
        }
    };

    const auto cleanup = [this](Compiler*) {
        // Tokenizers report to the caller's context, so they must not outlive this call.
        for (auto& scriptFile: m_scripts) {
            scriptFile.tokens.reset();
        }

        m_functionLinks.clear();
        m_functionCallDependencies.clear();
        m_returningFunctions.clear();
        m_pendingFormatStrings.clear();
    };
    const auto cleanupGuard = std::unique_ptr<Compiler, decltype(cleanup)>(this, cleanup);

    const auto beginPhase = [this, context, &reportExceptions, &hasNewErrors](const CompilationPhase phase) {
        if (hasNewErrors()) {
            return false;
        }
        const auto hookCount = m_phaseHooks.size();
        for (auto index = std::size_t{0}; index < hookCount; ++index) {
            reportExceptions({}, [&] {
                m_phaseHooks[index](context, phase);
            });
            if (hasNewErrors()) {
                return false;
            }
        }
        return true;
    };

    if (!beginPhase(CompilationPhase::Configure) || !beginPhase(CompilationPhase::PreParse)) {
        return false;
    }

    log::info("Pre-Parsing functions");

    std::vector<ScriptCompilation*> orderedScripts;
    for (auto& script : m_scripts) {
        orderedScripts.push_back(&script);
    }
    std::sort(orderedScripts.begin(), orderedScripts.end(), [](const auto* left, const auto* right) {
        return left->script.scriptName < right->script.scriptName;
    });

    for (auto* script : orderedScripts) {
        const auto& source = script->script.source;
        log::debug("Pre-Parsing '" + script->script.scriptName + "' (" + source.sourceName + ")");
        reportExceptions(source.sourceName, [&] {
            script->tokens = std::make_unique<Tokenizer>(context, source.sourceName, source.text);

            preParseFunction(context, *script, true);
        });
    }
    if (hasNewErrors()) {
        return false;
    }
    for (auto* script : orderedScripts) {
        reportExceptions(script->script.source.sourceName, [&] {
            preParseFunction(context, *script);
        });
    }

    // Each pass depends on the declarations collected by the previous one. Stop after a pass that reported errors so
    // they do not cascade into unrelated errors (e.g. unknown `fun` names or `fun` bodies parsed as top-level code).
    if (hasNewErrors()) {
        return false;
    }

    if (!beginPhase(CompilationPhase::Parse)) {
        return false;
    }
    log::info("Parsing functions");

    for (auto* script : orderedScripts) {
        const auto& source = script->script.source;
        log::debug("Parsing '" + script->script.scriptName + "' (" + source.sourceName + ")");

        reportExceptions(source.sourceName, [&] {
            parseFunction(context, *script);
        });
    }

    if (hasNewErrors()) {
        return false;
    }

    if (!beginPhase(CompilationPhase::PrepareFormatStrings)) {
        return false;
    }
    log::info("Preparing format strings");

    // Parsing a placeholder can append nested format literals to the queue.
    for (auto index = NomadIndex{0}; index < m_pendingFormatStrings.size(); ++index) {
        const auto pending = std::move(m_pendingFormatStrings[index]);
        auto* function = m_runtime->getScriptFunction(pending.functionId);
        reportExceptions(function->getPath(), [&] {
            CompilerContext placeholderContext(this, context->getMode(), nullptr, pending.hubInitializer);
            placeholderContext.setHub(pending.hub);
            (void)pending.literal->parsePlaceholders(&placeholderContext, function, pending.sourceContent);
            context->reportDiagnostics(placeholderContext.getDiagnostics());
        });
        if (hasNewErrors()) {
            return false;
        }
    }
    m_pendingFormatStrings.clear();

    if (!beginPhase(CompilationPhase::Resolve)) {
        return false;
    }
    log::info("Syntax checking functions");

    reportExceptions({}, [&] {
        inferTypes(context);
    });

    for (const auto& source: m_functions) {
        log::debug("Syntax checking function '" + source.function->getName() + "' (" + source.function->getPath() + ")");

        reportExceptions(source.function->getPath(), [&] {
            (void)resolveFunction(context, source);
            checkReturnPaths(context, source);
            source.ast->checkReachability(context, source.function);
        });
    }

    if (hasNewErrors()) {
        return false;
    }

    reportExceptions({}, [&] {
        prepareScriptInitialization(context);
    });
    if (hasNewErrors()) {
        return false;
    }

    if (target == CompilationTarget::Analysis) {
        return beginPhase(CompilationPhase::Completed);
    }

    if (!beginPhase(CompilationPhase::Emit)) {
        return false;
    }
    log::info("Compiling functions");

    for (auto& source: m_functions) {
        log::debug("Compiling function '" + source.function->getName() + "' (" + source.function->getPath() + ")");

        reportExceptions(source.function->getPath(), [&] {
            compileFunction(source);
        });
    }

    if (hasNewErrors()) {
        return false;
    }

    if (!beginPhase(CompilationPhase::Link)) {
        return false;
    }
    log::info("Linking functions");

    reportExceptions({}, [&] {
        linkFunctions();
    });

    if (!beginPhase(CompilationPhase::Verify)) {
        return false;
    }
    log::info("Verification");

    reportExceptions({}, [&] {
        postCompile(context);
    });

    if (!beginPhase(CompilationPhase::Completed)) {
        return false;
    }
    if (m_scriptInitializerId.isValid()) {
        reportExceptions({}, [&] {
            installScriptInitialValues();
            m_runtime->setScriptInitializer(m_scriptInitializerId);
        });
        if (hasNewErrors()) {
            return false;
        }
    }
    log::info("Functions compiled");

    return true;
}
void Compiler::addFunctionLink(CallableId functionId, NomadIndex callIndex) {
    m_functionLinks.emplace_back(FunctionLink{functionId, callIndex});
}

void Compiler::addFunctionCallDependency(const CallableId callerFunctionId, const CallableId calleeFunctionId) {
    m_functionCallDependencies[callerFunctionId].insert(calleeFunctionId);
}

bool Compiler::isRecursiveFunctionCall(const CallableId callerFunctionId, const CallableId calleeFunctionId) const {
    std::unordered_set<CallableId, IdHash> visited;
    std::vector<CallableId> pending = { calleeFunctionId };

    while (!pending.empty()) {
        const auto functionId = pending.back();
        pending.pop_back();

        const auto dependencies = m_functionCallDependencies.find(functionId);

        if (dependencies == m_functionCallDependencies.end()) {
            continue;
        }

        for (const auto dependencyId: dependencies->second) {
            if (dependencyId == callerFunctionId || dependencyId == calleeFunctionId) {
                return true;
            }

            if (visited.insert(dependencyId).second) {
                pending.push_back(dependencyId);
            }
        }
    }

    return callerFunctionId == calleeFunctionId;
}

void Compiler::addReturningFunction(const CallableId functionId) {
    m_returningFunctions.insert(functionId);
}

void Compiler::linkFunctions() {
    for (auto& link: m_functionLinks) {
        auto function = m_runtime->getScriptFunction(link.functionId);

        if (function == nullptr) {
            reportInternalError("Failed to link function '" + toString(link.functionId.id) + "'");
        }
        if (function->getFunctionStart() == NOMAD_INVALID_INDEX) {
            reportInternalError("Function '" + function->getName() + "' has not been compiled");
        }

        setIndex(link.callIndex, function->getFunctionStart());
    }
}

void Compiler::postCompile(CompilerContext* compilerContext) {
    // verify that all variables have been initialized and are used
    auto contextId = m_runtime->getFirstVariableContextId();

    while (contextId != NOMAD_INVALID_ID) {
        const auto context = m_runtime->getVariableContext(contextId);

        auto variableId = context->getFirstVariableId();

        while (variableId != NOMAD_INVALID_ID) {
            const auto variableName = context->getVariableName(variableId);

            if (!context->isWritten(variableId)) {
                compilerContext->reportError("Variable '" + variableName + "' has not been initialized");
            }

            if (!context->isRead(variableId)) {
                log::debug("Variable '" + variableName + "' has been declared but not used");
            }

            variableId = context->getNextVariableId(variableId);
        }

        contextId = m_runtime->getNextVariableContextId(contextId);
    }
}

NomadString Compiler::generateFunctionName(const NomadString& generatedName, const NomadString& hostFunctionName, NomadIndex line) {
    NomadString generatedFunctionName;
    auto index = NomadIndex{0};

    do {
        generatedFunctionName = FUNCTION_INTERNAL_NAME_PREFIX + generatedName + "_" + hostFunctionName + "_" + std::to_string(line) + "_" + std::to_string(index);
        ++index;
    } while (m_runtime->getFunctionId(generatedFunctionName).isValid());

    return generatedFunctionName;
}

NomadString Compiler::generateFunctionName(const NomadString& name, const ScriptFunction* function, NomadIndex line) {
    return generateFunctionName(name, function->getName(), line);
}

} // nomad
