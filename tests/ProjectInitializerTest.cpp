// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/CompilerTools.hpp>
#include <nomad/project/ProjectConfiguration.hpp>
#include <nomad/project/ProjectInitializer.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/system/Path.hpp>
#include <nomad/system/ProjectSession.hpp>
#include <nomad/Version.hpp>

#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

#include <fstream>
#include <iterator>

using namespace nomad;
using namespace nomad::test;

namespace {

NomadPath getProjectTemplateDirectory() {
    return pathFromUtf8(NOMAD_TEST_PROJECT_TEMPLATE_DIR);
}

NomadString readFile(const NomadPath& path) {
    std::ifstream input(path, std::ios::binary);
    return NomadString{
        std::istreambuf_iterator<NomadChar>(input),
        std::istreambuf_iterator<NomadChar>()
    };
}

} // namespace

BOOST_AUTO_TEST_SUITE(project_initializer)

BOOST_AUTO_TEST_CASE(normalizes_project_names_for_executables)
{
    BOOST_TEST(makeProjectExecutableName("My Example Game") == "my-example-game");
    BOOST_TEST(makeProjectExecutableName("Wishlair 1") == "wishlair-1");
    BOOST_TEST(makeProjectExecutableName("  Multiple---Separators  ") == "multiple-separators");
    BOOST_CHECK_THROW((void)makeProjectExecutableName("---"), ProjectInitializationException);
}

BOOST_AUTO_TEST_CASE(creates_a_valid_minimal_project)
{
    TestDirectory directory("nomad_project_initializer");
    const auto destination = directory.getPath() / "nested" / "projects" / "Example Game";

    const auto result = initializeProject(destination, getProjectTemplateDirectory());

    BOOST_TEST(result.root == std::filesystem::absolute(destination));
    BOOST_REQUIRE(result.createdFiles.size() == 8U);
    BOOST_TEST(std::filesystem::is_regular_file(destination / ".gitignore"));
    BOOST_TEST(std::filesystem::is_regular_file(destination / "README.md"));
    BOOST_TEST(std::filesystem::is_regular_file(destination / "nomadw"));
    BOOST_TEST(std::filesystem::is_regular_file(destination / "nomadw.cmd"));
    BOOST_TEST(std::filesystem::is_regular_file(destination / "nomadw.ps1"));
    BOOST_CHECK(
        (std::filesystem::status(destination / "nomadw").permissions() &
         std::filesystem::perms::owner_exec) != std::filesystem::perms::none
    );
    BOOST_TEST(std::filesystem::is_regular_file(destination / "nomad.toml"));
    BOOST_TEST(std::filesystem::is_regular_file(destination / "res/scripts/main.nomad"));
    BOOST_TEST(std::filesystem::is_regular_file(destination / "res/text/text.csv"));

    const auto project = loadNomadProject(destination / "nomad.toml");
    BOOST_TEST(project.configuration.project.name == "Example Game");
    BOOST_TEST(project.configuration.project.identifier == "com.example.example-game");
    BOOST_TEST(project.configuration.project.executable == "example-game");
    BOOST_TEST(project.gameOptions.entryFunction == "main");
    BOOST_TEST(project.configuration.sdk.version == getNomadVersion());
    BOOST_TEST(project.configuration.resources.directory == NomadPath("res"));
    BOOST_REQUIRE(project.gameOptions.text.directories.has_value());
    BOOST_REQUIRE(project.gameOptions.text.directories->size() == 1U);
    BOOST_TEST((*project.gameOptions.text.directories)[0] == NomadPath("text/text.csv"));

    const auto readme = readFile(destination / "README.md");
    BOOST_TEST(readme.find("# Example Game") != NomadString::npos);
    BOOST_TEST(readme.find("{{") == NomadString::npos);

    const auto session = createProjectSession(project, ModuleMode::Analysis);
    auto compilation = checkPath(destination / "res/scripts/main.nomad", session->getRuntime());
    validateEntryFunction(compilation, session->getRuntime(), project.gameOptions.entryFunction, destination / "nomad.toml");
    BOOST_TEST(compilation.succeeded());
}

BOOST_AUTO_TEST_CASE(can_omit_project_wrappers)
{
    TestDirectory directory("nomad_project_initializer_without_wrapper");
    const auto destination = directory.getPath() / "project";

    const auto result = initializeProject(destination, getProjectTemplateDirectory(), false);

    BOOST_REQUIRE(result.createdFiles.size() == 5U);
    BOOST_TEST(!std::filesystem::exists(destination / "nomadw"));
    BOOST_TEST(!std::filesystem::exists(destination / "nomadw.cmd"));
    BOOST_TEST(!std::filesystem::exists(destination / "nomadw.ps1"));
    BOOST_TEST(std::filesystem::is_regular_file(destination / "nomad.toml"));
}

BOOST_AUTO_TEST_CASE(refuses_to_overwrite_existing_project_files)
{
    TestDirectory directory("nomad_project_initializer_conflicts");
    const auto destination = directory.getPath() / "existing";
    const auto projectFile = directory.write("existing/nomad.toml", "existing");

    BOOST_CHECK_EXCEPTION(
        (void)initializeProject(destination, getProjectTemplateDirectory()),
        ProjectInitializationException,
        [](const ProjectInitializationException& error) {
            return NomadString(error.what()).find("Refusing to overwrite existing file") != NomadString::npos;
        }
    );

    const auto content = readFile(projectFile);
    BOOST_TEST(content == "existing");
    BOOST_TEST(!std::filesystem::exists(destination / "res/scripts/main.nomad"));
}

BOOST_AUTO_TEST_CASE(rejects_unknown_template_placeholders_and_rolls_back)
{
    TestDirectory directory("nomad_project_initializer_invalid_template");
    const auto templateDirectory = directory.getPath() / "template";
    directory.write("template/unknown.txt.in", "{{unknown}}");
    const auto destination = directory.getPath() / "project";

    BOOST_CHECK_EXCEPTION(
        (void)initializeProject(destination, templateDirectory),
        ProjectInitializationException,
        [](const ProjectInitializationException& error) {
            return NomadString(error.what()).find("Unknown placeholder '{{unknown}}'") != NomadString::npos;
        }
    );

    BOOST_TEST(!std::filesystem::exists(destination));
}

BOOST_AUTO_TEST_SUITE_END()
