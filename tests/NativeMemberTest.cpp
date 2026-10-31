// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/script/NativeMember.hpp>
#include <nomad/script/NativeProperty.hpp>
#include <nomad/script/Runtime.hpp>

#include <algorithm>
#include <array>
#include <sstream>
#include <type_traits>
#include <utility>
#include <vector>

using namespace nomad;

static_assert(std::is_same_v<decltype(NativeIndexerInfo{}.id), IndexerId>);
static_assert(!std::is_convertible_v<NomadId, IndexerId>);
static_assert(!std::is_convertible_v<IndexerId, NomadId>);
static_assert(!std::is_convertible_v<IndexerId, PropertyId>);
static_assert(!std::is_convertible_v<IndexerId, CallableId>);
static_assert(std::is_same_v<decltype(std::declval<const Runtime&>().getNativeIndexers()),
    const std::vector<const NativeIndexerInfo*>&>);
static_assert(std::is_same_v<decltype(ParameterDefinition{}.typeId), TypeId>);
static_assert(std::is_same_v<decltype(NativeMethodDefinition{}.returnTypeId), TypeId>);
static_assert(std::is_same_v<decltype(NativePropertyDefinition{}.valueType), const Type*>);
static_assert(std::is_same_v<decltype(NativeIndexerDefinition{}.indexTypes), std::vector<TypeId>>);
static_assert(std::is_same_v<decltype(NativeInterableDefinition{}.factory), NativeMethodDefinition>);
static_assert(std::is_same_v<decltype(NativePropertyInfo{}.id), PropertyId>);
static_assert(std::is_same_v<decltype(std::declval<const NativeProperty&>().getInfo()), NativePropertyInfo>);
static_assert(std::is_same_v<decltype(std::declval<const Runtime&>().getNativePropertyInfo(PropertyId{})),
    std::optional<NativePropertyInfo>>);
static_assert(std::is_same_v<decltype(std::declval<const Runtime&>().getNativeIndexerInfo(IndexerId{})),
    std::optional<NativeIndexerInfo>>);
static_assert(!std::is_same_v<NativePropertyDefinition, NativePropertyInfo>);
static_assert(!std::is_same_v<NativeIndexerDefinition, NativeIndexerInfo>);
static_assert(std::is_aggregate_v<ParameterDefinition>);
static_assert(std::is_aggregate_v<NativeMethodDefinition>);
static_assert(std::is_aggregate_v<NativeConstructorDefinition>);
static_assert(std::is_aggregate_v<NativePropertyDefinition>);
static_assert(std::is_aggregate_v<NativeIndexerDefinition>);
static_assert(std::is_aggregate_v<NativeInterableDefinition>);

namespace {

template<typename T>
concept HasAssignedId = requires(T value) { value.id; };

static_assert(!HasAssignedId<ParameterDefinition>);
static_assert(!HasAssignedId<NativeMethodDefinition>);
static_assert(!HasAssignedId<NativeConstructorDefinition>);
static_assert(!HasAssignedId<NativePropertyDefinition>);
static_assert(!HasAssignedId<NativeIndexerDefinition>);
static_assert(!HasAssignedId<NativeInterableDefinition>);

NativePropertyGetFn propertyGetter() {
    return [](VirtualMachine*, RuntimeValue&) {};
}

NativePropertySetFn propertySetter() {
    return [](VirtualMachine*, const RuntimeValue&) {};
}

NativeFunctionFn methodFunction() {
    return [](VirtualMachine*) {};
}

const ClassType* registerTestType(Runtime& runtime) {
    const auto descriptorId = runtime.registerClass<ClassType>("TestObject", ClassMode::Closed);
    BOOST_REQUIRE(descriptorId.isValid());
    const auto* type = runtime.getType(descriptorId).value()->asClassType();
    return type;
}

} // namespace

BOOST_AUTO_TEST_SUITE(native_members)

BOOST_AUTO_TEST_CASE(add_method_resolves_ids_and_owns_registered_metadata_and_callback) {
    Runtime runtime;
    const auto typeId = runtime.registerClass("MethodOwner", ClassMode::Closed);
    BOOST_REQUIRE(typeId.isValid());
    const auto* type = runtime.getType(typeId).value()->asClassType();
    auto calls = 0;
    NativeMethodDefinition definition{
        .name = "answer",
        .parameters = {{runtime.getTypeId("int"), "value", "Input value."}},
        .returnTypeId = runtime.getTypeId("bool"),
        .function = [&](VirtualMachine*) { ++calls; },
        .documentation = "Returns an answer."
    };
    const auto id = runtime.addMethod(typeId, definition);
    BOOST_REQUIRE(id.isValid());
    definition.name = "changed";
    definition.parameters.clear();
    definition.returnTypeId = runtime.getTypeId("float");
    definition.function = {};
    definition.documentation = "Changed documentation.";
    const auto* method = runtime.getNativeMethod(id);
    BOOST_REQUIRE(method != nullptr);
    BOOST_TEST(method->getId() == id);
    BOOST_TEST(method->getClassType() == type);
    BOOST_TEST(method->getName() == "answer");
    BOOST_TEST(method->getDoc() == "Returns an answer.");
    BOOST_TEST(method->getReturnType() == runtime.getBooleanType());
    BOOST_REQUIRE(method->getParameters().size() == 1u);
    BOOST_TEST(method->getParameters()[0].type == runtime.getIntegerType());
    BOOST_TEST(method->getParameters()[0].name == "value");
    BOOST_TEST(method->getParameters()[0].doc == "Input value.");
    BOOST_TEST(runtime.getMethod(id) == method);
    BOOST_TEST(runtime.getNativeMemberMethod(id) == method);
    BOOST_TEST((runtime.getMemberType(id) == MemberType::Method));
    BOOST_TEST(std::get<CallableId>(runtime.getNativeMemberId(type, "answer")) == id);
    const auto methods = type->getMethods();
    BOOST_REQUIRE(methods.size() == 1u);
    BOOST_TEST(methods[0] == method);
    BOOST_TEST(calls == 0);
    method->getFn()(nullptr);
    BOOST_TEST(calls == 1);

    const auto voidId = runtime.addMethod(typeId, {
        .name = "clear",
        .parameters = {},
        .returnTypeId = runtime.getTypeId("void"),
        .function = methodFunction(),
        .documentation = ""
    });
    BOOST_REQUIRE(voidId.isValid());
    BOOST_TEST(runtime.getNativeMethod(voidId)->getReturnType() == runtime.getVoidType());
}

BOOST_AUTO_TEST_CASE(add_method_rejects_invalid_definitions_without_partial_registration) {
    Runtime runtime;
    const auto typeId = runtime.registerClass("MethodOwner", ClassMode::Opened);
    BOOST_REQUIRE(typeId.isValid());
    const auto* type = runtime.getType(typeId).value()->asClassType();
    NativeMethodDefinition definition{
        .name = "convert",
        .parameters = {{runtime.getTypeId("int"), "value", ""}},
        .returnTypeId = runtime.getTypeId("int"),
        .function = methodFunction(),
        .documentation = ""
    };
    std::vector<MemberReference> membersBefore;
    runtime.getNativeMembers(membersBefore);
    for (const auto ownerId : {
        TypeId::INVALID, TypeId{NOMAD_ID_MAX}, runtime.getTypeId("int"), runtime.getTypeId("void")
    }) {
        BOOST_TEST(runtime.addMethod(ownerId, definition).isInvalid());
    }
    for (const auto returnTypeId : {
        TypeId::INVALID, TypeId{NOMAD_ID_MAX}, runtime.getFileNameType()->getId()
    }) {
        auto invalidDefinition = definition;
        invalidDefinition.returnTypeId = returnTypeId;
        BOOST_TEST(runtime.addMethod(typeId, std::move(invalidDefinition)).isInvalid());
    }
    for (const auto parameterTypeId : {
        TypeId::INVALID, TypeId{NOMAD_ID_MAX}, runtime.getTypeId("void")
    }) {
        auto invalidDefinition = definition;
        invalidDefinition.parameters.push_back({parameterTypeId, "second", ""});
        BOOST_TEST(runtime.addMethod(typeId, std::move(invalidDefinition)).isInvalid());
    }
    auto unnamedParameter = definition;
    unnamedParameter.parameters[0].name.clear();
    BOOST_TEST(runtime.addMethod(typeId, std::move(unnamedParameter)).isInvalid());
    auto missingFunction = definition;
    missingFunction.function = {};
    BOOST_TEST(runtime.addMethod(typeId, std::move(missingFunction)).isInvalid());
    BOOST_TEST(type->getMethods().empty());
    std::vector<MemberReference> membersAfter;
    runtime.getNativeMembers(membersAfter);
    BOOST_TEST(membersAfter == membersBefore);
    BOOST_REQUIRE(runtime.addMethod(typeId, std::move(definition)).isValid());
}

