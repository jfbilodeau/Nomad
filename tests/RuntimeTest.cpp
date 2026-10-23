// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include "nomad/game/Entity.hpp"
#include "nomad/game/EntityVariableContext.hpp"
#include "nomad/game/Scene.hpp"
#include "nomad/script/Documentation.hpp"
#include "nomad/script/Runtime.hpp"
#include "nomad/script/Type.hpp"
#include "nomad/script/Variable.hpp"

#include <sstream>

using namespace nomad;

BOOST_AUTO_TEST_CASE(bare_runtime_has_core_string_natives_without_standard_native_functions)
{
    Runtime runtime;

    for (const auto* name : {"toFloat", "toInt", "log.info", "math.floor"}) {
        BOOST_TEST_CONTEXT(name) {
            BOOST_TEST(runtime.getNativeFunctionId(name).isInvalid());
            BOOST_TEST(runtime.getCallableId(name).isInvalid());
        }
    }
    BOOST_TEST(runtime.getNativeMemberId(runtime.getStringType(), "length") != NOMAD_INVALID_ID);
    BOOST_TEST(runtime.getNativeMemberId(runtime.getStringType(), "toUpper") != NOMAD_INVALID_ID);
    BOOST_TEST(runtime.getNativeMemberId(runtime.getStringType(), "contains") != NOMAD_INVALID_ID);
}

BOOST_AUTO_TEST_CASE(runtime_owns_registered_functions_and_exposes_observer_views)
{
    {
        Runtime runtime;

        const auto firstFunctionId = runtime.registerScriptFunction("first", "first.nomad", "");
        const auto secondFunctionId = runtime.registerScriptFunction("second", "second.nomad", "");

        BOOST_TEST(firstFunctionId.isValid());
        BOOST_TEST(secondFunctionId.id == firstFunctionId.id + 1);
        BOOST_TEST(runtime.getFunctionCount() == 2U);
        BOOST_TEST(runtime.getScriptFunction(firstFunctionId) != nullptr);
        BOOST_TEST(runtime.getScriptFunction(secondFunctionId) != nullptr);
        BOOST_TEST((runtime.getFunctionId("first") == firstFunctionId));
        BOOST_TEST(runtime.getFunctionName(secondFunctionId).value() == "second");

        std::vector<ScriptFunction*> functions;
        runtime.getFunctions(functions);

        BOOST_TEST(functions.size() == 2U);
        BOOST_TEST(functions[0] == runtime.getScriptFunction(firstFunctionId));
        BOOST_TEST(functions[1] == runtime.getScriptFunction(secondFunctionId));
    }
}

BOOST_AUTO_TEST_CASE(runtime_rejects_invalid_and_duplicate_function_ids)
{
    Runtime runtime;

    const auto functionId = runtime.registerScriptFunction("function", "function.nomad", "");

    BOOST_TEST(functionId.isValid());
    BOOST_TEST(runtime.registerScriptFunction("function", "duplicate.nomad", "").isInvalid());
    BOOST_TEST(runtime.getFunctionCount() == 1U);
    BOOST_TEST(runtime.getScriptFunction(CallableId::INVALID) == nullptr);
    BOOST_TEST(runtime.getScriptFunction(CallableId{-2}) == nullptr);
    BOOST_TEST(runtime.getScriptFunction(CallableId{1}) == nullptr);
    BOOST_TEST(!runtime.getFunctionName(CallableId::INVALID).has_value());
    BOOST_TEST(!runtime.getFunctionName(CallableId{-2}).has_value());
    BOOST_TEST(!runtime.getFunctionName(CallableId{1}).has_value());
}

BOOST_AUTO_TEST_CASE(runtime_source_lookup_ignores_unknown_ranges_and_prefers_inner_functions)
{
    Runtime runtime;
    const auto outerId = runtime.registerScriptFunction("outer", "source.nomad", "");
    const auto innerId = runtime.registerScriptFunction("inner", "source.nomad", "");
    const auto unparsedId = runtime.registerScriptFunction("unparsed", "source.nomad", "");
    const auto* unparsed = runtime.getScriptFunction(unparsedId);

    BOOST_TEST(unparsed->getSourceStartLine() == NOMAD_INVALID_INDEX);
    BOOST_TEST(unparsed->getSourceEndLine() == NOMAD_INVALID_INDEX);
    BOOST_TEST(runtime.getFunctionAtLine("source.nomad", 1) == nullptr);
    BOOST_TEST(runtime.getFunctionAtLine("source.nomad", NOMAD_INVALID_INDEX) == nullptr);

    auto* outer = runtime.getScriptFunction(outerId);
    auto* inner = runtime.getScriptFunction(innerId);
    outer->setSourceLineRange(1, 4);
    inner->setSourceLineRange(2, 3);
    inner->setParentId(outerId);
    BOOST_TEST(runtime.getFunctionAtLine("source.nomad", 1) == outer);
    BOOST_TEST(runtime.getFunctionAtLine("source.nomad", 2) == inner);
    BOOST_TEST(runtime.getFunctionAtLine("source.nomad", 3) == inner);
    BOOST_TEST(runtime.getFunctionAtLine("source.nomad", 4) == outer);
    BOOST_TEST(runtime.getFunctionAtLine("source.nomad", 5) == nullptr);
    BOOST_TEST(runtime.getFunctionAtLine("other.nomad", 2) == nullptr);

    inner->setSourceLineRange(1, 4);
    BOOST_TEST(runtime.getFunctionAtLine("source.nomad", 2) == inner);
}

