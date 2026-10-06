// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <boost/test/unit_test.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace nomad;
using namespace nomad::test;

namespace {

// Creates a unique temporary compile root and removes it on destruction.
class ScriptDirectory {
public:
    explicit ScriptDirectory(const std::string& name) :
        m_root(std::filesystem::temp_directory_path() / ("nomad_language_test_" + name)) {
        std::filesystem::remove_all(m_root);
        std::filesystem::create_directories(m_root);
    }

    ScriptDirectory(const ScriptDirectory&) = delete;
    ScriptDirectory& operator=(const ScriptDirectory&) = delete;

    ~ScriptDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_root, error);
    }

    void write(const std::filesystem::path& relativePath, const std::string& source) const {
        const auto path = m_root / relativePath;
        std::filesystem::create_directories(path.parent_path());

        std::ofstream file(path, std::ios::binary);
        file << source;
    }

    [[nodiscard]] std::string getPath() const {
        return m_root.generic_string();
    }

private:
    std::filesystem::path m_root;
};

} // namespace

BOOST_AUTO_TEST_SUITE(language_script_discovery)

BOOST_AUTO_TEST_CASE(function_names_are_derived_from_relative_paths)
{
    ScriptDirectory directory("names");
    directory.write("init.nomad", "return 1");
    directory.write("entities/player.nomad", "return 2");
    directory.write("scene/play/intro.nomad", "return 3");
    directory.write("notes.txt", "this is not a function");
    directory.write("entities/readme.md", "neither is this");

    LanguageTestFixture fixture;
    fixture.getCompiler().loadScriptsFromPath(directory.getPath());

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
    ScriptDirectory directory("calls");
    directory.write("init.nomad", "return entities.player.speed 3");
    directory.write("entities/player/speed.nomad", "params factor:int\nreturn factor * 5");

    LanguageTestFixture fixture;
    fixture.getCompiler().loadScriptsFromPath(directory.getPath());

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(fixture.execute("init").integerValue == 15);
}

BOOST_AUTO_TEST_CASE(compile_errors_report_source_file)
{
    ScriptDirectory directory("errors");
    directory.write("good.nomad", "return 1");
    directory.write("sub/bad.nomad", "a = 1\nreturn missing");

    LanguageTestFixture fixture;
    fixture.getCompiler().loadScriptsFromPath(directory.getPath());

    BOOST_REQUIRE(!fixture.compile());

    const auto* error = fixture.findError("Unknown identifier");
    BOOST_REQUIRE_MESSAGE(error != nullptr, fixture.getDiagnostics());
    BOOST_TEST(error->sourceName.find("bad.nomad") != std::string::npos, error->sourceName);
    BOOST_TEST(error->line == 2u);
}

BOOST_AUTO_TEST_CASE(file_and_directory_name_collision_is_rejected)
{
    ScriptDirectory directory("collision");
    directory.write("scene/play/intro.nomad", "return 1");
    directory.write("scene.play.intro.nomad", "return 2");

    LanguageTestFixture fixture;
    BOOST_CHECK_NO_THROW(fixture.getCompiler().loadScriptsFromPath(directory.getPath()));

    // Both files map to the function name `scene.play.intro`.
    BOOST_REQUIRE(!fixture.compile());

    const auto* error = fixture.findError("scene.play.intro");
    BOOST_REQUIRE_MESSAGE(error != nullptr, fixture.getDiagnostics());
    BOOST_TEST(error->message.find("already used") != std::string::npos, error->message);
}

BOOST_AUTO_TEST_CASE(function_colliding_with_function_file_is_rejected)
{
    ScriptDirectory directory("functionCollision");
    directory.write("scene/play/intro.nomad", "return 1");
    directory.write("init.nomad", "fun scene.play.intro\nend");

    LanguageTestFixture fixture;
    fixture.getCompiler().loadScriptsFromPath(directory.getPath());

    BOOST_TEST(!fixture.compile());
}

BOOST_AUTO_TEST_SUITE_END()