BOOST_AUTO_TEST_CASE(add_method_rejects_conflicts_and_preserves_compatible_receiver_overloads) {
    Runtime runtime;
    const auto firstId = runtime.registerClass("FirstMethodOwner", ClassMode::Closed);
    const auto secondId = runtime.registerClass("SecondMethodOwner", ClassMode::Opened);
    BOOST_REQUIRE(firstId.isValid());
    BOOST_REQUIRE(secondId.isValid());
    const auto* first = runtime.getType(firstId).value()->asClassType();
    NativeMethodDefinition definition{
        .name = "convert",
        .parameters = {{runtime.getTypeId("int"), "value", ""}},
        .returnTypeId = runtime.getTypeId("int"),
        .function = methodFunction(),
        .documentation = "Original method."
    };
    const auto methodId = runtime.addMethod(firstId, definition);
    BOOST_REQUIRE(methodId.isValid());
    BOOST_TEST(runtime.addMethod(firstId, definition).isInvalid());
    const auto secondMethodId = runtime.addMethod(secondId, {
        .name = "convert",
        .parameters = {{runtime.getTypeId("float"), "value", ""}},
        .returnTypeId = runtime.getTypeId("float"),
        .function = methodFunction(),
        .documentation = ""
    });
    BOOST_REQUIRE(secondMethodId.isValid());
    BOOST_TEST(runtime.addMethod(secondId, definition).isInvalid());
    definition.name = "stored";
    BOOST_REQUIRE(runtime.registerProperty(first->getId(), "stored", runtime.getIntegerType()).isValid());
    BOOST_TEST(runtime.addMethod(firstId, definition).isInvalid());
    definition.name = "native";
    BOOST_REQUIRE(runtime.addProperty(firstId, {
        runtime.getIntegerType(), "native", propertyGetter(), {}, ""
    }).isValid());
    BOOST_TEST(runtime.addMethod(firstId, definition).isInvalid());
    definition.name = "convert";
    definition.parameters.push_back({runtime.getTypeId("int"), "extra", ""});
    const auto thirdId = runtime.registerClass("ThirdMethodOwner", ClassMode::Closed);
    BOOST_REQUIRE(thirdId.isValid());
    BOOST_TEST(runtime.addMethod(thirdId, definition).isInvalid());
    std::vector<MemberReference> overloads;
    runtime.getNativeMemberOverloads("convert", overloads);
    BOOST_REQUIRE(overloads.size() == 2u);
    BOOST_TEST(std::get<CallableId>(overloads[0]) == methodId);
    BOOST_TEST(std::get<CallableId>(overloads[1]) == secondMethodId);
    BOOST_TEST(runtime.getNativeMethod(methodId)->getDoc() == "Original method.");
}

BOOST_AUTO_TEST_CASE(add_property_owns_its_definition_and_exposes_registered_callbacks_and_metadata) {
    Runtime runtime;
    const auto typeId = runtime.registerClass("PropertyOwner", ClassMode::Closed);
    BOOST_REQUIRE(typeId.isValid());
    const auto* type = static_cast<const ClassType*>(runtime.getType(typeId).value());
    NomadInteger storedValue = 7;
    auto getterCalls = 0;
    auto setterCalls = 0;
    NativePropertyDefinition definition{
        .valueType = runtime.getIntegerType(),
        .name = "value",
        .getter = [&](VirtualMachine*, RuntimeValue& value) {
            ++getterCalls;
            value.setIntegerValue(storedValue);
        },
        .setter = [&](VirtualMachine*, const RuntimeValue& value) {
            ++setterCalls;
            storedValue = value.getIntegerValue();
        },
        .documentation = "Stored value."
    };
    const auto id = runtime.addProperty(typeId, definition);
    BOOST_REQUIRE(id.isValid());
    definition.valueType = runtime.getFloatType();
    definition.name = "changed";
    definition.getter = {};
    definition.setter = {};
    definition.documentation = "Changed documentation.";

    const auto info = runtime.getNativePropertyInfo(id);
    BOOST_REQUIRE(info.has_value());
    BOOST_TEST(info->id == id);
    BOOST_TEST(info->classType == type);
    BOOST_TEST(info->valueType == runtime.getIntegerType());
    BOOST_TEST(info->name == "value");
    BOOST_TEST(info->doc == "Stored value.");
    BOOST_REQUIRE(static_cast<bool>(info->getFn));
    BOOST_REQUIRE(static_cast<bool>(info->setFn));
    BOOST_TEST(runtime.getProperty(id) == type->getProperty("value"));
    BOOST_TEST(runtime.getNativeProperty(id) == type->getProperty(id));
    BOOST_TEST((runtime.getMemberType(id) == MemberType::Property));
    BOOST_TEST(std::get<PropertyId>(runtime.getNativeMemberId(type, "value")) == id);
    std::vector<MemberReference> members;
    runtime.getNativeMembers(type, members);
    BOOST_REQUIRE(members.size() == 1u);
    BOOST_TEST(std::get<PropertyId>(members[0]) == id);
    std::vector<MemberReference> overloads;
    runtime.getNativeMemberOverloads("value", overloads);
    BOOST_REQUIRE(overloads.size() == 1u);
    BOOST_TEST(std::get<PropertyId>(overloads[0]) == id);

    BOOST_TEST(getterCalls == 0);
    BOOST_TEST(setterCalls == 0);
    RuntimeValue value;
    info->getFn(nullptr, value);
    BOOST_TEST(value.getIntegerValue() == 7);
    info->setFn(nullptr, RuntimeValue(NomadInteger{42}));
    info->getFn(nullptr, value);
    BOOST_TEST(value.getIntegerValue() == 42);
    BOOST_TEST(storedValue == 42);
    BOOST_TEST(getterCalls == 2);
    BOOST_TEST(setterCalls == 1);
}

BOOST_AUTO_TEST_CASE(add_property_accepts_read_only_and_write_only_properties_on_open_and_closed_types) {
    Runtime runtime;
    for (const auto mode : {ClassMode::Opened, ClassMode::Closed}) {
        const auto typeId = runtime.registerClass(
            mode == ClassMode::Opened ? "OpenedProperties" : "ClosedProperties", mode
        );
        BOOST_REQUIRE(typeId.isValid());
        const auto readOnlyId = runtime.getTypeManager().registerNativeClassProperty(typeId, {
            .valueType = runtime.getIntegerType(),
            .name = "readOnly",
            .getter = propertyGetter(),
            .setter = {},
            .documentation = "Read only."
        });
        const auto writeOnlyId = runtime.getTypeManager().registerNativeClassProperty(typeId, {
            .valueType = runtime.getIntegerType(),
            .name = "writeOnly",
            .getter = {},
            .setter = propertySetter(),
            .documentation = "Write only."
        });
        BOOST_REQUIRE(readOnlyId.isValid());
        BOOST_REQUIRE(writeOnlyId.isValid());
        const auto readOnly = runtime.getNativePropertyInfo(readOnlyId);
        const auto writeOnly = runtime.getNativePropertyInfo(writeOnlyId);
        BOOST_REQUIRE(readOnly.has_value());
        BOOST_REQUIRE(writeOnly.has_value());
        BOOST_TEST(static_cast<bool>(readOnly->getFn));
        BOOST_TEST(!readOnly->setFn);
        BOOST_TEST(!writeOnly->getFn);
        BOOST_TEST(static_cast<bool>(writeOnly->setFn));
    }
}

BOOST_AUTO_TEST_CASE(manager_native_properties_preserve_catalog_identity_and_frozen_layouts) {
    Runtime runtime;
    auto& manager = runtime.getTypeManager();
    const auto ownerId = runtime.registerClass("ManagedProperties", ClassMode::Closed);
    const auto* owner = manager.getTypeByName("ManagedProperties")->asClassType();
    const auto storedId = manager.registerScriptClassProperty(ownerId, "stored", runtime.getIntegerType());
    BOOST_REQUIRE(storedId.isValid());
    manager.finalizeCompositeFieldLayout(ownerId);
    auto getterCalls = 0;
    auto setterCalls = 0;
    NomadInteger stored = 42;
    const NativePropertyDefinition definition{
        .valueType = runtime.getIntegerType(),
        .name = "managed",
        .getter = [&](VirtualMachine*, RuntimeValue& result) {
            ++getterCalls;
            result.setIntegerValue(stored);
        },
        .setter = [&](VirtualMachine*, const RuntimeValue& value) {
            ++setterCalls;
            stored = value.getIntegerValue();
        },
        .documentation = "Managed accessor."
    };
    const auto id = manager.registerNativeClassProperty(ownerId, definition);
    BOOST_REQUIRE(id.isValid());
    const auto* property = runtime.getNativeProperty(id);
    BOOST_REQUIRE(property != nullptr);
    BOOST_TEST(property == owner->getProperty("managed"));
    BOOST_TEST(property == runtime.getProperty(id));
    BOOST_TEST(property->getDoc() == "Managed accessor.");
    BOOST_TEST(property->getValueType() == runtime.getIntegerType());
    BOOST_TEST(property->isTypeResolved());
    BOOST_TEST(runtime.getNativeMemberId(owner, "managed") == MemberReference{id});
    std::vector<MemberReference> overloads;
    runtime.getNativeMemberOverloads("managed", overloads);
    BOOST_REQUIRE(overloads.size() == 1U);
    BOOST_TEST(overloads.front() == MemberReference{id});
    BOOST_TEST(getterCalls == 0);
    BOOST_TEST(setterCalls == 0);
    BOOST_TEST(owner->isFieldLayoutFinalized());
    BOOST_TEST(owner->getFieldStorageSize() == 1U);
    BOOST_TEST(owner->getFieldIndex(id) == NOMAD_INVALID_INDEX);
    BOOST_CHECK_THROW((void)owner->getFieldOffset(id), NomadException);
    BOOST_TEST(manager.registerNativeClassProperty(ownerId, definition).isInvalid());
    BOOST_TEST(runtime.addProperty(ownerId, definition).isInvalid());
    BOOST_CHECK_THROW(runtime.catalogNativeProperty(id), NomadException);
    BOOST_CHECK_THROW(runtime.catalogNativeProperty(storedId), NomadException);
    BOOST_CHECK_THROW(runtime.catalogNativeProperty(PropertyId::INVALID), NomadException);
    BOOST_TEST(runtime.getNativeMemberOverloadCount("managed") == 1U);
    const auto info = property->getInfo();
    RuntimeValue value;
    info.getFn(nullptr, value);
    BOOST_TEST(value.getIntegerValue() == 42);
    info.setFn(nullptr, RuntimeValue(NomadInteger{7}));
    BOOST_TEST(stored == 7);
    BOOST_TEST(getterCalls == 1);
    BOOST_TEST(setterCalls == 1);
}

