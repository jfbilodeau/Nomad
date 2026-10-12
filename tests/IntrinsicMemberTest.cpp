// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/script/IntrinsicMember.hpp>
#include <nomad/script/Runtime.hpp>

#include <algorithm>
#include <vector>

using namespace nomad;

namespace {

IntrinsicPropertyGetFn propertyGetter() {
    return [](VirtualMachine*, const RuntimeValue&, RuntimeValue&) {};
}

IntrinsicPropertySetFn propertySetter() {
    return [](VirtualMachine*, RuntimeValue&, const RuntimeValue&) {};
}

NativeFunctionFn methodFunction() {
    return [](VirtualMachine*) {};
}

} // namespace

BOOST_AUTO_TEST_SUITE(intrinsic_members)

BOOST_AUTO_TEST_CASE(registers_read_only_write_only_and_read_write_properties) {
    Runtime runtime;

    const auto readOnlyId = runtime.getIntrinsicMemberId(runtime.getStringType(), "length");
    const auto writeOnlyId = runtime.registerIntrinsicProperty(
        runtime.getStringType(), "sink", runtime.getIntegerType(), {}, propertySetter(), NomadDoc("Write-only value"));
    const auto readWriteId =
        runtime.registerIntrinsicProperty(runtime.getStringType(), "value", runtime.getIntegerType(), propertyGetter(),
                                          propertySetter(), NomadDoc("Read/write value"));

    BOOST_REQUIRE(readOnlyId != NOMAD_INVALID_ID);
    BOOST_REQUIRE(writeOnlyId != NOMAD_INVALID_ID);
    BOOST_REQUIRE(readWriteId != NOMAD_INVALID_ID);

    IntrinsicPropertyDefinition definition;
    BOOST_REQUIRE(runtime.getIntrinsicPropertyDefinition(readOnlyId, definition));
    BOOST_TEST(definition.id == readOnlyId);
    BOOST_TEST(definition.objectType == runtime.getStringType());
    BOOST_TEST(definition.name == "length");
    BOOST_TEST(definition.valueType == runtime.getIntegerType());
    BOOST_TEST(static_cast<bool>(definition.getFn));
    BOOST_TEST(!definition.setFn);

    BOOST_REQUIRE(runtime.getIntrinsicPropertyDefinition(writeOnlyId, definition));
    BOOST_TEST(!definition.getFn);
    BOOST_TEST(static_cast<bool>(definition.setFn));

    BOOST_REQUIRE(runtime.getIntrinsicPropertyDefinition(readWriteId, definition));
    BOOST_TEST(static_cast<bool>(definition.getFn));
    BOOST_TEST(static_cast<bool>(definition.setFn));
}

BOOST_AUTO_TEST_CASE(registers_method_metadata) {
    Runtime runtime;

    const auto methodId =
        runtime.registerIntrinsicMethod(runtime.getStringType(), "replace", methodFunction(),
                                        {
                                            defParameter("from", runtime.getStringType(), NomadDoc("Text to replace")),
                                            defParameter("to", runtime.getStringType(), NomadDoc("Replacement text")),
                                        },
                                        runtime.getStringType(), false, NomadDoc("Return a replaced string"));

    BOOST_REQUIRE(methodId != NOMAD_INVALID_ID);
    BOOST_TEST((runtime.getIntrinsicMemberType(methodId) == IntrinsicMemberType::Method));

    IntrinsicMethodDefinition definition;
    BOOST_REQUIRE(runtime.getIntrinsicMethodDefinition(methodId, definition));
    BOOST_TEST(definition.id == methodId);
    BOOST_TEST(definition.objectType == runtime.getStringType());
    BOOST_TEST(definition.name == "replace");
    BOOST_TEST(definition.parameters.size() == NomadIndex{2});
    BOOST_TEST(definition.returnType == runtime.getStringType());
    BOOST_TEST(!definition.mutatesReceiver);
}

BOOST_AUTO_TEST_CASE(rejects_duplicate_member_name_on_same_type) {
    Runtime runtime;

    BOOST_REQUIRE(runtime.registerIntrinsicProperty(runtime.getStringType(), "value", runtime.getIntegerType(),
                                                    propertyGetter(), {}, NomadDoc("Property")) != NOMAD_INVALID_ID);

    BOOST_TEST(runtime.registerIntrinsicMethod(runtime.getStringType(), "value", methodFunction(), {},
                                               runtime.getIntegerType(), false,
                                               NomadDoc("Method")) == NOMAD_INVALID_ID);
}

