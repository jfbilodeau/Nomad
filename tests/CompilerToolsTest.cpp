// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/CompilerTools.hpp>

#include <nomad/game/Alignment.hpp>
#include <nomad/game/BodyType.hpp>
#include <nomad/game/Cardinal.hpp>
#include <nomad/game/Game.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>

#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

#include <filesystem>
#include <utility>

using namespace nomad;
using namespace nomad::test;

BOOST_AUTO_TEST_SUITE(compilation)

BOOST_AUTO_TEST_CASE(checks_a_single_source_file)
{
    TestDirectory directory("nomad_compilation_test_single");
    const auto source = directory.write("answer.nomad", "return 42");

    const auto result = checkPath(source);

    BOOST_TEST(result.succeeded());
    BOOST_TEST(result.errorCount == 0U);
    BOOST_TEST(result.diagnostics.empty());
}

BOOST_AUTO_TEST_CASE(checks_source_files_recursively)
{
    TestDirectory directory("nomad_compilation_test_directory");
    directory.write("init.nomad", "return functions.answer");
    directory.write("functions/answer.nomad", "return 42");

    const auto result = checkPath(directory.getPath());

    BOOST_TEST(result.succeeded());
    BOOST_TEST(result.errorCount == 0U);
}

BOOST_AUTO_TEST_CASE(checks_multiple_script_roots_in_one_compilation)
{
    TestDirectory directory("nomad_compilation_test_multiple_roots");
    const auto scripts = directory.write("scripts/init.nomad", "return helpers.answer").parent_path();
    const auto mods = directory.write("mods/helpers/answer.nomad", "return 42").parent_path().parent_path();

    const auto result = checkPaths({scripts, mods});

    BOOST_TEST(result.succeeded());
    BOOST_TEST(result.errorCount == 0U);
}

BOOST_AUTO_TEST_CASE(validates_project_entry_function_presence_and_signature)
{
    TestDirectory directory("nomad_compilation_test_entry");
    Runtime runtime;
    auto result = checkPath(directory.write("other.nomad", "return"), &runtime);

    validateEntryFunction(result, &runtime, "init", directory.getPath() / "nomad.toml");

    BOOST_TEST(!result.succeeded());
    BOOST_REQUIRE(!result.diagnostics.empty());
    BOOST_TEST(result.diagnostics.back().message == "Configured entry function 'init' was not found");
    BOOST_TEST(result.diagnostics.back().sourceName == (directory.getPath() / "nomad.toml").generic_string());

    Runtime parameterRuntime;
    result = checkPath(directory.write("init.nomad", "params value:int\n"), &parameterRuntime);
    validateEntryFunction(result, &parameterRuntime, "init", directory.getPath() / "nomad.toml");

    BOOST_TEST(!result.succeeded());
    BOOST_TEST(result.diagnostics.back().message == "Configured entry function 'init' must not have parameters");

    Runtime returnRuntime;
    result = checkPath(directory.write("init.nomad", "return 1\n"), &returnRuntime);
    validateEntryFunction(result, &returnRuntime, "init", directory.getPath() / "nomad.toml");

    BOOST_TEST(!result.succeeded());
    BOOST_TEST(result.diagnostics.back().message == "Configured entry function 'init' must return void");

    Runtime validRuntime;
    result = checkPath(directory.write("init.nomad", "return\n"), &validRuntime);
    validateEntryFunction(result, &validRuntime, "init", directory.getPath() / "nomad.toml");

    BOOST_TEST(result.succeeded());
}

