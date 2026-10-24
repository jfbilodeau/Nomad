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
static_assert(std::is_same_v<decltype(NativePropertyDefinition{}.valueTypeId), TypeId>);
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

const ObjectType* registerTestType(Runtime& runtime) {
    auto descriptor = std::make_unique<ObjectType>(&runtime, "TestObject", ObjectTypeMode::Closed);
    const auto* type = descriptor.get();
    runtime.registerType(std::move(descriptor));
    return type;
}

} // namespace

BOOST_AUTO_TEST_SUITE(native_members)

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
        .factory = [](std::span<const RuntimeValue>) -> NomadObject* { return nullptr; },
        .documentation = "Constructs an object."
    };
    BOOST_TEST(constructor.parameters[0].typeId == integerId);
    BOOST_TEST(constructor.documentation == "Constructs an object.");
    BOOST_TEST(static_cast<bool>(constructor.factory));

    auto propertyValue = NomadInteger{0};
    NativePropertyDefinition property{
        .valueTypeId = integerId,
        .name = "value",
        .getter = [&propertyValue](VirtualMachine*, RuntimeValue& value) {
            value.setIntegerValue(propertyValue);
        },
        .setter = [&propertyValue](VirtualMachine*, const RuntimeValue& value) {
            propertyValue = value.getIntegerValue();
        },
        .documentation = "Stored value."
    };
    BOOST_TEST(property.valueTypeId == integerId);
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
    const auto id = runtime.registerNativeProperty(
        type, "value", runtime.getIntegerType(), propertyGetter(), propertySetter(), "Original documentation"
    );
    BOOST_REQUIRE(id.isValid());
    const auto* property = runtime.getNativeProperty(id);
    BOOST_REQUIRE(property != nullptr);
    auto info = property->getInfo();
    BOOST_TEST(info.id == id);
    BOOST_TEST(info.objectType == type);
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
    const auto methodId = runtime.registerNativeMethod(
        type, "read", methodFunction(), {}, runtime.getIntegerType(), ""
    );
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
            [&](Runtime* owningRuntime, const std::vector<const Type*>& arguments) {
                ++specializations;
                return std::make_unique<ObjectType>(owningRuntime, "Family<" + arguments[0]->getTypeName() + ">", ObjectTypeMode::Closed, arguments
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
    BOOST_TEST(!runtime.registerMemberTemplate("Family", "invalid.name", MemberType::Property));
    BOOST_TEST(!runtime.registerMemberTemplate("Family", "unknownKind", MemberType::Unknown));
    BOOST_REQUIRE(runtime.registerMemberTemplate("Other", "declaredProperty", MemberType::Method));
    BOOST_TEST(!runtime.registerMemberTemplate("Family", "withArguments", MemberType::Property, parameters));
    BOOST_TEST(!runtime.registerMemberTemplate("Family", "badIndex", MemberType::Method, {
        defTemplateParameter("value", TypeParameterIndex{1}, "")
    }));
    IntegerType foreignType(&runtime);
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
    const auto* objectType = registerTestType(runtime);
    BOOST_TEST(runtime.registerNativeMethod(
        objectType, "declaredMethod", methodFunction(),
        {defParameter("value", runtime.getIntegerType(), "")}, runtime.getVoidType(), ""
    ).isInvalid());
    BOOST_TEST(runtime.registerNativeMethod(
        objectType, "declaredMethod", methodFunction(),
        {
            defParameter("value", runtime.getIntegerType(), ""),
            defParameter("line", runtime.getFileNameType(), "")
        }, runtime.getVoidType(), ""
    ).isInvalid());
    BOOST_REQUIRE(runtime.registerNativeMethod(
        objectType, "declaredMethod", methodFunction(),
        {
            defParameter("value", runtime.getIntegerType(), ""),
            defParameter("line", runtime.getLineNumberType(), "")
        }, runtime.getVoidType(), ""
    ).isValid());
    BOOST_REQUIRE(runtime.registerNativeProperty(
        objectType, "declaredProperty", runtime.getIntegerType(), propertyGetter(), {}, ""
    ).isValid());
    const auto* callbackType = runtime.getCallbackType({}, runtime.getVoidType());
    BOOST_REQUIRE(runtime.registerMemberTemplate("Family", "declaredCallback", MemberType::Method, {
        defTemplateParameter("callback", callbackType, "")
    }));
    BOOST_TEST(!runtime.registerMemberTemplate("Other", "declaredCallback", MemberType::Method, {
        defTemplateParameter("value", TypeParameterIndex{0}, "")
    }));
    BOOST_TEST(runtime.registerNativeMethod(
        objectType, "declaredCallback", methodFunction(),
        {defParameter("callback", runtime.getCallbackType({}, runtime.getBooleanType()), "")},
        runtime.getVoidType(), ""
    ).isInvalid());
    BOOST_REQUIRE(runtime.registerNativeMethod(
        objectType, "declaredCallback", methodFunction(),
        {defParameter("callback", callbackType, "")}, runtime.getVoidType(), ""
    ).isValid());
    BOOST_TEST(specializations == 0u);
}

BOOST_AUTO_TEST_CASE(member_templates_validate_against_existing_concrete_members) {
    Runtime runtime;
    BOOST_REQUIRE(runtime.registerParameterizedType("Family", 1, [](Runtime* owningRuntime, const std::vector<const Type*>& arguments) {
        return std::make_unique<ObjectType>(owningRuntime, "Family<" + arguments[0]->getTypeName() + ">", ObjectTypeMode::Closed, arguments
        );
    }));
    const auto* type = registerTestType(runtime);
    BOOST_REQUIRE(runtime.registerNativeMethod(
        type, "existingMethod", methodFunction(),
        {defParameter("value", runtime.getIntegerType(), "")}, runtime.getVoidType(), ""
    ).isValid());
    BOOST_TEST(!runtime.registerMemberTemplate("Family", "existingMethod", MemberType::Property));
    BOOST_TEST(!runtime.registerMemberTemplate("Family", "existingMethod", MemberType::Method));
    BOOST_TEST(!runtime.registerMemberTemplate("Family", "existingMethod", MemberType::Method, {
        defTemplateParameter("value", runtime.getLineNumberType(), "")
    }));
    BOOST_REQUIRE(runtime.registerMemberTemplate("Family", "existingMethod", MemberType::Method, {
        defTemplateParameter("value", TypeParameterIndex{0}, "")
    }));
    BOOST_REQUIRE(runtime.registerNativeMethod(
        type, "zeroArguments", methodFunction(), {}, runtime.getVoidType(), ""
    ).isValid());
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
    const auto writeOnlyId = runtime.registerNativeProperty(
        runtime.getStringType(), "sink", runtime.getIntegerType(), {}, propertySetter(), NomadDoc("Write-only value"));
    const auto readWriteId =
        runtime.registerNativeProperty(runtime.getStringType(), "value", runtime.getIntegerType(), propertyGetter(),
                                          propertySetter(), NomadDoc("Read/write value"));

    BOOST_REQUIRE((runtime.getMemberType(readOnlyId) == MemberType::Property));
    BOOST_REQUIRE(writeOnlyId.isValid());
    BOOST_REQUIRE(readWriteId.isValid());

    const auto readOnlyInfo = runtime.getNativePropertyInfo(readOnlyId);
    BOOST_REQUIRE(readOnlyInfo.has_value());
    BOOST_TEST(readOnlyInfo->id == std::get<PropertyId>(readOnlyId));
    BOOST_TEST(readOnlyInfo->objectType == runtime.getStringType());
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
        runtime.registerNativeMethod(runtime.getStringType(), "replace", methodFunction(),
                                        {
                                            defParameter("from", runtime.getStringType(), NomadDoc("Text to replace")),
                                            defParameter("to", runtime.getStringType(), NomadDoc("Replacement text")),
                                        },
                                        runtime.getStringType(), NomadDoc("Return a replaced string"));

    BOOST_REQUIRE(methodId.isValid());
    BOOST_TEST((runtime.getMemberType(methodId) == MemberType::Method));

    const auto* method = runtime.getNativeMemberMethod(methodId);
    BOOST_REQUIRE(method != nullptr);
    BOOST_TEST(runtime.getMethod(method->getId()) == static_cast<const Method*>(method));
    BOOST_TEST(method->getObjectType() == runtime.getStringType());
    BOOST_TEST(method->getName() == "replace");
    BOOST_TEST(method->getParameters().size() == NomadIndex{2});
    BOOST_TEST(method->getReturnType() == runtime.getStringType());
}

BOOST_AUTO_TEST_CASE(rejects_duplicate_member_name_on_same_type) {
    Runtime runtime;

    BOOST_REQUIRE(runtime.registerNativeProperty(runtime.getStringType(), "value", runtime.getIntegerType(),
                                                    propertyGetter(), {}, NomadDoc("Property")).isValid());

    BOOST_TEST(runtime.registerNativeMethod(runtime.getStringType(), "value", methodFunction(), {},
                                               runtime.getIntegerType(),
                                               NomadDoc("Method")).isInvalid());
}

BOOST_AUTO_TEST_CASE(allows_compatible_member_names_on_different_types) {
    Runtime runtime;
    const auto* type = registerTestType(runtime);

    const auto stringId =
        runtime.registerNativeMethod(runtime.getStringType(), "find", methodFunction(),
                                        {defParameter("value", runtime.getStringType(), NomadDoc("Value"))},
                                        runtime.getIntegerType(), NomadDoc("Find string"));
    const auto integerId =
        runtime.registerNativeMethod(type, "find", methodFunction(),
                                        {defParameter("value", runtime.getIntegerType(), NomadDoc("Value"))},
                                        runtime.getBooleanType(), NomadDoc("Find integer"));

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

    BOOST_REQUIRE(runtime.registerNativeMethod(runtime.getStringType(), "find", methodFunction(),
                                                  {defParameter("value", runtime.getStringType(), NomadDoc("Value"))},
                                                  runtime.getIntegerType(),
                                                  NomadDoc("Find string")).isValid());

    BOOST_TEST(runtime.registerNativeMethod(type, "find", methodFunction(),
                                               {
                                                   defParameter("value", runtime.getIntegerType(), NomadDoc("Value")),
                                                   defParameter("start", runtime.getIntegerType(), NomadDoc("Start")),
                                               },
                                               runtime.getBooleanType(),
                                               NomadDoc("Find integer")).isInvalid());
}

BOOST_AUTO_TEST_CASE(rejects_invalid_member_definitions) {
    Runtime runtime;
    ObjectType unregisteredType(&runtime, "Unregistered", ObjectTypeMode::Closed);

    BOOST_TEST(runtime.registerNativeProperty(&unregisteredType, "value", runtime.getIntegerType(),
                                                 propertyGetter(), {}, NomadDoc("Unregistered owner")).isInvalid());
    BOOST_TEST(runtime.registerNativeProperty(nullptr, "value", runtime.getIntegerType(), propertyGetter(), {},
                                                 NomadDoc("Invalid owner")).isInvalid());
    BOOST_TEST(runtime.registerNativeProperty(runtime.getStringType(), "invalid.name", runtime.getIntegerType(),
                                                 propertyGetter(), {}, NomadDoc("Invalid name")).isInvalid());
    BOOST_TEST(runtime.registerNativeProperty(runtime.getStringType(), "value", runtime.getIntegerType(), {}, {},
                                                 NomadDoc("No access")).isInvalid());
    BOOST_TEST(runtime.registerNativeProperty(runtime.getStringType(), "value", runtime.getVoidType(),
                                                 propertyGetter(), {}, NomadDoc("Void value")).isInvalid());
    BOOST_TEST(runtime.registerNativeMethod(runtime.getStringType(), "convert", {}, {}, runtime.getStringType(), NomadDoc("No function")).isInvalid());
    BOOST_TEST(runtime.registerNativeMethod(runtime.getStringType(), "convert", methodFunction(), {}, nullptr,
                                               NomadDoc("No return type")).isInvalid());
}

BOOST_AUTO_TEST_CASE(iterates_members_by_type_and_globally) {
    Runtime runtime;
    std::vector<MemberReference> builtInMembers;
    runtime.getNativeMembers(builtInMembers);
    const auto* type = registerTestType(runtime);

    const auto stringMethodId = runtime.registerNativeMethod(
        runtime.getStringType(), "convert", methodFunction(), {}, runtime.getStringType(), NomadDoc("Convert"));
    const auto integerPropertyId = runtime.registerNativeProperty(
        type, "sign", runtime.getIntegerType(), propertyGetter(), {}, NomadDoc("Sign"));

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

BOOST_AUTO_TEST_CASE(indexer_registration_validates_types_and_accessors) {
    Runtime runtime;
    Runtime other;
    const auto* type = registerTestType(runtime);
    const NativeFunctionFn getter = [](VirtualMachine*) {};
    const NativeFunctionFn setter = [](VirtualMachine*) {};
    BOOST_TEST(runtime.registerNativeIndexer(nullptr, {runtime.getIntegerType()}, runtime.getIntegerType(), getter, {}, "").isInvalid());
    BOOST_TEST(runtime.registerNativeIndexer(type, {nullptr}, runtime.getIntegerType(), getter, {}, "").isInvalid());
    BOOST_TEST(runtime.registerNativeIndexer(type, {}, runtime.getIntegerType(), getter, {}, "").isInvalid());
    BOOST_TEST(runtime.registerNativeIndexer(type, {runtime.getVoidType()}, runtime.getIntegerType(), getter, {}, "").isInvalid());
    BOOST_TEST(runtime.registerNativeIndexer(type, {runtime.getIntegerType()}, other.getIntegerType(), getter, {}, "").isInvalid());
    BOOST_TEST(runtime.registerNativeIndexer(type, {runtime.getIntegerType()}, runtime.getVoidType(), getter, {}, "").isInvalid());
    BOOST_TEST(runtime.registerNativeIndexer(type, {runtime.getFileNameType()}, runtime.getIntegerType(), getter, {}, "").isInvalid());
    BOOST_TEST(runtime.registerNativeIndexer(type, {runtime.getIntegerType()}, runtime.getIntegerType(), {}, {}, "").isInvalid());
    const auto id = runtime.registerNativeIndexer(type, {runtime.getIntegerType()}, runtime.getIntegerType(), {}, setter, "Test indexer");
    BOOST_REQUIRE(id.isValid());
    BOOST_TEST(runtime.getNativeIndexerId(type, 1) == id);
    BOOST_TEST(runtime.registerNativeIndexer(type, {runtime.getIntegerType()}, runtime.getIntegerType(), getter, setter, "").isInvalid());
    const auto info = runtime.getNativeIndexerInfo(id);
    BOOST_REQUIRE(info.has_value());
    BOOST_TEST(info->objectType == type);
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
    Runtime other;
    const auto* type = registerTestType(runtime);
    const NativeFunctionFn getter = [](VirtualMachine*) {};
    const NativeFunctionFn setter = [](VirtualMachine*) {};
    const auto* integer = runtime.getIntegerType();
    const auto* string = runtime.getStringType();
    BOOST_TEST(runtime.registerNativeIndexer(nullptr, {integer, string}, integer, getter, {}, "").isInvalid());
    ObjectType unregistered(&runtime, "Unregistered", ObjectTypeMode::Closed);
    BOOST_TEST(runtime.registerNativeIndexer(&unregistered, {integer, string}, integer, getter, {}, "").isInvalid());
    for (const Type* invalid : std::array<const Type*, 4>{nullptr, runtime.getVoidType(),
        runtime.getFileNameType(), other.getIntegerType()}) {
        BOOST_TEST(runtime.registerNativeIndexer(type, {invalid, string}, integer, getter, {}, "").isInvalid());
        BOOST_TEST(runtime.registerNativeIndexer(type, {integer, invalid}, integer, getter, {}, "").isInvalid());
        BOOST_TEST(runtime.registerNativeIndexer(type, {integer, string}, invalid, getter, {}, "").isInvalid());
    }
    BOOST_TEST(runtime.registerNativeIndexer(type, {integer, string}, integer, {}, {}, "").isInvalid());
    const auto id = runtime.registerNativeIndexer(type, {integer, string}, integer, getter, setter, "Two keys");
    BOOST_REQUIRE(id.isValid());
    BOOST_TEST(runtime.getNativeIndexerId(type, 2) == id);
    BOOST_TEST(runtime.registerNativeIndexer(type, {integer, string}, integer, getter, {}, "").isInvalid());
    BOOST_TEST(runtime.registerNativeIndexer(type, {string, integer}, integer, getter, {}, "").isInvalid());
    BOOST_REQUIRE(runtime.registerNativeIndexer(type, {integer}, integer, {}, setter, "").isValid());
    const auto info = runtime.getNativeIndexerInfo(id);
    BOOST_REQUIRE(info.has_value());
    BOOST_TEST(info->id == id);
    BOOST_TEST(info->objectType == type);
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
    const auto id = runtime.registerNativeIndexer(type, {runtime.getIntegerType()},
        runtime.getIntegerType(), getter, {}, "Stable indexer");
    BOOST_REQUIRE(id.isValid());
    const auto* original = type->getNativeIndexer(1);
    BOOST_REQUIRE(original != nullptr);
    BOOST_TEST(original->id == id);
    BOOST_TEST(runtime.getNativeIndexers().back() == original);
    const auto ownedIndexers = type->getNativeIndexers();
    BOOST_REQUIRE(ownedIndexers.size() == 1U);
    BOOST_TEST(ownedIndexers[0] == original);
    for (auto count = NomadIndex{2}; count <= 32; ++count) {
        BOOST_REQUIRE(runtime.registerNativeIndexer(type,
            std::vector<const Type*>(count, runtime.getIntegerType()),
            runtime.getIntegerType(), getter, {}, "").isValid());
    }
    for (auto index = 0; index < 32; ++index) {
        auto descriptor = std::make_unique<ObjectType>(&runtime, "Additional" + std::to_string(index), ObjectTypeMode::Closed);
        const auto* additional = descriptor.get();
        runtime.registerType(std::move(descriptor));
        BOOST_REQUIRE(runtime.registerNativeIndexer(additional, {runtime.getIntegerType()},
            runtime.getIntegerType(), getter, {}, "").isValid());
    }
    BOOST_TEST(type->getNativeIndexer(1) == original);
    BOOST_TEST(original->doc == "Stable indexer");
    BOOST_TEST(type->getNativeIndexers().size() == 32U);
    BOOST_TEST(runtime.getNativeIndexers()[toNomadIndex(id.id)] == original);
    const auto info = runtime.getNativeIndexerInfo(id);
    BOOST_REQUIRE(info.has_value());
    BOOST_TEST(info->id == id);
    BOOST_TEST(info->doc == "Stable indexer");
    BOOST_TEST(info->objectType == type);
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
    auto descriptor = std::make_unique<ObjectType>(&runtime, "DirectIndexer", ObjectTypeMode::Closed);
    auto* type = descriptor.get();
    runtime.registerType(std::move(descriptor));
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
    wrongOwner.objectType = runtime.getStringType();
    BOOST_TEST(type->registerNativeIndexer(runtime, std::move(wrongOwner)).isInvalid());
    ObjectType unregistered(&runtime, "UnregisteredIndexer", ObjectTypeMode::Closed);
    auto unregisteredInfo = info;
    unregisteredInfo.objectType = &unregistered;
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
        BOOST_REQUIRE(runtime.registerNativeIndexer(
            type, {runtime.getIntegerType()}, runtime.getIntegerType(),
            [callbackState](VirtualMachine*) {}, [callbackState](VirtualMachine*) {}, ""
        ).isValid());
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
