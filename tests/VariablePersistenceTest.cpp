// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/game/VariablePersistence.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VariableContext.hpp>

#include <TestDirectory.hpp>

#include <boost/json.hpp>

#include <filesystem>
#include <fstream>
#include <limits>

using namespace nomad;
using namespace nomad::test;

namespace {

VariableId registerValue(SimpleVariableContext& context, const NomadString& name, const Type* type, RuntimeValue value) {
    const auto variableId = context.registerVariable(name, type);
    context.setValue(variableId, value);
    type->freeValue(value);
    return variableId;
}

RuntimeValue getValue(SimpleVariableContext& context, const NomadString& name) {
    RuntimeValue value;
    context.getValue(context.getVariableId(name), value);
    return value;
}

boost::json::object parseObject(const char* text) {
    return boost::json::parse(text).as_object();
}

} // namespace

BOOST_AUTO_TEST_CASE(variable_persistence_saves_context_as_flat_pretty_json)
{
    Runtime runtime;
    SimpleVariableContext context;

    registerValue(context, "inventory.weapon.speed", runtime.getFloatType(), RuntimeValue{NomadFloat{0.5f}});
    registerValue(context, "inventory.gold", runtime.getIntegerType(), RuntimeValue{NomadInteger{25}});
    registerValue(context, "inventory.playerName", runtime.getStringType(), RuntimeValue{NomadString{"Nomad"}});
    registerValue(context, "inventory.weapon.attack", runtime.getIntegerType(), RuntimeValue{NomadInteger{10}});
    registerValue(context, "inventory.items.lens", runtime.getBooleanType(), RuntimeValue{true});
    // Declared but never typed. Must be skipped.
    context.registerVariable("inventory.unused", nullptr);

    boost::json::object root;
    BOOST_REQUIRE(saveVariableContext(&runtime, context, root));

    const NomadString expected =
        "{\n"
        "    \"inventory.gold\": 25,\n"
        "    \"inventory.items.lens\": true,\n"
        "    \"inventory.playerName\": \"Nomad\",\n"
        "    \"inventory.weapon.attack\": 10,\n"
        "    \"inventory.weapon.speed\": 0.5\n"
        "}\n";

    BOOST_TEST(toPrettyJson(root) == expected);
}

BOOST_AUTO_TEST_CASE(variable_persistence_writes_floats_in_shortest_form)
{
    Runtime runtime;
    boost::json::object root;

    BOOST_REQUIRE(writeJsonVariable(&runtime, root, "settings.volume", runtime.getFloatType(), RuntimeValue{NomadFloat{0.1f}}));

    BOOST_TEST(toPrettyJson(root) == "{\n    \"settings.volume\": 0.1\n}\n");
}

BOOST_AUTO_TEST_CASE(variable_persistence_round_trips_all_supported_types)
{
    Runtime runtime;
    SimpleVariableContext source;

    registerValue(source, "inventory.gold", runtime.getIntegerType(), RuntimeValue{NomadInteger{-42}});
    registerValue(source, "inventory.weapon.speed", runtime.getFloatType(), RuntimeValue{NomadFloat{1.75f}});
    registerValue(source, "inventory.items.lens", runtime.getBooleanType(), RuntimeValue{true});
    registerValue(source, "inventory.playerName", runtime.getStringType(), RuntimeValue{NomadString{"J-F \"Nomad\" \xC3\xA9"}});

    boost::json::object root;
    BOOST_REQUIRE(saveVariableContext(&runtime, source, root));

    const auto reparsed = boost::json::parse(toPrettyJson(root)).as_object();

    SimpleVariableContext destination;
    registerValue(destination, "inventory.gold", runtime.getIntegerType(), RuntimeValue{NomadInteger{0}});
    registerValue(destination, "inventory.weapon.speed", runtime.getFloatType(), RuntimeValue{NomadFloat{0.0f}});
    registerValue(destination, "inventory.items.lens", runtime.getBooleanType(), RuntimeValue{false});
    registerValue(destination, "inventory.playerName", runtime.getStringType(), RuntimeValue{NomadString{"old"}});

    loadVariableContext(&runtime, destination, reparsed);

    BOOST_TEST(getValue(destination, "inventory.gold").getIntegerValue() == -42);
    BOOST_TEST(getValue(destination, "inventory.weapon.speed").getFloatValue() == 1.75f);
    BOOST_TEST(getValue(destination, "inventory.items.lens").getBooleanValue() == true);
    BOOST_TEST(NomadString(getValue(destination, "inventory.playerName").getStringValue()) == "J-F \"Nomad\" \xC3\xA9");
}

