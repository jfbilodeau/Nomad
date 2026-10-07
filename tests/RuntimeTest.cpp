// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include "nomad/game/Entity.hpp"
#include "nomad/game/EntityVariableContext.hpp"
#include "nomad/game/Scene.hpp"
#include "nomad/script/Runtime.hpp"
#include "nomad/script/Variable.hpp"

using namespace nomad;

BOOST_AUTO_TEST_CASE(runtime_owns_registered_functions_and_exposes_observer_views)
{
    {
        Runtime runtime;

        const auto firstFunctionId = runtime.registerFunction("first", "first.nomad", "");
        const auto secondFunctionId = runtime.registerFunction("second", "second.nomad", "");

        BOOST_TEST(firstFunctionId == 0);
        BOOST_TEST(secondFunctionId == 1);
        BOOST_TEST(runtime.getFunctionCount() == 2U);
        BOOST_TEST(runtime.getFunction(firstFunctionId) != nullptr);
        BOOST_TEST(runtime.getFunction(secondFunctionId) != nullptr);
        BOOST_TEST(runtime.getFunctionId("first") == firstFunctionId);
        BOOST_TEST(runtime.getFunctionName(secondFunctionId).value() == "second");

        std::vector<Function*> functions;
        runtime.getFunctions(functions);

        BOOST_TEST(functions.size() == 2U);
        BOOST_TEST(functions[0] == runtime.getFunction(firstFunctionId));
        BOOST_TEST(functions[1] == runtime.getFunction(secondFunctionId));
    }
}

BOOST_AUTO_TEST_CASE(runtime_rejects_invalid_and_duplicate_function_ids)
{
    Runtime runtime;

    const auto functionId = runtime.registerFunction("function", "function.nomad", "");

    BOOST_TEST(functionId == 0);
    BOOST_TEST(runtime.registerFunction("function", "duplicate.nomad", "") == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.getFunctionCount() == 1U);
    BOOST_TEST(runtime.getFunction(NOMAD_INVALID_ID) == nullptr);
    BOOST_TEST(runtime.getFunction(-2) == nullptr);
    BOOST_TEST(runtime.getFunction(1) == nullptr);
    BOOST_TEST(!runtime.getFunctionName(NOMAD_INVALID_ID).has_value());
    BOOST_TEST(!runtime.getFunctionName(-2).has_value());
    BOOST_TEST(!runtime.getFunctionName(1).has_value());
}

BOOST_AUTO_TEST_CASE(id_validation_helpers_distinguish_values_from_ranges)
{
    BOOST_TEST(isValidId(NOMAD_ID_MIN));
    BOOST_TEST(isValidId(NOMAD_ID_MAX - 1));
    BOOST_TEST(isInvalidId(NOMAD_INVALID_ID));
    BOOST_TEST(isInvalidId(-2));
    BOOST_TEST(isInvalidId(NOMAD_ID_MAX));

    BOOST_TEST(isIdInRange(0, 1U));
    BOOST_TEST(!isIdInRange(1, 1U));
    BOOST_TEST(!isIdInRange(NOMAD_INVALID_ID, 1U));
    BOOST_TEST(!isIdInRange(NOMAD_ID_MAX, static_cast<NomadIndex>(NOMAD_ID_MAX + 1)));
    BOOST_TEST(!isIdOutOfRange(0, 1U));
    BOOST_TEST(isIdOutOfRange(1, 1U));
    BOOST_TEST(isIdOutOfRange(NOMAD_INVALID_ID, 1U));
}

BOOST_AUTO_TEST_CASE(runtime_context_variable_ids_are_global_and_stable)
{
    Runtime runtime;

    auto firstContext = std::make_unique<SimpleVariableContext>();
    firstContext->registerVariable("first.value", runtime.getIntegerType());
    const auto firstContextId = runtime.registerVariableContext("first", "first.", std::move(firstContext));

    auto secondContext = std::make_unique<SimpleVariableContext>();
    secondContext->registerVariable("second.value", runtime.getIntegerType());
    const auto secondContextId = runtime.registerVariableContext("second", "second.", std::move(secondContext));

    BOOST_REQUIRE(firstContextId != NOMAD_INVALID_ID);
    BOOST_REQUIRE(secondContextId != NOMAD_INVALID_ID);

    const auto firstVariableId = runtime.registerContextVariable(
        firstContextId,
        "first.value",
        runtime.getIntegerType()
    );
    const auto secondVariableId = runtime.registerContextVariable(
        secondContextId,
        "second.value",
        runtime.getIntegerType()
    );

    BOOST_REQUIRE(firstVariableId != NOMAD_INVALID_ID);
    BOOST_REQUIRE(secondVariableId != NOMAD_INVALID_ID);
    BOOST_TEST(firstVariableId != secondVariableId);
    BOOST_TEST(runtime.getContextVariableId("first.value") == firstVariableId);
    BOOST_TEST(runtime.getContextVariableName(firstVariableId) == "first.value");
    BOOST_TEST(runtime.getContextVariableName(secondVariableId) == "second.value");
    BOOST_TEST(runtime.getContextVariableType(firstVariableId) == runtime.getIntegerType());
    BOOST_TEST(runtime.getContextVariableType(secondVariableId) == runtime.getIntegerType());

    runtime.setContextVariableValue(firstVariableId, RuntimeValue{NomadInteger{17}});
    runtime.setContextVariableValue(secondVariableId, RuntimeValue{NomadInteger{29}});

    RuntimeValue firstValue;
    RuntimeValue secondValue;
    runtime.getContextVariableValue(firstVariableId, firstValue);
    runtime.getContextVariableValue(secondVariableId, secondValue);

    BOOST_TEST(firstValue.getIntegerValue() == 17);
    BOOST_TEST(secondValue.getIntegerValue() == 29);
}

