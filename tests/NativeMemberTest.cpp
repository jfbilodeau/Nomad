// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/script/NativeMember.hpp>
#include <nomad/script/Runtime.hpp>

#include <algorithm>
#include <array>
#include <sstream>
#include <type_traits>
#include <utility>
#include <vector>

using namespace nomad;

namespace {

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
    auto descriptor = std::make_unique<ObjectType>("TestObject");
    const auto* type = descriptor.get();
    runtime.registerType(std::move(descriptor));
    return type;
}

} // namespace

BOOST_AUTO_TEST_SUITE(native_members)

BOOST_AUTO_TEST_CASE(member_templates_register_without_specializing_and_validate_shared_shapes) {
    Runtime runtime;
    auto specializations = NomadIndex{0};
    for (const auto* family : {"Family", "Other"}) {
        BOOST_REQUIRE(runtime.registerParameterizedType(
            family, 1,
            [&](const std::vector<const Type*>& arguments) {
                ++specializations;
                return std::make_unique<ObjectType>("Family<" + arguments[0]->getTypeName() + ">", arguments);
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
    IntegerType foreignType;
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
    ) == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerNativeMethod(
        objectType, "declaredMethod", methodFunction(),
        {
            defParameter("value", runtime.getIntegerType(), ""),
            defParameter("line", runtime.getFileNameType(), "")
        }, runtime.getVoidType(), ""
    ) == NOMAD_INVALID_ID);
    BOOST_REQUIRE(runtime.registerNativeMethod(
        objectType, "declaredMethod", methodFunction(),
        {
            defParameter("value", runtime.getIntegerType(), ""),
            defParameter("line", runtime.getLineNumberType(), "")
        }, runtime.getVoidType(), ""
    ) != NOMAD_INVALID_ID);
    BOOST_REQUIRE(runtime.registerNativeProperty(
        objectType, "declaredProperty", runtime.getIntegerType(), propertyGetter(), {}, ""
    ) != NOMAD_INVALID_ID);
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
    ) == NOMAD_INVALID_ID);
    BOOST_REQUIRE(runtime.registerNativeMethod(
        objectType, "declaredCallback", methodFunction(),
        {defParameter("callback", callbackType, "")}, runtime.getVoidType(), ""
    ) != NOMAD_INVALID_ID);
    BOOST_TEST(specializations == 0u);
}

BOOST_AUTO_TEST_CASE(member_templates_validate_against_existing_concrete_members) {
    Runtime runtime;
    BOOST_REQUIRE(runtime.registerParameterizedType("Family", 1, [](const std::vector<const Type*>& arguments) {
        return std::make_unique<ObjectType>("Family<" + arguments[0]->getTypeName() + ">", arguments);
    }));
    const auto* type = registerTestType(runtime);
    BOOST_REQUIRE(runtime.registerNativeMethod(
        type, "existingMethod", methodFunction(),
        {defParameter("value", runtime.getIntegerType(), "")}, runtime.getVoidType(), ""
    ) != NOMAD_INVALID_ID);
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
    ) != NOMAD_INVALID_ID);
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

    BOOST_REQUIRE(readOnlyId != NOMAD_INVALID_ID);
    BOOST_REQUIRE(writeOnlyId != NOMAD_INVALID_ID);
    BOOST_REQUIRE(readWriteId != NOMAD_INVALID_ID);

    NativePropertyDefinition definition;
    BOOST_REQUIRE(runtime.getNativePropertyDefinition(readOnlyId, definition));
    BOOST_TEST(definition.id == readOnlyId);
    BOOST_TEST(definition.objectType == runtime.getStringType());
    BOOST_TEST(definition.name == "length");
    BOOST_TEST(definition.valueType == runtime.getIntegerType());
    BOOST_TEST(static_cast<bool>(definition.getFn));
    BOOST_TEST(!definition.setFn);

    BOOST_REQUIRE(runtime.getNativePropertyDefinition(writeOnlyId, definition));
    BOOST_TEST(!definition.getFn);
    BOOST_TEST(static_cast<bool>(definition.setFn));

    BOOST_REQUIRE(runtime.getNativePropertyDefinition(readWriteId, definition));
    BOOST_TEST(static_cast<bool>(definition.getFn));
    BOOST_TEST(static_cast<bool>(definition.setFn));
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

    BOOST_REQUIRE(methodId != NOMAD_INVALID_ID);
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
                                                    propertyGetter(), {}, NomadDoc("Property")) != NOMAD_INVALID_ID);

    BOOST_TEST(runtime.registerNativeMethod(runtime.getStringType(), "value", methodFunction(), {},
                                               runtime.getIntegerType(),
                                               NomadDoc("Method")) == NOMAD_INVALID_ID);
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

    BOOST_REQUIRE(stringId != NOMAD_INVALID_ID);
    BOOST_REQUIRE(integerId != NOMAD_INVALID_ID);

    std::vector<NomadId> overloads;
    runtime.getNativeMemberOverloads("find", overloads);

    BOOST_TEST(overloads == std::vector<NomadId>({stringId, integerId}));
    BOOST_TEST(runtime.getNativeMemberOverloadCount("find") == NomadIndex{2});
}

