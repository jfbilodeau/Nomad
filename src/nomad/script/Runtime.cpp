// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Runtime.hpp>

#include <nomad/compiler/Compiler.hpp>

#include <nomad/script/BaseTypes.hpp>
#include <nomad/script/Closure.hpp>
#include <nomad/script/FormatString.hpp>
#include <nomad/script/Hub.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/NativeFunctions.hpp>
#include <nomad/script/NativeMembers.hpp>
#include <nomad/script/NativeProperty.hpp>
#include <nomad/script/NomadStringObject.hpp>
#include <nomad/script/RegisterRangeType.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/StructType.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/String.hpp>

#include <boost/json/array.hpp>

#include <algorithm>
#include <utility>

namespace nomad {

const ClassType* Runtime::getIteratorType(const Type* valueType) {
    if (!ownsType(valueType) || valueType->isVoid() || valueType->isInternal()) {
        throw NomadException("Iterator value type must be a registered non-void type");
    }
    const auto* type = getParameterizedType("Iterator", {valueType});
    if (type == nullptr) {
        throw NomadException("Failed to specialize iterator type");
    }
    return static_cast<const ClassType*>(type);
}

void Runtime::addIterable(const TypeId id, NativeInterableDefinition definition) {
    const auto type = getType(id);
    auto* owner = type.has_value() ? getMutableClassType((*type)->asClassType()) : nullptr;
    if (owner == nullptr) {
        throw NomadException("Cannot register iterable: owner must identify a class type");
    }
    const auto valueType = getType(definition.valueTypeId);
    if (!valueType.has_value() || (*valueType)->isVoid() || (*valueType)->isInternal()) {
        throw NomadException("Cannot register iterable: value type must identify a public, non-void type");
    }
    if (owner->getIterableInfo().has_value()) {
        throw NomadException("Cannot register iterable: type already has an iterable association");
    }
    if (!definition.factory.parameters.empty() || !definition.factory.function) {
        throw NomadException("Cannot register iterable: a parameterless factory callback is required");
    }
    const auto returnType = getType(definition.factory.returnTypeId);
    if (!returnType.has_value() || *returnType != getIteratorType(*valueType)) {
        throw NomadException("Cannot register iterable: factory must return the matching iterator type");
    }
    const auto factoryId = addMethod(id, std::move(definition.factory));
    if (factoryId.isInvalid()) {
        throw NomadException("Cannot register iterable: failed to register factory method");
    }
    owner->registerIterableType(*this, *valueType, getMethod(factoryId));
}

std::optional<IterableInfo> Runtime::getIterableInfo(const Type* sourceType) const {
    if (!ownsType(sourceType)) {
        return std::nullopt;
    }
    const auto* owner = sourceType->asClassType();
    return owner == nullptr ? std::nullopt : owner->getIterableInfo();
}

Runtime::Runtime():
    m_typeManager(*this)
{
    // Types
    registerType(std::make_unique<VoidType>(this, reserveTypeId()));
    registerType(std::make_unique<IdType>(this, reserveTypeId()));
    registerType(std::make_unique<FloatType>(this, reserveTypeId()));
    registerType(std::make_unique<IntegerType>(this, reserveTypeId()));
    registerType(std::make_unique<BooleanType>(this, reserveTypeId()));
    registerType(std::make_unique<StringType>(this, reserveTypeId()));
    registerType(std::make_unique<FileNameType>(this, reserveTypeId()));
    registerType(std::make_unique<FunctionNameType>(this, reserveTypeId()));
    registerType(std::make_unique<FunctionLineNumberType>(this, reserveTypeId()));
    registerType(std::make_unique<EventCallbackType>(this, reserveTypeId()));
    registerType(std::make_unique<EventDispatchType>(this, reserveTypeId()));
    if (!registerParameterizedType(
        "Iterator", 1,
        [](Runtime* runtime, TypeId id, const std::vector<const Type*>& arguments) {
            return std::make_unique<ClassType>(
                runtime, id, "Iterator<" + arguments[0]->getTypeName() + ">", ClassMode::Closed, arguments
            );
        },
        [](Runtime* runtime, const TypeId id) {
            const auto* type = runtime->getType(id).value();
            const auto* iteratorType = static_cast<const ClassType*>(type);
            const auto* valueType = iteratorType->getTypeArguments()[0];
            const auto advanceId = runtime->addMethod(id, {
                .name = "moveNext",
                .parameters = {},
                .returnTypeId = runtime->getTypeId("bool"),
                .function = [](VirtualMachine* machine) {
                    machine->setBooleanResult(machine->getThisAs<IteratorObject>()->moveNext());
                },
                .documentation = NomadDoc("Advances the iterator. Returns false at the end.")
            });
            const auto currentId = runtime->addProperty(id, {
                .valueType = valueType,
                .name = "current",
                .getter = [valueType](VirtualMachine* machine, RuntimeValue& result) {
                    RuntimeValue owned;
                    valueType->assignValue(
                        &machine->getThisAs<IteratorObject>()->getCurrent(),
                        &owned
                    );
                    result = owned;
                },
                .setter = {},
                .documentation = NomadDoc("The value published by the last successful moveNext call.")
            });
            if (advanceId.isInvalid() || currentId.isInvalid()) {
                throw NomadException("Failed to register iterator members");
            }
        }
    ) || !registerMemberTemplate("Iterator", "moveNext", MemberType::Method) ||
        !registerMemberTemplate("Iterator", "current", MemberType::Property)) {
        throw NomadException("Failed to register iterator type");
    }

    registerRangeType(this);

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
    registerBuiltInNativeMembers(this);

    registerCoreNativeFunctions(this);

//    register_keyword("stringId" NomadDoc("Load the string id of the given string"));
    registerKeyword("fun", NomadDoc("Function declaration"));
    registerKeyword("class", NomadDoc("Declares a closed class type; use open class to allow compile-time extension"));
    registerKeyword("hub", NomadDoc("Groups declarations in a namespace; use open hub to allow additional declaration blocks"));
    registerKeyword("open", NomadDoc("Declares an extensible class or hub"));
    registerKeyword("new", NomadDoc("Constructs a new object"));
    registerKeyword("if", NomadDoc("if statement"));
    registerKeyword("else", NomadDoc("`else` branch of an `if` statement"));
    registerKeyword("end", NomadDoc("Closes a function, conditional, or loop body"));
    registerKeyword("while", NomadDoc("Repeats while a boolean condition is true"));
    registerKeyword("until", NomadDoc("Repeats until a boolean condition is true"));
    registerKeyword("do", NomadDoc("Starts a post-test loop, closed by `end while` or `end until`"));
    registerKeyword("for", NomadDoc("Iterates an inclusive integer range or an iterable object"));
    registerKeyword("in", NomadDoc("Introduces the iterable source of a for loop"));
    registerKeyword("break", NomadDoc("Exits the nearest loop in the current function"));
    registerKeyword("continue", NomadDoc("Advances the nearest loop in the current function"));
    registerKeyword("then", NomadDoc("Uses the remainder of a script or function root as a callback body"));
    registerKeyword("const", NomadDoc("Declares a constant"));
    registerKeyword("enum", NomadDoc("Declares an enum"));

    registerConstant("false", RuntimeValue(NOMAD_FALSE), getBooleanType());
    registerConstant("true", RuntimeValue(NOMAD_TRUE), getBooleanType());
    registerConstant("pi", RuntimeValue(NOMAD_PI), getFloatType());

    registerVariableContext("global", "global.", std::make_unique<SimpleVariableContext>());
}

Runtime::~Runtime() {
    for (NomadIndex constantIndex = 0; constantIndex < m_constants.size(); ++constantIndex) {
        if (const auto* type = m_constantsMap.getVariableType(VariableId{toNomadId(constantIndex)})) {
            type->freeValue(&m_constants[constantIndex]);
        }
    }

    for (auto* string : m_strings) {
        string->releaseRef();
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

CallableId Runtime::registerNativeFunction(
    const NomadString& name,
    NativeFunctionFn nativeFunction_fn,
    const std::vector<Parameter>& parameters,
    const Type* returnType,
    NomadDocArg
) {
    return registerNativeCallable(name, std::move(nativeFunction_fn), parameters, returnType, doc);
}

std::optional<NomadString> Runtime::qualifyHubMemberName(const HubId hubId, const NomadString& name) const {
    const auto* hub = getHub(hubId);
    if (hub == nullptr) {
        log::error("Cannot register hub member '" + name + "': invalid hub");
        return std::nullopt;
    }
    return hub->getName() + "." + name;
}

TypeId Runtime::registerHubClass(
    const HubId hubId,
    NomadString name,
    const ClassMode mode,
    NomadString documentation
) {
    const auto qualifiedName = qualifyHubMemberName(hubId, name);
    if (!qualifiedName) {
        return TypeId::INVALID;
    }
    return registerClass(*qualifiedName, mode, std::move(documentation));
}

ConstantId Runtime::registerHubConstant(
    const HubId hubId,
    const NomadString& name,
    const RuntimeValue& value,
    const Type* type
) {
    const auto* hub = getHub(hubId);
    if (hub == nullptr) {
        log::error("Cannot register hub constant '" + name + "': invalid hub");
        return ConstantId::INVALID;
    }
    if (!ownsType(type) || type->isVoid() || type->isInternal()) {
        log::error("Cannot register hub constant '" + name + "': invalid value type");
        return ConstantId::INVALID;
    }
    return registerConstant(hub->getName() + "." + name, value, type);
}

TypeId Runtime::registerHubEnum(
    const HubId hubId,
    NomadString name,
    std::vector<EnumValueDefinition> values,
    NomadString documentation
) {
    const auto* hub = getHub(hubId);
    if (hub == nullptr) {
        log::error("Cannot register hub enum '" + name + "': invalid hub");
        return TypeId::INVALID;
    }
    return createEnum(hub->getName() + "." + name, std::move(values), std::move(documentation));
}

CallableId Runtime::registerHubNativeFunction(
    const HubId hubId,
    const NomadString& name,
    NativeFunctionFn function,
    const std::vector<Parameter>& parameters,
    const Type* returnType,
    NomadDocArg
) {
    const auto* hub = getHub(hubId);
    if (hub == nullptr) {
        log::error("Cannot register hub native function '" + name + "': invalid hub");
        return CallableId::INVALID;
    }
    return registerNativeFunction(hub->getName() + "." + name, std::move(function), parameters, returnType, doc);
}

CallableId Runtime::registerHubScriptFunction(
    const HubId hubId,
    const NomadString& name,
    const NomadString& path,
    const NomadString& source
) {
    const auto qualifiedName = qualifyHubMemberName(hubId, name);
    if (!qualifiedName) {
        return CallableId::INVALID;
    }
    return registerScriptFunction(*qualifiedName, path, source);
}

bool Runtime::isHub(const NomadString& name) const {
    return getHubId(name).isValid();
}

HubId Runtime::getHubId(const NomadString& name) const {
    for (auto index = NomadIndex{0}; index < m_hubs.size(); ++index) {
        if (m_hubs[index]->getName() == name) {
            return HubId{toNomadId(index)};
        }
    }
    return HubId::INVALID;
}

const Hub* Runtime::getHub(const HubId id) const {
    return isIdOutOfRange(id.id, m_hubs.size()) ? nullptr : m_hubs[toNomadIndex(id.id)].get();
}

NomadString Runtime::getContainingHub(const NomadString& name) const {
    auto prefix = name;
    while (true) {
        const auto separator = prefix.rfind('.');
        if (separator == NomadString::npos) {
            return {};
        }
        prefix.resize(separator);
        if (isHub(prefix)) {
            return prefix;
        }
    }
}

bool Runtime::isHubVariable(const NomadString& name) const {
    const auto* hub = getHub(getHubId(getContainingHub(name)));
    if (hub == nullptr) {
        return false;
    }
    const auto* member = hub->getMember(name.substr(hub->getName().size() + 1));
    return member != nullptr && std::holds_alternative<VariableId>(*member);
}

ContextVariableId Runtime::registerHubVariable(const HubId hubId, const NomadString& name) {
    const auto qualifiedName = qualifyHubMemberName(hubId, name);
    if (!qualifiedName) {
        return ContextVariableId::INVALID;
    }
    return registerHubVariable(*qualifiedName);
}

ContextVariableId Runtime::registerHubVariable(const NomadString& name) {
    if (m_scriptInitialization.state != ScriptInitializationState::Pending ||
        getContainingHub(name).empty() || !canRegisterHubMember(name) ||
        isHub(name) || isHubVariable(name) ||
        getCallableId(name).isValid() || getConstantId(name).isValid() ||
        getTypeByName(name) != nullptr || getEventId(name).isValid() ||
        getDynamicVariableId(name).isValid() ||
        getVariableContextIdByPrefix(name).isValid()) {
        log::error("Cannot register hub variable '" + name + "': missing hub or conflicting declaration");
        return ContextVariableId::INVALID;
    }
    auto& hub = *m_hubs[toNomadIndex(getHubId(getContainingHub(name)).id)];
    if (hub.m_variableContextId.isInvalid()) {
        hub.m_variableContextId = VariableContextId{toNomadId(m_variables.size())};
        m_variables.push_back({
            hub.m_variableContextId, hub.getName(), "$hub." + hub.getName() + ".", hub.m_variables
        });
    }
    const auto id = registerContextVariable(hub.m_variableContextId, name, nullptr);
    if (id.isValid()) {
        registerHubMember(name, hub.m_variables->getVariableId(name));
    }
    return id;
}

void Runtime::setScriptInitializer(const CallableId functionId) {
    const auto* function = getScriptFunction(functionId);
    if (m_scriptInitialization.state != ScriptInitializationState::Pending ||
        m_scriptInitialization.functionId != CallableId{} || function == nullptr ||
        function->getParameterCount() != 0 || function->getReturnType() != getVoidType() ||
        function->getFunctionStart() == NOMAD_INVALID_INDEX) {
        throw NomadException("Script initialization requires a compiled, parameterless void function before startup");
    }
    m_scriptInitialization.functionId = functionId;
}

void Runtime::initializeScripts() {
    auto& initialization = m_scriptInitialization;
    if (initialization.state == ScriptInitializationState::Failed) {
        throw VirtualMachineException("Script initialization failed: " + initialization.error);
    }
    if (initialization.state == ScriptInitializationState::Complete) {
        return;
    }
    if (initialization.state == ScriptInitializationState::Running) {
        throw VirtualMachineException("Script initialization cannot be re-entered");
    }
    if (initialization.functionId == CallableId{}) {
        for (const auto& hub : m_hubs) {
            if (hub->m_variables->getVariableCount() != 0) {
                throw VirtualMachineException("Script initialization requires successful executable compilation");
            }
        }
        initialization.state = ScriptInitializationState::Complete;
        return;
    }
    initialization.state = ScriptInitializationState::Running;
    try {
        VirtualMachine machine(this);
        machine.run(getScriptFunction(initialization.functionId), {});
        initialization.state = ScriptInitializationState::Complete;
    } catch (const std::exception& exception) {
        initialization.error = exception.what();
        initialization.state = ScriptInitializationState::Failed;
        throw;
    }
}

bool Runtime::canRegisterHubMember(const NomadString& name, const bool function) const {
    if (name.find('@') != NomadString::npos) {
        return true;
    }
    const auto* hub = getHub(getHubId(getContainingHub(name)));
    if (hub == nullptr) {
        return true;
    }
    const auto* member = hub->getMember(name.substr(hub->getName().size() + 1));
    if (member == nullptr || (function && std::holds_alternative<HubFunctions>(*member))) {
        return true;
    }
    log::error("Hub member '" + name + "' is already declared");
    return false;
}

namespace {

void addHubMember(
    std::unordered_map<NomadString, HubMember>& members,
    const NomadString& name,
    HubMember member
) {
    const auto existing = members.find(name);
    if (existing == members.end()) {
        members.emplace(name, std::move(member));
        return;
    }
    auto* functions = std::get_if<HubFunctions>(&existing->second);
    const auto* additions = std::get_if<HubFunctions>(&member);
    if (functions == nullptr || additions == nullptr) {
        throw NomadException("Conflicting hub member '" + name + "'");
    }
    functions->ids.insert(functions->ids.end(), additions->ids.begin(), additions->ids.end());
}

} // namespace

void Runtime::registerHubMember(const NomadString& name, HubMember member) {
    if (name.find('@') != NomadString::npos) {
        return;
    }
    const auto id = getHubId(getContainingHub(name));
    if (id.isValid()) {
        auto& hub = *m_hubs[toNomadIndex(id.id)];
        addHubMember(hub.m_members, name.substr(hub.getName().size() + 1), std::move(member));
    }
}

void Runtime::rebuildHubMembers() {
    std::vector<std::unordered_map<NomadString, HubMember>> members(m_hubs.size());
    const auto add = [&](const NomadString& name, HubMember member) {
        if (name.find('@') != NomadString::npos) {
            return;
        }
        const auto id = getHubId(getContainingHub(name));
        if (id.isValid()) {
            const auto index = toNomadIndex(id.id);
            addHubMember(members[index], name.substr(m_hubs[index]->getName().size() + 1), std::move(member));
        }
    };
    for (auto index = NomadIndex{0}; index < m_hubs.size(); ++index) {
        const auto& hub = *m_hubs[index];
        for (auto id = hub.m_variables->getFirstVariableId(); id.isValid();
             id = hub.m_variables->getNextVariableId(id)) {
            add(hub.m_variables->getVariableName(id), id);
        }
    }
    for (const auto* callable : m_callables) {
        if (callable->getType() == CallableType::Function) {
            add(callable->getName(), HubFunctions{{callable->getId()}});
        }
    }
    for (auto id = getFirstConstantId(); id.isValid(); id = getNextConstantId(id)) {
        add(getConstantName(id), id);
    }
    for (auto index = NomadIndex{0}; index < m_typeManager.getTypeCount(); ++index) {
        const auto type = getType(TypeId{toNomadId(index)});
        if (type.has_value() && getEventId((*type)->getTypeName()).isInvalid()) {
            add((*type)->getTypeName(), *type);
        }
    }
    for (const auto& event : m_events) {
        add(event.name, event.id);
    }
    for (auto index = NomadIndex{0}; index < m_dynamicVariables.size(); ++index) {
        add(m_dynamicVariables[index].name, DynamicVariableId{toNomadId(index)});
    }
    for (const auto& family : m_parameterizedTypes) {
        add(family.name, HubTypeFamily{family.argumentCount});
    }
    for (const auto& family : m_parameterizedConstructors) {
        add(family.name, HubTypeFamily{family.argumentCount});
    }
    for (auto index = NomadIndex{0}; index < m_hubs.size(); ++index) {
        m_hubs[index]->m_members = std::move(members[index]);
    }
}

HubId Runtime::registerHub(const NomadString& name, const HubMode mode) {
    if (name.empty() || (mode != HubMode::Opened && mode != HubMode::Closed)) {
        log::error("Cannot register a hub with an empty name or invalid mode");
        return HubId::INVALID;
    }
    const auto existingId = getHubId(name);
    if (existingId.isValid()) {
        const auto* existing = getHub(existingId);
        if (existing->getMode() != mode || mode == HubMode::Closed) {
            log::error("Cannot redeclare hub '" + name + "': declarations must all be open");
            return HubId::INVALID;
        }
        return existingId;
    }
    NomadIndex start = 0;
    while (start < name.size()) {
        const auto end = name.find('.', start);
        const auto length = (end == NomadString::npos ? name.size() : end) - start;
        if (length == 0) {
            log::error("Invalid hub name '" + name + "'");
            return HubId::INVALID;
        }
        if (end == NomadString::npos) {
            break;
        }
        start = end + 1;
        if (start == name.size()) {
            log::error("Invalid hub name '" + name + "'");
            return HubId::INVALID;
        }
    }
    if (getCallableId(name).isValid() || getConstantId(name).isValid() ||
        getTypeByName(name) != nullptr || getEventId(name).isValid() ||
        getDynamicVariableId(name).isValid() || isHubVariable(name) ||
        getVariableContextIdByPrefix(name + ".").isValid() ||
        getParameterizedTypeArgumentCount(name).has_value() ||
        getParameterizedConstructorArgumentCount(name).has_value()) {
        log::error("Cannot register hub '" + name + "': identifier is already used");
        return HubId::INVALID;
    }
    const auto previousCount = m_hubs.size();
    m_hubs.push_back(std::make_unique<Hub>(name, mode));
    if (previousCount != m_hubs.size()) {
        try {
            rebuildHubMembers();
        } catch (const NomadException& exception) {
            m_hubs.resize(previousCount);
            log::error("Cannot register hub '" + name + "': " + exception.what());
            return HubId::INVALID;
        }
    }
    return getHubId(name);
}

void Runtime::validateScriptExecution() const {
    if (m_scriptInitialization.state == ScriptInitializationState::Failed) {
        throw VirtualMachineException("Script initialization failed: " + m_scriptInitialization.error);
    }
    if (m_scriptInitialization.state == ScriptInitializationState::Pending) {
        if (m_scriptInitialization.functionId != CallableId{} ||
            std::ranges::any_of(m_hubs, [](const auto& hub) {
                return hub->m_variables->getVariableCount() != 0;
            })) {
            throw VirtualMachineException("Call Runtime::initializeScripts before executing scripts with hub variables");
        }
    }
}

CallableId Runtime::registerNativeCallable(
    const NomadString& name,
    NativeFunctionFn fn,
    const std::vector<Parameter>& parameters,
    const Type* returnType,
    const NomadString& doc
) {
    if (isHub(name) || !canRegisterHubMember(name, true)) {
        log::error("Cannot register function '" + name + "': name is reserved by a hub");
        return CallableId::INVALID;
    }
    const auto isDeferred = [this](const Type* type) {
        return type != nullptr && type->getRuntime() == this &&
            dynamic_cast<const DeferredType*>(type) != nullptr;
    };
    const auto deferred = isDeferred(returnType) ||
        std::ranges::any_of(parameters, [&](const auto& parameter) { return isDeferred(parameter.type); });
    if (!fn || (!isDeferred(returnType) &&
        (!ownsType(returnType) || (returnType->isInternal() && !returnType->isVoid())))) {
        log::error("Cannot register native callable '" + name + "': invalid callback or return type");
        return CallableId::INVALID;
    }
    for (const auto& parameter : parameters) {
        if (parameter.name.empty() || (!isDeferred(parameter.type) &&
            (!ownsType(parameter.type) || parameter.type->isVoid()))) {
            log::error("Cannot register native callable '" + name + "': invalid parameter definition");
            return CallableId::INVALID;
        }
    }
    auto pending = deferred;
    for (const auto* callable : m_callables) {
        if (callable->getName() != name || callable->getType() != CallableType::Function) {
            continue;
        }
        const auto* native = static_cast<const Function*>(callable)->asNativeFunction();
        if (native == nullptr) {
            log::error("Cannot register native function '" + name + "': a callable of a different kind exists");
            return CallableId::INVALID;
        }
        pending = pending || !native->isSignatureResolved();
    }
    if (!pending && getCallableOverloadCount(name) != 0) {
        if (NomadString error; !canOverloadCallable(name, parameters, returnType, error)) {
            log::error("Cannot register native function '" + name + "': " + error);
            return CallableId::INVALID;
        }
    }
    auto function = std::make_unique<NativeFunction>(
        name, 
        std::move(fn), 
        parameters, 
        returnType, 
        doc, 
        pending
    );
    const auto id = addCallableToTable(std::move(function));

    registerHubMember(name, HubFunctions{{id}});

    return id;
}

NativeFunction* Runtime::getMutableNativeFunction(const CallableId id) {
    if (getNativeFunction(id) == nullptr) {
        return nullptr;
    }
    return static_cast<NativeFunction*>(m_callables[toNomadIndex(id.id)]);
}

void Runtime::validateNativeFunctionSignatures() {
    std::vector<NativeFunction*> pending;
    for (const auto& function : m_functions) {
        auto* native = dynamic_cast<NativeFunction*>(function.get());
        if (native != nullptr && !native->isSignatureResolved()) {
            if (!ownsType(native->getReturnType()) ||
                std::ranges::any_of(native->getParameters(), [this](const auto& parameter) {
                    return !ownsType(parameter.type);
                })) {
                throw NomadException("Native function '" + native->getName() + "' has an unresolved signature");
            }
            pending.push_back(native);
        }
    }
    for (const auto* function : pending) {
        if (getCallableOverloadCount(function->getName()) > 1) {
            NomadString error;
            if (!canOverloadCallable(
                function->getName(), function->getParameters(), function->getReturnType(), error, function->getId()
            )) {
                throw NomadException("Cannot resolve native function '" + function->getName() + "': " + error);
            }
        }
    }
    for (auto* function : pending) {
        function->completeSignatureResolution();
    }
}

CallableId Runtime::addCallableToTable(std::unique_ptr<Function> function) {
    if (function == nullptr) {
        return CallableId::INVALID;
    }

    m_functions.push_back(std::move(function));
    const auto id = m_functions.back()->registerWithRuntime(*this);
    if (id == CallableId{}) {
        m_functions.pop_back();
    }

    return id;
}

CallableId Runtime::catalogCallable(Callable* callable) {
    if (callable == nullptr || callable->getId() != CallableId{} ||
        m_callables.size() >= toNomadIndex(NOMAD_ID_MAX)) {
        return CallableId::INVALID;
    }

    const auto id = CallableId{toNomadId(m_callables.size())};
    m_callables.push_back(callable);
    return id;
}

const Callable* Runtime::getCallable(const CallableId functionId) const {
    if (isIdOutOfRange(functionId.id, m_callables.size())) {
        return nullptr;
    }

    return m_callables[toNomadIndex(functionId.id)];
}

const Function* Runtime::getFunction(const CallableId functionId) const {
    const auto* callable = getCallable(functionId);
    if (callable == nullptr || callable->getType() != CallableType::Function) {
        return nullptr;
    }
    return static_cast<const Function*>(callable);
}

const NativeFunction* Runtime::getNativeFunction(const CallableId functionId) const {
    const auto* function = getFunction(functionId);
    return function == nullptr ? nullptr : function->asNativeFunction();
}

const Method* Runtime::getMethod(const CallableId callableId) const {
    const auto* callable = getCallable(callableId);
    if (callable == nullptr || callable->getType() != CallableType::Method) {
        return nullptr;
    }
    return static_cast<const Method*>(callable);
}

const Constructor* Runtime::getConstructor(const CallableId callableId) const {
    const auto* callable = getCallable(callableId);
    if (callable == nullptr || callable->getType() != CallableType::Constructor) {
        return nullptr;
    }
    return static_cast<const Constructor*>(callable);
}

const NativeMethod* Runtime::getNativeMethod(const CallableId callableId) const {
    const auto* method = getMethod(callableId);
    return method == nullptr ? nullptr : method->asNativeMethod();
}

const NativeConstructor* Runtime::getNativeConstructor(const CallableId functionId) const {
    const auto* constructor = getConstructor(functionId);
    return constructor == nullptr ? nullptr : constructor->asNativeConstructor();
}

NomadIndex Runtime::getFunctionTableSize() const {
    return m_callables.size();
}

ResolveResult Runtime::resolveAmong(
    std::vector<CallableId> candidates,
    const std::vector<const Type*>& argumentTypes
) const {
    ResolveResult result;
    result.argumentTypes = argumentTypes;

    if (candidates.empty()) {
        result.status = ResolveStatus::UnknownName;

        return result;
    }

    result.candidates = candidates;

    for (const auto candidateId : candidates) {
        const auto* native = getNativeFunction(candidateId);
        if (native != nullptr && !native->isSignatureResolved()) {
            result.status = ResolveStatus::UnresolvedSignature;
            return result;
        }
    }

    std::vector<CallableId> exactMatches;
    std::vector<CallableId> compatibleMatches;
    auto hasArity = false;

    for (const auto candidateId : candidates) {
        const auto* callable = getCallable(candidateId);
        if (callable == nullptr) {
            continue;
        }
        const auto& parameters = callable->getParameters();

        if (parameters.size() != argumentTypes.size()) {
            continue;
        }

        hasArity = true;

        auto isExact = true;
        auto isCompatible = true;

        for (NomadIndex index = 0; index < argumentTypes.size(); ++index) {
            const auto* argumentType = argumentTypes[index];

            // The compiler supplies this argument, so it cannot tell the overloads apart.
            if (argumentType == nullptr) {
                continue;
            }

            const auto* parameterType = parameters[index].type;

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
            exactMatches.push_back(candidateId);
        } else if (isCompatible) {
            compatibleMatches.push_back(candidateId);
        }
    }

    const auto& matches = exactMatches.empty() ? compatibleMatches : exactMatches;

    if (matches.size() == 1) {
        result.status = ResolveStatus::Resolved;
        result.id = matches.front();
    } else if (matches.size() > 1) {
        result.status = ResolveStatus::Ambiguous;
        result.candidates = matches;
    } else {
        result.status = hasArity ? ResolveStatus::NoMatchingTypes : ResolveStatus::NoMatchingArity;
    }

    return result;
}

ResolveResult Runtime::resolveFunction(
    const NomadString& name,
    const std::vector<const Type*>& argumentTypes
) const {
    std::vector<CallableId> candidates;

    for (const auto* callable : m_callables) {
        if (callable->getName() == name && callable->getType() == CallableType::Function) {
            candidates.push_back(callable->getId());
        }
    }

    return resolveAmong(std::move(candidates), argumentTypes);
}

ResolveResult Runtime::resolveMethod(
    const Type* classType,
    const NomadString& name,
    const std::vector<const Type*>& argumentTypes
) const {
    std::vector<CallableId> candidates;

    for (const auto* callable : m_callables) {
        if (callable->getName() == name &&
            callable->getType() == CallableType::Method &&
            callable->getClassType() == classType) {
            candidates.push_back(callable->getId());
        }
    }

    return resolveAmong(std::move(candidates), argumentTypes);
}

ResolveResult Runtime::resolveConstructor(
    const Type* classType,
    const std::vector<const Type*>& argumentTypes
) const {
    std::vector<CallableId> candidates;

    for (const auto* callable : m_callables) {
        if (callable->getType() == CallableType::Constructor && callable->getClassType() == classType) {
            candidates.push_back(callable->getId());
        }
    }

    return resolveAmong(std::move(candidates), argumentTypes);
}

NomadString Runtime::describeResolveFailure(const NomadString& name, const ResolveResult& result) const {
    NomadString argumentList;

    for (const auto* argumentType : result.argumentTypes) {
        if (argumentType == nullptr) {
            continue;
        }

        if (!argumentList.empty()) {
            argumentList += ", ";
        }

        argumentList += getTypeDisplayName(argumentType);
    }

    switch (result.status) {
    case ResolveStatus::Resolved:
        return {};
    case ResolveStatus::UnknownName:
        return "Unknown function '" + name + "'";
    case ResolveStatus::NoMatchingArity:
        return "No overload of '" + name + "' accepts " + std::to_string(result.argumentTypes.size()) + " argument(s)";
    case ResolveStatus::NoMatchingTypes:
        return "No overload of '" + name + "' accepts arguments (" + argumentList + ")";
    case ResolveStatus::Ambiguous:
        return "Ambiguous call to '" + name + "' with arguments (" + argumentList + ")";
    case ResolveStatus::UnresolvedSignature:
        return "Native function '" + name + "' has an unresolved signature";
    }

    return {};
}

CallableId Runtime::getCallableId(const NomadString& name) const {
    for (const auto* callable : m_callables) {
        if (callable->getName() == name && callable->getType() == CallableType::Function) {
            return callable->getId();
        }
    }

    return CallableId::INVALID;
}

void Runtime::getCallableOverloads(const NomadString& name, std::vector<CallableId>& overloads) const {
    overloads.clear();

    for (const auto* callable : m_callables) {
        if (callable->getName() == name && callable->getType() == CallableType::Function) {
            overloads.push_back(callable->getId());
        }
    }
}

NomadIndex Runtime::getCallableOverloadCount(const NomadString& name) const {
    return static_cast<NomadIndex>(std::ranges::count_if(m_callables, [&name](const auto* callable) {
        return callable->getName() == name && callable->getType() == CallableType::Function;
    }));
}

ParameterShape Runtime::getParameterShape(const Type* type) const {
    // Mirrors the order in which `parseCallableArguments` tests parameter types.
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
    const NomadString& name,
    const std::vector<Parameter>& parameters,
    const Type* returnType,
    NomadString& error,
    const CallableId excludedId
) const {
    const auto parameterCount = parameters.size();

    for (NomadIndex index = 0; index < parameterCount; ++index) {
        if (!isOverloadableShape(getParameterShape(parameters[index].type))) {
            error = "parameter '" + parameters[index].name + "' cannot take part in overloading";

            return false;
        }
    }

    const auto returnsValue = returnType != nullptr && !returnType->isVoid();

    for (const auto* callable : m_callables) {
        if (callable->getId() == excludedId ||
            callable->getName() != name || callable->getType() != CallableType::Function) {
            continue;
        }
        const auto* existingFunction = static_cast<const Function*>(callable);
        if (existingFunction == nullptr || existingFunction->asNativeFunction() == nullptr) {
            error = "a callable of a different kind is already registered under that name";

            return false;
        }

        if (existingFunction->getParameterCount() != parameterCount) {
            const auto ordinaryValues = [this](const auto& parameter) {
                return getParameterShape(parameter.type) == ParameterShape::Value;
            };
            if (!std::ranges::all_of(parameters, ordinaryValues) ||
                !std::ranges::all_of(existingFunction->getParameters(), ordinaryValues)) {
                error = "overloads with different parameter counts require ordinary value parameters";
                return false;
            }
        }

        const auto* existingReturnType = existingFunction->getReturnType();

        // The parser decides whether a call is a statement or an expression from the name alone, so the whole set
        // has to agree on whether it yields a value.
        if ((existingReturnType != nullptr && !existingReturnType->isVoid()) != returnsValue) {
            error = "overloads must all return a value or all return nothing";

            return false;
        }

        auto hasSameSignature = existingFunction->getParameterCount() == parameterCount;

        for (NomadIndex index = 0; index < std::min(parameterCount, existingFunction->getParameterCount()); ++index) {
            const auto* candidateType = parameters[index].type;
            const auto* existingType = existingFunction->getParameters()[index].type;
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

TypeId Runtime::reserveTypeId() {
    return m_typeManager.reserveTypeId();
}

void Runtime::registerType(std::unique_ptr<Type>&& type) {
    if (type == nullptr || type->getRuntime() != this ||
        isIdOutOfRange(type->getId().id, m_typeManager.getTypeCount()) ||
        getType(type->getId()).has_value()) {
        throw NomadException("Cannot register a type without a matching runtime and reserved ID");
    }
    if (type->asCallback() == nullptr) {
        if (isHub(type->getTypeName()) || isHubVariable(type->getTypeName())) {
            throw NomadException("Type name '" + type->getTypeName() + "' is reserved by a hub");
        }
        if (getEventId(type->getTypeName()).isInvalid()) {
            if (!canRegisterHubMember(type->getTypeName())) {
                throw NomadException("Type name '" + type->getTypeName() + "' is already a hub member");
            }
            registerHubMember(type->getTypeName(), type.get());
        }
    }
    m_typeManager.catalogType(std::move(type));
}

TypeId Runtime::registerClass(
    NomadString name,
    const ClassMode mode,
    NomadString documentation
) {
    return registerClass<ClassType>(
        std::move(name), mode, std::vector<const Type*>{}, std::move(documentation)
    );
}

bool Runtime::canRegisterClass(const NomadString& name, const ClassMode mode) const {
    if (name.empty()) {
        log::error("Cannot register a class with an empty name");
        return false;
    }
    if (mode != ClassMode::Opened && mode != ClassMode::Closed) {
        log::error("Cannot register class '" + name + "': invalid class mode");
        return false;
    }
    if (getTypeId(name).isValid()) {
        log::error("Type with name '" + name + "' already registered");
        return false;
    }
    if (isHub(name) || isHubVariable(name) || !canRegisterHubMember(name)) {
        log::error("Cannot register class '" + name + "': name is reserved by a hub declaration");
        return false;
    }

    return true;
}

TypeId Runtime::registerStruct(
    NomadString name,
    NomadString documentation
) {
    if (name.empty() || getTypeId(name).isValid() || !canRegisterHubMember(name) ||
        isHub(name) || isHubVariable(name)) {
        log::error("Cannot register struct '" + name + "': invalid or reserved type name");
        return TypeId::INVALID;
    }
    const auto id = reserveTypeId();
    registerType(std::make_unique<StructType>(this, id, std::move(name), std::move(documentation)));
    return id;
}

TypeId Runtime::publishClass(
    const TypeId id,
    const NomadString& name,
    const ClassMode mode,
    std::unique_ptr<ClassType> type
) {
    if (type->getRuntime() != this || type->getId() != id || type->getTypeName() != name ||
        !canRegisterClass(name, mode) ||
        type->isOpen() != (mode == ClassMode::Opened)) {
        log::error("Cannot register class '" + name + "': descriptor does not match its registration request");
        return TypeId::INVALID;
    }
    registerType(std::move(type));
    return id;
}

TypeId Runtime::getTypeId(const NomadString& typeName) const {
    return m_typeManager.getTypeId(typeName);
}

std::optional<const Type*> Runtime::getType(const TypeId id) const {
    return m_typeManager.getType(id);
}

const Type* Runtime::getTypeByName(const NomadString& name) const {
    return m_typeManager.getTypeByName(name);
}

void Runtime::getTypes(std::vector<const Type*>& types) const {
    m_typeManager.getTypes(types);
}

TypeManager& Runtime::getTypeManager() {
    return m_typeManager;
}

const TypeManager& Runtime::getTypeManager() const {
    return m_typeManager;
}

const OpaqueIdType* Runtime::registerOpaqueIdType(const NomadString& name, const NomadString& doc) {
    if (isHub(name) || isHubVariable(name)) {
        log::error("Cannot register type '" + name + "': name is reserved by a hub declaration");
        return nullptr;
    }
    if (name.empty()) {
        log::error("Cannot register an opaque ID type with an empty name");
        return nullptr;
    }

    if (getTypeByName(name) != nullptr) {
        log::error("Type with name '" + name + "' already registered");
        return nullptr;
    }

    auto opaqueType = std::make_unique<OpaqueIdType>(this, reserveTypeId(), name, doc);
    const auto* registeredType = opaqueType.get();
    registerType(std::move(opaqueType));
    return registeredType;
}

TypeId Runtime::createEnum(
    NomadString name,
    std::vector<EnumValueDefinition> values,
    NomadString documentation
) {
    if (isHub(name) || isHubVariable(name) || !canRegisterHubMember(name)) {
        log::error("Cannot create enum '" + name + "': name is reserved by a hub declaration");
        return TypeId::INVALID;
    }
    if (name.empty()) {
        log::error("Cannot create an enum with an empty name");
        return TypeId::INVALID;
    }

    if (getTypeByName(name) != nullptr) {
        log::error("Type with name '" + name + "' already registered");
        return TypeId::INVALID;
    }

    if (values.empty()) {
        log::error("Cannot create enum '" + name + "' without values");
        return TypeId::INVALID;
    }

    for (NomadIndex index = 0; index < values.size(); ++index) {
        const auto& definition = values[index];
        const auto constantName = name + "." + definition.name;

        if (definition.name.empty()) {
            log::error("Cannot create an unnamed value in enum '" + name + "'");
            return TypeId::INVALID;
        }

        if (isHub(constantName) || !canRegisterHubMember(constantName)) {
            log::error("Cannot create enum '" + name + "': value '" + constantName + "' is reserved by a hub");
            return TypeId::INVALID;
        }
        if (getConstantId(constantName).isValid()) {
            log::error("Constant with name '" + constantName + "' already registered");
            return TypeId::INVALID;
        }

        for (NomadIndex previousIndex = 0; previousIndex < index; ++previousIndex) {
            const auto& previousDefinition = values[previousIndex];

            if (previousDefinition.name == definition.name) {
                log::error("Enum '" + name + "' has duplicate value name '" + definition.name + "'");
                return TypeId::INVALID;
            }

            if (previousDefinition.value == definition.value) {
                log::error(
                    "Enum '" + name + "' assigns value " + std::to_string(definition.value) + " more than once"
                );
                return TypeId::INVALID;
            }
        }
    }

    const auto id = reserveTypeId();
    auto enumType = std::make_unique<EnumType>(this, id, name, std::move(values), std::move(documentation));
    const auto* registeredType = enumType.get();
    registerType(std::move(enumType));

    for (NomadIndex index = 0; index < registeredType->getValueCount(); ++index) {
        const auto& definition = registeredType->getValue(index);
        if (registerConstant(
            name + "." + definition.name,
            RuntimeValue(definition.value),
            registeredType
        ).isInvalid()) {
            throw NomadException("Failed to register value '" + definition.name + "' in enum '" + name + "'");
        }
    }

    registerBinaryOperator(
        BinaryOperator::EqualEqual,
        registeredType,
        registeredType,
        getBooleanType(),
        OpCodes::op_integer_equal,
        foldIntegerBinaryEqualEqual
    );
    registerBinaryOperator(
        BinaryOperator::BangEqual,
        registeredType,
        registeredType,
        getBooleanType(),
        OpCodes::op_integer_not_equal,
        foldIntegerBinaryBangEqual
    );

    return id;
}

bool Runtime::registerParameterizedType(
    const NomadString& name,
    const NomadIndex argumentCount,
    ParameterizedTypeFactory factory,
    ParameterizedTypeInitializer initializer
) {
    if (name.empty() || isHub(name) || !canRegisterHubMember(name) ||
        argumentCount == 0 || !factory || getTypeByName(name) != nullptr ||
        getParameterizedConstructorArgumentCount(name) ||
        std::ranges::any_of(m_parameterizedTypes, [&](const auto& entry) { return entry.name == name; })) {
        log::error("Cannot register parameterized type '" + name + "': invalid or duplicate definition");
        return false;
    }
    m_parameterizedTypes.push_back({name, argumentCount, std::move(factory), std::move(initializer), std::nullopt, {}});
    registerHubMember(name, HubTypeFamily{argumentCount});
    return true;
}

std::optional<NomadIndex> Runtime::getParameterizedTypeArgumentCount(const NomadString& name) const {
    const auto registration = std::ranges::find(m_parameterizedTypes, name, &ParameterizedTypeRegistration::name);
    if (registration == m_parameterizedTypes.end()) {
        return std::nullopt;
    }
    return registration->argumentCount;
}

bool Runtime::registerConstructorTemplate(
    const NomadString& name,
    std::vector<TemplateParameter> parameters
) {
    const auto registration = std::ranges::find(m_parameterizedTypes, name, &ParameterizedTypeRegistration::name);
    if (registration == m_parameterizedTypes.end()) {
        log::error("Cannot register constructor template '" + name + "': a registered parameterized type is required");
        return false;
    }
    if (registration->constructorTemplate) {
        log::error("Cannot register constructor template '" + name + "': duplicate definition");
        return false;
    }
    std::vector<bool> boundArguments(registration->argumentCount, false);
    for (const auto& parameter : parameters) {
        if (const auto* argument = std::get_if<TypeParameterIndex>(&parameter.type)) {
            if (argument->index >= registration->argumentCount) {
                log::error("Cannot register constructor template '" + name + "': type argument index is out of range");
                return false;
            }
            boundArguments[argument->index] = true;
        } else {
            const auto* type = std::get<const Type*>(parameter.type);
            if (!ownsType(type) || type->isVoid()) {
                log::error("Cannot register constructor template '" + name +
                    "': parameter types must be registered and non-void");
                return false;
            }
        }
    }
    if (std::ranges::find(boundArguments, false) != boundArguments.end()) {
        log::error("Cannot register constructor template '" + name + "': every type argument must appear in the signature");
        return false;
    }
    registration->constructorTemplate = std::move(parameters);
    return true;
}

std::optional<std::vector<TemplateParameter>> Runtime::getConstructorTemplate(const NomadString& name) const {
    const auto registration = std::ranges::find(m_parameterizedTypes, name, &ParameterizedTypeRegistration::name);
    if (registration == m_parameterizedTypes.end()) {
        return std::nullopt;
    }
    return registration->constructorTemplate;
}

ConstructorInferenceResult Runtime::inferConstructorTypeArguments(
    const NomadString& name,
    const std::vector<const Type*>& argumentTypes
) const {
    const auto registration = std::ranges::find(m_parameterizedTypes, name, &ParameterizedTypeRegistration::name);
    if (registration == m_parameterizedTypes.end() || !registration->constructorTemplate) {
        return {
            ResolveStatus::UnknownName, {},
            "No constructor inference signature is registered for '" + name + "'"
        };
    }
    const auto& parameters = *registration->constructorTemplate;
    if (argumentTypes.size() != parameters.size()) {
        return {
            ResolveStatus::NoMatchingArity, {},
            "Constructor inference for '" + name + "' expects " + std::to_string(parameters.size()) +
                " argument(s), received " + std::to_string(argumentTypes.size())
        };
    }

    std::vector<const Type*> typeArguments(registration->argumentCount, nullptr);
    for (auto index = NomadIndex{0}; index < parameters.size(); ++index) {
        const auto& parameter = parameters[index];
        const auto* argumentType = argumentTypes[index];
        const auto prefix = "Cannot infer constructor '" + name + "' from argument " +
            std::to_string(index + 1) + " ('" + parameter.name + "'): ";
        if (const auto* typeParameter = std::get_if<TypeParameterIndex>(&parameter.type)) {
            if (!ownsType(argumentType) || argumentType->isVoid() || argumentType->isInternal()) {
                return {
                    ResolveStatus::NoMatchingTypes, {},
                    prefix + "a registered public, non-void argument type is required; supply explicit type arguments"
                };
            }
            auto& binding = typeArguments[typeParameter->index];
            if (binding != nullptr && !binding->sameType(argumentType)) {
                return {
                    ResolveStatus::NoMatchingTypes, {},
                    prefix + "conflicting types for type parameter " + std::to_string(typeParameter->index) +
                        " ('" + binding->getTypeName() + "' and '" + argumentType->getTypeName() + "')"
                };
            }
            if (binding == nullptr) {
                binding = argumentType;
            }
        } else {
            const auto* parameterType = std::get<const Type*>(parameter.type);
            const auto shape = getParameterShape(parameterType);
            if (argumentType == nullptr && (shape == ParameterShape::SourceFile ||
                shape == ParameterShape::SourceFunction || shape == ParameterShape::SourceLine)) {
                continue;
            }
            if (!ownsType(argumentType) || !parameterType->sameType(argumentType)) {
                return {
                    ResolveStatus::NoMatchingTypes, {},
                    prefix + "expected exact type '" + parameterType->getTypeName() + "'"
                };
            }
        }
    }
    return {ResolveStatus::Resolved, std::move(typeArguments), {}};
}

const Type* Runtime::getParameterizedType(
    const NomadString& name,
    const std::vector<const Type*>& arguments
) {
    const auto registration = std::ranges::find(m_parameterizedTypes, name, &ParameterizedTypeRegistration::name);
    if (registration == m_parameterizedTypes.end()) {
        log::error("Unknown parameterized type '" + name + "'");
        return nullptr;
    }
    if (arguments.size() != registration->argumentCount ||
        std::ranges::any_of(arguments, [this](const Type* type) {
            return !ownsType(type) || type->isVoid() || type->isInternal();
        })) {
        log::error("Invalid type arguments for '" + name + "'");
        return nullptr;
    }
    for (const auto& entry : m_typeSpecializations) {
        if (entry.name == name && entry.arguments == arguments) {
            return entry.type;
        }
    }
    const auto factory = registration->factory;
    const auto initializer = registration->initializer;
    const auto id = reserveTypeId();
    auto type = factory(this, id, arguments);
    if (type == nullptr || type->getRuntime() != this || type->getId() != id ||
        type->getTypeName().empty() || getTypeByName(type->getTypeName()) != nullptr) {
        log::error("Factory for '" + name + "' returned an invalid or duplicate type");
        return nullptr;
    }
    const auto* result = type.get();
    registerType(std::move(type));
    if (initializer) {
        initializer(this, id);
    }
    m_typeSpecializations.push_back({name, arguments, result});
    return result;
}

const Type* Runtime::getCallbackType(const std::vector<const Type*>& parameterTypes, const Type* returnType) {
    for (auto typeIndex = NomadIndex{0}; typeIndex < m_typeManager.getTypeCount(); ++typeIndex) {
        const auto type = getType(TypeId{toNomadId(typeIndex)});
        const auto* callback = type.has_value() ? (*type)->asCallback() : nullptr;
        if (callback != nullptr && callback->getReturnType() == returnType &&
            callback->getParameterCount() == parameterTypes.size()) {
            auto matches = true;
            for (NomadIndex index = 0; index < parameterTypes.size(); ++index) {
                if (callback->getParameterType(index) != parameterTypes[index]) {
                    matches = false;
                    break;
                }
            }
            if (matches) {
                return *type;
            }
        }
    }
    const auto id = reserveTypeId();
    auto type = std::make_unique<FunctionType>(this, id, parameterTypes, returnType);
    const auto* result = type.get();
    registerType(std::move(type));
    return result;
}

const Type * Runtime::getEventType(const NomadString &name) const {
    return m_typeManager.getTypeByName(name);
}

const Type* Runtime::getPredicateType() {
    return getCallbackType({}, getBooleanType());
}

bool Runtime::ownsType(const Type* type) const {
    return m_typeManager.ownsType(type);
}

ClassType* Runtime::getMutableClassType(const ClassType* type) {
    return m_typeManager.getMutableClassType(type);
}

bool Runtime::canRegisterNativeMember(const Type* classType, const NomadString& name, const MemberType type,
                                         const std::vector<Parameter>& parameters,
                                         NomadString& error) const {
    if (!ownsType(classType)) {
        error = "class type is not registered with this runtime";
        return false;
    }

    const auto* owner = classType->asClassType();
    if (owner == nullptr) {
        error = "only class types can have native members";
        return false;
    }

    if (owner->getProperty(name) != nullptr ||
        getMemberType(getNativeMemberId(classType, name)) != MemberType::Unknown) {
        error = "the type already has a member with that name";
        return false;
    }

    return canRegisterMemberSignature(name, type, parameters, error);
}

bool Runtime::canRegisterMemberSignature(
    const NomadString& name,
    const MemberType type,
    const std::vector<Parameter>& parameters,
    NomadString& error
) const {
    if (type != MemberType::Property && type != MemberType::Method) {
        error = "member must be a property or method";
        return false;
    }
    if (type == MemberType::Property && !parameters.empty()) {
        error = "properties cannot take parameters";
        return false;
    }
    if (const auto existing = getMemberSignature(name)) {
        const auto& existingParameters = existing->parameters;
        if (existingParameters.size() != parameters.size()) {
            error = "members sharing a name must take the same number of parameters";
            return false;
        }

        for (NomadIndex index = 0; index < parameters.size(); ++index) {
            const auto candidateShape = getParameterShape(parameters[index].type);
            const auto existingShape = getParameterShape(existingParameters[index].type);

            if (candidateShape != existingShape) {
                error = "members sharing a name must agree on the kind of every parameter";
                return false;
            }

            if (candidateShape != ParameterShape::Value && parameters[index].type != existingParameters[index].type) {
                error = "members sharing a name must agree on parser-dependent parameter types";
                return false;
            }
        }
    }

    return true;
}

bool Runtime::registerMemberTemplate(
    const NomadString& family,
    const NomadString& name,
    const MemberType type,
    const std::vector<TemplateParameter>& parameters
) {
    const auto registration = std::ranges::find(m_parameterizedTypes, family, &ParameterizedTypeRegistration::name);
    if (registration == m_parameterizedTypes.end()) {
        log::error("Cannot register member template '" + name + "': a registered parameterized type is required");
        return false;
    }
    if (registration->memberTemplates.contains(name)) {
        log::error("Cannot register member template '" + name + "': duplicate definition for '" + family + "'");
        return false;
    }
    std::vector<Parameter> parserParameters;
    for (const auto& parameter : parameters) {
        const Type* parameterType = nullptr;
        if (const auto* index = std::get_if<TypeParameterIndex>(&parameter.type)) {
            if (index->index >= registration->argumentCount) {
                log::error("Cannot register member template '" + name + "': type argument index is out of range");
                return false;
            }
        } else {
            parameterType = std::get<const Type*>(parameter.type);
            if (!ownsType(parameterType) || parameterType->isVoid()) {
                log::error("Cannot register member template '" + name + "': parameter types must be registered and non-void");
                return false;
            }
        }
        if (parameter.name.empty()) {
            log::error("Cannot register member template '" + name + "': parameter name is empty");
            return false;
        }
        parserParameters.push_back(defParameter(parameter.name, parameterType, parameter.doc));
    }
    NomadString error;
    if (!canRegisterMemberSignature(name, type, parserParameters, error)) {
        log::error("Cannot register member template '" + name + "': " + error);
        return false;
    }
    registration->memberTemplates.emplace(name, MemberSignature{type, std::move(parserParameters)});
    return true;
}

std::optional<MemberSignature> Runtime::getMemberSignature(const NomadString& name) const {
    for (const auto& registration : m_parameterizedTypes) {
        if (const auto member = registration.memberTemplates.find(name); member != registration.memberTemplates.end()) {
            return member->second;
        }
    }
    const auto overloads = m_nativeMembersByName.find(name);
    if (overloads == m_nativeMembersByName.end() || overloads->second.empty()) {
        return std::nullopt;
    }
    const auto memberId = overloads->second.front();
    const auto type = getMemberType(memberId);
    if (type == MemberType::Method) {
        const auto* method = getNativeMemberMethod(memberId);
        if (method == nullptr) {
            throw NomadBug("Invalid native method registration");
        }
        return MemberSignature{type, method->getParameters()};
    }
    return MemberSignature{type, {}};
}

CallableId Runtime::addConstructor(const TypeId id, NativeConstructorDefinition definition) {
    return addConstructor(id, id, std::move(definition));
}

CallableId Runtime::addConstructor(
    const TypeId id,
    const TypeId createdTypeId,
    NativeConstructorDefinition definition
) {
    return m_typeManager.registerNativeClassConstructor(id, createdTypeId, std::move(definition));
}

std::vector<const NativeConstructor*> Runtime::getConstructors() const {
    std::vector<const NativeConstructor*> constructors;
    for (const auto* callable : m_callables) {
        if (callable->getType() == CallableType::Constructor) {
            if (const auto* native = static_cast<const Constructor*>(callable)->asNativeConstructor()) {
                constructors.push_back(native);
            }
        }
    }
    return constructors;
}

std::vector<const NativeConstructor*> Runtime::getConstructors(const Type* classType) const {
    std::vector<const NativeConstructor*> constructors;
    for (const auto* constructor : getConstructors()) {
        if (constructor->getClassType() == classType) {
            constructors.push_back(constructor);
        }
    }
    return constructors;
}

bool Runtime::registerNativeParameterizedConstructor(
    const NomadString& name,
    const NomadIndex argumentCount,
    ParameterizedConstructorInitializer initializer
) {
    if (name.empty() || isHub(name) || !canRegisterHubMember(name) || argumentCount == 0 || !initializer ||
        getParameterizedConstructorArgumentCount(name) || getParameterizedTypeArgumentCount(name) ||
        getTypeByName(name) != nullptr ||
        std::ranges::any_of(m_callables, [&](const auto* callable) {
            return callable->getType() == CallableType::Constructor && callable->getName() == name;
        })) {
        log::error("Cannot register parameterized constructor '" + name + "': invalid or duplicate definition");
        return false;
    }
    m_parameterizedConstructors.push_back({name, argumentCount, std::move(initializer)});
    registerHubMember(name, HubTypeFamily{argumentCount});
    return true;
}

std::optional<NomadIndex> Runtime::getParameterizedConstructorArgumentCount(const NomadString& name) const {
    const auto registration = std::ranges::find(
        m_parameterizedConstructors, name, &ParameterizedConstructorRegistration::name
    );
    if (registration == m_parameterizedConstructors.end()) {
        return std::nullopt;
    }
    return registration->argumentCount;
}

NomadString Runtime::getParameterizedConstructorName(
    const NomadString& name,
    const std::vector<const Type*>& arguments
) {
    const auto registration = std::ranges::find(
        m_parameterizedConstructors, name, &ParameterizedConstructorRegistration::name
    );
    if (registration == m_parameterizedConstructors.end() || arguments.size() != registration->argumentCount ||
        std::ranges::any_of(arguments, [this](const auto* type) {
            return !ownsType(type) || type->isVoid() || type->isInternal();
        })) {
        log::error("Cannot resolve parameterized constructor '" + name + "': invalid name or type arguments");
        return {};
    }
    auto specializedName = name + "<";
    for (auto index = NomadIndex{0}; index < arguments.size(); ++index) {
        if (index != 0) {
            specializedName += " ";
        }
        specializedName += arguments[index]->getTypeName();
    }
    specializedName += ">";
    if (std::ranges::find(m_constructorSpecializations, specializedName) != m_constructorSpecializations.end()) {
        return specializedName;
    }
    const auto initializer = registration->initializer;
    initializer(specializedName, arguments);
    if (std::ranges::none_of(m_callables, [&](const auto* callable) {
        return callable->getType() == CallableType::Constructor && callable->getName() == specializedName;
    })) {
        log::error("No constructors registered for '" + specializedName + "'");
        return {};
    }
    m_constructorSpecializations.push_back(specializedName);
    return specializedName;
}

IndexerId Runtime::addIndexer(const TypeId id, NativeIndexerDefinition definition) {
    const auto type = getType(id);
    const auto* classType = type.has_value() ? (*type)->asClassType() : nullptr;
    if (classType == nullptr) {
        log::error("Cannot register native indexer: owner must identify a class type");
        return IndexerId::INVALID;
    }
    const auto valueType = getType(definition.valueTypeId);
    if (!valueType.has_value()) {
        log::error("Cannot register native indexer: invalid value type ID");
        return IndexerId::INVALID;
    }
    std::vector<const Type*> indexTypes;
    indexTypes.reserve(definition.indexTypes.size());
    for (const auto indexTypeId : definition.indexTypes) {
        const auto indexType = getType(indexTypeId);
        if (!indexType.has_value()) {
            log::error("Cannot register native indexer: invalid index type ID");
            return IndexerId::INVALID;
        }
        indexTypes.push_back(*indexType);
    }
    auto* owner = getMutableClassType(classType);
    if (owner == nullptr) {
        log::error("Cannot register native indexer: class type must be registered with this runtime");
        return IndexerId::INVALID;
    }
    return owner->registerNativeIndexer(*this, {
        IndexerId::INVALID, classType, std::move(indexTypes), *valueType,
        std::move(definition.getter), std::move(definition.setter), definition.documentation
    });
}

IndexerId Runtime::catalogNativeIndexer(const NativeIndexerInfo* info) {
    if (info == nullptr || info->id.isValid() || !ownsType(info->classType) ||
        info->classType->getNativeIndexer(info->indexTypes.size()) != info ||
        m_nativeIndexers.size() >= toNomadIndex(NOMAD_ID_MAX)) {
        log::error("Cannot catalog native indexer: invalid owner, identity, or indexer capacity");
        return IndexerId::INVALID;
    }
    const auto id = IndexerId{toNomadId(m_nativeIndexers.size())};
    m_nativeIndexers.push_back(info);
    return id;
}

IndexerId Runtime::getNativeIndexerId(const Type* classType, const NomadIndex indexCount) const {
    if (!ownsType(classType)) {
        return IndexerId::INVALID;
    }
    const auto* owner = classType->asClassType();
    const auto* info = owner == nullptr ? nullptr : owner->getNativeIndexer(indexCount);
    return info == nullptr ? IndexerId::INVALID : info->id;
}

std::optional<NativeIndexerInfo> Runtime::getNativeIndexerInfo(const IndexerId indexerId) const {
    if (isIdOutOfRange(indexerId.id, m_nativeIndexers.size())) {
        return std::nullopt;
    }
    return *m_nativeIndexers[toNomadIndex(indexerId.id)];
}

const std::vector<const NativeIndexerInfo*>& Runtime::getNativeIndexers() const {
    return m_nativeIndexers;
}

PropertyId Runtime::registerProperty(
    const TypeId ownerId,
    const NomadString& name,
    const Type* valueType
) {
    const auto type = getType(ownerId);
    if (type.has_value() && (*type)->asClassType() != nullptr) {
        return m_typeManager.registerScriptClassProperty(ownerId, name, valueType);
    }
    return m_typeManager.registerScriptStructProperty(ownerId, name, valueType);
}

void Runtime::setPropertyHasExplicitInitializer(
    const PropertyId id,
    const bool hasExplicitInitializer
) {
    const auto* property = getProperty(id);
    if (property == nullptr || property->asNativeProperty() != nullptr) {
        throw NomadBug("Explicit initializer metadata requires a stored property");
    }
    m_properties[toNomadIndex(id.id)]->setHasExplicitInitializer(hasExplicitInitializer);
}

void Runtime::finalizeCompositeFieldLayout(const TypeId id) {
    m_typeManager.finalizeCompositeFieldLayout(id);
}

PropertyId Runtime::catalogProperty(Property* property) {
    if (property == nullptr || property->getId().isValid() || !ownsType(property->getOwnerType()) ||
        property->getOwnerType()->getProperty(property->getName()) != property ||
        m_properties.size() >= toNomadIndex(NOMAD_ID_MAX)) {
        log::error("Cannot catalog property: invalid owner, identity, or property capacity");
        return PropertyId::INVALID;
    }
    const auto id = PropertyId{toNomadId(m_properties.size())};
    m_properties.push_back(property);
    return id;
}

const Property* Runtime::getProperty(const PropertyId id) const {
    return isIdInRange(id.id, m_properties.size()) ? m_properties[toNomadIndex(id.id)] : nullptr;
}

const NativeProperty* Runtime::getNativeProperty(const PropertyId id) const {
    const auto* property = getProperty(id);
    return property == nullptr ? nullptr : property->asNativeProperty();
}

NativeProperty* Runtime::getMutableNativeProperty(const PropertyId id) {
    if (getNativeProperty(id) == nullptr) {
        return nullptr;
    }
    return static_cast<NativeProperty*>(m_properties[toNomadIndex(id.id)]);
}

void Runtime::validateNativePropertyTypes() {
    for (auto* property : m_properties) {
        const auto* native = property->asNativeProperty();
        if (native != nullptr && !native->isTypeResolved()) {
            getMutableNativeProperty(property->getId())->completeTypeResolution();
        }
    }
}

PropertyId Runtime::addProperty(const TypeId id, NativePropertyDefinition definition) {
    return m_typeManager.registerNativeClassProperty(id, std::move(definition));
}

void Runtime::catalogNativeProperty(const PropertyId id) {
    const auto* property = getNativeProperty(id);
    if (property == nullptr || !ownsType(property->getClassType()) ||
        property->getClassType()->getProperty(property->getName()) != property ||
        std::ranges::find(m_nativeMembers, MemberReference{id}) != m_nativeMembers.end()) {
        throw NomadException("Cannot catalog native property: invalid registration or duplicate native member");
    }
    NomadString error;
    if (!canRegisterMemberSignature(property->getName(), MemberType::Property, {}, error)) {
        throw NomadException("Cannot catalog native property '" + property->getName() + "': " + error);
    }
    m_nativeMembers.push_back(id);
    m_nativeMembersByName[property->getName()].push_back(id);
}

CallableId Runtime::registerScriptMethod(
    const TypeId id,
    NomadString name,
    std::vector<Parameter> parameters,
    NomadString path,
    NomadString source
) {
    return m_typeManager.registerScriptClassMethod(
        id, std::move(name), std::move(parameters), std::move(path), std::move(source)
    );
}

CallableId Runtime::registerScriptConstructor(
    const TypeId id,
    std::vector<Parameter> parameters,
    NomadString path,
    NomadString source
) {
    return m_typeManager.registerScriptClassConstructor(
        id, std::move(parameters), std::move(path), std::move(source)
    );
}

CallableId Runtime::addMethod(const TypeId id, NativeMethodDefinition definition) {
    return m_typeManager.registerNativeClassMethod(id, std::move(definition));
}

void Runtime::catalogNativeMethod(const CallableId id) {
    const auto* method = getNativeMethod(id);
    if (method == nullptr || !ownsType(method->getClassType()) ||
        method->getClassType()->asClassType() == nullptr ||
        !std::ranges::any_of(method->getClassType()->asClassType()->getMethods(),
            [method](const auto* candidate) { return candidate == method; })) {
        throw NomadException("Cannot catalog native method: invalid registration");
    }
    if (std::ranges::find(m_nativeMembers, MemberReference{id}) != m_nativeMembers.end()) {
        throw NomadException("Cannot catalog native method: duplicate native member");
    }
    NomadString error;
    if (!canRegisterMemberSignature(method->getName(), MemberType::Method, method->getParameters(), error)) {
        throw NomadException("Cannot catalog native method '" + method->getName() + "': " + error);
    }
    m_nativeMembers.push_back(id);
    m_nativeMembersByName[method->getName()].push_back(id);
}

MemberReference Runtime::getNativeMemberId(const Type* classType, const NomadString& name) const {
    const auto overloads = m_nativeMembersByName.find(name);
    if (overloads == m_nativeMembersByName.end()) {
        return PropertyId::INVALID;
    }

    for (const auto memberId : overloads->second) {
        switch (getMemberType(memberId)) {
        case MemberType::Property: {
            const auto info = getNativePropertyInfo(memberId);
            if (info && info->classType == classType) {
                return memberId;
            }
            break;
        }

        case MemberType::Method: {
            const auto* method = getNativeMemberMethod(memberId);
            if (method != nullptr && method->getClassType() == classType) {
                return memberId;
            }
            break;
        }

        case MemberType::Unknown:
            break;
        }
    }

    return PropertyId::INVALID;
}

MemberType Runtime::getMemberType(const MemberReference& member) const {
    if (const auto* propertyId = std::get_if<PropertyId>(&member)) {
        return getProperty(*propertyId) == nullptr ? MemberType::Unknown : MemberType::Property;
    }
    return getMethod(std::get<CallableId>(member)) == nullptr ? MemberType::Unknown : MemberType::Method;
}

std::optional<NativePropertyInfo> Runtime::getNativePropertyInfo(const MemberReference& member) const {
    const auto* id = std::get_if<PropertyId>(&member);
    if (id == nullptr) {
        return std::nullopt;
    }
    const auto* property = getNativeProperty(*id);
    if (property == nullptr) {
        return std::nullopt;
    }
    return property->getInfo();
}

const NativeMethod* Runtime::getNativeMemberMethod(const MemberReference& member) const {
    const auto* id = std::get_if<CallableId>(&member);
    return id == nullptr ? nullptr : getNativeMethod(*id);
}

void Runtime::getNativeMembers(const Type* classType, std::vector<MemberReference>& members) const {
    members.clear();

    for (const auto& memberId : m_nativeMembers) {
        const auto type = getMemberType(memberId);

        if (type == MemberType::Property) {
            const auto info = getNativePropertyInfo(memberId);
            if (info && info->classType == classType) {
                members.push_back(memberId);
            }
        } else if (type == MemberType::Method) {
            const auto* method = getNativeMemberMethod(memberId);
            if (method != nullptr && method->getClassType() == classType) {
                members.push_back(memberId);
            }
        }
    }
}

void Runtime::getNativeMembers(std::vector<MemberReference>& members) const {
    members = m_nativeMembers;
}

void Runtime::getNativeMemberOverloads(const NomadString& name, std::vector<MemberReference>& overloads) const {
    overloads.clear();

    const auto members = m_nativeMembersByName.find(name);
    if (members != m_nativeMembersByName.end()) {
        overloads = members->second;
    }
}

NomadIndex Runtime::getNativeMemberOverloadCount(const NomadString& name) const {
    const auto members = m_nativeMembersByName.find(name);
    return members == m_nativeMembersByName.end() ? 0 : members->second.size();
}

OpCodeId Runtime::registerInstruction(const NomadString& name, const InstructionFn fn, const NomadString& doc,
                                     std::vector<Operand> operands) {
    auto id = getInstructionId(name);

    if (id.isValid()) {
        log::warning("Instruction '" + name + "' already registered");

        return id;
    }

    id = OpCodeId{toNomadId(m_opCodes.size())};

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

const StringType* Runtime::getStringType() const {
    return static_cast<const StringType*>(getTypeByName(STRING_TYPE_NAME));
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
    if (opCodeId.isInvalid()) {
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
    if (opCodeId.isInvalid()) {
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

OpCodeId Runtime::getUnaryOperatorOpCodeId(const UnaryOperator op, const Type* operand) const {
    for (const auto& definition: m_unaryOperators) {
        if (definition.op == op && definition.operand == operand) {
            return definition.opCodeId;
        }
    }

    return OpCodeId::INVALID;
}

OpCodeId Runtime::getBinaryOperatorOpCodeId(
    const BinaryOperator op,
    const Type* lhs,
    const Type* rhs
) const {
    if (lhs == nullptr || rhs == nullptr) {
        return OpCodeId::INVALID;
    }

    for (const auto& definition: m_binaryOperators) {
        if (definition.op == op && definition.lhs == lhs && definition.rhs == rhs) {
            return definition.opCodeId;
        }
    }

    return OpCodeId::INVALID;
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

OpCodeId Runtime::getInstructionId(const NomadString& name) const {
    for (NomadIndex i = 0; i < m_opCodes.size(); ++i) {
        if (m_opCodes[i].name == name) {
            return m_opCodes[i].id;
        }
    }

    return OpCodeId::INVALID;
}

OpCodeId Runtime::getInstructionId(const InstructionFn fn) const {
    for (auto& op_code: m_opCodes) {
        if (op_code.fn == fn) {
            return op_code.id;
        }
    }

    return OpCodeId::INVALID;
}

InstructionFn Runtime::getInstructionFn(const OpCodeId id) const {
    return m_opCodes[toNomadIndex(id.id)].fn;
}

const NomadString& Runtime::getInstructionName(const OpCodeId id) const {
    return m_opCodes[toNomadIndex(id.id)].name;
}

const std::vector<Operand>& Runtime::getInstructionOperands(const OpCodeId id) const {
    return m_opCodes[toNomadIndex(id.id)].operands;
}

CallableId Runtime::getNativeFunctionId(const NomadString& name) const {
    const auto* function = getNativeFunction(getCallableId(name));
    return function == nullptr ? CallableId{} : function->getId();
}

NativeFunctionFn Runtime::getNativeFunctionFn(const CallableId id) const {
    return getNativeFunction(id)->getFn();
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
const NativeFunction* Runtime::getNativeFunction(const NomadString& name) const {
    const auto id = getNativeFunctionId(name);

    return id == CallableId{} ? nullptr : getNativeFunction(id);
}

std::vector<const NativeFunction*> Runtime::getNativeFunctions() const {
    std::vector<const NativeFunction*> nativeFunctions;

    for (auto id = getFirstNativeFunctionId(); id != CallableId{}; id = getNextNativeFunctionId(id)) {
        nativeFunctions.push_back(getNativeFunction(id));
    }

    return nativeFunctions;
}

CallableId Runtime::getFirstNativeFunctionId() const {
    for (const auto* callable : m_callables) {
        if (callable->getSource() == CallableSource::Native && callable->getType() == CallableType::Function) {
            return callable->getId();
        }
    }
    return CallableId::INVALID;
}

CallableId Runtime::getNextNativeFunctionId(const CallableId currentId) const {
    for (auto index = toNomadIndex(currentId.id) + 1; index < m_callables.size(); ++index) {
        if (m_callables[index]->getSource() == CallableSource::Native &&
            m_callables[index]->getType() == CallableType::Function) {
            return CallableId{toNomadId(index)};
        }
    }
    return CallableId::INVALID;
}

KeywordId Runtime::registerKeyword(const NomadString& keyword, const NomadString& /*doc*/) {
    const auto existingId = getKeywordId(keyword);
    if (existingId.isValid()) {
        return existingId;
    }

    const auto id = KeywordId{toNomadId(m_keywords.size())};

    KeywordDefinition definition;

    definition.keyword = keyword;
#if defined(NOMAD_FUNCTION_DOC)
    definition.doc = doc;
#endif

    m_keywords.push_back(definition);

    return id;
}

KeywordId Runtime::getKeywordId(const NomadString& keyword) const {
    for (NomadIndex i = 0; i < m_keywords.size(); ++i) {
        if (m_keywords[i].keyword == keyword) {
            return KeywordId{toNomadId(i)};
        }
    }

    return KeywordId::INVALID;
}

void Runtime::getKeywords(std::vector<KeywordDefinition>& keywords) const {
    keywords = m_keywords;
}

ConstantId Runtime::registerConstant(const NomadString& name, const RuntimeValue& value, const Type* type) {
    if (isHub(name) || !canRegisterHubMember(name)) {
        log::error("Cannot register constant '" + name + "': name is reserved by a hub");
        return ConstantId::INVALID;
    }
    if (getConstantId(name).isValid()) {
        log::error("Constant with name '" + name + "' already registered.");

        return ConstantId::INVALID;
    }

    const auto constantId = ConstantId{m_constantsMap.registerVariable(name, type).id};
    const auto constantIndex = toNomadIndex(constantId.id);

    m_constants.resize(constantIndex + 1);
    type->copyValue(&value, &m_constants[constantIndex]);
    registerHubMember(name, constantId);

    return constantId;
}

void Runtime::getConstantValue(const ConstantId id, RuntimeValue& value) const {
    const auto constantIndex = toNomadIndex(id.id);

    if (constantIndex >= m_constants.size()) {
        return;
    }

    value = m_constants[constantIndex];
}

ConstantId Runtime::getConstantId(const NomadString& name) const {
    return ConstantId{m_constantsMap.getVariableId(name).id};
}

const NomadString& Runtime::getConstantName(const ConstantId id) const {
    return m_constantsMap.getVariableName(VariableId{id.id});
}

const Type* Runtime::getConstantType(const ConstantId id) const {
    return m_constantsMap.getVariableType(VariableId{id.id});
}

ConstantId Runtime::getFirstConstantId() const
{
    if (m_constants.empty()) {
        return ConstantId::INVALID;
    }

    return ConstantId{0};
}

ConstantId Runtime::getNextConstantId(const ConstantId currentId) const {
    const auto currentIndex = toNomadIndex(currentId.id);

    if (currentIndex + 1 >= m_constants.size()) {
        return ConstantId::INVALID;
    }

    return ConstantId{currentId.id + 1};
}

EventId Runtime::registerEvent(const NomadString &name, const std::vector<EventParameter> &parameterTypes) {
    if (isHub(name) || !canRegisterHubMember(name)) {
        log::error("Cannot register event '" + name + "': name is reserved by a hub");
        return EventId::INVALID;
    }
    if (getEventId(name).isValid()) {
        log::error("Event with name '" + name + "' already registered.");

        return EventId::INVALID;
    }

    const auto id = EventId{toNomadId(m_events.size())};

    m_events.emplace_back(id, name, parameterTypes);

    // Also register event type.
    registerType(std::make_unique<EventType>(this, reserveTypeId(), &m_events.back()));
    registerHubMember(name, id);

    return id;
}

EventId Runtime::getEventId(const NomadString &name) const {
    for (auto& event: m_events) {
        if (event.name == name) {
            return event.id;
        }
    }

    return EventId::INVALID;
}

std::optional<EventDefinition> Runtime::getEventDefinition(EventId id) const {
    for (const auto& event: m_events) {
        if (event.id == id) {
            return event;
        }
    }

    return std::nullopt;
}

EventId Runtime::getFirstEventId() const {
    if (m_events.empty()) {
        return EventId::INVALID;
    }

    return EventId{0};
}

EventId Runtime::getNextEventId(const EventId currentId) const {
    const auto currentIndex = toNomadIndex(currentId.id);

    if (currentIndex + 1 >= m_events.size()) {
        return EventId::INVALID;
    }

    return EventId{currentId.id + 1};
}

StringId Runtime::registerString(const NomadString& string) {
    auto id = getStringId(string);

    if (id.isValid()) {
        return id;
    }

    id = StringId{toNomadId(m_strings.size())};

    m_strings.push_back(NomadStringObject::create(string));

    return id;
}

StringId Runtime::getStringId(const NomadString& string) const {
    for (NomadIndex i = 0; i < m_strings.size(); ++i) {
        if (m_strings[i]->getView() == string) {
            return StringId{toNomadId(i)};
        }
    }

    return StringId::INVALID;
}

NomadStringObject* Runtime::getString(const StringId stringId) const {
    const auto stringIndex = toNomadIndex(stringId.id);
    return m_strings[stringIndex];
}

NomadStringObject* Runtime::getStringByName(const NomadString& name) const {
    const auto stringId = getStringId(name);

    return getString(stringId);
}

FormatStringId Runtime::registerFormatString(const NomadString& formatString, CallableId functionId) {
    auto id = getFormatStringId(formatString, functionId);

    if (id.isValid()) {
        return id;
    }

    id = FormatStringId{toNomadId(m_formatStrings.size())};

    m_formatStrings.push_back(
        {
            id,
            functionId,
            std::make_unique<FormatString>(formatString, functionId)
        }
        );

    return id;
}

FormatString* Runtime::getFormatString(const FormatStringId id) const {
    const auto formatStringIndex = toNomadIndex(id.id);

    if (formatStringIndex >= m_formatStrings.size()) {
        return nullptr;
    }

    return m_formatStrings[formatStringIndex].formatString.get();
}

FormatStringId Runtime::getFormatStringId(const NomadString& formatString, const CallableId functionId) const {
    for (auto& i: m_formatStrings) {
        if (i.functionId == functionId && i.formatString->getFormatString() == formatString) {
            return i.id;
        }
    }

    return FormatStringId::INVALID;
}

DynamicVariableId Runtime::registerDynamicVariable(
    const NomadString& name,
    DynamicVariableSetFn setFn,
    DynamicVariableGetFn getFn,
    const Type* type,
    const NomadString& /*doc*/
    ) {
    if (isHub(name) || !canRegisterHubMember(name)) {
        log::error("Cannot register dynamic variable '" + name + "': name is reserved by a hub declaration");
        return DynamicVariableId::INVALID;
    }
    const auto id = DynamicVariableId{toNomadId(m_dynamicVariables.size())};

    DynamicVariableRegistration definition;
    definition.name = name;
    definition.type = type;
    definition.setFn = std::move(setFn);
    definition.getFn = std::move(getFn);
#if defined(NOMAD_FUNCTION_DOC)
    definition.doc = doc;
#endif

    m_dynamicVariables.push_back(definition);
    registerHubMember(name, id);

    return id;
}

DynamicVariableId Runtime::getDynamicVariableId(const NomadString& name) const {
    for (NomadIndex i = 0; i < m_dynamicVariables.size(); ++i) {
        if (m_dynamicVariables[i].name == name) {
            return DynamicVariableId{toNomadId(i)};
        }
    }

    return DynamicVariableId::INVALID;
}

NomadString Runtime::getDynamicVariableName(const DynamicVariableId id) const {
    const auto dynamicVariableIndex = toNomadIndex(id.id);
    return m_dynamicVariables[dynamicVariableIndex].name;
}

const Type* Runtime::getDynamicVariableType(const DynamicVariableId id) const {
    const auto dynamicVariableIndex = toNomadIndex(id.id);
    return m_dynamicVariables[dynamicVariableIndex].type;
}

bool Runtime::canSetDynamicVariable(const DynamicVariableId id) const {
    const auto dynamicVariableIndex = toNomadIndex(id.id);
    return m_dynamicVariables[dynamicVariableIndex].setFn != nullptr;
}

bool Runtime::canGetDynamicVariable(const DynamicVariableId id) const {
    const auto dynamicVariableIndex = toNomadIndex(id.id);
    return m_dynamicVariables[dynamicVariableIndex].getFn != nullptr;
}

void Runtime::setDynamicVariable(VirtualMachine* interpreter, const DynamicVariableId id, const RuntimeValue& value) {
    const auto dynamicVariableIndex = toNomadIndex(id.id);
    m_dynamicVariables[dynamicVariableIndex].setFn(interpreter, value);
}

void Runtime::getDynamicVariableValue(VirtualMachine* interpreter, const DynamicVariableId id, RuntimeValue& value) const {
    const auto dynamicVariableIndex = toNomadIndex(id.id);
    m_dynamicVariables[dynamicVariableIndex].getFn(interpreter, value);
}

void Runtime::setStringDynamicVariable(VirtualMachine* interpreter, const DynamicVariableId id, const NomadString& value) {
    RuntimeValue stringValue;
    stringValue.setStringValue(value);

    setDynamicVariable(interpreter, id, stringValue);

    stringValue.freeObjectValue();
}

DynamicVariableId Runtime::getFirstDynamicVariableId() const {
    if (m_dynamicVariables.empty()) {
        return DynamicVariableId::INVALID;
    }

    return DynamicVariableId{0};
}

DynamicVariableId Runtime::getNextDynamicVariableId(const DynamicVariableId currentId) const {
    const auto dynamicVariableIndex = toNomadIndex(currentId.id);
    if (dynamicVariableIndex + 1 >= m_dynamicVariables.size()) {
        return DynamicVariableId::INVALID;
    }

    return DynamicVariableId{toNomadId(dynamicVariableIndex + 1)};
}

VariableContextId Runtime::registerVariableContext(
    const NomadString& name,
    const NomadString& prefix,
    std::unique_ptr<VariableContext> context
    ) {
    const auto id = VariableContextId{toNomadId(m_variables.size())};

    auto registration = VariableContextRegistration{
        id,
        name,
        prefix,
        std::move(context)
    };

    m_variables.emplace_back(std::move(registration));

    return id;
}

VariableContextId Runtime::getContextId(const NomadString& name) const {
    for (NomadIndex i = 0; i < m_variables.size(); ++i) {
        if (m_variables[i].name == name) {
            return VariableContextId{toNomadId(i)};
        }
    }

    return VariableContextId::INVALID;
}

ContextVariableId Runtime::registerContextVariable(
    const VariableContextId contextId,
    const NomadString& variableName,
    const Type* type
) {
    const auto contextIndex = toNomadIndex(contextId.id);

    if (contextIndex >= m_variables.size()) {
        return ContextVariableId::INVALID;
    }

    const auto localVariableId = m_variables[contextIndex].context->registerVariable(variableName, type);

    if (localVariableId.isInvalid()) {
        return ContextVariableId::INVALID;
    }

    return internContextVariable(contextId, localVariableId);
}

ContextVariableId Runtime::registerOrUpdateContextVariable(const NomadString &variableName, const Type *type) {
    const auto contextId = getVariableContextIdByPrefix(variableName);

    if (contextId.isInvalid()) {
        return ContextVariableId::INVALID;
    }

    return registerContextVariable(contextId, variableName, type);
}

VariableContext* Runtime::getVariableContext(const VariableContextId contextId) const {
    const auto contextIndex = toNomadIndex(contextId.id);
    if (contextIndex >= m_variables.size()) {
        return nullptr;
    }

    return m_variables[contextIndex].context.get();
}

NomadString Runtime::getContextName(const VariableContextId id) const {
    const auto contextIndex = toNomadIndex(id.id);
    return m_variables[contextIndex].name;
}

ContextVariableId Runtime::getContextVariableId(const NomadString& variableName) {
    const auto contextId = getVariableContextIdByPrefix(variableName);

    if (contextId.isInvalid()) {
        return ContextVariableId::INVALID;
    }

    const auto contextIndex = toNomadIndex(contextId.id);
    const auto localVariableId = m_variables[contextIndex].context->getVariableId(variableName);

    if (localVariableId.isInvalid()) {
        return ContextVariableId::INVALID;
    }

    return internContextVariable(contextId, localVariableId);
}

VariableContextId Runtime::getVariableContextIdByPrefix(const NomadString& variableName) const {
    if (isHubVariable(variableName)) {
        return m_hubs[toNomadIndex(getHubId(getContainingHub(variableName)).id)]->m_variableContextId;
    }
    for (const auto& context: m_variables) {
        if (variableName.rfind(context.prefix, 0) == 0) {
            return context.id;
        }
    }

    return VariableContextId::INVALID;
}

NomadString Runtime::getContextVariableName(const ContextVariableId contextVariableId) const {
    const auto* contextVariable = getContextVariable(contextVariableId);

    if (contextVariable == nullptr) {
        log::error("Invalid context variable id: " + std::to_string(contextVariableId.id));
        return NOMAD_EMPTY_STRING;
    }

    return getVariableContext(contextVariable->contextId)->getVariableName(contextVariable->localVariableId);
}

const Type* Runtime::getContextVariableType(const ContextVariableId contextVariableId) const {
    const auto* contextVariable = getContextVariable(contextVariableId);

    if (contextVariable == nullptr) {
        log::error("Invalid context variable id: " + std::to_string(contextVariableId.id));
        return nullptr;
    }

    return getVariableContext(contextVariable->contextId)->getVariableType(contextVariable->localVariableId);
}

void Runtime::setContextVariableWritten(const ContextVariableId contextVariableId, const bool written) const {
    const auto* contextVariable = getContextVariable(contextVariableId);

    if (contextVariable == nullptr) {
        log::error("Invalid context variable id: " + std::to_string(contextVariableId.id));
        return;
    }

    getVariableContext(contextVariable->contextId)->setWritten(contextVariable->localVariableId, written);
}

void Runtime::setContextVariableRead(const ContextVariableId contextVariableId, const bool read) const {
    const auto* contextVariable = getContextVariable(contextVariableId);

    if (contextVariable == nullptr) {
        log::error("Invalid context variable id: " + std::to_string(contextVariableId.id));
        return;
    }

    getVariableContext(contextVariable->contextId)->setRead(contextVariable->localVariableId, read);
}

void Runtime::setContextVariableValue(const ContextVariableId contextVariableId, const RuntimeValue& value) const {
    const auto* contextVariable = getContextVariable(contextVariableId);

    if (contextVariable == nullptr) {
        log::error("Invalid context variable id: " + std::to_string(contextVariableId.id));
        return;
    }

    getVariableContext(contextVariable->contextId)->setValue(contextVariable->localVariableId, value);
}

void Runtime::getContextVariableValue(const ContextVariableId contextVariableId, RuntimeValue& value) const {
    const auto* contextVariable = getContextVariable(contextVariableId);

    if (contextVariable == nullptr) {
        log::error("Invalid context variable id: " + std::to_string(contextVariableId.id));
        return;
    }

    getVariableContext(contextVariable->contextId)->getValue(contextVariable->localVariableId, value);
}

void Runtime::setStringContextVariableValue(const ContextVariableId contextVariableId, const NomadString& value) const {
    RuntimeValue stringValue;
    stringValue.setStringValue(value);

    setContextVariableValue(contextVariableId, stringValue);

    stringValue.freeObjectValue();
}

void Runtime::getStringContextVariableValue(const ContextVariableId contextVariableId, NomadString& value) const {
    RuntimeValue stringValue;
    getContextVariableValue(contextVariableId, stringValue);
    value = stringValue.getStringValue();
}

ContextVariableId Runtime::internContextVariable(const VariableContextId contextId, const VariableId localVariableId) {
    for (NomadIndex contextVariableIndex = 0; contextVariableIndex < m_contextVariables.size(); ++contextVariableIndex) {
        const auto& contextVariable = m_contextVariables[contextVariableIndex];

        if (contextVariable.contextId == contextId && contextVariable.localVariableId == localVariableId) {
            return ContextVariableId{toNomadId(contextVariableIndex)};
        }
    }

    const auto contextVariableId = ContextVariableId{toNomadId(m_contextVariables.size())};
    m_contextVariables.push_back({contextId, localVariableId});
    return contextVariableId;
}

const Runtime::ContextVariable* Runtime::getContextVariable(const ContextVariableId contextVariableId) const {
    if (isIdOutOfRange(contextVariableId.id, m_contextVariables.size())) {
        return nullptr;
    }

    return &m_contextVariables[toNomadIndex(contextVariableId.id)];
}

VariableContextId Runtime::getFirstVariableContextId() const {
    if (m_variables.empty()) {
        return VariableContextId::INVALID;
    }

    return VariableContextId{0};
}

VariableContextId Runtime::getNextVariableContextId(const VariableContextId currentId) const {
    const auto currentIndex = toNomadIndex(currentId.id);

    if (currentIndex + 1 >= m_variables.size()) {
        return VariableContextId::INVALID;
    }

    return VariableContextId{currentId.id + 1};
}

CallableId Runtime::registerScriptFunction(
    const NomadString& name,
    const NomadString& path,
    const NomadString& source
    ) {
    if (isHub(name) || !canRegisterHubMember(name, true)) {
        log::error("Cannot register function '" + name + "': name is reserved by a hub declaration");
        return CallableId::INVALID;
    }
    if (getCallableId(name) != CallableId{}) {
        log::error("Function with name '" + name + "' conflicts with an already registered callable.");

        return CallableId::INVALID;
    }

    const auto id = addCallableToTable(
        std::make_unique<ScriptFunction>(CallableId{}, name, path, source)
    );

    registerHubMember(name, HubFunctions{{id}});

    return id;
}

ScriptFunction* Runtime::getScriptFunction(const CallableId functionId) {
    if (isIdOutOfRange(functionId.id, m_callables.size())) {
        return nullptr;
    }
    auto* callable = m_callables[toNomadIndex(functionId.id)];
    if (callable->getType() != CallableType::Function) {
        return nullptr;
    }
    return static_cast<Function*>(callable)->asScriptFunction();
}

ScriptCallableBase* Runtime::getScriptCallable(const CallableId callableId) {
    if (isIdOutOfRange(callableId.id, m_callables.size())) {
        return nullptr;
    }
    return m_callables[toNomadIndex(callableId.id)]->asScriptCallable();
}

const ScriptCallableBase* Runtime::getScriptCallable(const CallableId callableId) const {
    const auto* callable = getCallable(callableId);
    return callable == nullptr ? nullptr : callable->asScriptCallable();
}

const ScriptFunction* Runtime::getScriptFunction(const CallableId functionId) const {
    const auto* function = getFunction(functionId);
    return function == nullptr ? nullptr : function->asScriptFunction();
}

bool Runtime::setScriptFunctionReturnType(const CallableId functionId, const Type* returnType) {
    auto* function = getScriptFunction(functionId);

    if (function == nullptr) {
        return false;
    }

    function->setReturnType(returnType);

    return true;
}

CallableId Runtime::getFunctionId(const NomadString& name) const {
    const auto* function = getScriptFunction(getCallableId(name));
    return function == nullptr ? CallableId{} : function->getId();
}

ScriptFunction* Runtime::getFunctionAtLine(
    const NomadString& sourceName,
    const NomadIndex line
) const {
    ScriptFunction* selected = nullptr;
    auto selectedSpan = NOMAD_INVALID_INDEX;
    for (auto* callable : m_callables) {
        if (callable->getType() != CallableType::Function || callable->getSource() != CallableSource::Script) {
            continue;
        }
        auto* function = static_cast<ScriptFunction*>(callable);
        if (function->getPath() != sourceName) continue;
        const auto start = function->getSourceStartLine();
        const auto end = function->getSourceEndLine();
        if (start == NOMAD_INVALID_INDEX || end == NOMAD_INVALID_INDEX || line < start || line > end) continue;
        if (end - start <= selectedSpan) {
            selected = function;
            selectedSpan = end - start;
        }
    }
    return selected;
}

std::optional<NomadString> Runtime::getFunctionName(CallableId functionId) const {
    const auto* function = getScriptFunction(functionId);

    if (function == nullptr) {
        return std::nullopt;
    }

    return function->getName();
}

ScriptFunction * Runtime::getFunctionByStartAddress(NomadIndex startAddress) const {
    for (auto* callable : m_callables) {
        if (callable->getType() != CallableType::Function || callable->getSource() != CallableSource::Script) {
            continue;
        }
        auto* function = static_cast<ScriptFunction*>(callable);
        if (function->getFunctionStart() == startAddress) {
            return function;
        }
    }

    return nullptr;
}

NomadIndex Runtime::getFunctionCount() const {
    return static_cast<NomadIndex>(std::count_if(m_callables.begin(), m_callables.end(), [](const auto* callable) {
        return callable->getType() == CallableType::Function && callable->getSource() == CallableSource::Script;
    }));
}

void Runtime::getFunctions(std::vector<ScriptFunction*>& functions) const {
    functions.clear();
    functions.reserve(getFunctionCount());

    for (auto* callable : m_callables) {
        if (callable->getType() == CallableType::Function && callable->getSource() == CallableSource::Script) {
            functions.push_back(static_cast<ScriptFunction*>(callable));
        }
    }
}

void Runtime::getFunctions(TempVector<ScriptFunction*>& functions) {
    functions.clear();
    functions.reserve(getFunctionCount());

    for (auto* callable : m_callables) {
        if (callable->getType() == CallableType::Function && callable->getSource() == CallableSource::Script) {
            functions.push_back(static_cast<ScriptFunction*>(callable));
        }
    }
}

ScriptFunction * Runtime::getFunctionAtInstructionIndex(const NomadIndex instructionIndex) const {
    for (auto* callable : m_callables) {
        if (callable->getType() != CallableType::Function || callable->getSource() != CallableSource::Script) {
            continue;
        }
        auto* function = static_cast<ScriptFunction*>(callable);
        if (instructionIndex >= function->getFunctionStart() && instructionIndex < function->getFunctionEnd()) {
            return function;
        }
    }

    return nullptr;
}

const ScriptCallableBase* Runtime::getScriptCallableAtLine(
    const NomadString& sourceName,
    const NomadIndex line
) const {
    if (line == 0 || line == NOMAD_INVALID_INDEX) {
        return nullptr;
    }
    const ScriptCallableBase* selected = nullptr;
    auto selectedSpan = NOMAD_INVALID_INDEX;
    for (const auto* callable : m_callables) {
        const auto* script = callable->asScriptCallable();
        if (script == nullptr || script->getPath() != sourceName) continue;
        const auto start = script->getSourceStartLine();
        const auto end = script->getSourceEndLine();
        if (start == NOMAD_INVALID_INDEX || end == NOMAD_INVALID_INDEX || line < start || line > end) continue;
        if (end - start <= selectedSpan) {
            selected = script;
            selectedSpan = end - start;
        }
    }
    return selected;
}

const ScriptCallableBase* Runtime::getScriptCallableByStartAddress(const NomadIndex startAddress) const {
    const auto* callable = getScriptCallableAtInstructionIndex(startAddress);
    return callable != nullptr && callable->getFunctionStart() == startAddress ? callable : nullptr;
}

const ScriptCallableBase* Runtime::getScriptCallableAtInstructionIndex(const NomadIndex instructionIndex) const {
    for (const auto* callable : m_callables) {
        const auto* script = callable->asScriptCallable();
        if (script == nullptr) continue;
        const auto start = script->getFunctionStart();
        const auto end = script->getFunctionEnd();
        if (start != NOMAD_INVALID_INDEX && end != NOMAD_INVALID_INDEX &&
            instructionIndex >= start && instructionIndex < end) {
            return script;
        }
    }
    return nullptr;
}

void Runtime::getScriptCallables(std::vector<const ScriptCallableBase*>& callables) const {
    callables.clear();
    for (const auto* callable : m_callables) {
        if (const auto* script = callable->asScriptCallable()) {
            callables.push_back(script);
        }
    }
}

NomadIndex Runtime::getFunctionSize() const {
    NomadIndex size = 0;

    for (const auto* callable : m_callables) {
        if (callable->getType() == CallableType::Function && callable->getSource() == CallableSource::Script) {
            size += static_cast<const ScriptFunction*>(callable)->getFunctionLength();
        }
    }

    return size;
}

std::unique_ptr<Compiler> Runtime::createCompiler() {
    auto compiler = std::make_unique<Compiler>(this, m_instructions);
    for (const auto& setup : m_compilerSetups) {
        setup(compiler.get());
    }
    return compiler;
}

void Runtime::registerCompilerSetup(std::function<void(Compiler*)> setup) {
    if (!setup) {
        throw NomadBug("Cannot register an empty compiler setup callback");
    }
    m_compilerSetups.push_back(std::move(setup));
}

std::unique_ptr<VirtualMachine> Runtime::createVirtualMachine() {
    return std::make_unique<VirtualMachine>(this);
}

void Runtime::executeFunction(const CallableId functionId) {
    executeFunction(functionId, {});
}

const ScriptFunction* Runtime::runClosure(const Closure* closure, const std::vector<RuntimeValue>& args) {
    validateScriptExecution();
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
        returnType->freeValue(&m_virtualMachine->getResult());
    }
}

void Runtime::executeFunction(const Closure* closure, const std::vector<RuntimeValue>& args, RuntimeValue& returnValue) {
    const auto function = runClosure(closure, args);

    if (function == nullptr) {
        return;
    }

    if (const auto returnType = function->getReturnType()) {
        // The caller receives its own copy; the virtual machine's result is released.
        returnType->copyValue(&m_virtualMachine->getResult(), &returnValue);
        returnType->freeValue(&m_virtualMachine->getResult());
    }  // Else keep existing value
}

void Runtime::executeFunction(const CallableId functionId, const std::vector<RuntimeValue> &args) {
    validateScriptExecution();
    if (m_virtualMachine == nullptr) {
        m_virtualMachine = std::make_unique<VirtualMachine>(this);
    }

    const auto function = getScriptFunction(functionId);

    if (function == nullptr) {
        log::error("Invalid function id");
        return;
    }

    m_virtualMachine->run(function, args);

    // The result is discarded.
    if (const auto returnType = function->getReturnType()) {
        returnType->freeValue(&m_virtualMachine->getResult());
    }
}

void Runtime::executeFunction(const CallableId functionId, const std::vector<RuntimeValue> &args, RuntimeValue& returnValue) {
    validateScriptExecution();
    if (m_virtualMachine == nullptr) {
        m_virtualMachine = std::make_unique<VirtualMachine>(this);
    }

    const auto function = getScriptFunction(functionId);

    if (function == nullptr) {
        log::error("Invalid function id");
        return;
    }

    m_virtualMachine->run(function, args);

    const auto returnType = function->getReturnType();

    if (returnType) {
        // The caller receives its own copy; the virtual machine's result is released.
        returnType->copyValue(&m_virtualMachine->getResult(), &returnValue);
        returnType->freeValue(&m_virtualMachine->getResult());
    }  // Else keep existing value
}

void Runtime::executeNativeFunction(
    const CallableId nativeFunctionId,
    const std::vector<RuntimeValue>& args,
    InterpreterContext& context
) {
    if (context.hasError()) {
        return;
    }

    const auto* definition = getNativeFunction(nativeFunctionId);
    if (definition == nullptr) {
        context.clearResult();
        context.setError("Unknown native function id");
        return;
    }
    if (!definition->isSignatureResolved()) {
        context.clearResult();
        context.setError("Native function '" + definition->getName() + "' has an unresolved signature");
        return;
    }
    if (args.size() != definition->getParameterCount()) {
        context.clearResult();
        context.setError("Incorrect argument count when calling native function '" + definition->getName() + "'");
        return;
    }
    if (definition->getReturnType() == nullptr) {
        context.clearResult();
        context.setError("NativeFunction '" + definition->getName() + "' has no return type");
        return;
    }
    if (std::ranges::any_of(definition->getParameters(), [](const auto& parameter) {
        return parameter.type == nullptr;
    })) {
        context.clearResult();
        context.setError("NativeFunction '" + definition->getName() + "' has an invalid parameter type");
        return;
    }

    try {
        context.evaluateNativeFunctionCall(nativeFunctionId, args);
    } catch (const NomadException& exception) {
        context.clearResult();
        context.setError(exception.what());
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

        const auto constantId = ConstantId{toNomadId(i)};

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
            constantTextValue = getFunction(CallableId{constantValue.getIdValue()})->getName();
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

    for (auto id = getFirstNativeFunctionId(); id.isValid(); id = getNextNativeFunctionId(id)) {
        const auto& nativeFunction = *getNativeFunction(id);
        out << "### `" << nativeFunction.getName() << "`" << std::endl;

        for (auto& parameter: nativeFunction.getParameters()) {
            out << "- `" << parameter.name << ":" << parameter.getTypeName() << "`" << std::endl;
        }

        out << std::endl;

        out << "`return " << nativeFunction.getReturnTypeName() << "`" << std::endl;

        out << std::endl;

        out << nativeFunction.getDoc() << std::endl;
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