BOOST_AUTO_TEST_CASE(reports_compiler_diagnostics_without_executing_source)
{
    TestDirectory directory("nomad_compilation_test_diagnostic");
    const auto source = directory.write("broken.nomad", "return missing");

    const auto result = checkPath(source);

    BOOST_TEST(!result.succeeded());
    BOOST_TEST(result.errorCount == 1U);
    BOOST_REQUIRE(!result.diagnostics.empty());
    BOOST_TEST(result.diagnostics.front().sourceName == source.generic_string());
    BOOST_TEST(result.diagnostics.front().message.find("Unknown identifier") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(reports_missing_paths)
{
    TestDirectory directory("nomad_compilation_test_missing");
    const auto missing = directory.getPath() / "missing.nomad";

    const auto result = checkPath(missing);

    BOOST_TEST(!result.succeeded());
    BOOST_TEST(result.errorCount == 1U);
    BOOST_REQUIRE(!result.diagnostics.empty());
    BOOST_TEST(result.diagnostics.front().message == "Path does not exist");
}

BOOST_AUTO_TEST_CASE(dumps_generated_instructions_without_executing_source)
{
    TestDirectory directory("nomad_compilation_test_dump");
    const auto source = directory.write("answer.nomad", "return 42");

    const auto result = dumpInstructions(source);

    BOOST_REQUIRE(result.compilation.succeeded());
    BOOST_TEST(result.instructions.find("function answer") != NomadString::npos);
    BOOST_TEST(result.instructions.find("return") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(dumps_only_the_selected_function)
{
    TestDirectory directory("nomad_compilation_test_dump_selected");
    directory.write("first.nomad", "return 1");
    directory.write("second.nomad", "return 2");

    const auto result = dumpInstructions(directory.getPath(), NomadString{"second"});

    BOOST_REQUIRE(result.compilation.succeeded());
    BOOST_TEST(result.instructions.find("function second") != NomadString::npos);
    BOOST_TEST(result.instructions.find("function first") == NomadString::npos);
}

BOOST_AUTO_TEST_CASE(reports_unknown_dump_functions)
{
    TestDirectory directory("nomad_compilation_test_dump_unknown");
    const auto source = directory.write("answer.nomad", "return 42");

    const auto result = dumpInstructions(source, NomadString{"missing"});

    BOOST_TEST(!result.compilation.succeeded());
    BOOST_REQUIRE(!result.compilation.diagnostics.empty());
    BOOST_TEST(result.compilation.diagnostics.back().message == "Unknown function 'missing'");
    BOOST_TEST(result.instructions.empty());
}

BOOST_AUTO_TEST_CASE(generates_documentation_without_executing_source)
{
    TestDirectory directory("nomad_compilation_test_documentation");
    const auto source = directory.write(
        "answer.nomad",
        "enum State\n"
        "    idle\n"
        "    walking\n"
        "end\n"
        "const ANSWER = 42\n"
        "return ANSWER"
    );

    const auto result = generateDocumentationForPath(source);

    BOOST_REQUIRE(result.compilation.succeeded());
    BOOST_TEST(result.documentation.find("# Nomad") != NomadString::npos);
    BOOST_TEST(result.documentation.find("## Enums") != NomadString::npos);
    BOOST_TEST(result.documentation.find("## Debugging") == NomadString::npos);
    BOOST_TEST(result.documentation.find("## Intrinsic Members") != NomadString::npos);
    BOOST_TEST(result.documentation.find("## Functions") != NomadString::npos);
    BOOST_TEST(result.documentation.find("[Constants](#constants)") == NomadString::npos);
    BOOST_TEST(result.documentation.find("[Debugging](#debugging)") == NomadString::npos);
    BOOST_TEST(result.documentation.find("### `string`") != NomadString::npos);
    BOOST_TEST(result.documentation.find("#### `length` (*read-only property*)") != NomadString::npos);
    BOOST_TEST(result.documentation.find("#### `toUpper` (*method*)") != NomadString::npos);
    BOOST_TEST(result.documentation.find("### State") != NomadString::npos);
    BOOST_TEST(result.documentation.find("* `State.idle`") != NomadString::npos);
    BOOST_TEST(result.documentation.find("* `State.walking`") != NomadString::npos);
    BOOST_TEST(result.documentation.find("State.idle:State") == NomadString::npos);
    BOOST_TEST(result.documentation.find("ANSWER:int = 42") != NomadString::npos);
    BOOST_TEST(result.documentation.find("[Instructions](#instructions)") == NomadString::npos);
}

BOOST_AUTO_TEST_CASE(headless_game_exposes_the_engine_api_to_the_compiler)
{
    TestDirectory directory("nomad_compilation_test_headless");
    directory.write(
        "start.nomad",
        "window.setTitle \"Nomad\"\n"
        "font = game.loadFont \"font.ttf\" 16\n"
        "this.text.font = font\n"
        "this.setText font \"Nomad\"\n"
        "this.text.alignment = alignment.topLeft\n"
        "this.body.circle body.dynamic 1.0\n"
        "return cardinal.north == cardinal.north\n"
    );

    // A bare runtime knows only the language built-ins, so the engine symbols are unresolved.
    const auto withoutEngine = checkPath(directory.getPath());
    BOOST_TEST(!withoutEngine.succeeded());

    GameOptions options;
    options.resourcePath = directory.getPath().generic_string();

    auto game = Game::createHeadless(std::move(options));
    auto* runtime = game->getRuntime();

    // The dummy drivers still produce a real renderer, so the engine is fully constructed.
    BOOST_TEST(game->getCanvas() != nullptr);
    BOOST_TEST(game->getResources() != nullptr);

    const auto* alignmentType = runtime->getTypeByName("alignment")->asEnum();
    const auto* horizontalAlignmentType = runtime->getTypeByName("horizontalAlignment")->asEnum();
    const auto* verticalAlignmentType = runtime->getTypeByName("verticalAlignment")->asEnum();
    const auto* bodyType = runtime->getTypeByName("body")->asEnum();
    const auto* cardinalType = runtime->getTypeByName("cardinal")->asEnum();
    const auto* fontType = runtime->getTypeByName("font");
    BOOST_REQUIRE(alignmentType != nullptr);
    BOOST_REQUIRE(horizontalAlignmentType != nullptr);
    BOOST_REQUIRE(verticalAlignmentType != nullptr);
    BOOST_REQUIRE(bodyType != nullptr);
    BOOST_REQUIRE(cardinalType != nullptr);
    BOOST_REQUIRE(fontType != nullptr);
    BOOST_TEST(fontType->getDocumentation() == "A font at a specific point size for displaying text.");
    BOOST_TEST(alignmentType->getValueCount() == 9U);
    BOOST_TEST(horizontalAlignmentType->getValueCount() == 3U);
    BOOST_TEST(verticalAlignmentType->getValueCount() == 3U);
    BOOST_TEST(bodyType->getValueCount() == 3U);
    BOOST_TEST(cardinalType->getValueCount() == 4U);

    NomadInteger enumValue = 0;
    BOOST_REQUIRE(alignmentType->getValueByName("topLeft", enumValue));
    BOOST_TEST(enumValue == static_cast<NomadInteger>(Alignment::TopLeft));
    BOOST_REQUIRE(bodyType->getValueByName("dynamic", enumValue));
    BOOST_TEST(enumValue == static_cast<NomadInteger>(BodyType::Dynamic));
    BOOST_REQUIRE(cardinalType->getValueByName("east", enumValue));
    BOOST_TEST(enumValue == static_cast<NomadInteger>(Cardinal::East));

    NativeFunctionDefinition thisCircleBody;
    NativeFunctionDefinition otherRectangleBody;
    NativeFunctionDefinition thisSetText;
    NativeFunctionDefinition otherSetText;
    NativeFunctionDefinition loadFont;
    BOOST_REQUIRE(runtime->getNativeFunctionDefinition("this.body.circle", thisCircleBody));
    BOOST_REQUIRE(runtime->getNativeFunctionDefinition("other.body.rectangle", otherRectangleBody));
    BOOST_REQUIRE(runtime->getNativeFunctionDefinition("this.setText", thisSetText));
    BOOST_REQUIRE(runtime->getNativeFunctionDefinition("other.setText", otherSetText));
    BOOST_REQUIRE(runtime->getNativeFunctionDefinition("game.loadFont", loadFont));
    BOOST_REQUIRE(!thisCircleBody.parameters.empty());
    BOOST_REQUIRE(!otherRectangleBody.parameters.empty());
    BOOST_REQUIRE(!thisSetText.parameters.empty());
    BOOST_REQUIRE(!otherSetText.parameters.empty());
    BOOST_TEST(thisCircleBody.parameters[0].type == bodyType);
    BOOST_TEST(otherRectangleBody.parameters[0].type == bodyType);
    BOOST_TEST(thisSetText.parameters[0].type == fontType);
    BOOST_TEST(otherSetText.parameters[0].type == fontType);
    BOOST_TEST(loadFont.returnType == fontType);

    const auto thisAlignmentId = runtime->getDynamicVariableId("this.text.alignment");
    const auto otherAlignmentId = runtime->getDynamicVariableId("other.text.alignment");
    const auto thisFontId = runtime->getDynamicVariableId("this.text.font");
    const auto otherFontId = runtime->getDynamicVariableId("other.text.font");
    const auto debugDrawVisibleId = runtime->getDynamicVariableId(DEBUG_DRAW_VISIBLE_VARIABLE);
    BOOST_REQUIRE(thisAlignmentId != NOMAD_INVALID_ID);
    BOOST_REQUIRE(otherAlignmentId != NOMAD_INVALID_ID);
    BOOST_REQUIRE(thisFontId != NOMAD_INVALID_ID);
    BOOST_REQUIRE(otherFontId != NOMAD_INVALID_ID);
    BOOST_REQUIRE(debugDrawVisibleId != NOMAD_INVALID_ID);
    BOOST_TEST(runtime->getDynamicVariableType(thisAlignmentId) == alignmentType);
    BOOST_TEST(runtime->getDynamicVariableType(otherAlignmentId) == alignmentType);
    BOOST_TEST(runtime->getDynamicVariableType(thisFontId) == fontType);
    BOOST_TEST(runtime->getDynamicVariableType(otherFontId) == fontType);
    BOOST_TEST(runtime->getDynamicVariableType(debugDrawVisibleId) == runtime->getBooleanType());
    BOOST_TEST(!game->isDebugDrawVisible());
    game->setDebugDrawVisible(true);
    BOOST_TEST(game->isDebugDrawVisible());

    const auto result = checkPath(directory.getPath(), runtime);

    for (const auto& diagnostic : result.diagnostics) {
        BOOST_TEST_MESSAGE(formatDiagnostic(diagnostic));
    }

    BOOST_TEST(result.succeeded());
    BOOST_TEST(result.errorCount == 0U);

    directory.write("invalid-font.nomad", "this.text.font = 1\n");
    const auto invalidFontResult = checkPath(directory.getPath(), runtime);
    BOOST_TEST(!invalidFontResult.succeeded());
    BOOST_TEST(invalidFontResult.errorCount > 0U);
}

BOOST_AUTO_TEST_SUITE_END()