BOOST_AUTO_TEST_CASE(rejects_incompatible_member_shapes) {
    Runtime runtime;
    const auto* type = registerTestType(runtime);

    BOOST_REQUIRE(runtime.registerNativeMethod(runtime.getStringType(), "find", methodFunction(),
                                                  {defParameter("value", runtime.getStringType(), NomadDoc("Value"))},
                                                  runtime.getIntegerType(),
                                                  NomadDoc("Find string")) != NOMAD_INVALID_ID);

    BOOST_TEST(runtime.registerNativeMethod(type, "find", methodFunction(),
                                               {
                                                   defParameter("value", runtime.getIntegerType(), NomadDoc("Value")),
                                                   defParameter("start", runtime.getIntegerType(), NomadDoc("Start")),
                                               },
                                               runtime.getBooleanType(),
                                               NomadDoc("Find integer")) == NOMAD_INVALID_ID);
}

BOOST_AUTO_TEST_CASE(rejects_invalid_member_definitions) {
    Runtime runtime;
    ObjectType unregisteredType("Unregistered");

    BOOST_TEST(runtime.registerNativeProperty(&unregisteredType, "value", runtime.getIntegerType(),
                                                 propertyGetter(), {}, NomadDoc("Unregistered owner")) == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerNativeProperty(nullptr, "value", runtime.getIntegerType(), propertyGetter(), {},
                                                 NomadDoc("Invalid owner")) == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerNativeProperty(runtime.getStringType(), "invalid.name", runtime.getIntegerType(),
                                                 propertyGetter(), {}, NomadDoc("Invalid name")) == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerNativeProperty(runtime.getStringType(), "value", runtime.getIntegerType(), {}, {},
                                                 NomadDoc("No access")) == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerNativeProperty(runtime.getStringType(), "value", runtime.getVoidType(),
                                                 propertyGetter(), {}, NomadDoc("Void value")) == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerNativeMethod(runtime.getStringType(), "convert", {}, {}, runtime.getStringType(), NomadDoc("No function")) == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerNativeMethod(runtime.getStringType(), "convert", methodFunction(), {}, nullptr,
                                               NomadDoc("No return type")) == NOMAD_INVALID_ID);
}