BOOST_AUTO_TEST_CASE(variable_persistence_round_trips_enums_by_member_name)
{
    Runtime runtime;
    const auto stateId = runtime.createEnum("State", {{"idle", 10}, {"walking", 20}});
    BOOST_REQUIRE(stateId.isValid());
    const auto* stateType = runtime.getType(stateId).value();
    BOOST_REQUIRE(stateType != nullptr);

    SimpleVariableContext source;
    registerValue(source, "inventory.state", stateType, RuntimeValue{NomadInteger{20}});

    boost::json::object root;
    BOOST_REQUIRE(saveVariableContext(&runtime, source, root));
    BOOST_REQUIRE(root.at("inventory.state").is_string());
    BOOST_TEST(root.at("inventory.state").as_string() == "walking");
    BOOST_TEST(toPrettyJson(root) == "{\n    \"inventory.state\": \"walking\"\n}\n");

    SimpleVariableContext destination;
    registerValue(destination, "inventory.state", stateType, RuntimeValue{NomadInteger{10}});

    loadVariableContext(&runtime, destination, root);

    BOOST_TEST(getValue(destination, "inventory.state").getIntegerValue() == 20);
}

BOOST_AUTO_TEST_CASE(variable_persistence_round_trips_uninitialized_enums_as_blank_strings)
{
    Runtime runtime;
    const auto stateId = runtime.createEnum("State", {{"idle", 1}, {"walking", 2}});
    BOOST_REQUIRE(stateId.isValid());
    const auto* stateType = runtime.getType(stateId).value();
    BOOST_REQUIRE(stateType != nullptr);

    SimpleVariableContext source;
    registerValue(source, "inventory.state", stateType, RuntimeValue{NomadInteger{0}});

    boost::json::object root;
    BOOST_REQUIRE(saveVariableContext(&runtime, source, root));
    BOOST_REQUIRE(root.at("inventory.state").is_string());
    BOOST_TEST(root.at("inventory.state").as_string() == "");
    BOOST_TEST(toPrettyJson(root) == "{\n    \"inventory.state\": \"\"\n}\n");

    SimpleVariableContext destination;
    registerValue(destination, "inventory.state", stateType, RuntimeValue{NomadInteger{1}});

    loadVariableContext(&runtime, destination, root);

    BOOST_TEST(getValue(destination, "inventory.state").getIntegerValue() == 0);
}

BOOST_AUTO_TEST_CASE(variable_persistence_resolves_enum_names_against_the_destination_type)
{
    Runtime runtime;
    const auto stateId = runtime.createEnum("State", {{"idle", 1}, {"walking", 2}});
    const auto directionId = runtime.createEnum("Direction", {{"idle", 10}, {"right", 20}});
    BOOST_REQUIRE(stateId.isValid());
    BOOST_REQUIRE(directionId.isValid());
    const auto* stateType = runtime.getType(stateId).value();
    const auto* directionType = runtime.getType(directionId).value();
    BOOST_REQUIRE(stateType != nullptr);
    BOOST_REQUIRE(directionType != nullptr);

    SimpleVariableContext context;
    registerValue(context, "inventory.state", stateType, RuntimeValue{NomadInteger{1}});
    registerValue(context, "inventory.direction", directionType, RuntimeValue{NomadInteger{20}});

    const auto root = parseObject(R"({
        "inventory.state": "walking",
        "inventory.direction": "idle"
    })");

    loadVariableContext(&runtime, context, root);

    BOOST_TEST(getValue(context, "inventory.state").getIntegerValue() == 2);
    BOOST_TEST(getValue(context, "inventory.direction").getIntegerValue() == 10);
}

BOOST_AUTO_TEST_CASE(variable_persistence_rejects_invalid_enum_names_and_numeric_values)
{
    Runtime runtime;
    const auto stateId = runtime.createEnum("State", {{"idle", 1}, {"walking", 2}});
    BOOST_REQUIRE(stateId.isValid());
    const auto* stateType = runtime.getType(stateId).value();
    BOOST_REQUIRE(stateType != nullptr);

    SimpleVariableContext context;
    registerValue(context, "inventory.unknown", stateType, RuntimeValue{NomadInteger{1}});
    registerValue(context, "inventory.numeric", stateType, RuntimeValue{NomadInteger{2}});

    const auto root = parseObject(R"({
        "inventory.unknown": "running",
        "inventory.numeric": 1
    })");

    loadVariableContext(&runtime, context, root);

    BOOST_TEST(getValue(context, "inventory.unknown").getIntegerValue() == 1);
    BOOST_TEST(getValue(context, "inventory.numeric").getIntegerValue() == 2);
}

