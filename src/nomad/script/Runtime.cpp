// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Runtime.hpp>

#include <nomad/compiler/Compiler.hpp>

#include <nomad/script/NativeFunctions.hpp>
#include <nomad/script/Closure.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/VirtualMachine.hpp>
#include <nomad/script/Type.hpp>

#include <boost/json/array.hpp>

#include <algorithm>
#include <utility>

#include <iomanip>

namespace nomad {

Runtime::Runtime() {
    // Types
    registerType(std::make_unique<VoidType>());
    registerType(std::make_unique<IdType>());
    registerType(std::make_unique<FloatType>());
    registerType(std::make_unique<IntegerType>());
    registerType(std::make_unique<BooleanType>());
    registerType(std::make_unique<StringType>());
    registerType(std::make_unique<StringRefType>());
    registerType(std::make_unique<FileNameType>());
    registerType(std::make_unique<FunctionNameType>());
    registerType(std::make_unique<FunctionLineNumberType>());
    registerType(std::make_unique<EventCallbackType>());
    registerType(std::make_unique<EventDispatchType>());

    // Register instructions
    registerDefaultInstructions();

    const auto* typeBoolean = getBooleanType();
    const auto* typeInteger = getIntegerType();
    const auto* typeFloat = getFloatType();
    const auto* typeString = getStringType();

    registerUnaryOperator(UnaryOperator::Bang, typeBoolean, typeBoolean, OpCodes::op_boolean_not, foldBooleanUnaryBang);
    registerUnaryOperator(UnaryOperator::Plus, typeInteger, typeInteger, OpCodes::op_integer_absolute, foldIntegerUnaryPlus);
    registerUnaryOperator(UnaryOperator::Minus, typeInteger, typeInteger, OpCodes::op_integer_negate, foldIntegerUnaryMinus);
    registerUnaryOperator(UnaryOperator::Plus, typeFloat, typeFloat, OpCodes::op_float_absolute, foldFloatUnaryPlus);
    registerUnaryOperator(UnaryOperator::Minus, typeFloat, typeFloat, OpCodes::op_float_negate, foldFloatUnaryMinus);
    registerUnaryOperator(UnaryOperator::Sin, typeFloat, typeFloat, OpCodes::op_float_sin, foldFloatUnarySin);
    registerUnaryOperator(UnaryOperator::Cos, typeFloat, typeFloat, OpCodes::op_float_cosine, foldFloatUnaryCos);
    registerUnaryOperator(UnaryOperator::Tan, typeFloat, typeFloat, OpCodes::op_float_tangent, foldFloatUnaryTan);

    registerBinaryOperator(BinaryOperator::AndAnd, typeBoolean, typeBoolean, typeBoolean, OpCodes::op_boolean_and, foldBooleanBinaryAndAnd);
    registerBinaryOperator(BinaryOperator::PipePipe, typeBoolean, typeBoolean, typeBoolean, OpCodes::op_boolean_or, foldBooleanBinaryPipePipe);
    registerBinaryOperator(BinaryOperator::EqualEqual, typeBoolean, typeBoolean, typeBoolean, OpCodes::op_boolean_equal, foldBooleanBinaryEqualEqual);
    registerBinaryOperator(BinaryOperator::BangEqual, typeBoolean, typeBoolean, typeBoolean, OpCodes::op_boolean_not_equal, foldBooleanBinaryBangEqual);

    registerBinaryOperator(BinaryOperator::Plus, typeInteger, typeInteger, typeInteger, OpCodes::op_integer_add, foldIntegerBinaryPlus);
    registerBinaryOperator(BinaryOperator::Minus, typeInteger, typeInteger, typeInteger, OpCodes::op_integer_subtract, foldIntegerBinaryMinus);
    registerBinaryOperator(BinaryOperator::Star, typeInteger, typeInteger, typeInteger, OpCodes::op_integer_multiply, foldIntegerBinaryStar);
    registerBinaryOperator(BinaryOperator::Slash, typeInteger, typeInteger, typeInteger, OpCodes::op_integer_divide, foldIntegerBinarySlash);
    registerBinaryOperator(BinaryOperator::Percent, typeInteger, typeInteger, typeInteger, OpCodes::op_integer_modulo, foldIntegerBinaryPercent);
    registerBinaryOperator(BinaryOperator::Caret, typeInteger, typeInteger, typeInteger, OpCodes::op_integer_xor, foldIntegerBinaryCaret);
    registerBinaryOperator(BinaryOperator::And, typeInteger, typeInteger, typeInteger, OpCodes::op_integer_and, foldIntegerAnd);
    registerBinaryOperator(BinaryOperator::Pipe, typeInteger, typeInteger, typeInteger, OpCodes::op_integer_or, foldIntegerPipe);
    registerBinaryOperator(BinaryOperator::EqualEqual, typeInteger, typeInteger, typeBoolean, OpCodes::op_integer_equal, foldIntegerBinaryEqualEqual);
    registerBinaryOperator(BinaryOperator::BangEqual, typeInteger, typeInteger, typeBoolean, OpCodes::op_integer_not_equal, foldIntegerBinaryBangEqual);
    registerBinaryOperator(BinaryOperator::LessThan, typeInteger, typeInteger, typeBoolean, OpCodes::op_integer_less_than, foldIntegerBinaryLessThan);
    registerBinaryOperator(BinaryOperator::LessThanEqual, typeInteger, typeInteger, typeBoolean, OpCodes::op_integer_less_than_or_equal, foldIntegerBinaryLessThanEqual);
    registerBinaryOperator(BinaryOperator::GreaterThan, typeInteger, typeInteger, typeBoolean, OpCodes::op_integer_greater_than, foldIntegerBinaryGreaterThan);
    registerBinaryOperator(BinaryOperator::GreaterThanEqual, typeInteger, typeInteger, typeBoolean, OpCodes::op_integer_greater_than_or_equal, foldIntegerBinaryGreaterThanEqual);

    registerBinaryOperator(BinaryOperator::Plus, typeFloat, typeFloat, typeFloat, OpCodes::op_float_add, foldFloatBinaryPlus);
    registerBinaryOperator(BinaryOperator::Minus, typeFloat, typeFloat, typeFloat, OpCodes::op_float_subtract, foldFloatBinaryMinus);
    registerBinaryOperator(BinaryOperator::Star, typeFloat, typeFloat, typeFloat, OpCodes::op_float_multiply, foldFloatBinaryStar);
    registerBinaryOperator(BinaryOperator::Slash, typeFloat, typeFloat, typeFloat, OpCodes::op_float_divide, foldFloatBinarySlash);
    registerBinaryOperator(BinaryOperator::EqualEqual, typeFloat, typeFloat, typeBoolean, OpCodes::op_float_equal, foldFloatBinaryEqualEqual);
    registerBinaryOperator(BinaryOperator::BangEqual, typeFloat, typeFloat, typeBoolean, OpCodes::op_float_not_equal, foldFloatBinaryBangEqual);
    registerBinaryOperator(BinaryOperator::LessThan, typeFloat, typeFloat, typeBoolean, OpCodes::op_float_less_than, foldFloatBinaryLessThan);
    registerBinaryOperator(BinaryOperator::LessThanEqual, typeFloat, typeFloat, typeBoolean, OpCodes::op_float_less_than_or_equal, foldFloatBinaryLessThanEqual);
    registerBinaryOperator(BinaryOperator::GreaterThan, typeFloat, typeFloat, typeBoolean, OpCodes::op_float_greater_than, foldFloatBinaryGreaterThan);
    registerBinaryOperator(BinaryOperator::GreaterThanEqual, typeFloat, typeFloat, typeBoolean, OpCodes::op_float_greater_than_or_equal, foldFloatBinaryGreaterThanEqual);

    registerBinaryOperator(BinaryOperator::EqualEqual, typeString, typeString, typeBoolean, OpCodes::op_string_equal, foldStringBinaryEqualEqual);
    registerBinaryOperator(BinaryOperator::BangEqual, typeString, typeString, typeBoolean, OpCodes::op_string_not_equal, foldStringBinaryBangEqual);

    // Register build-in nativeFunctions
    registerBuildInNativeFunctions(this);

//    register_keyword("stringId" NomadDoc("Load the string id of the given string"));
    registerKeyword("fun", NomadDoc("Function declaration"));
    registerKeyword("if", NomadDoc("if statement"));
    registerKeyword("else", NomadDoc("`else` branch of an `if` statement"));
    registerKeyword("end", NomadDoc("End of a `fun` or `if` statement"));
    registerKeyword("const", NomadDoc("Declares a constant"));

    registerConstant("false", RuntimeValue(NOMAD_FALSE), getBooleanType());
    registerConstant("true", RuntimeValue(NOMAD_TRUE), getBooleanType());
    registerConstant("pi", RuntimeValue(NOMAD_PI), getFloatType());

    registerVariableContext("global", "global.", std::make_unique<SimpleVariableContext>());
}

Runtime::~Runtime() {
    for (NomadIndex constantIndex = 0; constantIndex < m_constants.size(); ++constantIndex) {
        if (const auto* type = m_constantsMap.getVariableType(toNomadId(constantIndex))) {
            type->freeValue(m_constants[constantIndex]);
        }
    }
}

void Runtime::setDebug(const bool debug) {
    m_debug = debug;
}

bool Runtime::isDebug() const {
    return m_debug;
}

const std::vector<Instruction>& Runtime::getInstructions() const {
    return m_instructions;
}

NomadId Runtime::registerNativeFunction(
    const NomadString& name,
    NativeFunctionFn nativeFunction_fn,
    const std::vector<NativeFunctionParameterDefinition>& parameters,
    const Type* returnType,
    NomadDocArg
) {
    const auto id = toNomadId(m_nativeFunctions.size());

    m_nativeFunctions.push_back(
        {
            id,
            name,
            std::move(nativeFunction_fn),
            parameters,
            returnType,
            doc
        }
    );

    const auto callable = CallableId{CallableKind::NativeFunction, id};

    // The definition has to be in place before the signature can be compared against the existing overloads.
    if (const auto* overloadSet = findCallableOverloadSet(name)) {
        if (NomadString error; !canOverloadCallable(*overloadSet, callable, error)) {
            log::error("Cannot register native function '" + name + "': " + error);

            m_nativeFunctions.pop_back();

            return NOMAD_INVALID_ID;
        }
    }

    addCallable(name, callable);

    return id;
}

void Runtime::addCallable(const NomadString& name, const CallableId callable) {
    const auto existing = m_callablesByName.find(name);

    if (existing == m_callablesByName.end()) {
        m_callablesByName.emplace(name, m_callables.size());
        m_callables.push_back(CallableOverloadSet{name, {callable}});

        return;
    }

    m_callables[existing->second].overloads.push_back(callable);
}

const CallableOverloadSet* Runtime::findCallableOverloadSet(const NomadString& name) const {
    const auto entry = m_callablesByName.find(name);

    if (entry == m_callablesByName.end()) {
        return nullptr;
    }

    return &m_callables[entry->second];
}

CallableId Runtime::getCallableId(const NomadString& name) const {
    const auto* overloadSet = findCallableOverloadSet(name);

    if (overloadSet == nullptr || overloadSet->overloads.empty()) {
        return NOMAD_INVALID_CALLABLE_ID;
    }

    return overloadSet->overloads.front();
}

void Runtime::getCallableOverloads(const NomadString& name, std::vector<CallableId>& overloads) const {
    overloads.clear();

    if (const auto* overloadSet = findCallableOverloadSet(name)) {
        overloads = overloadSet->overloads;
    }
}

NomadIndex Runtime::getCallableOverloadCount(const NomadString& name) const {
    const auto* overloadSet = findCallableOverloadSet(name);

    return overloadSet == nullptr ? 0 : overloadSet->overloads.size();
}

const NomadString& Runtime::getCallableName(const CallableId callable) const {
    static const NomadString emptyName;

    switch (callable.kind) {
        case CallableKind::NativeFunction: {
            if (isIdOutOfRange(callable.id, m_nativeFunctions.size())) {
                return emptyName;
            }

            return m_nativeFunctions[toNomadIndex(callable.id)].name;
        }

        case CallableKind::Function: {
            const auto* function = getFunction(callable.id);

            return function == nullptr ? emptyName : function->getName();
        }
    }

    return emptyName;
}

NomadIndex Runtime::getCallableParameterCount(const CallableId callable) const {
    switch (callable.kind) {
        case CallableKind::NativeFunction: {
            if (isIdOutOfRange(callable.id, m_nativeFunctions.size())) {
                return 0;
            }

            return m_nativeFunctions[toNomadIndex(callable.id)].parameters.size();
        }

        case CallableKind::Function: {
            const auto* function = getFunction(callable.id);

            return function == nullptr ? 0 : function->getParameterCount();
        }
    }

    return 0;
}

const Type* Runtime::getCallableParameterType(const CallableId callable, const NomadIndex parameterIndex) const {
    if (parameterIndex >= getCallableParameterCount(callable)) {
        return nullptr;
    }

    switch (callable.kind) {
        case CallableKind::NativeFunction:
            return m_nativeFunctions[toNomadIndex(callable.id)].parameters[parameterIndex].type;

        case CallableKind::Function:
            return getFunction(callable.id)->getParameterType(toNomadId(parameterIndex));
    }

    return nullptr;
}

const NomadString& Runtime::getCallableParameterName(const CallableId callable, const NomadIndex parameterIndex) const {
    static const NomadString emptyName;

    if (parameterIndex >= getCallableParameterCount(callable)) {
        return emptyName;
    }

    switch (callable.kind) {
        case CallableKind::NativeFunction:
            return m_nativeFunctions[toNomadIndex(callable.id)].parameters[parameterIndex].name;

        case CallableKind::Function:
            return getFunction(callable.id)->getParameterName(toNomadId(parameterIndex));
    }

    return emptyName;
}

const Type* Runtime::getCallableReturnType(const CallableId callable) const {
    switch (callable.kind) {
        case CallableKind::NativeFunction: {
            if (isIdOutOfRange(callable.id, m_nativeFunctions.size())) {
                return nullptr;
            }

            return m_nativeFunctions[toNomadIndex(callable.id)].returnType;
        }

        case CallableKind::Function: {
            const auto* function = getFunction(callable.id);

            return function == nullptr ? nullptr : function->getReturnType();
        }
    }

    return nullptr;
}

ParameterShape Runtime::getParameterShape(const Type* type) const {
    // Mirrors the order in which `parseNativeFunctionArguments` tests parameter types.
    if (type == getFileNameType()) {
        return ParameterShape::SourceFile;
    }

    if (type == getFunctionNameType()) {
        return ParameterShape::SourceFunction;
    }

    if (type == getLineNumberType()) {
        return ParameterShape::SourceLine;
    }

    if (type == getEventDispatchType()) {
        return ParameterShape::EventDispatch;
    }

    if (type == getEventCallbackType()) {
        return ParameterShape::EventCallback;
    }

    if (type != nullptr && type->asCallback() != nullptr) {
        return ParameterShape::Callback;
    }

    return ParameterShape::Value;
}

bool Runtime::canOverloadCallable(
    const CallableOverloadSet& overloadSet,
    const CallableId candidate,
    NomadString& error
) const {
    const auto parameterCount = getCallableParameterCount(candidate);

    for (NomadIndex index = 0; index < parameterCount; ++index) {
        if (!isOverloadableShape(getParameterShape(getCallableParameterType(candidate, index)))) {
            error = "parameter '" + getCallableParameterName(candidate, index) + "' cannot take part in overloading";

            return false;
        }
    }

    const auto* returnType = getCallableReturnType(candidate);
    const auto returnsValue = returnType != nullptr && !returnType->isVoid();

    for (const auto existing : overloadSet.overloads) {
        if (existing.kind != candidate.kind) {
            error = "a callable of a different kind is already registered under that name";

            return false;
        }

        if (getCallableParameterCount(existing) != parameterCount) {
            error = "overloads must all take the same number of parameters";

            return false;
        }

        const auto* existingReturnType = getCallableReturnType(existing);

        // The parser decides whether a call is a statement or an expression from the name alone, so the whole set
        // has to agree on whether it yields a value.
        if ((existingReturnType != nullptr && !existingReturnType->isVoid()) != returnsValue) {
            error = "overloads must all return a value or all return nothing";

            return false;
        }

        auto hasSameSignature = true;

        for (NomadIndex index = 0; index < parameterCount; ++index) {
            const auto* candidateType = getCallableParameterType(candidate, index);
            const auto* existingType = getCallableParameterType(existing, index);
            const auto candidateShape = getParameterShape(candidateType);

            if (candidateShape != getParameterShape(existingType)) {
                error = "overloads must agree on the kind of every parameter";

                return false;
            }

            if (candidateShape != ParameterShape::Value && candidateType != existingType) {
                error = "overloads must agree on the type of compiler-supplied parameters";

                return false;
            }

            if (candidateType != existingType) {
                hasSameSignature = false;
            }
        }

        if (hasSameSignature) {
            error = "an overload with the same parameter types is already registered";

            return false;
        }
    }

    return true;
}

CallableId Runtime::resolveCallableOverload(
    const NomadString& name,
    const std::vector<const Type*>& argumentTypes,
    NomadString& error
) const {
    const auto* overloadSet = findCallableOverloadSet(name);

    if (overloadSet == nullptr || overloadSet->overloads.empty()) {
        error = "Unknown function '" + name + "'";

        return NOMAD_INVALID_CALLABLE_ID;
    }

    auto exactMatch = NOMAD_INVALID_CALLABLE_ID;
    auto compatibleMatch = NOMAD_INVALID_CALLABLE_ID;
    NomadIndex compatibleCount = 0;

    for (const auto candidate : overloadSet->overloads) {
        if (getCallableParameterCount(candidate) != argumentTypes.size()) {
            continue;
        }

        auto isExact = true;
        auto isCompatible = true;

        for (NomadIndex index = 0; index < argumentTypes.size(); ++index) {
            const auto* argumentType = argumentTypes[index];

            // The compiler supplies this argument, so it cannot tell the overloads apart.
            if (argumentType == nullptr) {
                continue;
            }

            const auto* parameterType = getCallableParameterType(candidate, index);

            if (parameterType == argumentType) {
                continue;
            }

            isExact = false;

            if (parameterType == nullptr || !parameterType->acceptsArgument(argumentType)) {
                isCompatible = false;

                break;
            }
        }

        if (isExact) {
            if (exactMatch.isValid()) {
                error = "Ambiguous call to '" + name + "'";

                return NOMAD_INVALID_CALLABLE_ID;
            }

            exactMatch = candidate;
        } else if (isCompatible) {
            compatibleMatch = candidate;
            ++compatibleCount;
        }
    }

    if (exactMatch.isValid()) {
        return exactMatch;
    }

    if (compatibleCount == 1) {
        return compatibleMatch;
    }

    NomadString argumentList;

    for (const auto* argumentType : argumentTypes) {
        if (argumentType == nullptr) {
            continue;
        }

        if (!argumentList.empty()) {
            argumentList += ", ";
        }

        argumentList += argumentType->getTypeName();
    }

    error = compatibleCount > 1
        ? "Ambiguous call to '" + name + "' with arguments (" + argumentList + ")"
        : "No overload of '" + name + "' accepts arguments (" + argumentList + ")";

    return NOMAD_INVALID_CALLABLE_ID;
}

void Runtime::registerType(std::unique_ptr<Type>&& type) {
    m_types.push_back(std::move(type));

    // return toNomadId(m_types.size() - 1);
}

NomadId Runtime::getTypeId(const NomadString& typeName) const {
    for (NomadIndex i = 0; i < m_types.size(); ++i) {
        if (m_types[i]->getTypeName() == typeName) {
            return toNomadId(i);
        }
    }

    return NOMAD_INVALID_ID;
}

std::optional<const Type*> Runtime::getType(const NomadId id) const {
    const auto typeIndex = toNomadIndex(id);

    if (typeIndex >= m_types.size()) {
        return std::nullopt;
    }

    return m_types[typeIndex].get();
}

const Type* Runtime::getTypeByName(const NomadString& name) const {
    for (const auto& type: m_types) {
        if (type->getTypeName() == name) {
            return type.get();
        }
    }

    return nullptr;
}

const Type* Runtime::getCallbackType(const std::vector<const Type*>& parameterTypes, const Type* returnType) {
    auto test_type = std::make_unique<FunctionType>(parameterTypes, returnType);

    for (const auto& type: m_types) {
        if (type->sameType(test_type.get())) {
            return type.get();
        }
    }

    m_types.push_back(std::move(test_type));

    return m_types.back().get();
}

const Type * Runtime::getEventType(const NomadString &name) const {
    for (const auto& type: m_types) {
        if (type->getTypeName() == name) {
            return type.get();
        }
    }

    return nullptr;
}

const Type* Runtime::getPredicateType() {
    return getCallbackType({}, getBooleanType());
}

NomadId Runtime::registerInstruction(
    const NomadString& name,
    const InstructionFn fn,
    const NomadString& doc,
    std::vector<Operand> operands
) {
    auto id = getInstructionId(name);

    if (id != NOMAD_INVALID_ID) {
        log::warning("Instruction '" + name + "' already registered");

        return id;
    }

    id = toNomadId(m_opCodes.size());

    m_opCodes.emplace_back(
        OpCodeDefinition{
            id,
            name,
            fn,
            std::move(operands),
            doc
        }
    );

    return id;
}

const Type* Runtime::getVoidType() const {
    return getTypeByName(VOID_TYPE_NAME);
}
//
//const Type* Runtime::get_id_type() const {
//    return get_type(ID_TYPE_NAME);
//}

const Type* Runtime::getBooleanType() const {
    return getTypeByName(BOOLEAN_TYPE_NAME);
}

const Type* Runtime::getIntegerType() const {
    return getTypeByName(INTEGER_TYPE_NAME);
}

const Type* Runtime::getFloatType() const {
    return getTypeByName(FLOAT_TYPE_NAME);
}

const Type* Runtime::getStringType() const {
    return getTypeByName(STRING_TYPE_NAME);
}

const Type* Runtime::getStringRefType() const {
    return getTypeByName(STRING_REF_TYPE_NAME);
}

const Type* Runtime::getFunctionType() const {
    return getTypeByName(FUNCTION_TYPE_NAME);
}

const Type* Runtime::getEventCallbackType() const {
    return getTypeByName(EVENT_CALLBACK_TYPE_NAME);
}

const Type* Runtime::getEventDispatchType() const {
    return getTypeByName(EVENT_DISPATCH_TYPE_NAME);
}

const Type* Runtime::getFileNameType() const {
    return getTypeByName(FILE_NAME_TYPE_NAME);
}

const Type* Runtime::getFunctionNameType() const {
    return getTypeByName(FUNCTION_NAME_TYPE_NAME);
}

const Type* Runtime::getLineNumberType() const {
    return getTypeByName(FUNCTION_LINE_NUMBER_TYPE_NAME);
}

void Runtime::registerUnaryOperator(
    const UnaryOperator op,
    const Type* operand,
    const Type* result,
    const NomadString& opCodeName,
    const UnaryFoldingFn fn
) {
    const auto opCodeId = getInstructionId(opCodeName);
    if (opCodeId == NOMAD_INVALID_ID) {
        throw NomadBug("Unknown op code '" + opCodeName + "'");
    }

    for (auto& definition: m_unaryOperators) {
        if (definition.op == op && definition.operand == operand) {
            throw NomadBug("Unary operator '" + opCodeName + "' already registered for type '" + operand->getTypeName() + "'");
        }
    }

    m_unaryOperators.push_back({op, operand, result, opCodeId, fn});
}

void Runtime::registerBinaryOperator(
    const BinaryOperator op,
    const Type* lhs,
    const Type* rhs,
    const Type* result,
    const NomadString& opCodeName,
    const BinaryFoldingFn fn
) {
    const auto opCodeId = getInstructionId(opCodeName);
    if (opCodeId == NOMAD_INVALID_ID) {
        throw NomadBug("Unknown op code '" + opCodeName + "'");
    }

    for (auto& definition: m_binaryOperators) {
        if (definition.op == op && definition.lhs == lhs && definition.rhs == rhs) {
            throw NomadBug("Binary operator '" + opCodeName + "' already registered for types '" + lhs->getTypeName() + "' and '" + rhs->getTypeName() + "'");
        }
    }

    m_binaryOperators.push_back({op, lhs, rhs, result, opCodeId, fn});
}

const Type* Runtime::getUnaryOperatorResultType(const UnaryOperator op, const Type* operandType) const {
    for (const auto& definition: m_unaryOperators) {
        if (definition.op == op && definition.operand == operandType) {
            return definition.result;
        }
    }

    return nullptr;
}

const Type* Runtime::getBinaryOperatorResultType(
    const BinaryOperator op,
    const Type* lhsType,
    const Type* rhsType
) const {
    for (const auto& definition: m_binaryOperators) {
        if (definition.op == op && definition.lhs == lhsType && definition.rhs == rhsType) {
            return definition.result;
        }
    }

    return nullptr;
}

NomadId Runtime::getUnaryOperatorOpCodeId(const UnaryOperator op, const Type* operand) const {
    for (const auto& definition: m_unaryOperators) {
        if (definition.op == op && definition.operand == operand) {
            return definition.opCodeId;
        }
    }

    return NOMAD_INVALID_ID;
}

NomadId Runtime::getBinaryOperatorOpCodeId(
    const BinaryOperator op,
    const Type* lhs,
    const Type* rhs
) const {
    if (lhs == nullptr || rhs == nullptr) {
        return NOMAD_INVALID_ID;
    }

    for (const auto& definition: m_binaryOperators) {
        if (definition.op == op && definition.lhs == lhs && definition.rhs == rhs) {
            return definition.opCodeId;
        }
    }

    return NOMAD_INVALID_ID;
}

bool Runtime::foldUnary(
    const UnaryOperator op,
    const Type* operandType,
    const RuntimeValue& value,
    RuntimeValue& result
) const {
    for (const auto& definition: m_unaryOperators) {
        if (definition.op == op && definition.operand == operandType) {
            definition.fn(value, result);
            return true;
        }
    }

    return false;
}

bool Runtime::foldBinary(
    const BinaryOperator op,
    const Type* lhsType,
    const RuntimeValue& lhs,
    const Type* rhsType,
    const RuntimeValue& rhs,
    RuntimeValue& result
) const {
    for (const auto& definition: m_binaryOperators) {
        if (definition.op == op && definition.lhs == lhsType && definition.rhs == rhsType) {
            definition.fn(lhs, rhs, result);
            return true;
        }
    }

    return false;
}

NomadId Runtime::getInstructionId(const NomadString& name) const {
    for (NomadIndex i = 0; i < m_opCodes.size(); ++i) {
        if (m_opCodes[i].name == name) {
            return static_cast<NomadId>(i);
        }
    }

    return NOMAD_INVALID_ID;
}

NomadId Runtime::getInstructionId(const InstructionFn fn) const {
    for (auto& op_code: m_opCodes) {
        if (op_code.fn == fn) {
            return op_code.id;
        }
    }

    return NOMAD_INVALID_ID;
}

InstructionFn Runtime::getInstructionFn(const NomadId id) const {
    return m_opCodes[toNomadIndex(id)].fn;
}

const NomadString& Runtime::getInstructionName(const NomadId id) const {
    return m_opCodes[toNomadIndex(id)].name;
}

const std::vector<Operand>& Runtime::getInstructionOperands(const NomadId id) const {
    return m_opCodes[toNomadIndex(id)].operands;
}

NomadId Runtime::getNativeFunctionId(const NomadString& name) const {
    const auto callable = getCallableId(name);

    if (callable.kind != CallableKind::NativeFunction) {
        return NOMAD_INVALID_ID;
    }

    return callable.id;
}

NativeFunctionFn Runtime::getNativeFunctionFn(const NomadId id) const {
    return m_nativeFunctions[toNomadIndex(id)].fn;
}

//NomadId Runtime::register_statement(const String& name, ParseStatementFn fn) {
//    auto id = to_nomad_id(m_statements.size());
//
//    ParseStatementFnRegistration registration;
//
//    registration.id = id;
//    registration.name = name;
//    registration.fn = fn;
//
//    m_statements.push_back(registration);
//
//    return id;
//}

//CompilerStatementFn Runtime::get_statement(const String& name) const {
//    for (const auto& statement: m_statements) {
//        if (statement.name == name) {
//            return statement.fn;
//        }
//    }
//
//    return nullptr;
//}
//
//void Runtime::get_statements(std::vector<CompilerStatementFnRegistration>& statements) const {
//    statements = m_statements;
//}
//
bool Runtime::getNativeFunctionDefinition(const NomadId id, NativeFunctionDefinition& definition) const {
    const auto nativeFunctionIndex = toNomadIndex(id);

    if (nativeFunctionIndex >= m_nativeFunctions.size()) {
        return false;
    }

    definition.id = id;
    definition.name = m_nativeFunctions[nativeFunctionIndex].name;
    definition.fn = m_nativeFunctions[nativeFunctionIndex].fn;
    definition.parameters = m_nativeFunctions[nativeFunctionIndex].parameters;
    definition.returnType = m_nativeFunctions[nativeFunctionIndex].returnType;
    definition.doc = m_nativeFunctions[nativeFunctionIndex].doc;

    return true;
}

bool Runtime::getNativeFunctionDefinition(const NomadString& name, NativeFunctionDefinition& definition) const {
    auto id = getNativeFunctionId(name);

    if (id == NOMAD_INVALID_ID) {
        return false;
    }

    return getNativeFunctionDefinition(id, definition);
}

void Runtime::getNativeFunctions(std::vector<NativeFunctionDefinition>& nativeFunctions) const {
    for (auto& nativeFunction: m_nativeFunctions) {
        NativeFunctionDefinition definition;

        definition.id = nativeFunction.id;
        definition.name = nativeFunction.name;
        definition.fn = nativeFunction.fn;
        definition.parameters = nativeFunction.parameters;
#if defined(NOMAD_FUNCTION_DOC)
        definition.doc = nativeFunction.doc;
#endif

        nativeFunctions.push_back(definition);
    }
}

NomadId Runtime::getFirstNativeFunctionId() const {
    if (m_nativeFunctions.empty()) {
        return NOMAD_INVALID_ID;
    }

    return 0;
}

NomadId Runtime::getNextNativeFunctionId(const NomadId currentId) const {
    if (toNomadIndex(currentId) + 1 >= m_nativeFunctions.size()) {
        return NOMAD_INVALID_ID;
    }

    return currentId + 1;
}

NomadId Runtime::registerKeyword(const NomadString& keyword, const NomadString& /*doc*/) {
    auto id = toNomadId(m_keywords.size());

    KeywordDefinition definition;

    definition.keyword = keyword;
#if defined(NOMAD_FUNCTION_DOC)
    definition.doc = doc;
#endif

    m_keywords.push_back(definition);

    return id;
}

NomadId Runtime::getKeywordId(const NomadString& keyword) const {
    for (NomadIndex i = 0; i < m_keywords.size(); ++i) {
        if (m_keywords[i].keyword == keyword) {
            return toNomadId(i);
        }
    }

    return NOMAD_INVALID_ID;
}

void Runtime::getKeywords(std::vector<KeywordDefinition>& keywords) const {
    keywords = m_keywords;
}

NomadId Runtime::registerConstant(const NomadString& name, const RuntimeValue& value, const Type* type) {
    if (getConstantId(name) != NOMAD_INVALID_ID) {
        log::error("Constant with name '" + name + "' already registered.");

        return NOMAD_INVALID_ID;
    }

    auto constantId = m_constantsMap.registerVariable(name, type);
    const auto constantIndex = toNomadIndex(constantId);

    m_constants.resize(constantIndex + 1);
    type->copyValue(value, m_constants[constantIndex]);

    return constantId;
}

void Runtime::getConstantValue(const NomadId id, RuntimeValue& value) const {
    const auto constantIndex = toNomadIndex(id);

    if (constantIndex >= m_constants.size()) {
        return;
    }

    value = m_constants[constantIndex];
}

NomadId Runtime::getConstantId(const NomadString& name) const {
    return m_constantsMap.getVariableId(name);
}

const NomadString& Runtime::getConstantName(const NomadId id) const {
    return m_constantsMap.getVariableName(id);
}

const Type* Runtime::getConstantType(const NomadId id) const {
    return m_constantsMap.getVariableType(id);
}

NomadId Runtime::getFirstConstantId() const
{
    if (m_constants.empty()) {
        return NOMAD_INVALID_ID;
    }

    return 0;
}

NomadId Runtime::getNextConstantId(const NomadId currentId) const {
    const auto currentIndex = toNomadIndex(currentId);

    if (currentIndex + 1 >= m_constants.size()) {
        return NOMAD_INVALID_ID;
    }

    return currentId + 1;
}

NomadId Runtime::registerEvent(const NomadString &name, const std::vector<EventParameter> &parameterTypes) {
    if (getEventId(name) != NOMAD_INVALID_ID) {
        log::error("Event with name '" + name + "' already registered.");

        return NOMAD_INVALID_ID;
    }

    NomadId id = toNomadId(m_events.size());

    m_events.emplace_back(id, name, parameterTypes);

    // Also register event type.
    registerType(std::make_unique<EventType>(&m_events.back()));

    return id;
}

NomadId Runtime::getEventId(const NomadString &name) const {
    for (auto& event: m_events) {
        if (event.name == name) {
            return event.id;
        }
    }

    return NOMAD_INVALID_ID;
}

std::optional<EventDefinition> Runtime::getEventDefinition(NomadId id) const {
    for (const auto& event: m_events) {
        if (event.id == id) {
            return event;
        }
    }

    return std::nullopt;
}

NomadId Runtime::getFirstEventId() const {
    if (m_events.empty()) {
        return NOMAD_INVALID_ID;
    }

    return toNomadId(0);
}

NomadId Runtime::getNextEventId(const NomadId currentId) const {
    const auto currentIndex = toNomadIndex(currentId);

    if (currentIndex + 1 >= m_events.size()) {
        return NOMAD_INVALID_ID;
    }

    return currentId + 1;
}

NomadId Runtime::registerString(const NomadString& string) {
    auto id = getStringId(string);

    if (id != NOMAD_INVALID_ID) {
        return id;
    }

    id = toNomadId(m_strings.size());

    m_strings.push_back(string);

    return id;
}

NomadId Runtime::getStringId(const NomadString& string) const {
    for (NomadIndex i = 0; i < m_strings.size(); ++i) {
        if (m_strings[i] == string) {
            return toNomadId(i);
        }
    }

    return NOMAD_INVALID_ID;
}

const NomadString& Runtime::getString(const NomadId stringId) const {
    const auto stringIndex = toNomadIndex(stringId);
    return m_strings[stringIndex];
}

const NomadString& Runtime::getStringByName(const NomadString& name) const {
    const auto stringId = getStringId(name);

    return getString(stringId);
}

NomadId Runtime::registerFormatString(const NomadString& formatString, NomadId functionId) {
    auto id = getFormatStringId(formatString, functionId);

    if (id != NOMAD_INVALID_ID) {
        return id;
    }

    id = toNomadId(m_formatStrings.size());

    m_formatStrings.push_back(
        {
            id,
            functionId,
            std::make_unique<FormatString>(formatString, functionId)
        }
        );

    return id;
}

FormatString* Runtime::getFormatString(const NomadId id) const {
    const auto formatStringIndex = toNomadIndex(id);

    if (formatStringIndex >= m_formatStrings.size()) {
        return nullptr;
    }

    return m_formatStrings[formatStringIndex].formatString.get();
}

NomadId Runtime::getFormatStringId(const NomadString& formatString, const NomadId functionId) const {
    for (auto& i: m_formatStrings) {
        if (i.functionId == functionId && i.formatString->getFormatString() == formatString) {
            return i.id;
        }
    }

    return NOMAD_INVALID_ID;
}

NomadId Runtime::registerDynamicVariable(
    const NomadString& name,
    DynamicVariableSetFn setFn,
    DynamicVariableGetFn getFn,
    const Type* type,
    const NomadString& /*doc*/
    ) {
    auto id = toNomadId(m_dynamicVariables.size());

    DynamicVariableRegistration definition;
    definition.name = name;
    definition.type = type;
    definition.setFn = std::move(setFn);
    definition.getFn = std::move(getFn);
#if defined(NOMAD_FUNCTION_DOC)
    definition.doc = doc;
#endif

    m_dynamicVariables.push_back(definition);

    return id;
}

NomadId Runtime::getDynamicVariableId(const NomadString& name) const {
    for (NomadIndex i = 0; i < m_dynamicVariables.size(); ++i) {
        if (m_dynamicVariables[i].name == name) {
            return toNomadId(i);
        }
    }

    return NOMAD_INVALID_ID;
}

NomadString Runtime::getDynamicVariableName(const NomadId id) const {
    const auto dynamicVariableIndex = toNomadIndex(id);
    return m_dynamicVariables[dynamicVariableIndex].name;
}

const Type* Runtime::getDynamicVariableType(const NomadId id) const {
    const auto dynamicVariableIndex = toNomadIndex(id);
    return m_dynamicVariables[dynamicVariableIndex].type;
}

bool Runtime::canSetDynamicVariable(const NomadId id) const {
    const auto dynamicVariableIndex = toNomadIndex(id);
    return m_dynamicVariables[dynamicVariableIndex].setFn != nullptr;
}

bool Runtime::canGetDynamicVariable(const NomadId id) const {
    const auto dynamicVariableIndex = toNomadIndex(id);
    return m_dynamicVariables[dynamicVariableIndex].getFn != nullptr;
}

void Runtime::setDynamicVariable(VirtualMachine* interpreter, const NomadId id, const RuntimeValue& value) {
    const auto dynamicVariableIndex = toNomadIndex(id);
    m_dynamicVariables[dynamicVariableIndex].setFn(interpreter, value);
}

void Runtime::getDynamicVariableValue(VirtualMachine* interpreter, const NomadId id, RuntimeValue& value) const {
    const auto dynamicVariableIndex = toNomadIndex(id);
    m_dynamicVariables[dynamicVariableIndex].getFn(interpreter, value);
}

void Runtime::setStringDynamicVariable(VirtualMachine* interpreter, const NomadId id, const NomadString& value) {
    setStringDynamicVariable(interpreter, id, value.c_str());
}

void Runtime::setStringDynamicVariable(VirtualMachine* interpreter, const NomadId id, const NomadChar* value) {
    // The setter copies what it keeps, so the string is only borrowed.
    RuntimeValue stringValue;
    stringValue.setStringRefValue(value);
    setStringDynamicVariable(interpreter, id, stringValue);
}

void Runtime::setStringDynamicVariable(VirtualMachine* interpreter, const NomadId id, const RuntimeValue& value) {
    const auto dynamicVariableIndex = toNomadIndex(id);
    m_dynamicVariables[dynamicVariableIndex].setFn(interpreter, value);
}

void Runtime::getStringDynamicVariableValue(VirtualMachine* interpreter, const NomadId id, NomadString& value) const {
    // Dynamic getters hand over an owned string.
    RuntimeValue stringValue;

    getStringDynamicVariableValue(interpreter, id, stringValue);

    const auto* characters = stringValue.getStringValue();
    value = characters != nullptr ? characters : "";

    stringValue.freeStringValue();
}

void Runtime::getStringDynamicVariableValue(VirtualMachine* interpreter, NomadId id, RuntimeValue& value) const {
    const auto dynamicVariableIndex = toNomadIndex(id);
    m_dynamicVariables[dynamicVariableIndex].getFn(interpreter, value);
}

NomadId Runtime::getFirstDynamicVariableId() const {
    if (m_dynamicVariables.empty()) {
        return NOMAD_INVALID_ID;
    }

    return toNomadId(0);
}

NomadId Runtime::getNextDynamicVariableId(const NomadId currentId) const {
    const auto dynamicVariableIndex = toNomadIndex(currentId);
    if (dynamicVariableIndex + 1 >= m_dynamicVariables.size()) {
        return NOMAD_INVALID_ID;
    }

    return toNomadId(dynamicVariableIndex + 1);
}

NomadId Runtime::registerVariableContext(
    const NomadString& name,
    const NomadString& prefix,
    std::unique_ptr<VariableContext> context
    ) {
    NomadId id = toNomadId(m_variables.size());

    auto registration = VariableContextRegistration{
        id,
        name,
        prefix,
        std::move(context)
    };

    m_variables.emplace_back(std::move(registration));

    return id;
}

NomadId Runtime::getContextId(const NomadString& name) const {
    for (NomadIndex i = 0; i < m_variables.size(); ++i) {
        if (m_variables[i].name == name) {
            return toNomadId(i);
        }
    }

    return NOMAD_INVALID_ID;
}

NomadId Runtime::registerContextVariable(const NomadId contextId, const NomadString &variableName, const Type *type) {
    const auto contextIndex = toNomadIndex(contextId);

    if (contextIndex >= m_variables.size()) {
        return NOMAD_INVALID_ID;
    }

    const auto localVariableId = m_variables[contextIndex].context->registerVariable(variableName, type);

    if (localVariableId == NOMAD_INVALID_ID) {
        return NOMAD_INVALID_ID;
    }

    return internContextVariable(contextId, localVariableId);
}

NomadId Runtime::registerOrUpdateContextVariable(const NomadString &variableName, const Type *type) {
    const auto contextId = getVariableContextIdByPrefix(variableName);

    if (contextId == NOMAD_INVALID_ID) {
        return NOMAD_INVALID_ID;
    }

    return registerContextVariable(contextId, variableName, type);
}

VariableContext* Runtime::getVariableContext(const NomadId contextId) const {
    const auto contextIndex = toNomadIndex(contextId);
    if (contextIndex >= m_variables.size()) {
        return nullptr;
    }

    return m_variables[contextIndex].context.get();
}

NomadString Runtime::getContextName(const NomadId id) const {
    const auto contextIndex = toNomadIndex(id);
    return m_variables[contextIndex].name;
}

NomadId Runtime::getContextVariableId(const NomadString& variableName) {
    const auto contextId = getVariableContextIdByPrefix(variableName);

    if (contextId == NOMAD_INVALID_ID) {
        return NOMAD_INVALID_ID;
    }

    const auto contextIndex = toNomadIndex(contextId);
    const auto localVariableId = m_variables[contextIndex].context->getVariableId(variableName);

    if (localVariableId == NOMAD_INVALID_ID) {
        return NOMAD_INVALID_ID;
    }

    return internContextVariable(contextId, localVariableId);
}

NomadId Runtime::getVariableContextIdByPrefix(const NomadString& variableName) const {
    for (const auto& context: m_variables) {
        if (variableName.rfind(context.prefix, 0) == 0) {
            return context.id;
        }
    }

    return NOMAD_INVALID_ID;
}

NomadString Runtime::getContextVariableName(const NomadId contextVariableId) const {
    const auto* contextVariable = getContextVariable(contextVariableId);

    if (contextVariable == nullptr) {
        log::error("Invalid context variable id: " + std::to_string(contextVariableId));
        return NOMAD_EMPTY_STRING;
    }

    return getVariableContext(contextVariable->contextId)->getVariableName(contextVariable->localVariableId);
}

const Type* Runtime::getContextVariableType(const NomadId contextVariableId) const {
    const auto* contextVariable = getContextVariable(contextVariableId);

    if (contextVariable == nullptr) {
        log::error("Invalid context variable id: " + std::to_string(contextVariableId));
        return nullptr;
    }

    return getVariableContext(contextVariable->contextId)->getVariableType(contextVariable->localVariableId);
}

void Runtime::setContextVariableWritten(const NomadId contextVariableId, const bool written) const {
    const auto* contextVariable = getContextVariable(contextVariableId);

    if (contextVariable == nullptr) {
        log::error("Invalid context variable id: " + std::to_string(contextVariableId));
        return;
    }

    getVariableContext(contextVariable->contextId)->setWritten(contextVariable->localVariableId, written);
}

void Runtime::setContextVariableRead(const NomadId contextVariableId, const bool read) const {
    const auto* contextVariable = getContextVariable(contextVariableId);

    if (contextVariable == nullptr) {
        log::error("Invalid context variable id: " + std::to_string(contextVariableId));
        return;
    }

    getVariableContext(contextVariable->contextId)->setRead(contextVariable->localVariableId, read);
}

void Runtime::setContextVariableValue(const NomadId contextVariableId, const RuntimeValue& value) const {
    const auto* contextVariable = getContextVariable(contextVariableId);

    if (contextVariable == nullptr) {
        log::error("Invalid context variable id: " + std::to_string(contextVariableId));
        return;
    }

    getVariableContext(contextVariable->contextId)->setValue(contextVariable->localVariableId, value);
}

void Runtime::getContextVariableValue(const NomadId contextVariableId, RuntimeValue& value) const {
    const auto* contextVariable = getContextVariable(contextVariableId);

    if (contextVariable == nullptr) {
        log::error("Invalid context variable id: " + std::to_string(contextVariableId));
        return;
    }

    getVariableContext(contextVariable->contextId)->getValue(contextVariable->localVariableId, value);
}

void Runtime::setStringContextVariableValue(const NomadId contextVariableId, const NomadString& value) const {
    setStringContextVariableValue(contextVariableId, value.c_str());
}

void Runtime::setStringContextVariableValue(const NomadId contextVariableId, const NomadChar* value) const {
    // The variable context copies what it keeps, so the string is only borrowed.
    RuntimeValue stringValue;
    stringValue.setStringRefValue(value);

    setContextVariableValue(contextVariableId, stringValue);
}

void Runtime::getStringContextVariableValue(const NomadId contextVariableId, NomadString& value) const {
    RuntimeValue stringValue;
    getContextVariableValue(contextVariableId, stringValue);
    value = stringValue.getStringValue();
}

NomadId Runtime::internContextVariable(const NomadId contextId, const NomadId localVariableId) {
    for (NomadIndex contextVariableIndex = 0; contextVariableIndex < m_contextVariables.size(); ++contextVariableIndex) {
        const auto& contextVariable = m_contextVariables[contextVariableIndex];

        if (contextVariable.contextId == contextId && contextVariable.localVariableId == localVariableId) {
            return toNomadId(contextVariableIndex);
        }
    }

    const auto contextVariableId = toNomadId(m_contextVariables.size());
    m_contextVariables.push_back({contextId, localVariableId});
    return contextVariableId;
}

const Runtime::ContextVariable* Runtime::getContextVariable(const NomadId contextVariableId) const {
    if (isIdOutOfRange(contextVariableId, m_contextVariables.size())) {
        return nullptr;
    }

    return &m_contextVariables[toNomadIndex(contextVariableId)];
}

NomadId Runtime::getFirstVariableContextId() const {
    if (m_variables.empty()) {
        return NOMAD_INVALID_ID;
    }

    return toNomadId(0);
}

NomadId Runtime::getNextVariableContextId(const NomadId currentId) const {
    const auto currentIndex = toNomadIndex(currentId);

    if (currentIndex + 1 >= m_variables.size()) {
        return NOMAD_INVALID_ID;
    }

    return currentId + 1;
}

NomadId Runtime::registerFunction(
    const NomadString& name,
    const NomadString& path,
    const NomadString& source
    ) {
    if (getCallableId(name).isValid()) {
        log::error("Function with name '" + name + "' conflicts with an already registered callable.");

        return NOMAD_INVALID_ID;
    }

    const auto id = toNomadId(m_functions.size());

    m_functions.push_back(std::make_unique<Function>(id, name, path, source));

    addCallable(name, CallableId{CallableKind::Function, id});

    return id;
}

NomadId Runtime::getFunctionId(const NomadString& name) const {
    const auto callable = getCallableId(name);

    if (callable.kind != CallableKind::Function) {
        return NOMAD_INVALID_ID;
    }

    return callable.id;
}

Function* Runtime::getFunction(NomadId function_id) const {
    if (isIdOutOfRange(function_id, m_functions.size())) {
        return nullptr;
    }

    return m_functions[static_cast<NomadIndex>(function_id)].get();
}

std::optional<NomadString> Runtime::getFunctionName(NomadId functionId) const {
    if (isIdOutOfRange(functionId, m_functions.size())) {
        return std::nullopt;
    }

    return m_functions[static_cast<NomadIndex>(functionId)]->getName();
}

Function * Runtime::getFunctionByStartAddress(NomadIndex startAddress) const {
    for (const auto& function: m_functions) {
        if (function->getFunctionStart() == startAddress) {
            return function.get();
        }
    }

    return nullptr;
}

NomadIndex Runtime::getFunctionCount() const {
    return m_functions.size();
}

void Runtime::getFunctions(std::vector<Function*>& functions) const {
    functions.clear();
    functions.reserve(m_functions.size());

    for (const auto& function : m_functions) {
        functions.push_back(function.get());
    }
}

void Runtime::getFunctions(TempVector<Function*>& functions) {
    functions.clear();
    functions.reserve(m_functions.size());

    for (const auto& function : m_functions) {
        functions.push_back(function.get());
    }
}

Function * Runtime::getFunctionAtInstructionIndex(const NomadIndex instructionIndex) const {
    for (const auto& function : m_functions) {
        if (instructionIndex >= function->getFunctionStart() && instructionIndex < function->getFunctionEnd()) {
            return function.get();
        }
    }

    return nullptr;
}

NomadIndex Runtime::getFunctionSize() const {
    NomadIndex size = 0;

    for (const auto& function: m_functions) {
        size += function->getFunctionLength();
    }

    return size;
}

std::unique_ptr<Compiler> Runtime::createCompiler() {
    return std::make_unique<Compiler>(this, m_instructions);
}

std::unique_ptr<VirtualMachine> Runtime::createVirtualMachine() {
    return std::make_unique<VirtualMachine>(this);
}

void Runtime::executeFunction(const NomadId functionId) {
    executeFunction(functionId, {});
}

const Function* Runtime::runClosure(const Closure* closure, const std::vector<RuntimeValue>& args) {
    if (m_virtualMachine == nullptr) {
        m_virtualMachine = std::make_unique<VirtualMachine>(this);
    }

    const auto function = closure != nullptr ? closure->getFunction() : nullptr;

    if (function == nullptr) {
        log::error("Invalid function in closure");
        return nullptr;
    }

    // New design: Pass captured parameters as closures vector, explicit parameters separately
    // TODO: retrieve const ref to captures from `closure` instead of creating a new `std::vector`
    std::vector<RuntimeValue> captures;
    for (NomadIndex i = 0; i < closure->getCaptureCount(); ++i) {
        captures.push_back(closure->getCaptureValue(i));
    }

    m_virtualMachine->run(function, args, captures);

    return function;
}

void Runtime::executeFunction(const Closure* closure, const std::vector<RuntimeValue>& args) {
    const auto function = runClosure(closure, args);

    if (function == nullptr) {
        return;
    }

    // The result is discarded.
    if (const auto returnType = function->getReturnType()) {
        returnType->freeValue(m_virtualMachine->getResult());
    }
}

void Runtime::executeFunction(const Closure* closure, const std::vector<RuntimeValue>& args, RuntimeValue& returnValue) {
    const auto function = runClosure(closure, args);

    if (function == nullptr) {
        return;
    }

    if (const auto returnType = function->getReturnType()) {
        // The caller receives its own copy; the virtual machine's result is released.
        returnType->copyValue(m_virtualMachine->getResult(), returnValue);
        returnType->freeValue(m_virtualMachine->getResult());
    }  // Else keep existing value
}

void Runtime::executeFunction(const NomadId functionId, const std::vector<RuntimeValue> &args) {
    if (m_virtualMachine == nullptr) {
        m_virtualMachine = std::make_unique<VirtualMachine>(this);
    }

    const auto function = getFunction(functionId);

    if (function == nullptr) {
        log::error("Invalid function id");
        return;
    }

    m_virtualMachine->run(function, args);

    // The result is discarded.
    if (const auto returnType = function->getReturnType()) {
        returnType->freeValue(m_virtualMachine->getResult());
    }
}

void Runtime::executeFunction(const NomadId functionId, const std::vector<RuntimeValue> &args, RuntimeValue& returnValue) {
    if (m_virtualMachine == nullptr) {
        m_virtualMachine = std::make_unique<VirtualMachine>(this);
    }

    const auto function = getFunction(functionId);

    if (function == nullptr) {
        log::error("Invalid function id");
        return;
    }

    m_virtualMachine->run(function, args);

    const auto returnType = function->getReturnType();

    if (returnType) {
        // The caller receives its own copy; the virtual machine's result is released.
        returnType->copyValue(m_virtualMachine->getResult(), returnValue);
        returnType->freeValue(m_virtualMachine->getResult());
    }  // Else keep existing value
}

void Runtime::executeNativeFunction(
    const NomadId nativeFunctionId,
    const std::vector<RuntimeValue>& args,
    Interpreter& context
) {
    if (context.hasError()) {
        return;
    }

    NativeFunctionDefinition definition;
    if (!getNativeFunctionDefinition(nativeFunctionId, definition)) {
        context.clearResult();
        context.setError("Unknown native function id");
        return;
    }
    if (args.size() != definition.parameters.size()) {
        context.clearResult();
        context.setError("Incorrect argument count when calling native function '" + definition.name + "'");
        return;
    }
    if (definition.returnType == nullptr) {
        context.clearResult();
        context.setError("NativeFunction '" + definition.name + "' has no return type");
        return;
    }
    if (std::any_of(definition.parameters.begin(), definition.parameters.end(), [](const auto& parameter) {
        return parameter.type == nullptr;
    })) {
        context.clearResult();
        context.setError("NativeFunction '" + definition.name + "' has an invalid parameter type");
        return;
    }

    VirtualMachine virtualMachine(this);
    try {
        virtualMachine.runNativeFunction(nativeFunctionId, args);
        if (definition.returnType->isVoid()) {
            context.setVoidResult();
        } else {
            context.setResult(definition.returnType, virtualMachine.getResult());
        }
    } catch (const NomadException& exception) {
        context.clearResult();
        context.setError(exception.what());
        if (!definition.returnType->isVoid()) {
            definition.returnType->freeValue(virtualMachine.m_result);
        }
        return;
    } catch (...) {
        if (!definition.returnType->isVoid()) {
            definition.returnType->freeValue(virtualMachine.m_result);
        }
        throw;
    }

    if (!definition.returnType->isVoid()) {
        definition.returnType->freeValue(virtualMachine.m_result);
    }
}

void dumpFunctionDeclaration(const Function* function, std::ostream& out) {
    out << "function " << function->getName();

    for (NomadIndex i = 0; i < function->getParameterCount(); ++i) {
        const auto parameterId = toNomadId(i);

        out << " " << function->getParameterName(parameterId) << ":" << function->getParameterType(parameterId)->getTypeName();
    }

    out << std::endl;
}

void Runtime::dumpInstructions(std::ostream& out) const {
    std::vector<Function*> functions;
    getFunctions(functions);

    for (const auto* function : functions) {
        dumpInstructions(out, function);
    }
}

void Runtime::dumpInstructions(std::ostream& out, const Function* function) const {
    dumpFunctionDeclaration(function, out);

    for (auto i = function->getFunctionStart(); i < function->getFunctionEnd(); ++i) {
        auto fn = m_instructions[i].fn;

        auto instructionId = getInstructionId(fn);

        if (instructionId == NOMAD_INVALID_ID) {
            out << std::setfill('0') << std::setw(6) << i << ": Unknown instruction" << std::endl;
            continue;
        }

        auto& name = getInstructionName(instructionId);
        auto& operands = getInstructionOperands(instructionId);

        out << std::setfill('0') << std::setw(6) << i << ": " << name << std::endl;

        for (auto operand : operands) {
            i++;

            auto& value = m_instructions[i].value;

            NomadString textValue;

            switch (operand) {
                case Operand::Boolean:
                    textValue = value.getBooleanValue() ? "true" : "false";
                    break;
                case Operand::Integer:
                    textValue = std::to_string(value.getIntegerValue());
                    break;
                case Operand::Float:
                    textValue = std::to_string(value.getFloatValue());
                    break;
                case Operand::String:
                    textValue = "\"" + getString(value.getIdValue()) + "\" (" + std::to_string(value.getIdValue()) + ")";
                    break;
                case Operand::Id:
                    textValue = std::to_string(value.getIdValue());
                    break;
                case Operand::Function: {
                    const auto functionStart = value.getIndexValue();

                    const auto calledFunction = getFunctionByStartAddress(functionStart);

                    textValue = calledFunction->getName() + " (" + std::to_string(calledFunction->getId()) + ")";
                    break;
                }
                case Operand::NativeFunction: {
                    NativeFunctionDefinition definition;
                    auto result = getNativeFunctionDefinition(value.getIdValue(), definition);

                    if (result) {
                        textValue = definition.name + " (" + std::to_string(definition.id) + ")";
                    } else {
                        textValue = "<unknown native function> (" + std::to_string(value.getIdValue()) + ")";
                    }
                    break;
                }
                case Operand::FunctionVariable:
                    textValue = function->getVariableName(value.getIdValue()) + " (" + std::to_string(value.getIdValue()) + ")";
                    break;
                case Operand::DynamicVariable:
                    textValue = getDynamicVariableName(value.getIdValue()) + " (" + std::to_string(value.getIdValue()) + ")";
                    break;
                case Operand::ContextVariableId:
                    textValue = getContextVariableName(value.getIdValue()) + " (" + std::to_string(value.getIdValue()) + ")";
                    break;
                case Operand::FormatString:
                    textValue = "$\"" + getFormatString(value.getIdValue())->getFormatString() + "\" (" + std::to_string(value.getIdValue()) + ")";
                    break;
                case Operand::Index:
                    textValue = std::to_string(value.getIndexValue());
                    break;
                default:
                    textValue = "unknown (" + std::to_string(static_cast<int>(operand)) + ")";
                    log::debug("Unknown operand type: " + std::to_string(static_cast<int>(operand)));
            }

            out
                << std::setfill('0')
                << std::setw(6)
                << i
                << ": "
                << textValue
                << std::endl;
        }

        out.flush();
    }
}

void Runtime::dumpDocumentation(std::ostream &out) const {
    // Output TOC
    out << "# Nomad Engine Reference" << std::endl;

    out << "* [Constants](#constants)" << std::endl;
    out << "* [Variable contexts](#variable-contexts)" << std::endl;
    out << "* [NativeFunctions](#nativefunctions)" << std::endl;
    out << "* [Instructions](#instructions)" << std::endl;

    out << "---" << std::endl;

    out << "## Constants" << std::endl;

    for (NomadIndex i = 0; i < m_constantsMap.getVariableCount(); ++i) {
        RuntimeValue constantValue;
        NomadString constantTextValue;

        const auto constantId = toNomadId(i);

        const auto constantType = getConstantType(constantId);
        getConstantValue(constantId, constantValue);

        if (constantType == getVoidType()) {
            constantTextValue = "";
        } else if (constantType == getBooleanType()) {
            constantTextValue = constantValue.getBooleanValue() ? "true" : "false";
        } else if (constantType == getIntegerType()) {
            constantTextValue = std::format("{0} ({0:#0x})", constantValue.getIntegerValue());
        } else if (constantType == getFloatType()) {
            constantTextValue = std::format("{:}", constantValue.getFloatValue());
        } else if (constantType == getStringType()) {
            NomadString text = constantValue.getStringValue();
            NomadString escaped;

            escaped.reserve(text.size());

            for (char ch : text) {
                switch (ch) {
                    case '\\': escaped += "\\\\"; break;
                    case '\n': escaped += "\\n"; break;
                    case '\t': escaped += "\\t"; break;
                    case '\r': escaped += "\\r"; break;
                    case '\"': escaped += "\\\""; break;
                    case '\b': escaped += "\\b"; break;
                    case '\f': escaped += "\\f"; break;
                    default: {
                        escaped += ch;
                    }
                }
            }
            constantTextValue = std::format("\"{}\"", escaped);
        } else if (constantType == getFunctionType()) {
            constantTextValue = getFunction(constantValue.getIdValue())->getName();
        } else {
            constantTextValue = "<unknown_type>";
        }

        out
            << "* `"
            << getConstantName(constantId)
            << ":"
            << getConstantType(constantId)->getTypeName()
            << " = "
            << constantTextValue
            << "`"
            << std::endl;
    }

    out << std::endl;

    out << "## Variable contexts" << std::endl;

    for (auto& context: m_variables) {
        out << "- `" << context.prefix << "`" << " (" << context.name << ")" << std::endl;
    }

    out << std::endl;

    out << "## NativeFunctions" << std::endl;

    for (auto& nativeFunction: m_nativeFunctions) {
        out << "### `" << nativeFunction.name << "`" << std::endl;

        for (auto& parameter: nativeFunction.parameters) {
            out << "- `" << parameter.name << ":" << parameter.type->getTypeName() << "`" << std::endl;
        }

        out << std::endl;

        out << "`return " << nativeFunction.returnType->getTypeName() << "`" << std::endl;

        out << std::endl;

        out << nativeFunction.doc << std::endl;
        out << std::endl;
        out << "---" << std::endl;
    }

    out << std::endl;

    out << "## Instructions" << std::endl;

    for (auto& op_code: m_opCodes) {
        out << "`" << op_code.name << "`" << std::endl;
        out << op_code.doc << std::endl;
        out << std::endl;
    }

    out.flush();
}

} // namespace nomad