BOOST_AUTO_TEST_CASE(runtime_registers_strongly_typed_enums_with_explicit_values)
{
    Runtime runtime;

    const auto* bodyType = runtime.registerEnum(
        "body",
        {
            {"static", 3},
            {"dynamic", 7},
            {"kinematic", 11},
        }
    );

    BOOST_REQUIRE(bodyType != nullptr);
    BOOST_TEST(runtime.getTypeByName("body") == bodyType);
    BOOST_TEST(bodyType->getValueCount() == 3U);
    BOOST_TEST(bodyType->getValue(0).name == "static");
    BOOST_TEST(bodyType->getValue(0).value == 3);
    BOOST_TEST(bodyType->toString(RuntimeValue{NomadInteger{7}}) == "dynamic");
    BOOST_TEST(bodyType->toString(RuntimeValue{NomadInteger{99}}) == "<invalid body value>");

    NomadInteger value = 0;
    BOOST_TEST(bodyType->getValueByName("kinematic", value));
    BOOST_TEST(value == 11);
    BOOST_TEST(!bodyType->getValueByName("missing", value));

    NomadString name;
    BOOST_TEST(bodyType->getNameByValue(3, name));
    BOOST_TEST(name == "static");
    BOOST_TEST(!bodyType->getNameByValue(99, name));

    const auto staticId = runtime.getConstantId("body.static");
    BOOST_REQUIRE(staticId != NOMAD_INVALID_ID);
    BOOST_TEST(runtime.getConstantType(staticId) == bodyType);

    RuntimeValue staticValue;
    runtime.getConstantValue(staticId, staticValue);
    BOOST_TEST(staticValue.getIntegerValue() == 3);
}

BOOST_AUTO_TEST_CASE(runtime_enum_types_only_support_same_type_equality)
{
    Runtime runtime;
    const auto* bodyType = runtime.registerEnum("body", {{"static", 1}, {"dynamic", 2}});
    const auto* cardinalType = runtime.registerEnum("cardinal", {{"north", 1}, {"south", 2}});

    BOOST_REQUIRE(bodyType != nullptr);
    BOOST_REQUIRE(cardinalType != nullptr);
    BOOST_TEST(!bodyType->sameType(cardinalType));
    BOOST_TEST(!bodyType->sameType(runtime.getIntegerType()));
    BOOST_TEST(
        runtime.getBinaryOperatorResultType(BinaryOperator::EqualEqual, bodyType, bodyType) ==
        runtime.getBooleanType()
    );
    BOOST_TEST(
        runtime.getBinaryOperatorResultType(BinaryOperator::BangEqual, bodyType, bodyType) ==
        runtime.getBooleanType()
    );
    BOOST_TEST(runtime.getBinaryOperatorResultType(BinaryOperator::EqualEqual, bodyType, cardinalType) == nullptr);
    BOOST_TEST(
        runtime.getBinaryOperatorResultType(BinaryOperator::EqualEqual, bodyType, runtime.getIntegerType()) == nullptr
    );
    BOOST_TEST(runtime.getBinaryOperatorResultType(BinaryOperator::Plus, bodyType, bodyType) == nullptr);

    RuntimeValue result;
    BOOST_TEST(
        runtime.foldBinary(
            BinaryOperator::EqualEqual,
            bodyType,
            RuntimeValue{NomadInteger{2}},
            bodyType,
            RuntimeValue{NomadInteger{2}},
            result
        )
    );
    BOOST_TEST(result.getBooleanValue());
}

BOOST_AUTO_TEST_CASE(runtime_rejects_invalid_enum_definitions_without_partial_registration)
{
    Runtime runtime;

    BOOST_TEST(runtime.registerEnum("", {{"value", 1}}) == nullptr);
    BOOST_TEST(runtime.registerEnum("empty", {}) == nullptr);
    BOOST_TEST(runtime.registerEnum("unnamed", {{"", 1}}) == nullptr);
    BOOST_TEST(runtime.registerEnum("duplicateName", {{"value", 1}, {"value", 2}}) == nullptr);
    BOOST_TEST(runtime.registerEnum("duplicateValue", {{"first", 1}, {"second", 1}}) == nullptr);
    BOOST_TEST(runtime.registerEnum("int", {{"value", 1}}) == nullptr);

    BOOST_REQUIRE(runtime.registerEnum("valid", {{"first", 1}}) != nullptr);
    BOOST_TEST(runtime.registerEnum("valid", {{"second", 2}}) == nullptr);
    BOOST_TEST(runtime.getConstantId("valid.second") == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.getTypeByName("duplicateName") == nullptr);
    BOOST_TEST(runtime.getConstantId("duplicateName.value") == NOMAD_INVALID_ID);
}