BOOST_AUTO_TEST_CASE(variable_persistence_uses_variable_type_to_read_numbers)
{
    Runtime runtime;
    SimpleVariableContext context;

    registerValue(context, "inventory.wholeFloat", runtime.getIntegerType(), RuntimeValue{NomadInteger{1}});
    registerValue(context, "inventory.fraction", runtime.getIntegerType(), RuntimeValue{NomadInteger{2}});
    registerValue(context, "inventory.tooLarge", runtime.getIntegerType(), RuntimeValue{NomadInteger{3}});
    registerValue(context, "inventory.integer", runtime.getFloatType(), RuntimeValue{NomadFloat{0.0f}});
    registerValue(context, "inventory.double", runtime.getFloatType(), RuntimeValue{NomadFloat{0.0f}});

    const auto root = parseObject(R"({
        "inventory.wholeFloat": 10.0,
        "inventory.fraction": 10.5,
        "inventory.tooLarge": 18446744073709551615,
        "inventory.integer": 10,
        "inventory.double": 2.5
    })");

    loadVariableContext(&runtime, context, root);

    BOOST_TEST(getValue(context, "inventory.wholeFloat").getIntegerValue() == 10);
    BOOST_TEST(getValue(context, "inventory.fraction").getIntegerValue() == 2);
    BOOST_TEST(getValue(context, "inventory.tooLarge").getIntegerValue() == 3);
    BOOST_TEST(getValue(context, "inventory.integer").getFloatValue() == 10.0f);
    BOOST_TEST(getValue(context, "inventory.double").getFloatValue() == 2.5f);
}

BOOST_AUTO_TEST_CASE(variable_persistence_skips_mismatched_missing_and_unknown_values)
{
    Runtime runtime;
    SimpleVariableContext context;

    registerValue(context, "settings.fullscreen", runtime.getBooleanType(), RuntimeValue{false});
    registerValue(context, "settings.name", runtime.getStringType(), RuntimeValue{NomadString{"default"}});
    registerValue(context, "settings.volume", runtime.getFloatType(), RuntimeValue{NomadFloat{0.5f}});
    registerValue(context, "settings.keys.jump", runtime.getStringType(), RuntimeValue{NomadString{"space"}});
    registerValue(context, "settings.missing", runtime.getIntegerType(), RuntimeValue{NomadInteger{7}});

    const auto root = parseObject(R"({
        "settings.fullscreen": 1,
        "settings.name": 12,
        "settings.volume": "loud",
        "settings.keys.jump": 12,
        "settings.unknown.value": 3,
        "other": true
    })");

    loadVariableContext(&runtime, context, root);

    BOOST_TEST(getValue(context, "settings.fullscreen").getBooleanValue() == false);
    BOOST_TEST(NomadString(getValue(context, "settings.name").getStringValue()) == "default");
    BOOST_TEST(getValue(context, "settings.volume").getFloatValue() == 0.5f);
    BOOST_TEST(NomadString(getValue(context, "settings.keys.jump").getStringValue()) == "space");
    BOOST_TEST(getValue(context, "settings.missing").getIntegerValue() == 7);
    BOOST_TEST(context.getVariableId("settings.unknown.value") == VariableId::INVALID);
}

BOOST_AUTO_TEST_CASE(variable_persistence_allows_parent_and_child_variable_names)
{
    Runtime runtime;
    SimpleVariableContext context;
    registerValue(context, "inventory.weapon", runtime.getStringType(), RuntimeValue{NomadString{"sword"}});
    registerValue(context, "inventory.weapon.attack", runtime.getIntegerType(), RuntimeValue{NomadInteger{2}});

    boost::json::object root;
    BOOST_REQUIRE(saveVariableContext(&runtime, context, root));
    BOOST_TEST(root.at("inventory.weapon").as_string() == "sword");
    BOOST_TEST(root.at("inventory.weapon.attack").as_int64() == 2);

    SimpleVariableContext destination;
    registerValue(destination, "inventory.weapon", runtime.getStringType(), RuntimeValue{NomadString{"none"}});
    registerValue(destination, "inventory.weapon.attack", runtime.getIntegerType(), RuntimeValue{NomadInteger{0}});

    loadVariableContext(&runtime, destination, root);

    BOOST_TEST(NomadString(getValue(destination, "inventory.weapon").getStringValue()) == "sword");
    BOOST_TEST(getValue(destination, "inventory.weapon.attack").getIntegerValue() == 2);
}