BOOST_AUTO_TEST_CASE(manager_native_property_rejections_do_not_publish_members) {
    Runtime runtime;
    auto& manager = runtime.getTypeManager();
    const auto ownerId = runtime.registerClass("PropertyRegistration", ClassMode::Closed);
    const auto structId = runtime.registerStruct("NotAClass");
    const auto* owner = manager.getTypeByName("PropertyRegistration")->asClassType();
    NativePropertyDefinition definition{
        .valueType = runtime.getIntegerType(),
        .name = "candidate",
        .getter = propertyGetter()
    };
    BOOST_TEST(manager.registerNativeClassProperty(structId, definition).isInvalid());
    BOOST_TEST(manager.registerNativeClassProperty(manager.reserveTypeId(), definition).isInvalid());
    const auto methodOwnerId = runtime.registerClass("MethodRegistration", ClassMode::Closed);
    BOOST_REQUIRE(runtime.addMethod(methodOwnerId, {
        .name = "candidate",
        .parameters = {{runtime.getIntegerType()->getId(), "value", ""}},
        .returnTypeId = runtime.getVoidType()->getId(),
        .function = [](VirtualMachine*) {},
        .documentation = ""
    }).isValid());
    BOOST_TEST(manager.registerNativeClassProperty(ownerId, definition).isInvalid());
    BOOST_TEST(owner->getProperties().empty());
    BOOST_TEST(runtime.getNativeMemberId(owner, "candidate") == MemberReference{});
    BOOST_TEST(runtime.getNativeMemberOverloadCount("candidate") == 1U);
    definition.name = "noAccessors";
    definition.getter = {};
    BOOST_TEST(manager.registerNativeClassProperty(ownerId, definition).isInvalid());
    definition.name = "stored";
    definition.getter = propertyGetter();
    const auto storedId = manager.registerScriptClassProperty(ownerId, "stored", runtime.getIntegerType());
    BOOST_REQUIRE(storedId.isValid());
    BOOST_TEST(manager.registerNativeClassProperty(ownerId, definition).isInvalid());
    BOOST_TEST(runtime.getNativeProperty(storedId) == nullptr);
    BOOST_TEST(runtime.getNativeMemberOverloadCount("stored") == 0U);
    BOOST_TEST(owner->getPropertyCount() == 1U);
}

BOOST_AUTO_TEST_CASE(add_property_rejects_invalid_definitions_without_partial_registration) {
    Runtime runtime;
    const auto typeId = runtime.registerClass("PropertyOwner", ClassMode::Closed);
    BOOST_REQUIRE(typeId.isValid());
    const auto* type = static_cast<const ClassType*>(runtime.getType(typeId).value());
    NativePropertyDefinition definition{
        .valueType = runtime.getIntegerType(),
        .name = "before",
        .getter = propertyGetter(),
        .setter = propertySetter(),
        .documentation = "Original documentation."
    };
    const auto beforeId = runtime.addProperty(typeId, definition);
    BOOST_REQUIRE(beforeId.isValid());
    std::vector<MemberReference> membersBefore;
    runtime.getNativeMembers(membersBefore);
    definition.name = "value";

    for (const auto invalidOwner : {
        TypeId::INVALID, TypeId{NOMAD_ID_MAX}, runtime.getTypeId("int"), runtime.getTypeId("void")
    }) {
        BOOST_TEST(runtime.getTypeManager().registerNativeClassProperty(invalidOwner, definition).isInvalid());
    }
    Runtime otherRuntime;
    for (const auto* invalidValueType : {
        static_cast<const Type*>(nullptr), runtime.getVoidType(),
        runtime.getFileNameType(), otherRuntime.getIntegerType()
    }) {
        auto invalidDefinition = definition;
        invalidDefinition.valueType = invalidValueType;
        BOOST_TEST(runtime.getTypeManager().registerNativeClassProperty(typeId, std::move(invalidDefinition)).isInvalid());
    }
    auto inaccessibleDefinition = definition;
    inaccessibleDefinition.getter = {};
    inaccessibleDefinition.setter = {};
    BOOST_TEST(runtime.addProperty(typeId, std::move(inaccessibleDefinition)).isInvalid());

    BOOST_TEST(type->getProperty("value") == nullptr);
    BOOST_TEST(type->getProperties().size() == 1u);
    std::vector<MemberReference> membersAfter;
    runtime.getNativeMembers(membersAfter);
    BOOST_TEST(membersAfter == membersBefore);
    const auto original = runtime.getNativePropertyInfo(beforeId);
    BOOST_REQUIRE(original.has_value());
    BOOST_TEST(original->name == "before");
    BOOST_TEST(original->doc == "Original documentation.");
    definition.name = "after";
    const auto afterId = runtime.addProperty(typeId, std::move(definition));
    BOOST_REQUIRE(afterId.isValid());
    BOOST_TEST(afterId.id == beforeId.id + 1);
}

