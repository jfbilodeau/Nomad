// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>
#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

#include <filesystem>

using namespace nomad;
using namespace nomad::test;

BOOST_AUTO_TEST_SUITE(language_script_discovery)

BOOST_AUTO_TEST_CASE(function_names_are_derived_from_relative_paths)
{
    TestDirectory directory("nomad_language_test_names");
    directory.write("init.nomad", "return 1");
    directory.write("entities/player.nomad", "return 2");
    directory.write("scene/play/intro.nomad", "return 3");
    directory.write("notes.txt", "this is not a function");
    directory.write("entities/readme.md", "neither is this");

    LanguageTestFixture fixture;
    fixture.getCompiler().loadScriptsFromPath(directory.getPath().generic_string());

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

    auto& runtime = fixture.getRuntime();
    BOOST_TEST(runtime.getFunctionId("init") != NOMAD_INVALID_ID);
    BOOST_TEST(runtime.getFunctionId("entities.player") != NOMAD_INVALID_ID);
    BOOST_TEST(runtime.getFunctionId("scene.play.intro") != NOMAD_INVALID_ID);
    BOOST_TEST(runtime.getFunctionId("notes") == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.getFunctionId("entities.readme") == NOMAD_INVALID_ID);

    BOOST_TEST(fixture.execute("init").integerValue == 1);
    BOOST_TEST(fixture.execute("entities.player").integerValue == 2);
    BOOST_TEST(fixture.execute("scene.play.intro").integerValue == 3);
}

BOOST_AUTO_TEST_CASE(discovered_functions_call_each_other)
{
    TestDirectory directory("nomad_language_test_calls");
    directory.write("init.nomad", "return entities.player.speed 3");
    directory.write("entities/player/speed.nomad", "params factor:int\nreturn factor * 5");

    LanguageTestFixture fixture;
    fixture.getCompiler().loadScriptsFromPath(directory.getPath().generic_string());

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(fixture.execute("init").integerValue == 15);
}

BOOST_AUTO_TEST_CASE(compile_errors_report_source_file)
{
    TestDirectory directory("nomad_language_test_errors");
    directory.write("good.nomad", "return 1");
    directory.write("sub/bad.nomad", "a = 1\nreturn missing");

    LanguageTestFixture fixture;
    fixture.getCompiler().loadScriptsFromPath(directory.getPath().generic_string());

    BOOST_REQUIRE(!fixture.compile());

    const auto* error = fixture.findError("Unknown identifier");
    BOOST_REQUIRE_MESSAGE(error != nullptr, fixture.getDiagnostics());
    BOOST_TEST(error->sourceName.find("bad.nomad") != std::string::npos, error->sourceName);
    BOOST_TEST(error->line == 2u);
}

BOOST_AUTO_TEST_CASE(file_and_directory_name_collision_is_rejected)
{
    TestDirectory directory("nomad_language_test_collision");
    directory.write("scene/play/intro.nomad", "return 1");
    directory.write("scene.play.intro.nomad", "return 2");

    LanguageTestFixture fixture;
    BOOST_CHECK_NO_THROW(fixture.getCompiler().loadScriptsFromPath(directory.getPath().generic_string()));

    // Both files map to the function name `scene.play.intro`.
    BOOST_REQUIRE(!fixture.compile());

    const auto* error = fixture.findError("scene.play.intro");
    BOOST_REQUIRE_MESSAGE(error != nullptr, fixture.getDiagnostics());
    BOOST_TEST(error->message.find("already used") != std::string::npos, error->message);
}

BOOST_AUTO_TEST_CASE(function_colliding_with_function_file_is_rejected)
{
    TestDirectory directory("nomad_language_test_function_collision");
    directory.write("scene/play/intro.nomad", "return 1");
    directory.write("init.nomad", "fun scene.play.intro\nend");

    LanguageTestFixture fixture;
    fixture.getCompiler().loadScriptsFromPath(directory.getPath().generic_string());

    BOOST_TEST(!fixture.compile());
}

BOOST_AUTO_TEST_CASE(symbolic_linked_directories_are_not_scanned)
{
    TestDirectory directory("nomad_language_test_symlink");
    directory.write("init.nomad", "return 1");

    std::error_code error;
    std::filesystem::create_directory_symlink(
        directory.getPath() / "missing-target",
        directory.getPath() / "linked",
        error
    );

    if (error) {
        BOOST_TEST_MESSAGE("Directory symbolic links are unavailable: " << error.message());
        return;
    }

    LanguageTestFixture fixture;
    BOOST_CHECK_NO_THROW(fixture.getCompiler().loadScriptsFromPath(directory.getPath().generic_string()));
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(fixture.getRuntime().getFunctionId("init") != NOMAD_INVALID_ID);
}

BOOST_AUTO_TEST_SUITE_END()