BOOST_AUTO_TEST_CASE(iterates_members_by_type_and_globally) {
    Runtime runtime;
    std::vector<NomadId> builtInMembers;
    runtime.getNativeMembers(builtInMembers);
    const auto* type = registerTestType(runtime);

    const auto stringMethodId = runtime.registerNativeMethod(
        runtime.getStringType(), "convert", methodFunction(), {}, runtime.getStringType(), NomadDoc("Convert"));
    const auto integerPropertyId = runtime.registerNativeProperty(
        type, "sign", runtime.getIntegerType(), propertyGetter(), {}, NomadDoc("Sign"));

    std::vector<NomadId> stringMembers;
    runtime.getNativeMembers(runtime.getStringType(), stringMembers);
    BOOST_TEST(stringMembers.size() == NomadIndex{12});
    const auto hasCustomStringMethod = std::ranges::find(stringMembers, stringMethodId) != stringMembers.end();
    const auto hasLength =
        std::ranges::find(stringMembers, runtime.getNativeMemberId(runtime.getStringType(), "length")) !=
        stringMembers.end();
    BOOST_TEST(hasCustomStringMethod);
    BOOST_TEST(hasLength);

    std::vector<NomadId> allMembers;
    runtime.getNativeMembers(allMembers);
    BOOST_TEST(allMembers.size() == builtInMembers.size() + 2);
    const auto hasIntegerProperty = std::ranges::find(allMembers, integerPropertyId) != allMembers.end();
    BOOST_TEST(hasIntegerProperty);
}

BOOST_AUTO_TEST_CASE(indexer_registration_validates_types_and_accessors) {
    Runtime runtime;
    Runtime other;
    const auto* type = registerTestType(runtime);
    const NativeFunctionFn getter = [](VirtualMachine*) {};
    const NativeFunctionFn setter = [](VirtualMachine*) {};
    BOOST_TEST(runtime.registerNativeIndexer(nullptr, {runtime.getIntegerType()}, runtime.getIntegerType(), getter, {}, "") == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerNativeIndexer(type, {nullptr}, runtime.getIntegerType(), getter, {}, "") == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerNativeIndexer(type, {}, runtime.getIntegerType(), getter, {}, "") == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerNativeIndexer(type, {runtime.getVoidType()}, runtime.getIntegerType(), getter, {}, "") == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerNativeIndexer(type, {runtime.getIntegerType()}, other.getIntegerType(), getter, {}, "") == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerNativeIndexer(type, {runtime.getIntegerType()}, runtime.getVoidType(), getter, {}, "") == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerNativeIndexer(type, {runtime.getFileNameType()}, runtime.getIntegerType(), getter, {}, "") == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerNativeIndexer(type, {runtime.getIntegerType()}, runtime.getIntegerType(), {}, {}, "") == NOMAD_INVALID_ID);
    const auto id = runtime.registerNativeIndexer(type, {runtime.getIntegerType()}, runtime.getIntegerType(), {}, setter, "Test indexer");
    BOOST_REQUIRE(id != NOMAD_INVALID_ID);
    BOOST_TEST(runtime.getNativeIndexerId(type, 1) == id);
    BOOST_TEST(runtime.registerNativeIndexer(type, {runtime.getIntegerType()}, runtime.getIntegerType(), getter, setter, "") == NOMAD_INVALID_ID);
    NativeIndexerDefinition definition;
    BOOST_REQUIRE(runtime.getNativeIndexerDefinition(id, definition));
    BOOST_TEST(definition.objectType == type);
    BOOST_REQUIRE(definition.indexTypes.size() == 1u);
    BOOST_TEST(definition.indexTypes[0] == runtime.getIntegerType());
    BOOST_TEST(definition.valueType == runtime.getIntegerType());
    BOOST_TEST(!definition.getFn);
    BOOST_TEST(static_cast<bool>(definition.setFn));
    BOOST_TEST(!runtime.getNativeIndexerDefinition(NOMAD_INVALID_ID, definition));
    BOOST_TEST(runtime.getNativeIndexerId(runtime.getIntegerType(), 1) == NOMAD_INVALID_ID);
}