BOOST_AUTO_TEST_CASE(add_property_rejects_duplicate_and_conflicting_members) {
    Runtime runtime;
    const auto typeId = runtime.registerClass("PropertyOwner", ClassMode::Closed);
    BOOST_REQUIRE(typeId.isValid());
    const auto* type = static_cast<const ClassType*>(runtime.getType(typeId).value());
    NativePropertyDefinition definition{
        .valueType = runtime.getIntegerType(),
        .name = "value",
        .getter = propertyGetter(),
        .setter = propertySetter(),
        .documentation = "Original documentation."
    };
    const auto originalId = runtime.addProperty(typeId, definition);
    BOOST_REQUIRE(originalId.isValid());
    auto readOnlyDefinition = definition;
    readOnlyDefinition.name = "readOnly";
    readOnlyDefinition.setter = {};
    BOOST_REQUIRE(runtime.addProperty(typeId, std::move(readOnlyDefinition)).isValid());
    BOOST_REQUIRE(runtime.registerProperty(type->getId(), "stored", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.addMethod(type->getId(), {
        .name = "method",
        .parameters = {},
        .returnTypeId = runtime.getTypeId("int"),
        .function = methodFunction(),
        .documentation = ""
    }).isValid());
    std::vector<MemberReference> membersBefore;
    runtime.getNativeMembers(membersBefore);

    BOOST_TEST(runtime.addProperty(typeId, definition).isInvalid());
    auto conflictingDefinition = definition;
    conflictingDefinition.valueType = runtime.getFloatType();
    conflictingDefinition.documentation = "Replacement documentation.";
    BOOST_TEST(runtime.addProperty(typeId, std::move(conflictingDefinition)).isInvalid());
    for (const auto* name : {"readOnly", "stored", "method"}) {
        auto duplicateDefinition = definition;
        duplicateDefinition.name = name;
        BOOST_TEST(runtime.addProperty(typeId, std::move(duplicateDefinition)).isInvalid());
    }

    const auto original = runtime.getNativePropertyInfo(originalId);
    BOOST_REQUIRE(original.has_value());
    BOOST_TEST(original->valueType == runtime.getIntegerType());
    BOOST_TEST(original->doc == "Original documentation.");
    BOOST_TEST(static_cast<bool>(original->getFn));
    BOOST_TEST(static_cast<bool>(original->setFn));
    BOOST_TEST(type->getProperties().size() == 3u);
    std::vector<MemberReference> membersAfter;
    runtime.getNativeMembers(membersAfter);
    BOOST_TEST(membersAfter == membersBefore);
}

BOOST_AUTO_TEST_CASE(native_creation_definitions_own_their_inputs_without_registering_members) {
    Runtime runtime;
    const auto integerId = runtime.getTypeId("int");
    BOOST_REQUIRE(integerId.isValid());
    const auto originalIndexerCount = runtime.getNativeIndexers().size();
    std::vector<ParameterDefinition> parameters{
        {integerId, "value", "Input value."}
    };
    auto methodCalls = 0;
    NativeMethodDefinition method{
        .name = "answer",
        .parameters = parameters,
        .returnTypeId = integerId,
        .function = [&methodCalls](VirtualMachine*) { ++methodCalls; },
        .documentation = "Returns an answer."
    };
    parameters[0].name = "changed";
    BOOST_TEST(method.parameters[0].typeId == integerId);
    BOOST_TEST(method.parameters[0].name == "value");
    BOOST_TEST(method.parameters[0].documentation == "Input value.");
    BOOST_TEST(method.returnTypeId == integerId);
    BOOST_TEST(method.documentation == "Returns an answer.");

    NativeInterableDefinition iterable{integerId, method};
    method.name = "changed";
    method.parameters.clear();
    BOOST_TEST(iterable.valueTypeId == integerId);
    BOOST_TEST(iterable.factory.name == "answer");
    BOOST_REQUIRE(iterable.factory.parameters.size() == 1U);
    auto movedIterable = std::move(iterable);
    movedIterable.factory.function(nullptr);
    BOOST_TEST(methodCalls == 1);

    NativeConstructorDefinition constructor{
        .parameters = parameters,
        .function = [](VirtualMachine*) {},
        .documentation = "Constructs an object."
    };
    BOOST_TEST(constructor.parameters[0].typeId == integerId);
    BOOST_TEST(constructor.documentation == "Constructs an object.");
    BOOST_TEST(static_cast<bool>(constructor.function));

    auto propertyValue = NomadInteger{0};
    NativePropertyDefinition property{
        .valueType = runtime.getIntegerType(),
        .name = "value",
        .getter = [&propertyValue](VirtualMachine*, RuntimeValue& value) {
            value.setIntegerValue(propertyValue);
        },
        .setter = [&propertyValue](VirtualMachine*, const RuntimeValue& value) {
            propertyValue = value.getIntegerValue();
        },
        .documentation = "Stored value."
    };
    BOOST_TEST(property.valueType == runtime.getIntegerType());
    property.setter(nullptr, RuntimeValue(NomadInteger{42}));
    RuntimeValue value;
    property.getter(nullptr, value);
    BOOST_TEST(value.getIntegerValue() == 42);

    NativeIndexerDefinition indexer{
        .valueTypeId = integerId,
        .indexTypes = {integerId},
        .getter = method.function,
        .setter = {},
        .documentation = "Reads a value."
    };
    BOOST_TEST(indexer.valueTypeId == integerId);
    BOOST_REQUIRE(indexer.indexTypes.size() == 1U);
    BOOST_TEST(indexer.indexTypes[0] == integerId);
    BOOST_TEST(!indexer.setter);
    indexer.getter(nullptr);
    BOOST_TEST(methodCalls == 2);
    BOOST_TEST(runtime.getCallableId("answer").isInvalid());
    BOOST_TEST(runtime.getNativeIndexers().size() == originalIndexerCount);
}

BOOST_AUTO_TEST_CASE(native_property_info_is_a_snapshot_of_registered_metadata) {
    Runtime runtime;
    const auto* type = registerTestType(runtime);
    const auto id = runtime.addProperty(type->getId(), {
        .valueType = runtime.getIntegerType(),
        .name = "value",
        .getter = propertyGetter(),
        .setter = propertySetter(),
        .documentation = "Original documentation"
    });
    BOOST_REQUIRE(id.isValid());
    const auto* property = runtime.getNativeProperty(id);
    BOOST_REQUIRE(property != nullptr);
    auto info = property->getInfo();
    BOOST_TEST(info.id == id);
    BOOST_TEST(info.classType == type);
    BOOST_TEST(info.valueType == runtime.getIntegerType());
    BOOST_TEST(info.name == "value");
    BOOST_TEST(info.doc == "Original documentation");
    info.name = "changed";
    info.getFn = {};
    auto lookup = runtime.getNativePropertyInfo(MemberReference{id});
    BOOST_REQUIRE(lookup.has_value());
    BOOST_TEST(lookup->name == "value");
    BOOST_TEST(static_cast<bool>(lookup->getFn));
    lookup->name = "local snapshot";
    lookup->getFn = {};
    const auto freshInfo = runtime.getNativePropertyInfo(id);
    BOOST_REQUIRE(freshInfo.has_value());
    BOOST_TEST(freshInfo->name == "value");
    BOOST_TEST(static_cast<bool>(freshInfo->getFn));
    for (const auto invalid : {PropertyId::INVALID, PropertyId{-2},
        PropertyId{NOMAD_ID_MAX - 1}, PropertyId{NOMAD_ID_MAX}}) {
        BOOST_TEST(!runtime.getNativePropertyInfo(invalid).has_value());
    }
    const auto methodId = runtime.addMethod(type->getId(), {
        .name = "read",
        .parameters = {},
        .returnTypeId = runtime.getTypeId("int"),
        .function = methodFunction(),
        .documentation = ""
    });
    BOOST_REQUIRE(methodId.isValid());
    BOOST_TEST(!runtime.getNativePropertyInfo(MemberReference{methodId}).has_value());
    BOOST_TEST(!runtime.getNativePropertyInfo(MemberReference{CallableId::INVALID}).has_value());
    BOOST_TEST(lookup.has_value());
    BOOST_TEST(lookup->name == "local snapshot");
    BOOST_TEST(property->getInfo().id == id);
}

BOOST_AUTO_TEST_CASE(member_templates_register_without_specializing_and_validate_shared_shapes) {
    Runtime runtime;
    auto specializations = NomadIndex{0};
    for (const auto* family : {"Family", "Other"}) {
        BOOST_REQUIRE(runtime.registerParameterizedType(
            family, 1,
            [&](Runtime* owningRuntime, TypeId id, const std::vector<const Type*>& arguments) {
                ++specializations;
                return std::make_unique<ClassType>(owningRuntime, id, "Family<" + arguments[0]->getTypeName() + ">", ClassMode::Closed, arguments
                );
            }
        ));
    }
    BOOST_REQUIRE(runtime.registerMemberTemplate("Family", "declaredProperty", MemberType::Property));
    const std::vector<TemplateParameter> parameters = {
        defTemplateParameter("value", TypeParameterIndex{0}, ""),
        defTemplateParameter("line", runtime.getLineNumberType(), "")
    };
    BOOST_REQUIRE(runtime.registerMemberTemplate("Family", "declaredMethod", MemberType::Method, parameters));
    BOOST_REQUIRE(runtime.registerMemberTemplate("Other", "declaredMethod", MemberType::Method, parameters));
    BOOST_TEST(specializations == 0u);
    BOOST_TEST(runtime.getNativeMemberOverloadCount("declaredMethod") == 0u);
    auto signature = runtime.getMemberSignature("declaredMethod");
    BOOST_REQUIRE(signature.has_value());
    BOOST_TEST((signature->type == MemberType::Method));
    BOOST_REQUIRE(signature->parameters.size() == 2u);
    BOOST_TEST(signature->parameters[0].type == nullptr);
    BOOST_TEST(signature->parameters[1].type == runtime.getLineNumberType());
    signature->parameters.clear();
    BOOST_TEST(runtime.getMemberSignature("declaredMethod")->parameters.size() == 2u);
    BOOST_TEST(!runtime.getMemberSignature("missing").has_value());

    BOOST_TEST(!runtime.registerMemberTemplate("Family", "declaredMethod", MemberType::Method, parameters));
    BOOST_TEST(!runtime.registerMemberTemplate("Missing", "member", MemberType::Property));
    BOOST_REQUIRE(runtime.registerMemberTemplate("Family", "invalid.name", MemberType::Property));
    BOOST_TEST(!runtime.registerMemberTemplate("Family", "unknownKind", MemberType::Unknown));
    BOOST_REQUIRE(runtime.registerMemberTemplate("Other", "declaredProperty", MemberType::Method));
    BOOST_TEST(!runtime.registerMemberTemplate("Family", "withArguments", MemberType::Property, parameters));
    BOOST_TEST(!runtime.registerMemberTemplate("Family", "badIndex", MemberType::Method, {
        defTemplateParameter("value", TypeParameterIndex{1}, "")
    }));
    IntegerType foreignType(&runtime, TypeId::INVALID);
    for (const auto* type : {static_cast<const Type*>(nullptr), runtime.getVoidType(), static_cast<const Type*>(&foreignType)}) {
        BOOST_TEST(!runtime.registerMemberTemplate("Family", "badType", MemberType::Method, {
            defTemplateParameter("value", type, "")
        }));
    }
    BOOST_TEST(!runtime.registerMemberTemplate("Family", "badName", MemberType::Method, {
        defTemplateParameter("", TypeParameterIndex{0}, "")
    }));
    BOOST_TEST(!runtime.getMemberSignature("badType").has_value());
    BOOST_TEST(!runtime.getMemberSignature("badIndex").has_value());
    BOOST_TEST(!runtime.getMemberSignature("badName").has_value());
    const auto* classType = registerTestType(runtime);
    BOOST_TEST(runtime.addMethod(classType->getId(), {
        .name = "declaredMethod",
        .parameters = {{runtime.getTypeId("int"), "value", ""}},
        .returnTypeId = runtime.getTypeId("void"),
        .function = methodFunction(),
        .documentation = ""
    }).isInvalid());
    BOOST_TEST(runtime.addMethod(classType->getId(), {
        .name = "declaredMethod",
        .parameters = {
            {runtime.getTypeId("int"), "value", ""},
            {runtime.getFileNameType()->getId(), "line", ""}
        },
        .returnTypeId = runtime.getTypeId("void"),
        .function = methodFunction(),
        .documentation = ""
    }).isInvalid());
    BOOST_REQUIRE(runtime.addMethod(classType->getId(), {
        .name = "declaredMethod",
        .parameters = {
            {runtime.getTypeId("int"), "value", ""},
            {runtime.getLineNumberType()->getId(), "line", ""}
        },
        .returnTypeId = runtime.getTypeId("void"),
        .function = methodFunction(),
        .documentation = ""
    }).isValid());
    BOOST_REQUIRE(runtime.addProperty(classType->getId(), {
        .valueType = runtime.getIntegerType(),
        .name = "declaredProperty",
        .getter = propertyGetter(),
        .setter = {},
        .documentation = ""
    }).isValid());
    const auto* callbackType = runtime.getCallbackType({}, runtime.getVoidType());
    BOOST_REQUIRE(runtime.registerMemberTemplate("Family", "declaredCallback", MemberType::Method, {
        defTemplateParameter("callback", callbackType, "")
    }));
    BOOST_TEST(!runtime.registerMemberTemplate("Other", "declaredCallback", MemberType::Method, {
        defTemplateParameter("value", TypeParameterIndex{0}, "")
    }));
    BOOST_TEST(runtime.addMethod(classType->getId(), {
        .name = "declaredCallback",
        .parameters = {{runtime.getCallbackType({}, runtime.getBooleanType())->getId(), "callback", ""}},
        .returnTypeId = runtime.getTypeId("void"),
        .function = methodFunction(),
        .documentation = ""
    }).isInvalid());
    BOOST_REQUIRE(runtime.addMethod(classType->getId(), {
        .name = "declaredCallback",
        .parameters = {{callbackType->getId(), "callback", ""}},
        .returnTypeId = runtime.getTypeId("void"),
        .function = methodFunction(),
        .documentation = ""
    }).isValid());
    BOOST_TEST(specializations == 0u);
}

BOOST_AUTO_TEST_CASE(member_templates_validate_against_existing_concrete_members) {
    Runtime runtime;
    BOOST_REQUIRE(runtime.registerParameterizedType("Family", 1, [](Runtime* owningRuntime, TypeId id, const std::vector<const Type*>& arguments) {
        return std::make_unique<ClassType>(owningRuntime, id, "Family<" + arguments[0]->getTypeName() + ">", ClassMode::Closed, arguments
        );
    }));
    const auto* type = registerTestType(runtime);
    BOOST_REQUIRE(runtime.addMethod(type->getId(), {
        .name = "existingMethod",
        .parameters = {{runtime.getTypeId("int"), "value", ""}},
        .returnTypeId = runtime.getTypeId("void"),
        .function = methodFunction(),
        .documentation = ""
    }).isValid());
    BOOST_TEST(!runtime.registerMemberTemplate("Family", "existingMethod", MemberType::Property));
    BOOST_TEST(!runtime.registerMemberTemplate("Family", "existingMethod", MemberType::Method));
    BOOST_TEST(!runtime.registerMemberTemplate("Family", "existingMethod", MemberType::Method, {
        defTemplateParameter("value", runtime.getLineNumberType(), "")
    }));
    BOOST_REQUIRE(runtime.registerMemberTemplate("Family", "existingMethod", MemberType::Method, {
        defTemplateParameter("value", TypeParameterIndex{0}, "")
    }));
    BOOST_REQUIRE(runtime.addMethod(type->getId(), {
        .name = "zeroArguments",
        .parameters = {},
        .returnTypeId = runtime.getTypeId("void"),
        .function = methodFunction(),
        .documentation = ""
    }).isValid());
    BOOST_REQUIRE(runtime.registerMemberTemplate("Family", "zeroArguments", MemberType::Property));
}

BOOST_AUTO_TEST_CASE(string_type_getter_returns_concrete_descriptor) {
    static_assert(std::is_same_v<decltype(std::declval<const Runtime&>().getStringType()), const StringType*>);

    Runtime runtime;
    const auto* stringType = runtime.getStringType();

    BOOST_REQUIRE(stringType != nullptr);
    BOOST_TEST(stringType == runtime.getTypeByName(STRING_TYPE_NAME));
}

BOOST_AUTO_TEST_CASE(registers_read_only_write_only_and_read_write_properties) {
    Runtime runtime;

    const auto readOnlyId = runtime.getNativeMemberId(runtime.getStringType(), "length");
    const auto writeOnlyId = runtime.addProperty(runtime.getTypeId("string"), {
        .valueType = runtime.getIntegerType(),
        .name = "sink",
        .getter = {},
        .setter = propertySetter(),
        .documentation = NomadDoc("Write-only value")
    });
    const auto readWriteId = runtime.addProperty(runtime.getTypeId("string"), {
        .valueType = runtime.getIntegerType(),
        .name = "value",
        .getter = propertyGetter(),
        .setter = propertySetter(),
        .documentation = NomadDoc("Read/write value")
    });

    BOOST_REQUIRE((runtime.getMemberType(readOnlyId) == MemberType::Property));
    BOOST_REQUIRE(writeOnlyId.isValid());
    BOOST_REQUIRE(readWriteId.isValid());

    const auto readOnlyInfo = runtime.getNativePropertyInfo(readOnlyId);
    BOOST_REQUIRE(readOnlyInfo.has_value());
    BOOST_TEST(readOnlyInfo->id == std::get<PropertyId>(readOnlyId));
    BOOST_TEST(readOnlyInfo->classType == runtime.getStringType());
    BOOST_TEST(readOnlyInfo->name == "length");
    BOOST_TEST(readOnlyInfo->valueType == runtime.getIntegerType());
    BOOST_TEST(static_cast<bool>(readOnlyInfo->getFn));
    BOOST_TEST(!readOnlyInfo->setFn);

    const auto writeOnlyInfo = runtime.getNativePropertyInfo(writeOnlyId);
    BOOST_REQUIRE(writeOnlyInfo.has_value());
    BOOST_TEST(!writeOnlyInfo->getFn);
    BOOST_TEST(static_cast<bool>(writeOnlyInfo->setFn));

    const auto readWriteInfo = runtime.getNativePropertyInfo(readWriteId);
    BOOST_REQUIRE(readWriteInfo.has_value());
    BOOST_TEST(static_cast<bool>(readWriteInfo->getFn));
    BOOST_TEST(static_cast<bool>(readWriteInfo->setFn));
}

BOOST_AUTO_TEST_CASE(registers_method_metadata) {
    Runtime runtime;

    const auto methodId =
        runtime.addMethod(runtime.getTypeId("string"), {
            .name = "replace",
            .parameters = {
                                            {runtime.getTypeId("string"), "from", NomadDoc("Text to replace")},
                                            {runtime.getTypeId("string"), "to", NomadDoc("Replacement text")},
                                        },
            .returnTypeId = runtime.getTypeId("string"),
            .function = methodFunction(),
            .documentation = NomadDoc("Return a replaced string")
        });

    BOOST_REQUIRE(methodId.isValid());
    BOOST_TEST((runtime.getMemberType(methodId) == MemberType::Method));

    const auto* method = runtime.getNativeMemberMethod(methodId);
    BOOST_REQUIRE(method != nullptr);
    BOOST_TEST(runtime.getMethod(method->getId()) == static_cast<const Method*>(method));
    BOOST_TEST(method->getClassType() == runtime.getStringType());
    BOOST_TEST(method->getName() == "replace");
    BOOST_TEST(method->getParameters().size() == NomadIndex{2});
    BOOST_TEST(method->getReturnType() == runtime.getStringType());
}

BOOST_AUTO_TEST_CASE(rejects_duplicate_member_name_on_same_type) {
    Runtime runtime;

    BOOST_REQUIRE(runtime.addProperty(runtime.getTypeId("string"), {
        .valueType = runtime.getIntegerType(),
        .name = "value",
        .getter = propertyGetter(),
        .setter = {},
        .documentation = NomadDoc("Property")
    }).isValid());

    BOOST_TEST(runtime.addMethod(runtime.getTypeId("string"), {
        .name = "value",
        .parameters = {},
        .returnTypeId = runtime.getTypeId("int"),
        .function = methodFunction(),
        .documentation = NomadDoc("Method")
    }).isInvalid());
}

BOOST_AUTO_TEST_CASE(allows_compatible_member_names_on_different_types) {
    Runtime runtime;
    const auto* type = registerTestType(runtime);

    const auto stringId =
        runtime.addMethod(runtime.getTypeId("string"), {
            .name = "find",
            .parameters = {{runtime.getTypeId("string"), "value", NomadDoc("Value")}},
            .returnTypeId = runtime.getTypeId("int"),
            .function = methodFunction(),
            .documentation = NomadDoc("Find string")
        });
    const auto integerId =
        runtime.addMethod(type->getId(), {
            .name = "find",
            .parameters = {{runtime.getTypeId("int"), "value", NomadDoc("Value")}},
            .returnTypeId = runtime.getTypeId("bool"),
            .function = methodFunction(),
            .documentation = NomadDoc("Find integer")
        });

    BOOST_REQUIRE(stringId.isValid());
    BOOST_REQUIRE(integerId.isValid());

    std::vector<MemberReference> overloads;
    runtime.getNativeMemberOverloads("find", overloads);

    BOOST_TEST(overloads == std::vector<MemberReference>({stringId, integerId}));
    BOOST_TEST(runtime.getNativeMemberOverloadCount("find") == NomadIndex{2});
}

BOOST_AUTO_TEST_CASE(rejects_incompatible_member_shapes) {
    Runtime runtime;
    const auto* type = registerTestType(runtime);

    BOOST_REQUIRE(runtime.addMethod(runtime.getTypeId("string"), {
        .name = "find",
        .parameters = {{runtime.getTypeId("string"), "value", NomadDoc("Value")}},
        .returnTypeId = runtime.getTypeId("int"),
        .function = methodFunction(),
        .documentation = NomadDoc("Find string")
    }).isValid());

    BOOST_TEST(runtime.addMethod(type->getId(), {
        .name = "find",
        .parameters = {
                                                   {runtime.getTypeId("int"), "value", NomadDoc("Value")},
                                                   {runtime.getTypeId("int"), "start", NomadDoc("Start")},
                                               },
        .returnTypeId = runtime.getTypeId("bool"),
        .function = methodFunction(),
        .documentation = NomadDoc("Find integer")
    }).isInvalid());
}

BOOST_AUTO_TEST_CASE(rejects_invalid_member_definitions) {
    Runtime runtime;
    BOOST_TEST(runtime.addMethod(runtime.getTypeId("string"), {
        .name = "convert",
        .parameters = {},
        .returnTypeId = runtime.getTypeId("string"),
        .function = {},
        .documentation = NomadDoc("No function")
    }).isInvalid());
    BOOST_TEST(runtime.addMethod(runtime.getTypeId("string"), {
        .name = "convert",
        .parameters = {},
        .returnTypeId = TypeId::INVALID,
        .function = methodFunction(),
        .documentation = NomadDoc("No return type")
    }).isInvalid());
}

BOOST_AUTO_TEST_CASE(iterates_members_by_type_and_globally) {
    Runtime runtime;
    std::vector<MemberReference> builtInMembers;
    runtime.getNativeMembers(builtInMembers);
    const auto* type = registerTestType(runtime);

    const auto stringMethodId = runtime.addMethod(runtime.getTypeId("string"), {
        .name = "convert",
        .parameters = {},
        .returnTypeId = runtime.getTypeId("string"),
        .function = methodFunction(),
        .documentation = NomadDoc("Convert")
    });
    const auto integerPropertyId = runtime.addProperty(type->getId(), {
        .valueType = runtime.getIntegerType(),
        .name = "sign",
        .getter = propertyGetter(),
        .setter = {},
        .documentation = NomadDoc("Sign")
    });

    std::vector<MemberReference> stringMembers;
    runtime.getNativeMembers(runtime.getStringType(), stringMembers);
    BOOST_TEST(stringMembers.size() == NomadIndex{12});
    const auto hasCustomStringMethod = std::ranges::find(stringMembers, MemberReference{stringMethodId}) != stringMembers.end();
    const auto hasLength =
        std::ranges::find(stringMembers, runtime.getNativeMemberId(runtime.getStringType(), "length")) !=
        stringMembers.end();
    BOOST_TEST(hasCustomStringMethod);
    BOOST_TEST(hasLength);

    std::vector<MemberReference> allMembers;
    runtime.getNativeMembers(allMembers);
    BOOST_TEST(allMembers.size() == builtInMembers.size() + 2);
    const auto hasIntegerProperty = std::ranges::find(allMembers, MemberReference{integerPropertyId}) != allMembers.end();
    BOOST_TEST(hasIntegerProperty);
}

BOOST_AUTO_TEST_CASE(add_indexer_resolves_ids_and_owns_metadata_and_callbacks) {
    Runtime runtime;
    const auto* type = registerTestType(runtime);
    auto getterCalls = 0;
    auto setterCalls = 0;
    NativeIndexerDefinition definition{
        .valueTypeId = runtime.getStringType()->getId(),
        .indexTypes = {runtime.getIntegerType()->getId(), runtime.getStringType()->getId()},
        .getter = [&](VirtualMachine*) { ++getterCalls; },
        .setter = [&](VirtualMachine*) { ++setterCalls; },
        .documentation = "Two-key indexer."
    };
    const auto originalCount = runtime.getNativeIndexers().size();
    const auto id = runtime.addIndexer(type->getId(), definition);
    BOOST_REQUIRE(id.isValid());
    definition.valueTypeId = runtime.getBooleanType()->getId();
    definition.indexTypes.clear();
    definition.getter = {};
    definition.setter = {};
    definition.documentation = "Changed.";
    const auto info = runtime.getNativeIndexerInfo(id);
    BOOST_REQUIRE(info.has_value());
    BOOST_TEST(info->id == id);
    BOOST_TEST(info->classType == type);
    BOOST_TEST(info->valueType == runtime.getStringType());
    BOOST_REQUIRE(info->indexTypes.size() == 2u);
    BOOST_TEST(info->indexTypes[0] == runtime.getIntegerType());
    BOOST_TEST(info->indexTypes[1] == runtime.getStringType());
    BOOST_TEST(info->doc == "Two-key indexer.");
    const auto* owned = type->getNativeIndexer(2);
    BOOST_REQUIRE(owned != nullptr);
    BOOST_TEST(owned->id == id);
    BOOST_REQUIRE(type->getNativeIndexers().size() == 1u);
    BOOST_TEST(type->getNativeIndexers()[0] == owned);
    BOOST_TEST(runtime.getNativeIndexers().size() == originalCount + 1u);
    BOOST_TEST(runtime.getNativeIndexers().back() == owned);
    BOOST_TEST(runtime.getNativeIndexerId(type, 2) == id);
    BOOST_TEST(getterCalls == 0);
    BOOST_TEST(setterCalls == 0);
    info->getFn(nullptr);
    info->setFn(nullptr);
    BOOST_TEST(getterCalls == 1);
    BOOST_TEST(setterCalls == 1);
}

BOOST_AUTO_TEST_CASE(add_indexer_preserves_access_modes_and_shared_index_count_rules) {
    Runtime runtime;
    const auto* type = registerTestType(runtime);
    const auto integerId = runtime.getIntegerType()->getId();
    const auto getter = methodFunction();
    const auto setter = methodFunction();
    const auto readId = runtime.addIndexer(type->getId(), {
        integerId, {integerId}, getter, {}, "Read only."
    });
    BOOST_REQUIRE(readId.isValid());
    const auto read = runtime.getNativeIndexerInfo(readId);
    BOOST_REQUIRE(read.has_value());
    BOOST_TEST(static_cast<bool>(read->getFn));
    BOOST_TEST(!read->setFn);
    BOOST_TEST(runtime.addIndexer(type->getId(), {
        .valueTypeId = runtime.getIntegerType()->getId(),
        .indexTypes = {runtime.getStringType()->getId()},
        .getter = getter,
        .setter = setter,
        .documentation = ""
    }).isInvalid());
    BOOST_TEST(runtime.addIndexer(type->getId(), {
        integerId, {runtime.getStringType()->getId()}, getter, setter, ""
    }).isInvalid());
    const auto writeId = runtime.addIndexer(type->getId(), {
        .valueTypeId = runtime.getIntegerType()->getId(),
        .indexTypes = {runtime.getIntegerType()->getId(), runtime.getStringType()->getId()},
        .getter = {},
        .setter = setter,
        .documentation = "Write only."
    });
    BOOST_REQUIRE(writeId.isValid());
    const auto write = runtime.getNativeIndexerInfo(writeId);
    BOOST_REQUIRE(write.has_value());
    BOOST_TEST(!write->getFn);
    BOOST_TEST(static_cast<bool>(write->setFn));
    BOOST_TEST(runtime.addIndexer(type->getId(), {
        integerId, {integerId, integerId}, getter, {}, ""
    }).isInvalid());
    const auto otherTypeId = runtime.registerClass("WriteIndexerOwner", ClassMode::Closed);
    BOOST_REQUIRE(otherTypeId.isValid());
    const auto otherId = runtime.addIndexer(otherTypeId, {
        integerId, {integerId}, {}, setter, ""
    });
    BOOST_REQUIRE(otherId.isValid());
    const auto other = runtime.getNativeIndexerInfo(otherId);
    BOOST_REQUIRE(other.has_value());
    BOOST_TEST(!other->getFn);
    BOOST_TEST(static_cast<bool>(other->setFn));
}

BOOST_AUTO_TEST_CASE(add_indexer_rejects_invalid_definitions_without_partial_publication) {
    Runtime runtime;
    const auto* type = registerTestType(runtime);
    const auto integerId = runtime.getIntegerType()->getId();
    const auto getter = methodFunction();
    const NativeIndexerDefinition definition{integerId, {integerId}, getter, {}, ""};
    const auto originalCount = runtime.getNativeIndexers().size();
    for (const auto invalid : {TypeId::INVALID, TypeId{NOMAD_ID_MAX}, integerId}) {
        BOOST_TEST(runtime.addIndexer(invalid, definition).isInvalid());
    }
    for (const auto invalid : {
        TypeId::INVALID, TypeId{NOMAD_ID_MAX}, runtime.getVoidType()->getId(),
        runtime.getFileNameType()->getId()
    }) {
        BOOST_TEST(runtime.addIndexer(type->getId(), {
            invalid, {integerId}, getter, {}, ""
        }).isInvalid());
        BOOST_TEST(runtime.addIndexer(type->getId(), {
            integerId, {integerId, invalid}, getter, {}, ""
        }).isInvalid());
        BOOST_TEST(runtime.addIndexer(type->getId(), {
            integerId, {invalid, integerId}, getter, {}, ""
        }).isInvalid());
    }
    BOOST_TEST(runtime.addIndexer(type->getId(), {
        integerId, {}, getter, {}, ""
    }).isInvalid());
    BOOST_TEST(runtime.addIndexer(type->getId(), {
        integerId, {integerId}, {}, {}, ""
    }).isInvalid());
    BOOST_TEST(runtime.getNativeIndexers().size() == originalCount);
    BOOST_TEST(type->getNativeIndexers().empty());
    BOOST_TEST(runtime.getNativeIndexerId(type, 1).isInvalid());
    BOOST_TEST(runtime.getNativeIndexerId(type, 2).isInvalid());
    BOOST_REQUIRE(runtime.addIndexer(type->getId(), definition).isValid());
}

BOOST_AUTO_TEST_CASE(indexer_registration_validates_types_and_accessors) {
    Runtime runtime;
    const auto* type = registerTestType(runtime);
    const NativeFunctionFn getter = [](VirtualMachine*) {};
    const NativeFunctionFn setter = [](VirtualMachine*) {};
    BOOST_TEST(runtime.addIndexer(TypeId::INVALID, {
        .valueTypeId = runtime.getIntegerType()->getId(),
        .indexTypes = {runtime.getIntegerType()->getId()},
        .getter = getter,
        .setter = {},
        .documentation = ""
    }).isInvalid());
    BOOST_TEST(runtime.addIndexer(type->getId(), {
        .valueTypeId = runtime.getIntegerType()->getId(),
        .indexTypes = {TypeId::INVALID},
        .getter = getter,
        .setter = {},
        .documentation = ""
    }).isInvalid());
    BOOST_TEST(runtime.addIndexer(type->getId(), {
        .valueTypeId = runtime.getIntegerType()->getId(),
        .indexTypes = {},
        .getter = getter,
        .setter = {},
        .documentation = ""
    }).isInvalid());
    BOOST_TEST(runtime.addIndexer(type->getId(), {
        .valueTypeId = runtime.getIntegerType()->getId(),
        .indexTypes = {runtime.getVoidType()->getId()},
        .getter = getter,
        .setter = {},
        .documentation = ""
    }).isInvalid());
    BOOST_TEST(runtime.addIndexer(type->getId(), {
        .valueTypeId = TypeId{NOMAD_ID_MAX},
        .indexTypes = {runtime.getIntegerType()->getId()},
        .getter = getter,
        .setter = {},
        .documentation = ""
    }).isInvalid());
    BOOST_TEST(runtime.addIndexer(type->getId(), {
        .valueTypeId = runtime.getVoidType()->getId(),
        .indexTypes = {runtime.getIntegerType()->getId()},
        .getter = getter,
        .setter = {},
        .documentation = ""
    }).isInvalid());
    BOOST_TEST(runtime.addIndexer(type->getId(), {
        .valueTypeId = runtime.getIntegerType()->getId(),
        .indexTypes = {runtime.getFileNameType()->getId()},
        .getter = getter,
        .setter = {},
        .documentation = ""
    }).isInvalid());
    BOOST_TEST(runtime.addIndexer(type->getId(), {
        .valueTypeId = runtime.getIntegerType()->getId(),
        .indexTypes = {runtime.getIntegerType()->getId()},
        .getter = {},
        .setter = {},
        .documentation = ""
    }).isInvalid());
    const auto id = runtime.addIndexer(type->getId(), {
        .valueTypeId = runtime.getIntegerType()->getId(),
        .indexTypes = {runtime.getIntegerType()->getId()},
        .getter = {},
        .setter = setter,
        .documentation = "Test indexer"
    });
    BOOST_REQUIRE(id.isValid());
    BOOST_TEST(runtime.getNativeIndexerId(type, 1) == id);
    BOOST_TEST(runtime.addIndexer(type->getId(), {
        .valueTypeId = runtime.getIntegerType()->getId(),
        .indexTypes = {runtime.getIntegerType()->getId()},
        .getter = getter,
        .setter = setter,
        .documentation = ""
    }).isInvalid());
    const auto info = runtime.getNativeIndexerInfo(id);
    BOOST_REQUIRE(info.has_value());
    BOOST_TEST(info->classType == type);
    BOOST_REQUIRE(info->indexTypes.size() == 1u);
    BOOST_TEST(info->indexTypes[0] == runtime.getIntegerType());
    BOOST_TEST(info->valueType == runtime.getIntegerType());
    BOOST_TEST(!info->getFn);
    BOOST_TEST(static_cast<bool>(info->setFn));
    BOOST_TEST(!runtime.getNativeIndexerInfo(IndexerId::INVALID).has_value());
    BOOST_TEST(runtime.getNativeIndexerId(runtime.getIntegerType(), 1).isInvalid());
}

BOOST_AUTO_TEST_CASE(two_dimensional_indexers_validate_metadata_and_coexist_with_one_dimension) {
    Runtime runtime;
    const auto* type = registerTestType(runtime);
    const NativeFunctionFn getter = [](VirtualMachine*) {};
    const NativeFunctionFn setter = [](VirtualMachine*) {};
    const auto* integer = runtime.getIntegerType();
    const auto* string = runtime.getStringType();
    BOOST_TEST(runtime.addIndexer(TypeId::INVALID, {
        .valueTypeId = integer->getId(),
        .indexTypes = {integer->getId(), string->getId()},
        .getter = getter,
        .setter = {},
        .documentation = ""
    }).isInvalid());
    ClassType unregistered(&runtime, TypeId::INVALID, "Unregistered", ClassMode::Closed);
    BOOST_TEST(runtime.addIndexer(unregistered.getId(), {
        .valueTypeId = integer->getId(),
        .indexTypes = {integer->getId(), string->getId()},
        .getter = getter,
        .setter = {},
        .documentation = ""
    }).isInvalid());
    for (const auto invalid : {
        TypeId::INVALID, runtime.getVoidType()->getId(),
        runtime.getFileNameType()->getId(), TypeId{NOMAD_ID_MAX}
    }) {
        BOOST_TEST(runtime.addIndexer(type->getId(), {
            .valueTypeId = integer->getId(),
            .indexTypes = {invalid, string->getId()},
            .getter = getter,
            .setter = {},
            .documentation = ""
        }).isInvalid());
        BOOST_TEST(runtime.addIndexer(type->getId(), {
            .valueTypeId = integer->getId(),
            .indexTypes = {integer->getId(), invalid},
            .getter = getter,
            .setter = {},
            .documentation = ""
        }).isInvalid());
        BOOST_TEST(runtime.addIndexer(type->getId(), {
            .valueTypeId = invalid,
            .indexTypes = {integer->getId(), string->getId()},
            .getter = getter,
            .setter = {},
            .documentation = ""
        }).isInvalid());
    }
    BOOST_TEST(runtime.addIndexer(type->getId(), {
        .valueTypeId = integer->getId(),
        .indexTypes = {integer->getId(), string->getId()},
        .getter = {},
        .setter = {},
        .documentation = ""
    }).isInvalid());
    const auto id = runtime.addIndexer(type->getId(), {
        .valueTypeId = integer->getId(),
        .indexTypes = {integer->getId(), string->getId()},
        .getter = getter,
        .setter = setter,
        .documentation = "Two keys"
    });
    BOOST_REQUIRE(id.isValid());
    BOOST_TEST(runtime.getNativeIndexerId(type, 2) == id);
    BOOST_TEST(runtime.addIndexer(type->getId(), {
        .valueTypeId = integer->getId(),
        .indexTypes = {integer->getId(), string->getId()},
        .getter = getter,
        .setter = {},
        .documentation = ""
    }).isInvalid());
    BOOST_TEST(runtime.addIndexer(type->getId(), {
        .valueTypeId = integer->getId(),
        .indexTypes = {string->getId(), integer->getId()},
        .getter = getter,
        .setter = {},
        .documentation = ""
    }).isInvalid());
    BOOST_REQUIRE(runtime.addIndexer(type->getId(), {
        .valueTypeId = integer->getId(),
        .indexTypes = {integer->getId()},
        .getter = {},
        .setter = setter,
        .documentation = ""
    }).isValid());
    const auto info = runtime.getNativeIndexerInfo(id);
    BOOST_REQUIRE(info.has_value());
    BOOST_TEST(info->id == id);
    BOOST_TEST(info->classType == type);
    BOOST_REQUIRE(info->indexTypes.size() == 2u);
    BOOST_TEST(info->indexTypes[0] == integer);
    BOOST_TEST(info->indexTypes[1] == string);
    BOOST_TEST(info->valueType == integer);
    BOOST_TEST(static_cast<bool>(info->getFn));
    BOOST_TEST(static_cast<bool>(info->setFn));
    BOOST_TEST(runtime.getNativeIndexers().size() == 3u);
    BOOST_TEST(!runtime.getNativeIndexerInfo(IndexerId::INVALID).has_value());
    BOOST_TEST(!runtime.getNativeIndexerInfo(IndexerId{toNomadId(runtime.getNativeIndexers().size())}).has_value());
    BOOST_TEST(runtime.getNativeIndexerId(integer, 2).isInvalid());
    std::ostringstream documentation;
    generateNativeMemberDocumentation(&runtime, documentation);
    BOOST_TEST(documentation.str().find("TestObject[index1 index2]") != NomadString::npos);
    BOOST_TEST(documentation.str().find("- Index 1: `int`") != NomadString::npos);
    BOOST_TEST(documentation.str().find("- Index 2: `string`") != NomadString::npos);
    BOOST_TEST(documentation.str().find("Two keys") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(indexer_ids_remain_stable_as_the_catalog_grows) {
    Runtime runtime;
    const auto* type = registerTestType(runtime);
    const NativeFunctionFn getter = [](VirtualMachine*) {};
    const auto id = runtime.addIndexer(type->getId(), {
        .valueTypeId = runtime.getIntegerType()->getId(),
        .indexTypes = {runtime.getIntegerType()->getId()},
        .getter = getter,
        .setter = {},
        .documentation = "Stable indexer"
    });
    BOOST_REQUIRE(id.isValid());
    const auto* original = type->getNativeIndexer(1);
    BOOST_REQUIRE(original != nullptr);
    BOOST_TEST(original->id == id);
    BOOST_TEST(runtime.getNativeIndexers().back() == original);
    const auto ownedIndexers = type->getNativeIndexers();
    BOOST_REQUIRE(ownedIndexers.size() == 1U);
    BOOST_TEST(ownedIndexers[0] == original);
    for (auto count = NomadIndex{2}; count <= 32; ++count) {
        BOOST_REQUIRE(runtime.addIndexer(type->getId(), {
            .valueTypeId = runtime.getIntegerType()->getId(),
            .indexTypes = std::vector<TypeId>(count, runtime.getIntegerType()->getId()),
            .getter = getter,
            .setter = {},
            .documentation = ""
        }).isValid());
    }
    for (auto index = 0; index < 32; ++index) {
        const auto descriptorId = runtime.registerClass<ClassType>("Additional" + std::to_string(index), ClassMode::Closed);
    BOOST_REQUIRE(descriptorId.isValid());
    const auto* additional = runtime.getType(descriptorId).value()->asClassType();
        BOOST_REQUIRE(runtime.addIndexer(additional->getId(), {
            .valueTypeId = runtime.getIntegerType()->getId(),
            .indexTypes = {runtime.getIntegerType()->getId()},
            .getter = getter,
            .setter = {},
            .documentation = ""
        }).isValid());
    }
    BOOST_TEST(type->getNativeIndexer(1) == original);
    BOOST_TEST(original->doc == "Stable indexer");
    BOOST_TEST(type->getNativeIndexers().size() == 32U);
    BOOST_TEST(runtime.getNativeIndexers()[toNomadIndex(id.id)] == original);
    const auto info = runtime.getNativeIndexerInfo(id);
    BOOST_REQUIRE(info.has_value());
    BOOST_TEST(info->id == id);
    BOOST_TEST(info->doc == "Stable indexer");
    BOOST_TEST(info->classType == type);
    BOOST_TEST(runtime.getNativeIndexerId(type, 1) == id);
    for (const auto invalid : {IndexerId::INVALID, IndexerId{-2},
        IndexerId{NOMAD_ID_MAX - 1}, IndexerId{NOMAD_ID_MAX}}) {
        BOOST_TEST(!runtime.getNativeIndexerInfo(invalid).has_value());
    }
    BOOST_TEST(info->id == id);
    BOOST_TEST(info->doc == "Stable indexer");
}

BOOST_AUTO_TEST_CASE(type_owned_indexers_validate_direct_registration_and_catalog_membership) {
    Runtime runtime;
    Runtime other;
    const auto descriptorId = runtime.registerClass("DirectIndexer", ClassMode::Closed);
    BOOST_REQUIRE(descriptorId.isValid());
    auto* type = const_cast<ClassType*>(runtime.getType(descriptorId).value()->asClassType());
    const auto originalCount = runtime.getNativeIndexers().size();
    const NativeIndexerInfo info{
        IndexerId::INVALID, type, {runtime.getIntegerType()}, runtime.getIntegerType(),
        methodFunction(), {}, "Direct indexer"
    };
    BOOST_TEST(runtime.catalogNativeIndexer(nullptr).isInvalid());
    BOOST_TEST(runtime.catalogNativeIndexer(&info).isInvalid());
    BOOST_TEST(type->registerNativeIndexer(other, info).isInvalid());
    BOOST_TEST(other.getNativeIndexerId(type, 1).isInvalid());
    BOOST_TEST(runtime.getNativeIndexerId(other.getStringType(), 1).isInvalid());
    for (auto invalid : {IndexerId{0}, IndexerId{NOMAD_ID_MAX - 1}}) {
        auto preassigned = info;
        preassigned.id = invalid;
        BOOST_TEST(type->registerNativeIndexer(runtime, std::move(preassigned)).isInvalid());
    }
    auto wrongOwner = info;
    wrongOwner.classType = runtime.getStringType();
    BOOST_TEST(type->registerNativeIndexer(runtime, std::move(wrongOwner)).isInvalid());
    ClassType unregistered(&runtime, TypeId::INVALID, "UnregisteredIndexer", ClassMode::Closed);
    auto unregisteredInfo = info;
    unregisteredInfo.classType = &unregistered;
    BOOST_TEST(unregistered.registerNativeIndexer(runtime, std::move(unregisteredInfo)).isInvalid());
    for (const auto* invalidType : {other.getIntegerType(), runtime.getVoidType(), runtime.getFileNameType()}) {
        auto invalidValue = info;
        invalidValue.valueType = invalidType;
        BOOST_TEST(type->registerNativeIndexer(runtime, std::move(invalidValue)).isInvalid());
        auto invalidIndex = info;
        invalidIndex.indexTypes = {invalidType};
        BOOST_TEST(type->registerNativeIndexer(runtime, std::move(invalidIndex)).isInvalid());
    }
    auto noIndices = info;
    noIndices.indexTypes.clear();
    BOOST_TEST(type->registerNativeIndexer(runtime, std::move(noIndices)).isInvalid());
    auto noCallbacks = info;
    noCallbacks.getFn = {};
    BOOST_TEST(type->registerNativeIndexer(runtime, std::move(noCallbacks)).isInvalid());
    BOOST_TEST(type->getNativeIndexers().empty());
    BOOST_TEST(runtime.getNativeIndexers().size() == originalCount);

    const auto id = type->registerNativeIndexer(runtime, info);
    BOOST_REQUIRE(id.isValid());
    const auto* registered = type->getNativeIndexer(1);
    BOOST_REQUIRE(registered != nullptr);
    BOOST_TEST(registered->id == id);
    BOOST_TEST(runtime.getNativeIndexerId(type, 1) == id);
    BOOST_TEST(runtime.getNativeIndexers().size() == originalCount + 1);
    BOOST_TEST(runtime.getNativeIndexers().back() == registered);
    BOOST_TEST(runtime.catalogNativeIndexer(registered).isInvalid());
    BOOST_TEST(type->registerNativeIndexer(runtime, info).isInvalid());
    auto sameArity = info;
    sameArity.indexTypes = {runtime.getStringType()};
    BOOST_TEST(type->registerNativeIndexer(runtime, std::move(sameArity)).isInvalid());
    BOOST_TEST(type->getNativeIndexers().size() == 1U);
    BOOST_TEST(runtime.getNativeIndexers().size() == originalCount + 1);
    BOOST_TEST(type->getNativeIndexer(2) == nullptr);
    auto snapshot = runtime.getNativeIndexerInfo(id);
    BOOST_REQUIRE(snapshot.has_value());
    snapshot->indexTypes.clear();
    snapshot->getFn = {};
    BOOST_TEST(registered->indexTypes.size() == 1U);
    BOOST_TEST(static_cast<bool>(registered->getFn));
}

BOOST_AUTO_TEST_CASE(type_owned_indexer_callbacks_are_released_once_at_runtime_destruction) {
    auto destructions = 0;
    std::weak_ptr<int> lifetime;
    {
        Runtime runtime;
        const auto* type = registerTestType(runtime);
        auto callbackState = std::shared_ptr<int>(new int{42}, [&destructions](int* value) {
            ++destructions;
            delete value;
        });
        lifetime = callbackState;
        BOOST_REQUIRE(runtime.addIndexer(type->getId(), {
            .valueTypeId = runtime.getIntegerType()->getId(),
            .indexTypes = {runtime.getIntegerType()->getId()},
            .getter = [callbackState](VirtualMachine*) {},
            .setter = [callbackState](VirtualMachine*) {},
            .documentation = ""
        }).isValid());
        callbackState.reset();
        BOOST_TEST(!lifetime.expired());
        BOOST_TEST(lifetime.use_count() == 2);
        const auto* owned = type->getNativeIndexer(1);
        BOOST_REQUIRE(owned != nullptr);
        BOOST_TEST(runtime.getNativeIndexers().back() == owned);
        BOOST_TEST(type->getNativeIndexers()[0] == owned);
        BOOST_TEST(lifetime.use_count() == 2);
    }
    BOOST_TEST(lifetime.expired());
    BOOST_TEST(destructions == 1);
}

BOOST_AUTO_TEST_CASE(string_indexer_is_read_only_and_documented) {
    Runtime runtime;
    const auto info = runtime.getNativeIndexerInfo(runtime.getNativeIndexerId(runtime.getStringType(), 1));
    BOOST_REQUIRE(info.has_value());
    BOOST_TEST(static_cast<bool>(info->getFn));
    BOOST_TEST(!info->setFn);
    BOOST_TEST(info->valueType == runtime.getStringType());
    std::ostringstream documentation;
    generateNativeMemberDocumentation(&runtime, documentation);
    BOOST_TEST(documentation.str().find("string[index]") != NomadString::npos);
    BOOST_TEST(documentation.str().find("read-only indexer") != NomadString::npos);
}

BOOST_AUTO_TEST_SUITE_END()