BOOST_AUTO_TEST_CASE(variable_persistence_rejects_unsupported_and_non_finite_values)
{
    Runtime runtime;
    boost::json::object root;
    const auto stateId = runtime.createEnum("State", {{"idle", 1}});
    BOOST_REQUIRE(stateId.isValid());
    const auto* stateType = runtime.getType(stateId).value();
    BOOST_REQUIRE(stateType != nullptr);

    BOOST_TEST(!writeJsonVariable(&runtime, root, "inventory.id", runtime.getVoidType(), RuntimeValue{}));
    BOOST_TEST(!writeJsonVariable(
        &runtime,
        root,
        "inventory.state",
        stateType,
        RuntimeValue{NomadInteger{2}}
    ));
    BOOST_TEST(!writeJsonVariable(
        &runtime,
        root,
        "inventory.speed",
        runtime.getFloatType(),
        RuntimeValue{std::numeric_limits<NomadFloat>::infinity()}
    ));
    BOOST_TEST(!writeJsonVariable(&runtime, root, "inventory..gold", runtime.getIntegerType(), RuntimeValue{NomadInteger{1}}));
    BOOST_TEST(root.empty());
}

BOOST_AUTO_TEST_CASE(variable_persistence_validates_save_names)
{
    BOOST_TEST(makeSaveFileName("slot1").value() == "slot1.json");
    BOOST_TEST(makeSaveFileName("slot 1").value() == "slot 1.json");
    BOOST_TEST(makeSaveFileName("slot1.json").value() == "slot1.json");
    BOOST_TEST(makeSaveFileName("partie-\xC3\xA9t\xC3\xA9").value() == "partie-\xC3\xA9t\xC3\xA9.json");

    BOOST_TEST(!makeSaveFileName("").has_value());
    BOOST_TEST(!makeSaveFileName(".").has_value());
    BOOST_TEST(!makeSaveFileName("..").has_value());
    BOOST_TEST(!makeSaveFileName("../slot").has_value());
    BOOST_TEST(!makeSaveFileName("..\\slot").has_value());
    BOOST_TEST(!makeSaveFileName("C:slot").has_value());
    BOOST_TEST(!makeSaveFileName("/slot").has_value());
    BOOST_TEST(!makeSaveFileName("slot.").has_value());
    BOOST_TEST(!makeSaveFileName("slot ").has_value());
    BOOST_TEST(!makeSaveFileName("slot\n").has_value());
}

BOOST_AUTO_TEST_CASE(variable_persistence_writes_and_reads_json_files)
{
    const TestDirectory directory("nomad_variable_persistence_test");
    const auto fileName = directory.file("save.json");

    const auto root = parseObject(R"({"inventory.gold": 25})");

    BOOST_TEST(!pathExists(fileName));
    BOOST_REQUIRE(writeJsonFile(fileName, root));
    BOOST_TEST(pathExists(fileName));
    BOOST_TEST(!pathExists(fileName + ".tmp"));

    // Overwrites an existing file.
    const auto updatedRoot = parseObject(R"({"inventory.gold": 30})");
    BOOST_REQUIRE(writeJsonFile(fileName, updatedRoot));

    const auto loaded = readJsonFile(fileName);

    BOOST_REQUIRE(loaded.has_value());
    BOOST_TEST(loaded->at("inventory.gold").as_int64() == 30);
}

BOOST_AUTO_TEST_CASE(variable_persistence_rejects_invalid_json_files)
{
    const TestDirectory directory("nomad_variable_persistence_test");

    const auto malformedFileName = directory.file("malformed.json");
    std::ofstream(malformedFileName) << R"({"inventory.gold": )";

    const auto arrayFileName = directory.file("array.json");
    std::ofstream(arrayFileName) << "[1, 2, 3]";

    BOOST_TEST(!readJsonFile(malformedFileName).has_value());
    BOOST_TEST(!readJsonFile(arrayFileName).has_value());
    BOOST_TEST(!readJsonFile(directory.file("missing.json")).has_value());
}