BOOST_AUTO_TEST_CASE(runtime_stores_character_pointer_as_string_context_variable)
{
    Runtime runtime;

    const auto contextId = runtime.registerVariableContext("inventory", "inventory.", std::make_unique<SimpleVariableContext>());
    const auto variableId = runtime.registerContextVariable(contextId, "inventory.name", runtime.getStringType());

    const NomadChar* name = "Nomad";
    runtime.setStringContextVariableValue(variableId, name);

    NomadString value;
    runtime.getStringContextVariableValue(variableId, value);

    BOOST_TEST(value == "Nomad");
}

BOOST_AUTO_TEST_CASE(runtime_assigns_distinct_global_ids_to_mirrored_entity_variables)
{
    Runtime runtime;

    auto thisContext = std::make_unique<ThisEntityVariableContext>(nullptr);
    auto* thisContextPointer = thisContext.get();
    auto otherContext = std::make_unique<OtherEntityVariableContext>(thisContextPointer);

    const auto thisContextId = runtime.registerVariableContext(
        THIS_ENTITY_VARIABLE_CONTEXT,
        THIS_ENTITY_VARIABLE_PREFIX,
        std::move(thisContext)
    );
    const auto otherContextId = runtime.registerVariableContext(
        OTHER_ENTITY_VARIABLE_CONTEXT,
        OTHER_ENTITY_VARIABLE_PREFIX,
        std::move(otherContext)
    );

    const auto thisVariableId = runtime.registerContextVariable(
        thisContextId,
        "this.health",
        runtime.getIntegerType()
    );
    const auto otherVariableId = runtime.registerContextVariable(
        otherContextId,
        "other.health",
        runtime.getIntegerType()
    );

    BOOST_REQUIRE(thisVariableId != NOMAD_INVALID_ID);
    BOOST_REQUIRE(otherVariableId != NOMAD_INVALID_ID);
    BOOST_TEST(thisVariableId != otherVariableId);
    BOOST_TEST(runtime.getContextVariableId("this.health") == thisVariableId);
    BOOST_TEST(runtime.getContextVariableId("other.health") == otherVariableId);
    BOOST_TEST(runtime.getContextVariableName(thisVariableId) == "this.health");
    BOOST_TEST(runtime.getContextVariableName(otherVariableId) == "other.health");
}

BOOST_AUTO_TEST_CASE(scene_owns_and_removes_entities_without_game_initialization)
{
    VariableMap variableMap;
    Scene scene(1, &variableMap);

    auto entity = std::make_unique<Entity>(&scene, &variableMap, 2, 0.0f, 0.0f, 0);
    const auto entityPtr = entity.get();

    scene.addEntity(std::move(entity));
    BOOST_TEST(scene.getEntityById(2) == entityPtr);

    scene.removeEntity(entityPtr);
    BOOST_TEST(scene.getEntityById(2) == nullptr);
}

BOOST_AUTO_TEST_CASE(scene_rejects_invalid_animated_tile_ranges)
{
    VariableMap variableMap;
    Scene scene(1, &variableMap);

    BOOST_TEST(!scene.addAnimatedTile("missing", 4, 1));
    BOOST_TEST(!scene.addAnimatedTile("", 4, 1));
    BOOST_TEST(!scene.addAnimatedTile("missing", 0, 1));
    BOOST_TEST(!scene.addAnimatedTile("missing", 4, 0));
}

BOOST_AUTO_TEST_CASE(animated_tile_resolves_frames_at_the_configured_update_interval)
{
    const AnimatedTileDefinition animation{1408, 4, 2, 10};

    BOOST_TEST(animation.resolveTileId(1408, 10) == 1408U);
    BOOST_TEST(animation.resolveTileId(1408, 11) == 1408U);
    BOOST_TEST(animation.resolveTileId(1408, 12) == 1409U);
    BOOST_TEST(animation.resolveTileId(1408, 14) == 1410U);
    BOOST_TEST(animation.resolveTileId(1408, 16) == 1411U);
    BOOST_TEST(animation.resolveTileId(1408, 18) == 1408U);
    BOOST_TEST(animation.resolveTileId(1409, 12) == 1409U);
}