BOOST_AUTO_TEST_CASE(runtime_registers_nominal_opaque_id_types)
{
    Runtime runtime;

    const auto* soundType = runtime.registerOpaqueIdType("sound", NomadDoc("A loaded sound effect."));
    const auto* musicType = runtime.registerOpaqueIdType("music", NomadDoc("A music stream."));

    BOOST_REQUIRE(soundType != nullptr);
    BOOST_REQUIRE(musicType != nullptr);
    BOOST_TEST(runtime.getTypeByName("sound") == soundType);
    BOOST_TEST(runtime.getTypeByName("music") == musicType);
    BOOST_TEST(soundType->getDocumentation() == "A loaded sound effect.");
    BOOST_TEST(!soundType->sameType(musicType));
    BOOST_TEST(!soundType->sameType(runtime.getTypeByName("id")));

    RuntimeValue source;
    source.setIdValue(42);
    RuntimeValue destination;
    soundType->copyValue(source, destination);

    BOOST_TEST(destination.getIdValue() == 42);
    BOOST_TEST(soundType->toString(destination) == "sound(42)");
}

BOOST_AUTO_TEST_CASE(runtime_rejects_invalid_opaque_id_types)
{
    Runtime runtime;

    BOOST_TEST(runtime.registerOpaqueIdType("", NomadDoc("Invalid.")) == nullptr);
    BOOST_TEST(runtime.registerOpaqueIdType("int", NomadDoc("Duplicate.")) == nullptr);
    BOOST_REQUIRE(runtime.registerOpaqueIdType("resource", NomadDoc("A resource.")) != nullptr);
    BOOST_TEST(runtime.registerOpaqueIdType("resource", NomadDoc("Duplicate.")) == nullptr);
}

BOOST_AUTO_TEST_CASE(documentation_includes_registered_type_descriptions)
{
    Runtime runtime;
    BOOST_REQUIRE(
        runtime.registerOpaqueIdType("resource", NomadDoc("An opaque resource handle.")) != nullptr
    );
    BOOST_REQUIRE(runtime.registerOpaqueIdType("undocumented", NomadDoc("")) != nullptr);

    std::ostringstream output;
    generateTypeDocumentation(&runtime, output);

    const auto documentation = output.str();
    BOOST_TEST(documentation.find("## Types") != NomadString::npos);
    BOOST_TEST(documentation.find("### `resource`") != NomadString::npos);
    BOOST_TEST(documentation.find("An opaque resource handle.") != NomadString::npos);
    BOOST_TEST(documentation.find("undocumented") == NomadString::npos);
}

BOOST_AUTO_TEST_CASE(documentation_renders_function_parameters_as_a_markdown_list)
{
    Runtime runtime;
    const auto* fontType = runtime.registerOpaqueIdType(
        "font",
        NomadDoc("A loaded font at a specific point size.")
    );
    BOOST_REQUIRE(fontType != nullptr);

    runtime.registerNativeFunction(
        "game.loadFont",
        [](VirtualMachine*) {},
        {
            defParameter("fontName", runtime.getStringType(), NomadParamDoc("Name of the font to load.")),
            defParameter("fontSize", runtime.getIntegerType(), NomadParamDoc("Size of the font in point to load.")),
        },
        fontType,
        NomadDoc("Loads a font from a file and returns a font handle.")
    );

    std::ostringstream output;
    generateDocumentation(&runtime, output);
    std::ostringstream grammar;
    generateTextMateGrammar(&runtime, grammar);

    const auto documentation = output.str();
    BOOST_TEST(
        documentation.find(
            "parameters:\n\n"
            "- `fontName:string` - Name of the font to load.\n"
            "- `fontSize:int` - Size of the font in point to load.\n"
        ) != NomadString::npos
    );
    BOOST_TEST(documentation.find("`returns font`") != NomadString::npos);

    const auto grammarText = grammar.str();
    const auto typeScopePosition = grammarText.find("\"storage.type.nomad\"");
    const auto typePatternEndPosition = grammarText.find(")\\\\b", typeScopePosition);
    const auto fontTypePosition = grammarText.find("font", typeScopePosition);
    BOOST_REQUIRE(typeScopePosition != NomadString::npos);
    BOOST_REQUIRE(typePatternEndPosition != NomadString::npos);
    BOOST_TEST(fontTypePosition < typePatternEndPosition);
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
