// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/script/NativeMember.hpp>
#include <nomad/script/Runtime.hpp>

#include <algorithm>
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

const ObjectTypeBase* registerTestType(Runtime& runtime) {
    auto descriptor = std::make_unique<ObjectType>("TestObject");
    const auto* type = descriptor.get();
    runtime.registerType(std::move(descriptor));
    return type;
}

} // namespace

BOOST_AUTO_TEST_SUITE(native_members)

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
    BOOST_TEST((runtime.getNativeMemberType(methodId) == NativeMemberType::Method));

    NativeMethodDefinition definition;
    BOOST_REQUIRE(runtime.getNativeMethodDefinition(methodId, definition));
    BOOST_TEST(definition.id == methodId);
    BOOST_TEST(definition.objectType == runtime.getStringType());
    BOOST_TEST(definition.name == "replace");
    BOOST_TEST(definition.parameters.size() == NomadIndex{2});
    BOOST_TEST(definition.returnType == runtime.getStringType());
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
    BOOST_TEST(allMembers.size() == stringMembers.size() + 1);
    const auto hasIntegerProperty = std::ranges::find(allMembers, integerPropertyId) != allMembers.end();
    BOOST_TEST(hasIntegerProperty);
}

BOOST_AUTO_TEST_CASE(indexer_registration_validates_types_and_accessors) {
    Runtime runtime;
    Runtime other;
    const auto* type = registerTestType(runtime);
    const NativeIndexerGetFn1 getter = [](VirtualMachine*, const RuntimeValue&, RuntimeValue&) {};
    const NativeIndexerSetFn1 setter = [](VirtualMachine*, const RuntimeValue&, const RuntimeValue&) {};
    BOOST_TEST(runtime.registerNativeIndexer1(nullptr, runtime.getIntegerType(), runtime.getIntegerType(), getter, {}, "") == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerNativeIndexer1(type, nullptr, runtime.getIntegerType(), getter, {}, "") == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerNativeIndexer1(type, runtime.getVoidType(), runtime.getIntegerType(), getter, {}, "") == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerNativeIndexer1(type, runtime.getIntegerType(), other.getIntegerType(), getter, {}, "") == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerNativeIndexer1(type, runtime.getIntegerType(), runtime.getVoidType(), getter, {}, "") == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerNativeIndexer1(type, runtime.getFileNameType(), runtime.getIntegerType(), getter, {}, "") == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerNativeIndexer1(type, runtime.getIntegerType(), runtime.getIntegerType(), {}, {}, "") == NOMAD_INVALID_ID);
    const auto id = runtime.registerNativeIndexer1(type, runtime.getIntegerType(), runtime.getIntegerType(), {}, setter, "Test indexer");
    BOOST_REQUIRE(id != NOMAD_INVALID_ID);
    BOOST_TEST(runtime.getNativeIndexerId1(type) == id);
    BOOST_TEST(runtime.registerNativeIndexer1(type, runtime.getIntegerType(), runtime.getIntegerType(), getter, setter, "") == NOMAD_INVALID_ID);
    NativeIndexerDefinition1 definition;
    BOOST_REQUIRE(runtime.getNativeIndexerDefinition1(id, definition));
    BOOST_TEST(definition.objectType == type);
    BOOST_TEST(definition.indexType == runtime.getIntegerType());
    BOOST_TEST(definition.valueType == runtime.getIntegerType());
    BOOST_TEST(!definition.getFn);
    BOOST_TEST(static_cast<bool>(definition.setFn));
    BOOST_TEST(!runtime.getNativeIndexerDefinition1(NOMAD_INVALID_ID, definition));
    BOOST_TEST(runtime.getNativeIndexerId1(runtime.getIntegerType()) == NOMAD_INVALID_ID);
}

BOOST_AUTO_TEST_CASE(string_indexer_is_read_only_and_documented) {
    Runtime runtime;
    NativeIndexerDefinition1 definition;
    BOOST_REQUIRE(runtime.getNativeIndexerDefinition1(runtime.getNativeIndexerId1(runtime.getStringType()), definition));
    BOOST_TEST(static_cast<bool>(definition.getFn));
    BOOST_TEST(!definition.setFn);
    BOOST_TEST(definition.valueType == runtime.getStringType());
    std::ostringstream documentation;
    generateNativeMemberDocumentation(&runtime, documentation);
    BOOST_TEST(documentation.str().find("string[index]") != NomadString::npos);
    BOOST_TEST(documentation.str().find("read-only indexer") != NomadString::npos);
}

BOOST_AUTO_TEST_SUITE_END()