BOOST_AUTO_TEST_CASE(two_dimensional_indexers_validate_metadata_and_coexist_with_one_dimension) {
    Runtime runtime;
    Runtime other;
    const auto* type = registerTestType(runtime);
    const NativeFunctionFn getter = [](VirtualMachine*) {};
    const NativeFunctionFn setter = [](VirtualMachine*) {};
    const auto* integer = runtime.getIntegerType();
    const auto* string = runtime.getStringType();
    BOOST_TEST(runtime.registerNativeIndexer(nullptr, {integer, string}, integer, getter, {}, "") == NOMAD_INVALID_ID);
    ObjectType unregistered("Unregistered");
    BOOST_TEST(runtime.registerNativeIndexer(&unregistered, {integer, string}, integer, getter, {}, "") == NOMAD_INVALID_ID);
    for (const Type* invalid : std::array<const Type*, 4>{nullptr, runtime.getVoidType(),
        runtime.getFileNameType(), other.getIntegerType()}) {
        BOOST_TEST(runtime.registerNativeIndexer(type, {invalid, string}, integer, getter, {}, "") == NOMAD_INVALID_ID);
        BOOST_TEST(runtime.registerNativeIndexer(type, {integer, invalid}, integer, getter, {}, "") == NOMAD_INVALID_ID);
        BOOST_TEST(runtime.registerNativeIndexer(type, {integer, string}, invalid, getter, {}, "") == NOMAD_INVALID_ID);
    }
    BOOST_TEST(runtime.registerNativeIndexer(type, {integer, string}, integer, {}, {}, "") == NOMAD_INVALID_ID);
    const auto id = runtime.registerNativeIndexer(type, {integer, string}, integer, getter, setter, "Two keys");
    BOOST_REQUIRE(id != NOMAD_INVALID_ID);
    BOOST_TEST(runtime.getNativeIndexerId(type, 2) == id);
    BOOST_TEST(runtime.registerNativeIndexer(type, {integer, string}, integer, getter, {}, "") == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerNativeIndexer(type, {string, integer}, integer, getter, {}, "") == NOMAD_INVALID_ID);
    BOOST_REQUIRE(runtime.registerNativeIndexer(type, {integer}, integer, {}, setter, "") != NOMAD_INVALID_ID);
    NativeIndexerDefinition definition;
    BOOST_REQUIRE(runtime.getNativeIndexerDefinition(id, definition));
    BOOST_TEST(definition.id == id);
    BOOST_TEST(definition.objectType == type);
    BOOST_REQUIRE(definition.indexTypes.size() == 2u);
    BOOST_TEST(definition.indexTypes[0] == integer);
    BOOST_TEST(definition.indexTypes[1] == string);
    BOOST_TEST(definition.valueType == integer);
    BOOST_TEST(static_cast<bool>(definition.getFn));
    BOOST_TEST(static_cast<bool>(definition.setFn));
    BOOST_TEST(runtime.getNativeIndexers().size() == 3u);
    BOOST_TEST(!runtime.getNativeIndexerDefinition(NOMAD_INVALID_ID, definition));
    BOOST_TEST(!runtime.getNativeIndexerDefinition(static_cast<NomadId>(runtime.getNativeIndexers().size()), definition));
    BOOST_TEST(runtime.getNativeIndexerId(integer, 2) == NOMAD_INVALID_ID);
    std::ostringstream documentation;
    generateNativeMemberDocumentation(&runtime, documentation);
    BOOST_TEST(documentation.str().find("TestObject[index1 index2]") != NomadString::npos);
    BOOST_TEST(documentation.str().find("- Index 1: `int`") != NomadString::npos);
    BOOST_TEST(documentation.str().find("- Index 2: `string`") != NomadString::npos);
    BOOST_TEST(documentation.str().find("Two keys") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(string_indexer_is_read_only_and_documented) {
    Runtime runtime;
    NativeIndexerDefinition definition;
    BOOST_REQUIRE(runtime.getNativeIndexerDefinition(runtime.getNativeIndexerId(runtime.getStringType(), 1), definition));
    BOOST_TEST(static_cast<bool>(definition.getFn));
    BOOST_TEST(!definition.setFn);
    BOOST_TEST(definition.valueType == runtime.getStringType());
    std::ostringstream documentation;
    generateNativeMemberDocumentation(&runtime, documentation);
    BOOST_TEST(documentation.str().find("string[index]") != NomadString::npos);
    BOOST_TEST(documentation.str().find("read-only indexer") != NomadString::npos);
}

BOOST_AUTO_TEST_SUITE_END()
