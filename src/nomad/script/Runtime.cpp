// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Runtime.hpp>

#include <nomad/compiler/Compiler.hpp>

#include <nomad/script/Closure.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/NativeMembers.hpp>
#include <nomad/script/NativeProperty.hpp>
#include <nomad/script/NomadStringObject.hpp>
#include <nomad/script/RegisterRangeType.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/String.hpp>

#include <boost/json/array.hpp>

#include <algorithm>
#include <cctype>
#include <utility>

#include <iomanip>

namespace nomad {

const ObjectType* Runtime::getIteratorType(const Type* valueType) {
    if (!ownsType(valueType) || valueType->isVoid() || valueType->isInternal()) {
        throw NomadException("Iterator value type must be a registered non-void type");
    }
    const auto* type = getParameterizedType("Iterator", {valueType});
    if (type == nullptr) {
        throw NomadException("Failed to specialize iterator type");
    }
    return static_cast<const ObjectType*>(type);
}

void Runtime::registerIterableType(
    const Type* sourceType,
    const Type* valueType,
    const Method* factory
) {
    const auto* iteratorType = getIteratorType(valueType);
    if (!ownsType(sourceType) ||
        factory == nullptr || getMethod(factory->getId()) != factory ||
        factory->getObjectType() != sourceType || factory->getParameterCount() != 0 ||
        factory->getReturnType() != iteratorType || m_iterableTypes.contains(sourceType)) {
        throw NomadException("Invalid iterable registration");
    }
    m_iterableTypes.emplace(sourceType, IterableDefinition{valueType, iteratorType, factory->getId()});
}

std::optional<IterableDefinition> Runtime::getIterableDefinition(const Type* sourceType) const {
    const auto existing = m_iterableTypes.find(sourceType);
    return existing == m_iterableTypes.end() ? std::nullopt : std::optional{existing->second};
}

namespace {

bool isNativeMemberName(const NomadString& name) {
    if (name.empty()) {
        return false;
    }

    const auto firstCharacter = static_cast<unsigned char>(name.front());
    if (!std::isalpha(firstCharacter) && firstCharacter != '_') {
        return false;
    }

    return std::ranges::all_of(name.begin() + 1, name.end(), [](const NomadChar character) {
        const auto unsignedCharacter = static_cast<unsigned char>(character);
        return std::isalnum(unsignedCharacter) || character == '_';
    });
}

} // namespace

Runtime::Runtime() {
    // Types
    registerType(std::make_unique<VoidType>());
    registerType(std::make_unique<IdType>());
    registerType(std::make_unique<FloatType>());
    registerType(std::make_unique<IntegerType>());
    registerType(std::make_unique<BooleanType>());
    registerType(std::make_unique<StringType>());
    registerType(std::make_unique<FileNameType>());
    registerType(std::make_unique<FunctionNameType>());
    registerType(std::make_unique<FunctionLineNumberType>());
    registerType(std::make_unique<EventCallbackType>());
    registerType(std::make_unique<EventDispatchType>());
    if (!registerParameterizedType(
        "Iterator", 1,
        [](const std::vector<const Type*>& arguments) {
            return std::make_unique<ObjectType>("Iterator<" + arguments[0]->getTypeName() + ">", arguments);
        },
        [this](const Type* type) {
            const auto* iteratorType = static_cast<const ObjectType*>(type);
            const auto* valueType = iteratorType->getTypeArguments()[0];
            const auto advanceId = registerNativeMethod(
                iteratorType, "moveNext",
                [](VirtualMachine* machine) {
                    machine->setBooleanResult(machine->getThisAs<IteratorObject>()->moveNext());
                },
                {}, getBooleanType(), NomadDoc("Advances the iterator. Returns false at the end.")
            );
            const auto currentId = registerNativeProperty(
                iteratorType, "current", valueType,
                [valueType](VirtualMachine* machine, RuntimeValue& result) {
                    RuntimeValue owned;
                    valueType->assignValue(machine->getThisAs<IteratorObject>()->getCurrent(), owned);
                    result = owned;
                },
                {}, NomadDoc("The value published by the last successful moveNext call.")
            );
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

//    register_keyword("stringId" NomadDoc("Load the string id of the given string"));
    registerKeyword("fun", NomadDoc("Function declaration"));
    registerKeyword("hub", NomadDoc("Groups declarations under an open, shared name prefix"));
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
        if (const auto* type = m_constantsMap.getVariableType(toNomadId(constantIndex))) {
            type->freeValue(m_constants[constantIndex]);
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
    if (isHub(name) || !canRegisterHubMember(name, true)) {
        log::error("Cannot register function '" + name + "': name is reserved by a hub");
        return CallableId::INVALID;
    }
    return registerNativeCallable(nullptr, name, std::move(nativeFunction_fn), parameters, returnType, doc);
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
    return member != nullptr && std::holds_alternative<HubVariable>(*member);
}

NomadId Runtime::registerHubVariable(const NomadString& name) {
    if (m_scriptInitialization.state != ScriptInitializationState::Pending ||
        getContainingHub(name).empty() || !canRegisterHubMember(name) ||
        isHub(name) || isHubVariable(name) ||
        getCallableId(name).isValid() || getConstantId(name) != NOMAD_INVALID_ID ||
        getTypeByName(name) != nullptr || getEventId(name) != NOMAD_INVALID_ID ||
        getDynamicVariableId(name) != NOMAD_INVALID_ID ||
        getVariableContextIdByPrefix(name) != NOMAD_INVALID_ID) {
        log::error("Cannot register hub variable '" + name + "': missing hub or conflicting declaration");
        return NOMAD_INVALID_ID;
    }
    auto& hub = *m_hubs[toNomadIndex(getHubId(getContainingHub(name)).id)];
    if (hub.m_variableContextId == NOMAD_INVALID_ID) {
        hub.m_variableContextId = toNomadId(m_variables.size());
        m_variables.push_back({
            hub.m_variableContextId, hub.getName(), "$hub." + hub.getName() + ".", hub.m_variables
        });
    }
    const auto id = registerContextVariable(hub.m_variableContextId, name, nullptr);
    if (id != NOMAD_INVALID_ID) {
        registerHubMember(name, HubVariable{hub.m_variables->getVariableId(name)});
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
        add(hub.getName(), HubChild{HubId{toNomadId(index)}});
        for (auto id = hub.m_variables->getFirstVariableId(); id != NOMAD_INVALID_ID;
             id = hub.m_variables->getNextVariableId(id)) {
            add(hub.m_variables->getVariableName(id), HubVariable{id});
        }
    }
    for (const auto* callable : m_callables) {
        if (callable->getType() == CallableType::Function) {
            add(callable->getName(), HubFunctions{{callable->getId()}});
        }
    }
    for (auto id = getFirstConstantId(); id != NOMAD_INVALID_ID; id = getNextConstantId(id)) {
        add(getConstantName(id), HubConstant{id});
    }
    for (const auto& type : m_types) {
        if (getEventId(type->getTypeName()) == NOMAD_INVALID_ID) {
            add(type->getTypeName(), HubType{type.get()});
        }
    }
    for (const auto& event : m_events) {
        add(event.name, HubEvent{event.id});
    }
    for (auto index = NomadIndex{0}; index < m_dynamicVariables.size(); ++index) {
        add(m_dynamicVariables[index].name, HubDynamicVariable{toNomadId(index)});
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

bool Runtime::registerHub(const NomadString& name) {
    if (name.empty()) {
        log::error("Cannot register a hub with an empty name");
        return false;
    }
    std::vector<NomadString> prefixes;
    NomadIndex start = 0;
    while (start < name.size()) {
        const auto end = name.find('.', start);
        const auto length = (end == NomadString::npos ? name.size() : end) - start;
        const auto segment = name.substr(start, length);
        if (segment.empty() || !((segment[0] >= 'A' && segment[0] <= 'Z') ||
            (segment[0] >= 'a' && segment[0] <= 'z') || segment[0] == '_') ||
            segment.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") !=
                NomadString::npos || getKeywordId(segment).isValid()) {
            log::error("Invalid hub name '" + name + "'");
            return false;
        }
        auto prefix = name.substr(0, start + length);
        if (!isHub(prefix) && (getCallableId(prefix).isValid() ||
            getConstantId(prefix) != NOMAD_INVALID_ID || getTypeByName(prefix) != nullptr ||
            getEventId(prefix) != NOMAD_INVALID_ID || getDynamicVariableId(prefix) != NOMAD_INVALID_ID ||
            isHubVariable(prefix) ||
            getVariableContextIdByPrefix(prefix + ".") != NOMAD_INVALID_ID ||
            getParameterizedTypeArgumentCount(prefix).has_value() ||
            getParameterizedConstructorArgumentCount(prefix).has_value())) {
            log::error("Cannot register hub '" + name + "': identifier '" + prefix + "' is already used");
            return false;
        }
        prefixes.push_back(std::move(prefix));
        if (end == NomadString::npos) {
            break;
        }
        start = end + 1;
        if (start == name.size()) {
            log::error("Invalid hub name '" + name + "'");
            return false;
        }
    }
    const auto previousCount = m_hubs.size();
    for (const auto& prefix : prefixes) {
        if (!isHub(prefix)) {
            m_hubs.push_back(std::make_unique<Hub>(prefix, getHubId(getContainingHub(prefix))));
        }
    }
    if (previousCount != m_hubs.size()) {
        try {
            rebuildHubMembers();
        } catch (const NomadException& exception) {
            m_hubs.resize(previousCount);
            log::error("Cannot register hub '" + name + "': " + exception.what());
            return false;
        }
    }
    return true;
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
    const ObjectType* objectType,
    const NomadString& name,
    NativeFunctionFn fn,
    const std::vector<Parameter>& parameters,
    const Type* returnType,
    const NomadString& doc
) {
    if (objectType != nullptr) {
        if (!fn || !ownsType(returnType) || (returnType->isInternal() && !returnType->isVoid())) {
            log::error("Cannot register native method '" + name + "': invalid callback or return type");
            return CallableId::INVALID;
        }
        for (const auto& parameter : parameters) {
            if (parameter.name.empty() || !ownsType(parameter.type) || parameter.type->isVoid()) {
                log::error("Cannot register native method '" + name + "': invalid parameter definition");
                return CallableId::INVALID;
            }
        }
        NomadString error;
        if (!canRegisterNativeMember(objectType, name, MemberType::Method, parameters, error)) {
            log::error("Cannot register native method '" + name + "': " + error);
            return CallableId::INVALID;
        }
    }
    if (objectType == nullptr) {
        if (getCallableOverloadCount(name) != 0) {
            if (NomadString error; !canOverloadCallable(name, parameters, returnType, error)) {
                log::error("Cannot register native function '" + name + "': " + error);

                return CallableId::INVALID;
            }
        }
    }

    CallableId id;

    if (objectType != nullptr) {
        auto* mutableType = getMutableObjectType(objectType);
        if (mutableType == nullptr) {
            log::error("Cannot register native method '" + name + "': object type is not registered");
            return CallableId::INVALID;
        }
        id = mutableType->registerMethod(
            *this, std::make_unique<NativeMethod>(name, objectType, std::move(fn), parameters, returnType, doc)
        );
        if (id == CallableId{}) {
            return CallableId::INVALID;
        }

        m_nativeMembers.push_back(id);
        m_nativeMembersByName[name].push_back(id);

        return id;
    }
    id = addCallableToTable(std::make_unique<NativeFunction>(name, std::move(fn), parameters, returnType, doc));

    registerHubMember(name, HubFunctions{{id}});

    return id;
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
    const Type* objectType,
    const NomadString& name,
    const std::vector<const Type*>& argumentTypes
) const {
    std::vector<CallableId> candidates;

    for (const auto* callable : m_callables) {
        if (callable->getName() == name &&
            callable->getType() == CallableType::Method &&
            callable->getObjectType() == objectType) {
            candidates.push_back(callable->getId());
        }
    }

    return resolveAmong(std::move(candidates), argumentTypes);
}

ResolveResult Runtime::resolveConstructor(
    const Type* objectType,
    const std::vector<const Type*>& argumentTypes
) const {
    std::vector<CallableId> candidates;

    for (const auto* callable : m_callables) {
        if (callable->getType() == CallableType::Constructor && callable->getObjectType() == objectType) {
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
    NomadString& error
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
        if (callable->getName() != name || callable->getType() != CallableType::Function) {
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

void Runtime::registerType(std::unique_ptr<Type>&& type) {
    if (isHub(type->getTypeName()) || isHubVariable(type->getTypeName())) {
        throw NomadException("Type name '" + type->getTypeName() + "' is reserved by a hub");
    }
    if (getEventId(type->getTypeName()) == NOMAD_INVALID_ID) {
        if (!canRegisterHubMember(type->getTypeName())) {
            throw NomadException("Type name '" + type->getTypeName() + "' is already a hub member");
        }
        registerHubMember(type->getTypeName(), HubType{type.get()});
    }
    m_types.push_back(std::move(type));

    // return toNomadId(m_types.size() - 1);
}

TypeId Runtime::getTypeId(const NomadString& typeName) const {
    for (NomadIndex i = 0; i < m_types.size(); ++i) {
        if (m_types[i]->getTypeName() == typeName) {
            return TypeId{toNomadId(i)};
        }
    }

    return TypeId::INVALID;
}

std::optional<const Type*> Runtime::getType(const TypeId id) const {
    if (isIdOutOfRange(id.id, m_types.size())) {
        return std::nullopt;
    }

    return m_types[toNomadIndex(id.id)].get();
}

const Type* Runtime::getTypeByName(const NomadString& name) const {
    for (const auto& type: m_types) {
        if (type->getTypeName() == name) {
            return type.get();
        }
    }

    return nullptr;
}

void Runtime::getTypes(std::vector<const Type*>& types) const {
    types.reserve(types.size() + m_types.size());

    for (const auto& type : m_types) {
        types.push_back(type.get());
    }
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

    auto opaqueType = std::make_unique<OpaqueIdType>(name, doc);
    const auto* registeredType = opaqueType.get();
    registerType(std::move(opaqueType));
    return registeredType;
}

const EnumType* Runtime::registerEnum(
    const NomadString& name,
    const std::vector<EnumValueDefinition>& values
) {
    if (isHub(name) || isHubVariable(name)) {
        log::error("Cannot register enum '" + name + "': name is reserved by a hub declaration");
        return nullptr;
    }
    if (name.empty()) {
        log::error("Cannot register an enum with an empty name");
        return nullptr;
    }

    if (getTypeByName(name) != nullptr) {
        log::error("Type with name '" + name + "' already registered");
        return nullptr;
    }

    if (values.empty()) {
        log::error("Cannot register enum '" + name + "' without values");
        return nullptr;
    }

    for (NomadIndex index = 0; index < values.size(); ++index) {
        const auto& definition = values[index];
        const auto constantName = name + "." + definition.name;

        if (definition.name.empty()) {
            log::error("Cannot register an unnamed value in enum '" + name + "'");
            return nullptr;
        }

        if (getConstantId(constantName) != NOMAD_INVALID_ID) {
            log::error("Constant with name '" + constantName + "' already registered");
            return nullptr;
        }

        for (NomadIndex previousIndex = 0; previousIndex < index; ++previousIndex) {
            const auto& previousDefinition = values[previousIndex];

            if (previousDefinition.name == definition.name) {
                log::error("Enum '" + name + "' has duplicate value name '" + definition.name + "'");
                return nullptr;
            }

            if (previousDefinition.value == definition.value) {
                log::error(
                    "Enum '" + name + "' assigns value " + std::to_string(definition.value) + " more than once"
                );
                return nullptr;
            }
        }
    }

    auto enumType = std::make_unique<EnumType>(name, values);
    const auto* registeredType = enumType.get();
    registerType(std::move(enumType));

    for (const auto& definition: values) {
        registerConstant(
            name + "." + definition.name,
            RuntimeValue(definition.value),
            registeredType
        );
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

    return registeredType;
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
    auto type = factory(arguments);
    if (type == nullptr || type->getTypeName().empty() || getTypeByName(type->getTypeName()) != nullptr) {
        log::error("Factory for '" + name + "' returned an invalid or duplicate type");
        return nullptr;
    }
    const auto* result = type.get();
    registerType(std::move(type));
    if (initializer) {
        initializer(result);
    }
    m_typeSpecializations.push_back({name, arguments, result});
    return result;
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

bool Runtime::ownsType(const Type* type) const {
    return type != nullptr &&
           std::ranges::any_of(m_types, [type](const auto& registeredType) { return registeredType.get() == type; });
}

ObjectType* Runtime::getMutableObjectType(const ObjectType* type) {
    for (const auto& registeredType : m_types) {
        if (registeredType.get() == type) {
            return static_cast<ObjectType*>(registeredType.get());
        }
    }
    return nullptr;
}

bool Runtime::canRegisterNativeMember(const Type* objectType, const NomadString& name, const MemberType type,
                                         const std::vector<Parameter>& parameters,
                                         NomadString& error) const {
    if (!ownsType(objectType)) {
        error = "object type is not registered with this runtime";
        return false;
    }

    if (dynamic_cast<const ObjectType*>(objectType) == nullptr) {
        error = "only object types can have native members";
        return false;
    }

    const auto* owner = static_cast<const ObjectType*>(objectType);
    if (owner->getProperty(name) != nullptr ||
        getMemberType(getNativeMemberId(objectType, name)) != MemberType::Unknown) {
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
    if (!isNativeMemberName(name)) {
        error = "member name must be a simple identifier";
        return false;
    }
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

CallableId Runtime::registerNativeConstructor(
    const ObjectType* objectType,
    const std::vector<Parameter>& parameters,
    ConstructorFactory factory,
    const NomadString& doc
) {
    return registerNativeConstructor(objectType, parameters, objectType, std::move(factory), doc);
}

CallableId Runtime::registerNativeConstructor(
    const ObjectType* objectType,
    const std::vector<Parameter>& parameters,
    const ObjectType* createdType,
    ConstructorFactory factory,
    const NomadString& doc
) {
    auto* ownedType = getMutableObjectType(objectType);
    if (ownedType == nullptr || !ownsType(createdType) || createdType->isInternal() || !factory) {
        log::error("Cannot register constructor: a registered class, registered public object type, and factory are required");
        return CallableId::INVALID;
    }
    const auto name = objectType->getTypeName();
    if (name.empty() || getParameterizedConstructorArgumentCount(name) || getParameterizedTypeArgumentCount(name)) {
        log::error("Cannot register constructor '" + name + "': the name is reserved");
        return CallableId::INVALID;
    }
    for (const auto& parameter : parameters) {
        if (!ownsType(parameter.type) || parameter.type->isVoid()) {
            log::error("Cannot register constructor: parameter types must be registered and non-void");
            return CallableId::INVALID;
        }
    }
    for (const auto* existing : getConstructors(objectType)) {
        if (existing->getParameterCount() != parameters.size()) {
            continue;
        }
        const auto collides = std::ranges::equal(
            existing->getParameters(), parameters,
            [](const auto& left, const auto& right) { return left.type->sameType(right.type); }
        );
        if (collides) {
            log::error("Cannot register constructor '" + name + "': duplicate signature");
            return CallableId::INVALID;
        }
    }
    return ownedType->registerConstructor(
        *this,
        std::make_unique<NativeConstructor>(name, objectType, createdType, std::move(factory), parameters, doc)
    );
}

std::vector<const NativeConstructor*> Runtime::getConstructors() const {
    std::vector<const NativeConstructor*> constructors;
    for (const auto* callable : m_callables) {
        if (callable->getType() == CallableType::Constructor) {
            constructors.push_back(static_cast<const NativeConstructor*>(callable));
        }
    }
    return constructors;
}

std::vector<const NativeConstructor*> Runtime::getConstructors(const Type* objectType) const {
    std::vector<const NativeConstructor*> constructors;
    for (const auto* constructor : getConstructors()) {
        if (constructor->getObjectType() == objectType) {
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

IndexerId Runtime::registerNativeIndexer(
    const ObjectType* objectType,
    std::vector<const Type*> indexTypes,
    const Type* valueType,
    NativeFunctionFn getFn,
    NativeFunctionFn setFn,
    const NomadString& doc
) {
    if (!ownsType(objectType) || !ownsType(valueType) || valueType->isVoid() || valueType->isInternal()) {
        log::error("Cannot register native indexer: types must be registered, public, and non-void");
        return IndexerId::INVALID;
    }
    if (indexTypes.empty()) {
        log::error("Cannot register native indexer: at least one index type is required");
        return IndexerId::INVALID;
    }
    for (const auto* indexType : indexTypes) {
        if (!ownsType(indexType) || indexType->isVoid() || indexType->isInternal()) {
            log::error("Cannot register native indexer: index types must be registered, public, and non-void");
            return IndexerId::INVALID;
        }
    }
    if (!getFn && !setFn) {
        log::error("Cannot register native indexer: indexer has no accessor or mutator");
        return IndexerId::INVALID;
    }
    if (getNativeIndexerId(objectType, indexTypes.size()).isValid()) {
        log::error("Cannot register native indexer: type already has an indexer with " + toString(indexTypes.size()) + " indices");
        return IndexerId::INVALID;
    }
    const auto id = IndexerId{toNomadId(m_nativeIndexers.size())};
    m_nativeIndexers.push_back({id, objectType, std::move(indexTypes), valueType, std::move(getFn), std::move(setFn), doc});
    return id;
}

IndexerId Runtime::getNativeIndexerId(const Type* objectType, const NomadIndex indexCount) const {
    for (const auto& definition : m_nativeIndexers) {
        if (definition.objectType == objectType && definition.indexTypes.size() == indexCount) {
            return definition.id;
        }
    }
    return IndexerId::INVALID;
}

bool Runtime::getNativeIndexerDefinition(const IndexerId indexerId, NativeIndexerDefinition& definition) const {
    if (isIdOutOfRange(indexerId.id, m_nativeIndexers.size())) {
        return false;
    }
    definition = m_nativeIndexers[toNomadIndex(indexerId.id)];
    return true;
}

const std::vector<NativeIndexerDefinition>& Runtime::getNativeIndexers() const {
    return m_nativeIndexers;
}

PropertyId Runtime::registerProperty(
    const ObjectType* objectType,
    const NomadString& name,
    const Type* valueType
) {
    auto* owner = getMutableObjectType(objectType);
    if (owner == nullptr || !isNativeMemberName(name) || !ownsType(valueType) ||
        valueType->isVoid() || valueType->isInternal()) {
        log::error("Cannot register property '" + name + "': invalid owner, name, or value type");
        return PropertyId::INVALID;
    }
    if (const auto* existing = owner->getProperty(name)) {
        if (existing->asNativeProperty() == nullptr && existing->getValueType() == valueType) {
            return existing->getId();
        }
        log::error("Cannot register property '" + name + "': conflicting definition");
        return PropertyId::INVALID;
    }
    if (getMemberType(getNativeMemberId(objectType, name)) != MemberType::Unknown) {
        log::error("Cannot register property '" + name + "': the type already has a member with that name");
        return PropertyId::INVALID;
    }
    return owner->registerProperty(*this, std::make_unique<Property>(objectType, name, valueType));
}

PropertyId Runtime::catalogProperty(Property* property) {
    if (property == nullptr || property->getId().isValid() || !ownsType(property->getObjectType()) ||
        property->getObjectType()->getProperty(property->getName()) != property ||
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

PropertyId Runtime::registerNativeProperty(const ObjectType* objectType, const NomadString& name, const Type* valueType,
                                           NativePropertyGetFn getFn, NativePropertySetFn setFn,
                                           const NomadString& doc) {
    if (!ownsType(valueType) || valueType->isVoid() || valueType->isInternal()) {
        log::error("Cannot register native property '" + name + "': invalid value type");
        return PropertyId::INVALID;
    }

    if (!getFn && !setFn) {
        log::error("Cannot register native property '" + name + "': property has no accessor or mutator");
        return PropertyId::INVALID;
    }

    NomadString error;
    if (!canRegisterNativeMember(objectType, name, MemberType::Property, {}, error)) {
        log::error("Cannot register native property '" + name + "': " + error);
        return PropertyId::INVALID;
    }

    auto* owner = getMutableObjectType(objectType);
    const auto id = owner->registerProperty(*this, std::make_unique<NativeProperty>(
        objectType, name, valueType, std::move(getFn), std::move(setFn), doc
    ));
    if (id.isInvalid()) {
        return PropertyId::INVALID;
    }
    m_nativeMembers.push_back(id);
    m_nativeMembersByName[name].push_back(id);

    return id;
}

CallableId Runtime::registerNativeMethod(
    const ObjectType* objectType,
    const NomadString& name,
    NativeFunctionFn fn,
    const std::vector<Parameter>& parameters,
    const Type* returnType,
    const NomadString& doc
) {
    if (objectType == nullptr) {
        log::error("Cannot register native method '" + name + "': object type is null");
        return CallableId::INVALID;
    }
    return registerNativeCallable(objectType, name, std::move(fn), parameters, returnType, doc);
}

MemberReference Runtime::getNativeMemberId(const Type* objectType, const NomadString& name) const {
    const auto overloads = m_nativeMembersByName.find(name);
    if (overloads == m_nativeMembersByName.end()) {
        return PropertyId::INVALID;
    }

    for (const auto memberId : overloads->second) {
        switch (getMemberType(memberId)) {
        case MemberType::Property: {
            NativePropertyDefinition definition;
            if (getNativePropertyDefinition(memberId, definition) && definition.objectType == objectType) {
                return memberId;
            }
            break;
        }

        case MemberType::Method: {
            const auto* method = getNativeMemberMethod(memberId);
            if (method != nullptr && method->getObjectType() == objectType) {
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

bool Runtime::getNativePropertyDefinition(const MemberReference& member, NativePropertyDefinition& definition) const {
    const auto* id = std::get_if<PropertyId>(&member);
    if (id == nullptr) {
        return false;
    }
    const auto* property = getNativeProperty(*id);
    if (property == nullptr) {
        return false;
    }
    definition = property->getDefinition();
    return true;
}

const NativeMethod* Runtime::getNativeMemberMethod(const MemberReference& member) const {
    const auto* id = std::get_if<CallableId>(&member);
    return id == nullptr ? nullptr : getNativeMethod(*id);
}

void Runtime::getNativeMembers(const Type* objectType, std::vector<MemberReference>& members) const {
    members.clear();

    for (const auto& memberId : m_nativeMembers) {
        const auto type = getMemberType(memberId);

        if (type == MemberType::Property) {
            NativePropertyDefinition definition;
            if (getNativePropertyDefinition(memberId, definition) && definition.objectType == objectType) {
                members.push_back(memberId);
            }
        } else if (type == MemberType::Method) {
            const auto* method = getNativeMemberMethod(memberId);
            if (method != nullptr && method->getObjectType() == objectType) {
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

NomadId Runtime::registerConstant(const NomadString& name, const RuntimeValue& value, const Type* type) {
    if (isHub(name) || !canRegisterHubMember(name)) {
        log::error("Cannot register constant '" + name + "': name is reserved by a hub");
        return NOMAD_INVALID_ID;
    }
    if (getConstantId(name) != NOMAD_INVALID_ID) {
        log::error("Constant with name '" + name + "' already registered.");

        return NOMAD_INVALID_ID;
    }

    auto constantId = m_constantsMap.registerVariable(name, type);
    const auto constantIndex = toNomadIndex(constantId);

    m_constants.resize(constantIndex + 1);
    type->copyValue(value, m_constants[constantIndex]);
    registerHubMember(name, HubConstant{constantId});

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
    if (isHub(name) || !canRegisterHubMember(name)) {
        log::error("Cannot register event '" + name + "': name is reserved by a hub");
        return NOMAD_INVALID_ID;
    }
    if (getEventId(name) != NOMAD_INVALID_ID) {
        log::error("Event with name '" + name + "' already registered.");

        return NOMAD_INVALID_ID;
    }

    NomadId id = toNomadId(m_events.size());

    m_events.emplace_back(id, name, parameterTypes);

    // Also register event type.
    registerType(std::make_unique<EventType>(&m_events.back()));
    registerHubMember(name, HubEvent{id});

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

    m_strings.push_back(NomadStringObject::create(string));

    return id;
}

NomadId Runtime::getStringId(const NomadString& string) const {
    for (NomadIndex i = 0; i < m_strings.size(); ++i) {
        if (m_strings[i]->getView() == string) {
            return toNomadId(i);
        }
    }

    return NOMAD_INVALID_ID;
}

NomadStringObject* Runtime::getString(const NomadId stringId) const {
    const auto stringIndex = toNomadIndex(stringId);
    return m_strings[stringIndex];
}

NomadStringObject* Runtime::getStringByName(const NomadString& name) const {
    const auto stringId = getStringId(name);

    return getString(stringId);
}

NomadId Runtime::registerFormatString(const NomadString& formatString, CallableId functionId) {
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

NomadId Runtime::getFormatStringId(const NomadString& formatString, const CallableId functionId) const {
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
    if (isHub(name) || !canRegisterHubMember(name)) {
        log::error("Cannot register dynamic variable '" + name + "': name is reserved by a hub declaration");
        return NOMAD_INVALID_ID;
    }
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
    registerHubMember(name, HubDynamicVariable{id});

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
    RuntimeValue stringValue;
    stringValue.setStringValue(value);

    setDynamicVariable(interpreter, id, stringValue);

    stringValue.freeObjectValue();
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
    if (isHubVariable(variableName)) {
        return m_hubs[toNomadIndex(getHubId(getContainingHub(variableName)).id)]->m_variableContextId;
    }
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
    RuntimeValue stringValue;
    stringValue.setStringValue(value);

    setContextVariableValue(contextVariableId, stringValue);

    stringValue.freeObjectValue();
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
        returnType->freeValue(m_virtualMachine->getResult());
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
        returnType->copyValue(m_virtualMachine->getResult(), returnValue);
        returnType->freeValue(m_virtualMachine->getResult());
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

void dumpFunctionDeclaration(const ScriptFunction* function, std::ostream& out) {
    out << "function " << function->getName();

    for (NomadIndex i = 0; i < function->getParameterCount(); ++i) {
        const auto parameterId = toNomadId(i);

        out << " " << function->getParameterName(parameterId) << ":" << function->getParameterType(parameterId)->getTypeName();
    }

    out << std::endl;
}

void Runtime::dumpInstructions(std::ostream& out) const {
    std::vector<ScriptFunction*> functions;
    getFunctions(functions);

    for (const auto* function : functions) {
        dumpInstructions(out, function);
    }
}

void Runtime::dumpInstructions(std::ostream& out, const ScriptFunction* function) const {
    dumpFunctionDeclaration(function, out);

    for (auto i = function->getFunctionStart(); i < function->getFunctionEnd(); ++i) {
        auto fn = m_instructions[i].fn;

        auto instructionId = getInstructionId(fn);

        if (instructionId.isInvalid()) {
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
                    textValue = "\"" + getString(value.getIdValue())->getString() + "\" (" + std::to_string(value.getIdValue()) + ")";
                    break;
                case Operand::Id:
                    textValue = std::to_string(value.getIdValue());
                    break;
                case Operand::Function: {
                    const auto functionStart = value.getIndexValue();

                    const auto calledFunction = getFunctionByStartAddress(functionStart);

                textValue = calledFunction->getName() + " (" + std::to_string(calledFunction->getId().id) + ")";
                break;
            }
            case Operand::NativeFunction: {
                const auto* nativeFunction = getNativeFunction(CallableId{value.getIdValue()});

                if (nativeFunction != nullptr) {
                    textValue = nativeFunction->getName() + " (" + std::to_string(nativeFunction->getId().id) + ")";
                } else {
                    textValue = "<unknown native function> (" + std::to_string(value.getIdValue()) + ")";
                }
                break;
            }
            case Operand::FunctionVariable:
                textValue =
                    function->getVariableName(value.getIdValue()) + " (" + std::to_string(value.getIdValue()) + ")";
                break;
            case Operand::DynamicVariable:
                textValue =
                    getDynamicVariableName(value.getIdValue()) + " (" + std::to_string(value.getIdValue()) + ")";
                break;
            case Operand::ContextVariableId:
                textValue =
                    getContextVariableName(value.getIdValue()) + " (" + std::to_string(value.getIdValue()) + ")";
                break;
            case Operand::FormatString:
                textValue = "$\"" + getFormatString(value.getIdValue())->getFormatString() + "\" (" +
                            std::to_string(value.getIdValue()) + ")";
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
            out << "- `" << parameter.name << ":" << parameter.type->getTypeName() << "`" << std::endl;
        }

        out << std::endl;

        out << "`return " << nativeFunction.getReturnType()->getTypeName() << "`" << std::endl;

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
