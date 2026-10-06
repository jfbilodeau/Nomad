// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/game/VariablePersistence.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/VariableContext.hpp>

#include <boost/json.hpp>

#include <filesystem>
#include <fstream>
#include <limits>

using namespace nomad;

namespace {

NomadId registerValue(SimpleVariableContext& context, const NomadString& name, const Type* type, RuntimeValue value) {
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

struct TemporaryDirectory {
    TemporaryDirectory() :
        path(std::filesystem::temp_directory_path() / "nomad_variable_persistence_test")
    {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    [[nodiscard]] NomadString file(const NomadString& name) const {
        return (path / name).string();
    }

    std::filesystem::path path;
};

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
    BOOST_TEST(context.getVariableId("settings.unknown.value") == NOMAD_INVALID_ID);
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

    BOOST_TEST(!writeJsonVariable(&runtime, root, "inventory.id", runtime.getVoidType(), RuntimeValue{}));
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
    const TemporaryDirectory directory;
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
    const TemporaryDirectory directory;

    const auto malformedFileName = directory.file("malformed.json");
    std::ofstream(malformedFileName) << R"({"inventory.gold": )";

    const auto arrayFileName = directory.file("array.json");
    std::ofstream(arrayFileName) << "[1, 2, 3]";

    BOOST_TEST(!readJsonFile(malformedFileName).has_value());
    BOOST_TEST(!readJsonFile(arrayFileName).has_value());
    BOOST_TEST(!readJsonFile(directory.file("missing.json")).has_value());
}
