// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Runtime.hpp>

#include <nomad/compiler/Compiler.hpp>

#include <nomad/script/BaseTypes.hpp>
#include <nomad/script/ClassType.hpp>
#include <nomad/script/Closure.hpp>
#include <nomad/script/EnumType.hpp>
#include <nomad/script/FormatString.hpp>
#include <nomad/script/Hub.hpp>
#include <nomad/script/NativeFunctions.hpp>
#include <nomad/script/NativeMembers.hpp>
#include <nomad/script/NativeProperty.hpp>
#include <nomad/script/NomadStringObject.hpp>
#include <nomad/script/RegisterRangeType.hpp>
#include <nomad/script/RuntimeValueGuard.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/StructType.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/String.hpp>

#include <algorithm>
#include <utility>

namespace nomad {

Runtime::Runtime():
    m_typeManager(*this)
{
    // Types
    if (
        registerType(std::make_unique<VoidType>(this, m_typeManager.reserveTypeId())).isInvalid() ||
        registerType(std::make_unique<IdType>(this, m_typeManager.reserveTypeId())).isInvalid() ||
        registerType(std::make_unique<FloatType>(this, m_typeManager.reserveTypeId())).isInvalid() ||
        registerType(std::make_unique<IntegerType>(this, m_typeManager.reserveTypeId())).isInvalid() ||
        registerType(std::make_unique<BooleanType>(this, m_typeManager.reserveTypeId())).isInvalid() ||
        registerType(std::make_unique<StringType>(this, m_typeManager.reserveTypeId())).isInvalid() ||
        registerType(std::make_unique<FileNameType>(this, m_typeManager.reserveTypeId())).isInvalid() ||
        registerType(std::make_unique<FunctionNameType>(this, m_typeManager.reserveTypeId())).isInvalid() ||
        registerType(std::make_unique<FunctionLineNumberType>(this, m_typeManager.reserveTypeId())).isInvalid() ||
        registerType(std::make_unique<EventCallbackType>(this, m_typeManager.reserveTypeId())).isInvalid() ||
        registerType(std::make_unique<EventDispatchType>(this, m_typeManager.reserveTypeId())).isInvalid()
    ) {
        throw NomadException("Failed to register built-in types");
    }

    if (
        !registerParameterizedType(
            "Iterator", 
            1,
            [](Runtime* runtime, TypeId id, const std::vector<const Type*>& arguments) {
                return std::make_unique<ClassType>(
                    runtime, id, "Iterator<" + arguments[0]->getTypeName() + ">", ClassMode::Closed, arguments
                );
            },
            [](Runtime* runtime, const TypeId id) {
                const auto* iteratorType = runtime->getType(id).value()->asClassType();
                const auto* valueType = iteratorType->getTypeArguments()[0];
                const auto advanceId = runtime->registerNativeClassMethod(
                    id, 
                    {
                        .name = "moveNext",
                        .parameters = {},
                        .returnTypeId = runtime->getTypeId("bool"),
                        .function = [](VirtualMachine* machine) {
                            machine->setBooleanResult(machine->getThisAs<IteratorObject>()->moveNext());
                        },
                        .documentation = NomadDoc("Advances the iterator. Returns false at the end.")
                    }
                );
                const auto currentId = runtime->registerNativeClassProperty(
                    id, 
                    {
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
                    }
                );
                if (advanceId.isInvalid() || currentId.isInvalid()) {
                    throw NomadException("Failed to register iterator members");
                }
            }
        ) ||
        !registerMemberTemplate("Iterator", "moveNext", MemberType::Method) ||
        !registerMemberTemplate("Iterator", "current", MemberType::Property)
    ) {
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

    registerKeyword("fun", NomadDoc("Function declaration"));
    registerKeyword("class", NomadDoc("Declares a closed class type"));
    registerKeyword("struct", NomadDoc("Declares a closed struct"));
    registerKeyword("hub", NomadDoc("Declare a hub"));
    registerKeyword("open", NomadDoc("Declares an extensible class, struct or hub"));
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
    const auto qualifiedName = qualifyHubMemberName(hubId, name);
    if (!qualifiedName) {
        return ConstantId::INVALID;
    }

    return registerConstant(*qualifiedName, value, type);
}

TypeId Runtime::registerHubEnum(
    const HubId hubId,
    NomadString name,
    std::vector<EnumValueDefinition> values,
    NomadString documentation
) {
    const auto qualifiedName = qualifyHubMemberName(hubId, name);
    if (!qualifiedName) {
        return TypeId::INVALID;
    }

    return createEnum(*qualifiedName, std::move(values), std::move(documentation));
}

CallableId Runtime::registerHubNativeFunction(
    const HubId hubId,
    const NomadString& name,
    NativeFunctionFn function,
    const std::vector<Parameter>& parameters,
    const Type* returnType,
    NomadDocArg
) {
    const auto qualifiedName = qualifyHubMemberName(hubId, name);
    if (!qualifiedName) {
        return CallableId::INVALID;
    }

    const auto* member = getHub(hubId)->getMember(name);
    if (member != nullptr && !std::holds_alternative<HubFunctions>(*member)) {
        log::error("Hub member '" + *qualifiedName + "' is already declared");

        return CallableId::INVALID;
    }

    const auto id = registerNativeFunctionEntry(
        *qualifiedName,
        std::move(function),
        parameters,
        returnType,
        doc
    );
    if (id.isValid()) {
        addHubMember(m_hubs[toNomadIndex(hubId.id)]->m_members, name, HubFunctions{{id}});
    }

    return id;
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
    if (
        m_scriptInitialization.state != ScriptInitializationState::Pending ||
        getContainingHub(name).empty() || 
        !canRegisterHubMember(name) ||
        isHubVariable(name) ||
        getCallableId(name).isValid() || 
        getConstantId(name).isValid() ||
        getTypeByName(name) != nullptr || 
        getEventId(name).isValid() ||
        getDynamicVariableId(name).isValid() ||
        getVariableContextIdByPrefix(name).isValid()
    ) {
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

    if (
        m_scriptInitialization.state != ScriptInitializationState::Pending ||
        m_scriptInitialization.functionId != CallableId::INVALID || 
        function == nullptr ||
        function->getParameterCount() != 0 || 
        function->getReturnType() != getVoidType() ||
        function->getFunctionStart() == NOMAD_INVALID_INDEX
    ) {
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

    if (initialization.functionId == CallableId::INVALID) {
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
    if (isHub(name)) {
        return false;
    }

    if (name.find('@') != NomadString::npos) {
        return true;
    }
    const auto* hub = getHub(getHubId(getContainingHub(name)));
    if (hub == nullptr) {
        return true;
    }

    const auto* member = hub->getMember(name.substr(hub->getName().size() + 1));

    return member == nullptr || (function && std::holds_alternative<HubFunctions>(*member));
}

void Runtime::addHubMember(
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
        for (const auto& [name, member] : hub.m_members) {
            const auto* functions = std::get_if<HubFunctions>(&member);
            if (functions == nullptr) {
                continue;
            }

            HubFunctions nativeFunctions;
            for (const auto id : functions->ids) {
                if (getNativeFunction(id) != nullptr) {
                    nativeFunctions.ids.push_back(id);
                }
            }

            if (!nativeFunctions.ids.empty()) {
                addHubMember(members[index], name, std::move(nativeFunctions));
            }
        }

        for (auto id = hub.m_variables->getFirstVariableId(); id.isValid();
             id = hub.m_variables->getNextVariableId(id)) {
            add(hub.m_variables->getVariableName(id), id);
        }
    }
    for (const auto* callable : m_callables) {
        if (
            callable->getType() == CallableType::Function &&
            callable->getSource() == CallableSource::Script
        ) {
            add(callable->getName(), HubFunctions{{callable->getId()}});
        }
    }
    for (auto id = getFirstConstantId(); id.isValid(); id = getNextConstantId(id)) {
        add(getConstantName(id), id);
    }
    for (const auto* type : m_namespaceTypes) {
        if (getEventId(type->getTypeName()).isInvalid()) {
            add(type->getTypeName(), type);
        }
    }
    for (const auto& event : m_events) {
        add(event.name, event.id);
    }
    for (auto index = NomadIndex{0}; index < m_dynamicVariables.size(); ++index) {
        add(m_dynamicVariables[index].name, DynamicVariableId{toNomadId(index)});
    }
    for (const auto& family : m_namespaceTypeFamilies) {
        add(family.name, HubTypeFamily{family.argumentCount});
    }
    for (auto index = NomadIndex{0}; index < m_hubs.size(); ++index) {
        m_hubs[index]->m_members = std::move(members[index]);
    }
}

HubId Runtime::registerHub(const NomadString& name, const HubMode mode) {
    if (
        name.empty() || 
        (mode != HubMode::Opened && mode != HubMode::Closed)
    ) {
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

    if (
        getCallableId(name).isValid() || 
        getConstantId(name).isValid() ||
        getTypeByName(name) != nullptr || 
        getEventId(name).isValid() ||
        getDynamicVariableId(name).isValid() || 
        isHubVariable(name) ||
        getVariableContextIdByPrefix(name + ".").isValid() ||
        m_typeManager.getParameterizedTypeArgumentCount(name).has_value() ||
        m_typeManager.getParameterizedConstructorArgumentCount(name).has_value()
    ) {
        log::error("Cannot register hub '" + name + "': identifier is already used");
        return HubId::INVALID;
    }

    const auto previousCount = m_hubs.size();
    m_hubs.push_back(std::make_unique<Hub>(name, mode));

    try {
        rebuildHubMembers();
    } catch (const NomadException& exception) {
        m_hubs.resize(previousCount);
        log::error("Cannot register hub '" + name + "': " + exception.what());

        return HubId::INVALID;
    }

    return getHubId(name);
}

CallableId Runtime::registerNativeFunction(
    const NomadString& name,
    NativeFunctionFn fn,
    const std::vector<Parameter>& parameters,
    const Type* returnType,
    const NomadString& doc
) {
    return registerNativeFunctionEntry(name, std::move(fn), parameters, returnType, doc);
}

CallableId Runtime::registerNativeFunctionEntry(
    const NomadString& name,
    NativeFunctionFn fn,
    const std::vector<Parameter>& parameters,
    const Type* returnType,
    const NomadString& doc
) {
    if (!canRegisterHubMember(name, true)) {
        log::error("Cannot register function '" + name + "': name is reserved by a hub");
        return CallableId::INVALID;
    }

    const auto isDeferred = [this](const Type* type) {
        return type != nullptr && type->getRuntime() == this &&
            dynamic_cast<const DeferredType*>(type) != nullptr;
    };

    const auto deferred = 
        isDeferred(returnType) ||
        std::ranges::any_of(
            parameters, 
            [&](const auto& parameter) { 
                return isDeferred(parameter.type); 
            }
        );
    if (
        !fn ||
        (
            !isDeferred(returnType) &&
            (
                !ownsType(returnType) || 
                (
                    returnType->isInternal() && 
                    !returnType->isVoid()
                )
            )
        )
    ) {
        log::error("Cannot register native callable '" + name + "': invalid callback or return type");
        return CallableId::INVALID;
    }
    for (const auto& parameter : parameters) {
        if (
            parameter.name.empty() || 
            (
                !isDeferred(parameter.type) &&
                (
                    !ownsType(parameter.type) ||
                    parameter.type->isVoid()
                )
            )
        ) {
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
        getNextCallableId(),
        name, 
        std::move(fn), 
        parameters, 
        returnType, 
        doc, 
        pending
    );
    return addCallableToTable(std::move(function));
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
            if (
                !ownsType(native->getReturnType()) ||
                std::ranges::any_of(
                    native->getParameters(), 
                    [this](const auto& parameter) {
                        return !ownsType(parameter.type);
                    }
                )
            ) {
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
    const auto id = catalogCallable(m_functions.back().get());
    if (id == CallableId::INVALID) {
        m_functions.pop_back();
    }

    return id;
}

CallableId Runtime::getNextCallableId() const {
    if (m_callables.size() >= toNomadIndex(NOMAD_ID_MAX)) {
        return CallableId::INVALID;
    }

    return CallableId{toNomadId(m_callables.size())};
}

CallableId Runtime::catalogCallable(Callable* callable) {
    const auto id = getNextCallableId();
    if (callable == nullptr || id.isInvalid() || callable->getId() != id) {
        return CallableId::INVALID;
    }

    m_callables.push_back(callable);

    return id;
}

PropertyId Runtime::registerNativeClassProperty(
    const TypeId ownerId,
    NativePropertyDefinition definition
) {
    const auto type = getType(ownerId);
    const auto* classType = type.has_value() ? (*type)->asClassType() : nullptr;

    if (classType == nullptr) {
        log::error("Cannot register native property '" + definition.name + "': owner must identify a class type");
        return PropertyId::INVALID;
    }

    const auto* valueType = definition.valueType;
    const auto hasCallbacks = static_cast<bool>(definition.getter) || static_cast<bool>(definition.setter);
    const auto hasMethods = definition.getterMethodId.isValid() || definition.setterMethodId.isValid();

    if (hasMethods && hasCallbacks) {
        log::error("Cannot register native property '" + definition.name + "': callbacks and accessor methods cannot be mixed");
        return PropertyId::INVALID;
    }

    if (hasMethods) {
        const auto* getter = getMethod(definition.getterMethodId);
        const auto* setter = getMethod(definition.setterMethodId);

        if (!ownsType(valueType) || valueType->isVoid() || valueType->isInternal()) {
            log::error("Cannot register native property '" + definition.name + "': accessor methods require a resolved value type");
            return PropertyId::INVALID;
        }

        if (
            definition.getterMethodId.isValid() &&
            (getter == nullptr || getter->asNativeMethod() == nullptr ||
            getter->getClassType() != classType ||
             getter->getParameterCount() != 0 ||
             getter->getReturnType() != valueType)
        ) {
            log::error("Cannot register native property '" + definition.name + "': getter must be a native method on its owner with no arguments and a matching result");
            return PropertyId::INVALID;
        }

        if (
            definition.setterMethodId.isValid() &&
            (
                setter == nullptr ||
                setter->asNativeMethod() == nullptr ||
                setter->getClassType() != classType ||
                setter->getParameterCount() != 1 ||
                setter->getParameters()[0].type != valueType ||
                setter->getReturnType() == nullptr ||
                !setter->getReturnType()->isVoid()
            )
        ) {
            log::error("Cannot register native property '" + definition.name + "': setter must be a native method on its owner with one matching argument and a void result");
            return PropertyId::INVALID;
        }
    }

    const auto hasAccessors = hasCallbacks || hasMethods;
    auto property = std::make_unique<NativeProperty>(
        m_typeManager.getNextPropertyId(),
        classType,
        definition.name,
        valueType,
        std::move(definition.getter),
        std::move(definition.setter),
        std::move(definition.documentation),
        definition.getterMethodId,
        definition.setterMethodId
    );

    const auto belongsToRuntime = valueType != nullptr && valueType->getRuntime() == this;
    const auto isDeferredType = !property->isTypeResolved() && belongsToRuntime;
    // Same-runtime placeholders may be registered before their value type resolves.
    const auto hasValidValueType =
        isDeferredType ||
        (
            ownsType(valueType) &&
            !valueType->isVoid() &&
            !valueType->isInternal()
        );

    if (!hasValidValueType) {
        log::error("Cannot register native property '" + definition.name + "': invalid value type");
        return PropertyId::INVALID;
    }

    if (!hasAccessors) {
        log::error("Cannot register native property '" + definition.name + "': property has no accessor or mutator");
        return PropertyId::INVALID;
    }

    NomadString error;

    if (!canRegisterNativeMember(classType, definition.name, MemberType::Property, {}, error)) {
        log::error("Cannot register native property '" + definition.name + "': " + error);
        return PropertyId::INVALID;
    }

    auto* owner = m_typeManager.getMutableClassType(classType);

    const auto propertyId = m_typeManager.registerProperty(owner, std::move(property));
    if (propertyId.isInvalid()) {
        return PropertyId::INVALID;
    }

    catalogNativeProperty(propertyId);

    return propertyId;
}

CallableId Runtime::registerScriptClassMethod(
    const TypeId ownerId,
    NomadString name,
    std::vector<Parameter> parameters,
    NomadString path,
    NomadString source
) {
    const auto type = getType(ownerId);
    const auto* classType = type.has_value() ? (*type)->asClassType() : nullptr;
    auto* owner = m_typeManager.getMutableClassType(classType);

    return registerScriptMethod(
        owner,
        std::move(name),
        std::move(parameters),
        std::move(path),
        std::move(source)
    );
}

CallableId Runtime::registerScriptStructMethod(
    const TypeId ownerId,
    NomadString name,
    std::vector<Parameter> parameters,
    NomadString path,
    NomadString source
) {
    auto* owner = m_typeManager.getMutableCompositeType(ownerId);

    return registerScriptMethod(
        owner != nullptr && owner->isStruct() ? owner : nullptr,
        std::move(name),
        std::move(parameters),
        std::move(path),
        std::move(source)
    );
}

CallableId Runtime::registerScriptMethod(
    CompositeType* owner,
    NomadString name,
    std::vector<Parameter> parameters,
    NomadString path,
    NomadString source
) {
    const auto canValidateParameters = owner != nullptr && !name.empty();

    const auto hasInvalidParameters =
        canValidateParameters &&
         std::ranges::any_of(parameters,
        [this](const auto& parameter) {
            const auto isReceiverName = parameter.name == "this";
            const auto
                hasValidType = !isReceiverName &&
                ownsType(parameter.type) &&
                !parameter.type->isVoid() &&
                !parameter.type->isInternal();

            return !hasValidType;
        }
    );

    if (owner == nullptr || name.empty() || hasInvalidParameters) {
        log::error("Cannot register script method: invalid owner, name, or parameter type");
        return CallableId::INVALID;
    }

    for (const auto* method : owner->getMethods()) {
        const auto hasSameName = method->getName() == name;
        const auto hasSameSignature = hasSameName && std::ranges::equal(
            method->getParameters(), parameters,
            [](const auto& first, const auto& second) { return first.type == second.type; }
        );

        if (hasSameSignature) {
            log::error("Cannot register script method: duplicate signature");
            return CallableId::INVALID;
        }
    }

    auto method = std::make_unique<ScriptMethod>(
        getNextCallableId(),
        std::move(name),
        owner,
        std::move(parameters),
        nullptr,
        std::move(path),
        std::move(source)
    );

    return registerMethod(
        owner,
        std::move(method)
    );
}

CallableId Runtime::registerScriptClassConstructor(
    const TypeId ownerId,
    std::vector<Parameter> parameters,
    NomadString path,
    NomadString source
) {
    const auto type = getType(ownerId);
    const auto* classType = type.has_value() ? (*type)->asClassType() : nullptr;
    auto* owner = m_typeManager.getMutableClassType(classType);

    return registerScriptConstructor(
        owner,
        std::move(parameters),
        std::move(path),
        std::move(source)
    );
}

CallableId Runtime::registerScriptStructConstructor(
    const TypeId ownerId,
    std::vector<Parameter> parameters,
    NomadString path,
    NomadString source
) {
    const auto type = getType(ownerId);
    auto* owner = type.has_value() && (*type)->isStruct() ? m_typeManager.getMutableCompositeType(ownerId) : nullptr;

    return registerScriptConstructor(
        owner, 
        std::move(parameters), 
        std::move(path), 
        std::move(source)
    );
}

CallableId Runtime::registerScriptConstructor(
    CompositeType* owner,
    std::vector<Parameter> parameters,
    NomadString path,
    NomadString source
) {
    const auto hasInvalidParameters =
        owner != nullptr &&
        std::ranges::any_of(parameters, [this](const auto& parameter) {
            const auto isReceiverName = parameter.name == "this";
            const auto hasValidType = !isReceiverName && ownsType(parameter.type) &&
                !parameter.type->isVoid() && !parameter.type->isInternal();
            return !hasValidType;
        }
    );

    if (owner == nullptr || hasInvalidParameters) {
        log::error("Cannot register script constructor: invalid owner or parameter type");
        return CallableId::INVALID;
    }

    for (const auto* constructor : owner->getConstructors()) {
        const auto hasSameSignature = std::ranges::equal(
            constructor->getParameters(), parameters,
            [](const auto& first, const auto& second) { return first.type == second.type; }
        );
        if (hasSameSignature) {
            log::error("Cannot register script constructor: duplicate signature");
            return CallableId::INVALID;
        }
    }

    auto constructor = std::make_unique<ScriptConstructor>(
        getNextCallableId(),
        owner,
        std::move(parameters),
        std::move(path),
        std::move(source)
    );

    return registerConstructor(owner, std::move(constructor));
}

CallableId Runtime::registerNativeClassMethod(
    const TypeId ownerId,
    NativeMethodDefinition definition
) {
    const auto type = getType(ownerId);
    const auto* classType = type.has_value() ? (*type)->asClassType() : nullptr;

    return registerNativeMethod(m_typeManager.getMutableClassType(classType), std::move(definition));
}

CallableId Runtime::registerNativeStructMethod(
    const TypeId ownerId,
    NativeMethodDefinition definition
) {
    auto* owner = m_typeManager.getMutableCompositeType(ownerId);

    return registerNativeMethod(owner != nullptr && owner->isStruct() ? owner : nullptr,
        std::move(definition));
}

CallableId Runtime::registerNativeMethod(
    CompositeType* owner,
    NativeMethodDefinition definition
) {
    if (owner == nullptr) {
        log::error("Cannot register native method '" + definition.name + "': invalid owner type");
        return CallableId::INVALID;
    }

    const auto returnType = getType(definition.returnTypeId);

    if (!returnType.has_value()) {
        log::error("Cannot register native method '" + definition.name + "': invalid return type ID");
        return CallableId::INVALID;
    }

    std::vector<Parameter> parameters;
    parameters.reserve(definition.parameters.size());

    for (const auto& parameterDefinition : definition.parameters) {
        const auto parameterType = getType(parameterDefinition.typeId);

        if (!parameterType.has_value()) {
            log::error("Cannot register native method '" + definition.name + "': invalid parameter type ID");
            return CallableId::INVALID;
        }

        parameters.push_back(defParameter(
            parameterDefinition.name, *parameterType, parameterDefinition.documentation
        ));
    }

    const auto hasValidReturnType =
        definition.function &&
        (
            !(*returnType)->isInternal() ||
            (*returnType)->isVoid()
        );

    if (!definition.function || !hasValidReturnType) {
        log::error("Cannot register native callable '" + definition.name + "': invalid callback or return type");

        return CallableId::INVALID;
    }

    for (const auto& parameter : parameters) {
        if (parameter.name.empty() || parameter.type->isVoid()) {
            log::error("Cannot register native callable '" + definition.name + "': invalid parameter definition");

            return CallableId::INVALID;
        }
    }

    NomadString error;

    if (!canRegisterNativeMember(owner, definition.name, MemberType::Method, parameters, error)) {
        log::error("Cannot register native method '" + definition.name + "': " + error);

        return CallableId::INVALID;
    }

    const auto id = registerMethod(owner, std::make_unique<NativeMethod>(
        getNextCallableId(),
        definition.name,
        owner,
        std::move(definition.function),
        std::move(parameters),
        *returnType,
        std::move(definition.documentation)
    ));

    if (id.isInvalid()) {
        return CallableId::INVALID;
    }

    catalogNativeMethod(id);

    return id;
}

CallableId Runtime::registerNativeClassConstructor(
    const TypeId ownerId,
    NativeConstructorDefinition definition
) {
    return registerNativeClassConstructor(ownerId, ownerId, std::move(definition));
}

CallableId Runtime::registerNativeClassConstructor(
    const TypeId ownerId,
    const TypeId createdTypeId,
    NativeConstructorDefinition definition
) {
    const auto type = getType(ownerId);
    const auto* classType = type.has_value() ? (*type)->asClassType() : nullptr;

    if (classType == nullptr) {
        log::error("Cannot register constructor: owner must identify a class type");
        return CallableId::INVALID;
    }

    const auto resultType = getType(createdTypeId);
    const auto* createdType = resultType.has_value() ? (*resultType)->asClassType() : nullptr;

    if (createdType == nullptr) {
        log::error("Cannot register constructor: created type must identify a class type");
        return CallableId::INVALID;
    }

    return registerNativeConstructor(m_typeManager.getMutableClassType(classType), createdType, std::move(definition));
}

CallableId Runtime::registerNativeStructConstructor(
    const TypeId ownerId,
    NativeConstructorDefinition definition
) {
    const auto type = getType(ownerId);
    auto* owner = type.has_value() && (*type)->isStruct() ? m_typeManager.getMutableCompositeType(ownerId) : nullptr;

    if (owner == nullptr) {
        log::error("Cannot register constructor: owner must identify a struct type");
        return CallableId::INVALID;
    }

    return registerNativeConstructor(owner, owner, std::move(definition));
}

CallableId Runtime::registerNativeConstructor(
    CompositeType* owner,
    const Type* createdType,
    NativeConstructorDefinition definition
) {
    std::vector<Parameter> parameters;
    parameters.reserve(definition.parameters.size());

    for (const auto& parameterDefinition : definition.parameters) {
        const auto parameterType = getType(parameterDefinition.typeId);

        if (!parameterType.has_value()) {
            log::error("Cannot register constructor: invalid parameter type ID");
            return CallableId::INVALID;
        }

        parameters.push_back(defParameter(
            parameterDefinition.name, *parameterType, parameterDefinition.documentation
        ));
    }

    if (createdType->isInternal() || !definition.function) {
        log::error("Cannot register constructor: a registered public constructed type and callback are required");
        return CallableId::INVALID;
    }

    const auto name = owner->getTypeName();
    const auto isReservedName =
        name.empty() ||
        m_typeManager.getParameterizedConstructorArgumentCount(name).has_value() ||
        m_typeManager.getParameterizedTypeArgumentCount(name).has_value();

    if (isReservedName) {
        log::error("Cannot register constructor '" + name + "': the name is reserved");

        return CallableId::INVALID;
    }

    for (const auto& parameter : parameters) {
        if (
            owner->isStruct() &&
             (
                parameter.type->isInternal() ||
                parameter.name == "this")
            ) {
            log::error("Cannot register struct constructor: invalid parameter type or receiver parameter");

            return CallableId::INVALID;
        }

        if (parameter.type->isVoid()) {
            log::error("Cannot register constructor: parameter types must be registered and non-void");

            return CallableId::INVALID;
        }
    }

    for (const auto* existing : owner->getConstructors()) {
        if (existing->getParameterCount() != parameters.size()) {
            continue;
        }

        const auto hasSameSignature = std::ranges::equal(
            existing->getParameters(), parameters,
            [](const auto& left, const auto& right) {
                 return left.type->sameType(right.type);
            }
        );

        if (hasSameSignature) {
            log::error("Cannot register constructor '" + name + "': duplicate signature");
            return CallableId::INVALID;
        }
    }

    return registerConstructor(owner, std::make_unique<NativeConstructor>(
        getNextCallableId(),
        name,
        owner,
        createdType,
        std::move(definition.function),
        std::move(parameters),
        std::move(definition.documentation)
    ));
}

void Runtime::registerNativeClassIterable(const TypeId ownerId, NativeInterableDefinition definition) {
    const auto type = getType(ownerId);
    auto* owner = type.has_value() ? m_typeManager.getMutableClassType((*type)->asClassType()) : nullptr;

    if (owner == nullptr) {
        throw NomadException("Cannot register iterable: owner must identify a class type");
    }

    const auto valueType = getType(definition.valueTypeId);
    const auto hasValidValueType =
        valueType.has_value() &&
        !(
            (*valueType)->isVoid() ||
            (*valueType)->isInternal()
        );

    if (!hasValidValueType) {
        throw NomadException("Cannot register iterable: value type must identify a public, non-void type");
    }

    if (owner->getIterableInfo().has_value()) {
        throw NomadException("Cannot register iterable: type already has an iterable association");
    }

    const auto hasValidFactory = definition.factory.parameters.empty() && definition.factory.function;

    if (!hasValidFactory) {
        throw NomadException("Cannot register iterable: a parameterless factory callback is required");
    }

    const auto returnType = getType(definition.factory.returnTypeId);
    const auto hasMatchingReturnType = returnType.has_value() && *returnType == m_typeManager.getIteratorType(*valueType);

    if (!hasMatchingReturnType) {
        throw NomadException("Cannot register iterable: factory must return the matching iterator type");
    }

    const auto factoryId = registerNativeClassMethod(ownerId, std::move(definition.factory));

    if (factoryId.isInvalid()) {
        throw NomadException("Cannot register iterable: failed to register factory method");
    }

    owner->registerIterableType(*this, *valueType, getMethod(factoryId));
}

IndexerId Runtime::registerNativeClassIndexer(const TypeId ownerId, NativeIndexerDefinition definition) {
    const auto type = getType(ownerId);
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

    auto* owner = m_typeManager.getMutableClassType(classType);

    if (owner == nullptr) {
        log::error("Cannot register native indexer: class type must be registered with this runtime");
        return IndexerId::INVALID;
    }

    return owner->registerNativeIndexer(
        *this,
{
            IndexerId::INVALID, classType, std::move(indexTypes), *valueType,
            std::move(definition.getter), std::move(definition.setter), definition.documentation
        }
    );
}

CallableId Runtime::registerMethod(
    CompositeType* owner,
    std::unique_ptr<Method> method
) {
    if (!ownsType(owner) || method == nullptr || method->getId() != getNextCallableId() ||
        method->getId().isInvalid()) {
        log::error("Cannot register method: invalid owner, identity, or callable capacity");

        return CallableId::INVALID;
    }

    if (m_callables.size() == m_callables.capacity()) {
        m_callables.reserve(std::max(std::size_t{1}, m_callables.size() * 2));
    }

    auto* registered = method.get();
    const auto id = registered->getId();

    if (owner->registerMethod(m_typeManager, std::move(method)).isInvalid()) {
        return CallableId::INVALID;
    }

    m_callables.push_back(registered);

    return id;
}

CallableId Runtime::registerConstructor(
    CompositeType* owner,
    std::unique_ptr<Constructor> constructor
) {
    if (!ownsType(owner) || constructor == nullptr || constructor->getId() != getNextCallableId() ||
        constructor->getId().isInvalid()) {
        log::error("Cannot register constructor: invalid owner, identity, or callable capacity");

        return CallableId::INVALID;
    }

    if (m_callables.size() == m_callables.capacity()) {
        m_callables.reserve(std::max(std::size_t{1}, m_callables.size() * 2));
    }

    auto* registered = constructor.get();
    const auto id = registered->getId();

    if (owner->registerConstructor(m_typeManager, std::move(constructor)).isInvalid()) {
        return CallableId::INVALID;
    }

    m_callables.push_back(registered);

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

    return m_typeManager.getParameterShape(type);
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

    return !name.empty() &&
        (mode == ClassMode::Opened || mode == ClassMode::Closed) &&
        getTypeId(name).isInvalid() &&
        canRegisterTypeName(name);
}

TypeId Runtime::publishClass(
    const TypeId id,
    const NomadString& name,
    const ClassMode mode,
    std::unique_ptr<ClassType> type
) {
    const auto matchesIdentity = type->getRuntime() == this &&
        type->getId() == id && type->getTypeName() == name;
    const auto canPublish = matchesIdentity && canRegisterClass(name, mode);
    const auto matchesMode = canPublish && type->isOpen() == (mode == ClassMode::Opened);
    if (!canPublish || !matchesMode) {
        return TypeId::INVALID;
    }

    return registerType(std::move(type));
}

TypeId Runtime::registerStruct(NomadString name, NomadString documentation) {
    if (name.empty() || !canRegisterTypeName(name) || getTypeId(name).isValid()) {
        return TypeId::INVALID;
    }

    const auto id = m_typeManager.reserveTypeId();
    if (id.isInvalid()) {
        return TypeId::INVALID;
    }

    return registerType(std::make_unique<StructType>(this, id, std::move(name), std::move(documentation)));
}

bool Runtime::canPublishTypeNamespace(const Type& type) const {
    if (type.asCallback() != nullptr || type.asEvent() != nullptr) {
        return true;
    }

    const auto name = type.getTypeName();
    if (isHub(name) || isHubVariable(name)) {
        return false;
    }

    return getEventId(name).isValid() || canRegisterHubMember(name);
}

TypeId Runtime::registerType(std::unique_ptr<Type> type) {
    if (type == nullptr || type->getRuntime() != this || !canPublishTypeNamespace(*type)) {
        return TypeId::INVALID;
    }

    const auto* registeredType = type.get();
    const auto id = m_typeManager.registerType(std::move(type));
    if (id.isInvalid()) {
        return TypeId::INVALID;
    }

    publishTypeNamespace(*registeredType);

    return id;
}

const OpaqueIdType* Runtime::registerOpaqueIdType(const NomadString& name, const NomadString& doc) {
    if (name.empty() || !canRegisterTypeName(name) || getTypeId(name).isValid()) {
        return nullptr;
    }

    const auto id = m_typeManager.reserveTypeId();
    if (id.isInvalid()) {
        return nullptr;
    }

    auto type = std::make_unique<OpaqueIdType>(this, id, name, doc);
    const auto* registeredType = type.get();
    if (registerType(std::move(type)).isInvalid()) {
        return nullptr;
    }

    return registeredType;
}

TypeId Runtime::createEnum(
    NomadString name,
    std::vector<EnumValueDefinition> values,
    NomadString documentation
) {
    if (name.empty() || !canRegisterTypeName(name) || getTypeId(name).isValid()) {
        return TypeId::INVALID;
    }

    for (const auto& definition : values) {
        const auto constantName = name + "." + definition.name;
        if (!canRegisterTypeName(constantName) || getConstantId(constantName).isValid()) {
            return TypeId::INVALID;
        }
    }

    const auto id = m_typeManager.createEnum(name, std::move(values), std::move(documentation));
    if (id.isInvalid()) {
        return TypeId::INVALID;
    }

    const auto* type = getType(id).value()->asEnum();
    publishTypeNamespace(*type);
    for (auto index = NomadIndex{0}; index < type->getValueCount(); ++index) {
        const auto& definition = type->getValue(index);
        const auto constantId = registerConstant(
            name + "." + definition.name, RuntimeValue{definition.value}, type
        );
        if (constantId.isInvalid()) {
            throw NomadException("Failed to register value '" + definition.name + "' in enum '" + name + "'");
        }
    }

    registerEnumOperators(type);

    return id;
}

bool Runtime::registerNativeParameterizedConstructor(
    const NomadString& name,
    const NomadIndex argumentCount,
    ParameterizedConstructorInitializer initializer
) {
    if (name.empty() || !canRegisterTypeName(name)) {
        return false;
    }

    if (!m_typeManager.registerNativeParameterizedConstructor(name, argumentCount, std::move(initializer))) {
        return false;
    }

    publishTypeFamilyNamespace(name, argumentCount);

    return true;
}

bool Runtime::registerParameterizedType(
    const NomadString& name,
    const NomadIndex argumentCount,
    ParameterizedTypeFactory factory,
    ParameterizedTypeInitializer initializer
) {
    if (name.empty() || !canRegisterTypeName(name) || !factory) {
        return false;
    }

    const auto registered = m_typeManager.registerParameterizedType(
        name,
        argumentCount,
        [this, factory = std::move(factory)](
            Runtime* owner,
            const TypeId id,
            const std::vector<const Type*>& arguments
        ) {
            auto type = factory(owner, id, arguments);
            if (
                type != nullptr &&
                type->getRuntime() == this &&
                type->getId() == id &&
                !canPublishTypeNamespace(*type)
            ) {
                return std::unique_ptr<Type>{};
            }

            return type;
        },
        [this, initializer = std::move(initializer)](Runtime* owner, const TypeId id) {
            publishTypeNamespace(*m_typeManager.getType(id).value());
            if (initializer) {
                initializer(owner, id);
            }
        }
    );
    if (!registered) {
        return false;
    }

    publishTypeFamilyNamespace(name, argumentCount);

    return true;
}

bool Runtime::canRegisterTypeName(const NomadString& name) const {
    return !isHubVariable(name) && canRegisterHubMember(name);
}

void Runtime::publishTypeNamespace(const Type& type) {
    if (!m_typeManager.ownsType(&type)) {
        throw NomadException("Cannot publish a type without matching registered catalog identity");
    }
    if (type.asCallback() == nullptr && type.asEvent() == nullptr) {
        if (std::ranges::find(m_namespaceTypes, &type) != m_namespaceTypes.end()) {
            throw NomadException("Type namespace '" + type.getTypeName() + "' is already published");
        }

        if (isHub(type.getTypeName()) || isHubVariable(type.getTypeName())) {
            throw NomadException("Type name '" + type.getTypeName() + "' is reserved by a hub");
        }
        if (getEventId(type.getTypeName()).isInvalid()) {
            if (!canRegisterHubMember(type.getTypeName())) {
                throw NomadException("Type name '" + type.getTypeName() + "' is already a hub member");
            }
            registerHubMember(type.getTypeName(), &type);
        }

        m_namespaceTypes.push_back(&type);
    }
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

void Runtime::registerEnumOperators(const EnumType* type) {
    if (!ownsType(type)) {
        throw NomadException("Cannot register enum operators: type is not registered with this runtime");
    }
    registerBinaryOperator(
        BinaryOperator::EqualEqual,
        type,
        type,
        getBooleanType(),
        OpCodes::op_integer_equal,
        foldIntegerBinaryEqualEqual
    );
    registerBinaryOperator(
        BinaryOperator::BangEqual,
        type,
        type,
        getBooleanType(),
        OpCodes::op_integer_not_equal,
        foldIntegerBinaryBangEqual
    );

}

void Runtime::publishTypeFamilyNamespace(const NomadString& name, const NomadIndex argumentCount) {
    auto registeredArgumentCount = m_typeManager.getParameterizedTypeArgumentCount(name);
    if (!registeredArgumentCount.has_value()) {
        registeredArgumentCount = m_typeManager.getParameterizedConstructorArgumentCount(name);
    }
    const auto matchesRegistration = registeredArgumentCount.has_value() && *registeredArgumentCount == argumentCount;
    const auto alreadyPublished = std::ranges::any_of(m_namespaceTypeFamilies, [&](const auto& family) {

        return family.name == name;
    });
    const auto canPublish = matchesRegistration && !alreadyPublished && canRegisterTypeName(name);
    if (!canPublish) {
        throw NomadException("Cannot publish parameterized type '" + name + "': invalid registration or namespace collision");
    }
    registerHubMember(name, HubTypeFamily{argumentCount});
    m_namespaceTypeFamilies.push_back({name, argumentCount});
}

bool Runtime::ownsType(const Type* type) const {
    return m_typeManager.ownsType(type);
}

bool Runtime::canRegisterNativeMember(const Type* classType, const NomadString& name, const MemberType type,
                                         const std::vector<Parameter>& parameters,
                                         NomadString& error) const {
    if (!ownsType(classType)) {
        error = "class type is not registered with this runtime";
        return false;
    }

    const auto* owner = classType->asCompositeType();
    if (owner == nullptr || (type != MemberType::Method && !classType->isClass())) {
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

bool Runtime::registerMemberTemplate(
    const NomadString& family,
    const NomadString& name,
    const MemberType type,
    const std::vector<TemplateParameter>& parameters
) {
    const auto argumentCount = m_typeManager.getParameterizedTypeArgumentCount(family);
    if (!argumentCount) {
        log::error("Cannot register member template '" + name + "': a registered parameterized type is required");

        return false;
    }

    if (m_typeManager.getMemberTemplateSignature(family, name).has_value()) {
        log::error("Cannot register member template '" + name + "': duplicate definition for '" + family + "'");

        return false;
    }

    std::vector<Parameter> parserParameters;
    for (const auto& parameter : parameters) {
        const Type* parameterType = nullptr;
        if (const auto* index = std::get_if<TypeParameterIndex>(&parameter.type)) {
            if (index->index >= *argumentCount) {
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

    std::vector<NomadString> families;
    m_typeManager.getParameterizedTypeNames(families);
    const auto familyIndex = static_cast<NomadIndex>(std::ranges::find(families, family) - families.begin());
    if (!m_typeManager.registerMemberTemplate(family, name, {type, std::move(parserParameters)})) {
        return false;
    }

    const auto [entry, inserted] = m_memberTemplateSignatures.try_emplace(
        name, MemberTemplateSignatureRegistration{familyIndex, family}
    );
    if (!inserted && familyIndex < entry->second.familyIndex) {
        entry->second = {familyIndex, family};
    }

    return true;
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

std::optional<MemberSignature> Runtime::getMemberSignature(const NomadString& name) const {
    const auto templateSignature = m_memberTemplateSignatures.find(name);
    if (templateSignature != m_memberTemplateSignatures.end()) {
        const auto signature = m_typeManager.getMemberTemplateSignature(templateSignature->second.familyName, name);
        if (!signature) {
            throw NomadBug("Invalid member template signature registration");
        }

        return signature;
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

const NativeIndexerInfo* Runtime::getNativeIndexerInfo(const IndexerId indexerId) const {
    return isIdOutOfRange(indexerId.id, m_nativeIndexers.size()) ?
        nullptr : m_nativeIndexers[toNomadIndex(indexerId.id)];
}

const std::vector<const NativeIndexerInfo*>& Runtime::getNativeIndexers() const {
    return m_nativeIndexers;
}

void Runtime::setPropertyHasExplicitInitializer(
    const PropertyId id,
    const bool hasExplicitInitializer
) {
    const auto* property = getProperty(id);
    if (property == nullptr || property->asNativeProperty() != nullptr) {
        throw NomadBug("Explicit initializer metadata requires a stored property");
    }
    m_typeManager.getMutableProperty(id)->setHasExplicitInitializer(hasExplicitInitializer);
}

const Property* Runtime::getProperty(const PropertyId id) const {
    return m_typeManager.getProperty(id);
}

const NativeProperty* Runtime::getNativeProperty(const PropertyId id) const {
    const auto* property = getProperty(id);
    return property == nullptr ? nullptr : property->asNativeProperty();
}

NativeProperty* Runtime::getMutableNativeProperty(const PropertyId id) {
    if (getNativeProperty(id) == nullptr) {
        return nullptr;
    }
    return static_cast<NativeProperty*>(m_typeManager.getMutableProperty(id));
}

void Runtime::validateNativePropertyTypes() {
    for (const auto* property : m_typeManager.getProperties()) {
        const auto* native = property->asNativeProperty();
        if (native != nullptr && !native->isTypeResolved()) {
            getMutableNativeProperty(property->getId())->completeTypeResolution();
        }
    }
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

void Runtime::catalogNativeMethod(const CallableId id) {
    const auto* method = getNativeMethod(id);
    const auto* receiverType = method != nullptr ? method->getClassType() : nullptr;
    const auto* owner = receiverType != nullptr ? receiverType->asCompositeType() : nullptr;
    if (method == nullptr || !ownsType(method->getClassType()) ||
        owner == nullptr ||
        !std::ranges::any_of(owner->getMethods(),
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
            const auto* property = getNativeProperty(std::get<PropertyId>(memberId));
            if (property != nullptr && property->getClassType() == classType) {
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
            const auto* property = getNativeProperty(std::get<PropertyId>(memberId));
            if (property != nullptr && property->getClassType() == classType) {
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
                                     std::vector<Operand> operands, const StackEffect stackEffect) {
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
            doc,
            stackEffect
        }
    );

    return id;
}

const Type* Runtime::getVoidType() const {
    return getTypeByName(VOID_TYPE_NAME);
}

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
    if (isIdOutOfRange(id.id, m_opCodes.size())) {
        return nullptr;
    }

    return m_opCodes[toNomadIndex(id.id)].fn;
}

const NomadString& Runtime::getInstructionName(const OpCodeId id) const {
    if (isIdOutOfRange(id.id, m_opCodes.size())) {
        throw NomadException("Instruction ID out of range");
    }

    return m_opCodes[toNomadIndex(id.id)].name;
}

const std::vector<Operand>& Runtime::getInstructionOperands(const OpCodeId id) const {
    if (isIdOutOfRange(id.id, m_opCodes.size())) {
        throw NomadException("Instruction ID out of range");
    }

    return m_opCodes[toNomadIndex(id.id)].operands;
}

const StackEffect& Runtime::getInstructionStackEffect(const OpCodeId id) const {
    if (isIdOutOfRange(id.id, m_opCodes.size())) {
        throw NomadException("Instruction ID out of range");
    }

    return m_opCodes[toNomadIndex(id.id)].stackEffect;
}

CallableId Runtime::getNativeFunctionId(const NomadString& name) const {
    const auto* function = getNativeFunction(getCallableId(name));
    return function == nullptr ? CallableId::INVALID : function->getId();
}

const NativeFunction* Runtime::getNativeFunction(const NomadString& name) const {
    const auto id = getNativeFunctionId(name);

    return id == CallableId::INVALID ? nullptr : getNativeFunction(id);
}

std::vector<const NativeFunction*> Runtime::getNativeFunctions() const {
    std::vector<const NativeFunction*> nativeFunctions;

    for (auto id = getFirstNativeFunctionId(); id != CallableId::INVALID; id = getNextNativeFunctionId(id)) {
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

KeywordId Runtime::registerKeyword(const NomadString& keyword, const NomadString& doc) {
    const auto existingId = getKeywordId(keyword);
    if (existingId.isValid()) {
        return existingId;
    }

    const auto id = KeywordId{toNomadId(m_keywords.size())};

    KeywordDefinition definition;

    definition.keyword = keyword;
    definition.doc = doc;

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
    if (!ownsType(type) || type->isVoid() || type->isInternal() || type->isStruct()) {
        log::error("Cannot register constant '" + name + "': invalid value type");
        return ConstantId::INVALID;
    }
    if (!canRegisterHubMember(name)) {
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
    if (isIdOutOfRange(id.id, m_constants.size())) {
        throw NomadException("Constant ID out of range");
    }

    getUncheckedConstantValue(id, value);
}

ConstantId Runtime::getConstantId(const NomadString& name) const {
    return ConstantId{m_constantsMap.getVariableId(name).id};
}

const NomadString& Runtime::getConstantName(const ConstantId id) const {
    if (isIdOutOfRange(id.id, m_constants.size())) {
        throw NomadException("Constant ID out of range");
    }

    return m_constantsMap.getVariableName(VariableId{id.id});
}

const Type* Runtime::getConstantType(const ConstantId id) const {
    if (isIdOutOfRange(id.id, m_constants.size())) {
        return nullptr;
    }

    return getUncheckedConstantType(id);
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
    if (!canRegisterHubMember(name)) {
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
    if (registerType(std::make_unique<EventType>(this, m_typeManager.reserveTypeId(), &m_events.back())).isInvalid()) {
        m_events.pop_back();
        log::error("Cannot register event '" + name + "': event type registration failed");

        return EventId::INVALID;
    }
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
    if (isIdOutOfRange(stringId.id, m_strings.size())) {
        return nullptr;
    }

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

FormatString* Runtime::getFormatString(const FormatStringId id) {
    const auto formatStringIndex = toNomadIndex(id.id);

    if (formatStringIndex >= m_formatStrings.size()) {
        return nullptr;
    }

    return m_formatStrings[formatStringIndex].formatString.get();
}

const FormatString* Runtime::getFormatString(const FormatStringId id) const {
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
    const NomadString& doc
) {
    if (!canRegisterHubMember(name)) {
        log::error("Cannot register dynamic variable '" + name + "': name is reserved by a hub declaration");
        return DynamicVariableId::INVALID;
    }
    const auto id = DynamicVariableId{toNomadId(m_dynamicVariables.size())};

    DynamicVariableRegistration definition;
    definition.name = name;
    definition.type = type;
    definition.setFn = std::move(setFn);
    definition.getFn = std::move(getFn);
    definition.doc = doc;

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
    if (isIdOutOfRange(id.id, m_dynamicVariables.size())) {
        throw NomadException("Dynamic variable ID out of range");
    }

    const auto dynamicVariableIndex = toNomadIndex(id.id);

    return m_dynamicVariables[dynamicVariableIndex].name;
}

const Type* Runtime::getDynamicVariableType(const DynamicVariableId id) const {
    if (isIdOutOfRange(id.id, m_dynamicVariables.size())) {
        return nullptr;
    }

    const auto dynamicVariableIndex = toNomadIndex(id.id);

    return m_dynamicVariables[dynamicVariableIndex].type;
}

bool Runtime::canSetDynamicVariable(const DynamicVariableId id) const {
    if (isIdOutOfRange(id.id, m_dynamicVariables.size())) {
        return false;
    }

    const auto dynamicVariableIndex = toNomadIndex(id.id);

    return m_dynamicVariables[dynamicVariableIndex].setFn != nullptr;
}

bool Runtime::canGetDynamicVariable(const DynamicVariableId id) const {
    if (isIdOutOfRange(id.id, m_dynamicVariables.size())) {
        return false;
    }

    const auto dynamicVariableIndex = toNomadIndex(id.id);

    return m_dynamicVariables[dynamicVariableIndex].getFn != nullptr;
}

void Runtime::setDynamicVariable(VirtualMachine* vm, const DynamicVariableId id, const RuntimeValue& value) {
    if (isIdOutOfRange(id.id, m_dynamicVariables.size())) {
        throw NomadException("Dynamic variable ID out of range");
    }

    const auto dynamicVariableIndex = toNomadIndex(id.id);
    if (!m_dynamicVariables[dynamicVariableIndex].setFn) {
        throw NomadException(
            "Dynamic variable '" + m_dynamicVariables[dynamicVariableIndex].name + "' is read-only"
        );
    }

    setUncheckedDynamicVariable(vm, id, value);
}

void Runtime::getDynamicVariableValue(VirtualMachine* vm, const DynamicVariableId id, RuntimeValue& value) const {
    if (isIdOutOfRange(id.id, m_dynamicVariables.size())) {
        throw NomadException("Dynamic variable ID out of range");
    }

    const auto dynamicVariableIndex = toNomadIndex(id.id);
    if (!m_dynamicVariables[dynamicVariableIndex].getFn) {
        throw NomadException(
            "Dynamic variable '" + m_dynamicVariables[dynamicVariableIndex].name + "' cannot be read"
        );
    }

    getUncheckedDynamicVariableValue(vm, id, value);
}

void Runtime::setStringDynamicVariable(VirtualMachine* vm, const DynamicVariableId id, const NomadString& value) {
    RuntimeValue stringValue;
    const RuntimeValueGuard stringValueOwner(*getStringType(), &stringValue);
    stringValue.setStringValue(value);

    setDynamicVariable(vm, id, stringValue);
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
    if (isIdOutOfRange(id.id, m_variables.size())) {
        throw NomadException("Variable context ID out of range");
    }

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

        return NOMAD_EMPTY_STRING;
    }

    return getVariableContext(contextVariable->contextId)->getVariableName(contextVariable->localVariableId);
}

const Type* Runtime::getContextVariableType(const ContextVariableId contextVariableId) const {
    const auto* contextVariable = getContextVariable(contextVariableId);

    if (contextVariable == nullptr) {

        return nullptr;
    }

    return getVariableContext(contextVariable->contextId)->getVariableType(contextVariable->localVariableId);
}

void Runtime::setContextVariableWritten(const ContextVariableId contextVariableId, const bool written) const {
    const auto* contextVariable = getContextVariable(contextVariableId);

    if (contextVariable == nullptr) {

        return;
    }

    getVariableContext(contextVariable->contextId)->setWritten(contextVariable->localVariableId, written);
}

void Runtime::setContextVariableRead(const ContextVariableId contextVariableId, const bool read) const {
    const auto* contextVariable = getContextVariable(contextVariableId);

    if (contextVariable == nullptr) {

        return;
    }

    getVariableContext(contextVariable->contextId)->setRead(contextVariable->localVariableId, read);
}

void Runtime::setContextVariableValue(const ContextVariableId contextVariableId, const RuntimeValue& value) const {
    const auto* contextVariable = getContextVariable(contextVariableId);

    if (contextVariable == nullptr) {

        return;
    }

    getVariableContext(contextVariable->contextId)->setValue(contextVariable->localVariableId, value);
}

void Runtime::getContextVariableValue(const ContextVariableId contextVariableId, RuntimeValue& value) const {
    const auto* contextVariable = getContextVariable(contextVariableId);

    if (contextVariable == nullptr) {

        return;
    }

    getVariableContext(contextVariable->contextId)->getValue(contextVariable->localVariableId, value);
}

void Runtime::setStringContextVariableValue(const ContextVariableId contextVariableId, const NomadString& value) const {
    RuntimeValue stringValue;
    const RuntimeValueGuard stringValueOwner(*getStringType(), &stringValue);
    stringValue.setStringValue(value);

    setContextVariableValue(contextVariableId, stringValue);
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
        log::error("Invalid context variable id: " + std::to_string(contextVariableId.id));

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
    if (!canRegisterHubMember(name, true)) {
        log::error("Function registration failed for '" + name + "' due to hub name conflict.");
        return CallableId::INVALID;
    }

    if (getCallableId(name) != CallableId::INVALID) {
        log::error("Function with name '" + name + "' conflicts with an already registered callable.");

        return CallableId::INVALID;
    }

    const auto id = addCallableToTable(
        std::make_unique<ScriptFunction>(getNextCallableId(), name, path, source)
    );

    if (id.isValid()) {
        registerHubMember(name, HubFunctions{{id}});
    }

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

CallableId Runtime::getFunctionId(const NomadString& name) const {
    const auto* function = getScriptFunction(getCallableId(name));
    return function == nullptr ? CallableId::INVALID : function->getId();
}

std::optional<NomadString> Runtime::getFunctionName(CallableId functionId) const {
    const auto* function = getScriptFunction(functionId);

    if (function == nullptr) {
        return std::nullopt;
    }

    return function->getName();
}

NomadIndex Runtime::getFunctionCount() const {
    return static_cast<NomadIndex>(std::count_if(m_callables.begin(), m_callables.end(), [](const auto* callable) {
        return callable->getType() == CallableType::Function && callable->getSource() == CallableSource::Script;
    }));
}

void Runtime::getFunctions(std::vector<ScriptFunction*>& functions) {
    functions.clear();
    functions.reserve(getFunctionCount());

    for (auto* callable : m_callables) {
        if (callable->getType() == CallableType::Function && callable->getSource() == CallableSource::Script) {
            functions.push_back(static_cast<ScriptFunction*>(callable));
        }
    }
}

void Runtime::getFunctions(std::vector<const ScriptFunction*>& functions) const {
    functions.clear();
    functions.reserve(getFunctionCount());

    for (const auto* callable : m_callables) {
        if (callable->getType() == CallableType::Function && callable->getSource() == CallableSource::Script) {
            functions.push_back(static_cast<const ScriptFunction*>(callable));
        }
    }
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

} // namespace nomad