BOOST_AUTO_TEST_CASE(allows_compatible_member_names_on_different_types) {
    Runtime runtime;

    const auto stringId =
        runtime.registerIntrinsicMethod(runtime.getStringType(), "find", methodFunction(),
                                        {defParameter("value", runtime.getStringType(), NomadDoc("Value"))},
                                        runtime.getIntegerType(), false, NomadDoc("Find string"));
    const auto integerId =
        runtime.registerIntrinsicMethod(runtime.getIntegerType(), "find", methodFunction(),
                                        {defParameter("value", runtime.getIntegerType(), NomadDoc("Value"))},
                                        runtime.getBooleanType(), false, NomadDoc("Find integer"));

    BOOST_REQUIRE(stringId != NOMAD_INVALID_ID);
    BOOST_REQUIRE(integerId != NOMAD_INVALID_ID);

    std::vector<NomadId> overloads;
    runtime.getIntrinsicMemberOverloads("find", overloads);

    BOOST_TEST(overloads == std::vector<NomadId>({stringId, integerId}));
    BOOST_TEST(runtime.getIntrinsicMemberOverloadCount("find") == NomadIndex{2});
}

BOOST_AUTO_TEST_CASE(rejects_incompatible_member_shapes) {
    Runtime runtime;

    BOOST_REQUIRE(runtime.registerIntrinsicMethod(runtime.getStringType(), "find", methodFunction(),
                                                  {defParameter("value", runtime.getStringType(), NomadDoc("Value"))},
                                                  runtime.getIntegerType(), false,
                                                  NomadDoc("Find string")) != NOMAD_INVALID_ID);

    BOOST_TEST(runtime.registerIntrinsicMethod(runtime.getIntegerType(), "find", methodFunction(),
                                               {
                                                   defParameter("value", runtime.getIntegerType(), NomadDoc("Value")),
                                                   defParameter("start", runtime.getIntegerType(), NomadDoc("Start")),
                                               },
                                               runtime.getBooleanType(), false,
                                               NomadDoc("Find integer")) == NOMAD_INVALID_ID);
}

BOOST_AUTO_TEST_CASE(rejects_invalid_member_definitions) {
    Runtime runtime;

    BOOST_TEST(runtime.registerIntrinsicProperty(nullptr, "value", runtime.getIntegerType(), propertyGetter(), {},
                                                 NomadDoc("Invalid owner")) == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerIntrinsicProperty(runtime.getVoidType(), "value", runtime.getIntegerType(),
                                                 propertyGetter(), {}, NomadDoc("Void owner")) == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerIntrinsicProperty(runtime.getStringType(), "invalid.name", runtime.getIntegerType(),
                                                 propertyGetter(), {}, NomadDoc("Invalid name")) == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerIntrinsicProperty(runtime.getStringType(), "value", runtime.getIntegerType(), {}, {},
                                                 NomadDoc("No access")) == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerIntrinsicProperty(runtime.getStringType(), "value", runtime.getVoidType(),
                                                 propertyGetter(), {}, NomadDoc("Void value")) == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerIntrinsicMethod(runtime.getStringType(), "convert", {}, {}, runtime.getStringType(),
                                               false, NomadDoc("No function")) == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.registerIntrinsicMethod(runtime.getStringType(), "convert", methodFunction(), {}, nullptr, false,
                                               NomadDoc("No return type")) == NOMAD_INVALID_ID);
}

BOOST_AUTO_TEST_CASE(iterates_members_by_type_and_globally) {
    Runtime runtime;

    const auto stringMethodId = runtime.registerIntrinsicMethod(
        runtime.getStringType(), "convert", methodFunction(), {}, runtime.getStringType(), false, NomadDoc("Convert"));
    const auto integerPropertyId = runtime.registerIntrinsicProperty(
        runtime.getIntegerType(), "sign", runtime.getIntegerType(), propertyGetter(), {}, NomadDoc("Sign"));

    std::vector<NomadId> stringMembers;
    runtime.getIntrinsicMembers(runtime.getStringType(), stringMembers);
    BOOST_TEST(stringMembers.size() == NomadIndex{12});
    const auto hasCustomStringMethod = std::ranges::find(stringMembers, stringMethodId) != stringMembers.end();
    const auto hasLength =
        std::ranges::find(stringMembers, runtime.getIntrinsicMemberId(runtime.getStringType(), "length")) !=
        stringMembers.end();
    BOOST_TEST(hasCustomStringMethod);
    BOOST_TEST(hasLength);

    std::vector<NomadId> allMembers;
    runtime.getIntrinsicMembers(allMembers);
    BOOST_TEST(allMembers.size() == stringMembers.size() + 1);
    const auto hasIntegerProperty = std::ranges::find(allMembers, integerPropertyId) != allMembers.end();
    BOOST_TEST(hasIntegerProperty);
}

BOOST_AUTO_TEST_SUITE_END()
