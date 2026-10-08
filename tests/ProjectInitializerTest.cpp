// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/CompilerTools.hpp>
#include <nomad/project/ProjectConfiguration.hpp>
#include <nomad/project/ProjectInitializer.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/system/Path.hpp>
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
    BOOST_CHECK_THROW((void)makeProjectExecutableName("---"), ProjectInitializationError);
}

BOOST_AUTO_TEST_CASE(creates_a_valid_minimal_project)
{
    TestDirectory directory("nomad_project_initializer");
    const auto destination = directory.getPath() / "nested" / "projects" / "Example Game";

    const auto result = initializeProject(destination, getProjectTemplateDirectory());

    BOOST_TEST(result.root == std::filesystem::absolute(destination));
    BOOST_REQUIRE(result.createdFiles.size() == 4U);
    BOOST_TEST(std::filesystem::is_regular_file(destination / ".gitignore"));
    BOOST_TEST(std::filesystem::is_regular_file(destination / "README.md"));
    BOOST_TEST(std::filesystem::is_regular_file(destination / "nomad.toml"));
    BOOST_TEST(std::filesystem::is_regular_file(destination / "res/scripts/init.nomad"));

    const auto configuration = loadProjectConfiguration(destination / "nomad.toml");
    BOOST_TEST(configuration.project.name == "Example Game");
    BOOST_TEST(configuration.project.identifier == "com.example.example-game");
    BOOST_TEST(configuration.project.executable == "example-game");
    BOOST_TEST(configuration.project.entry == "init");
    BOOST_TEST(configuration.nomad.version == getNomadVersion());
    BOOST_TEST(configuration.resources.directory == NomadPath("res"));

    const auto readme = readFile(destination / "README.md");
    BOOST_TEST(readme.find("# Example Game") != NomadString::npos);
    BOOST_TEST(readme.find("{{") == NomadString::npos);

    Runtime runtime;
    auto compilation = checkPath(destination / "res/scripts/init.nomad", &runtime);
    validateEntryFunction(compilation, &runtime, "init", destination / "nomad.toml");
    BOOST_TEST(compilation.succeeded());
}

BOOST_AUTO_TEST_CASE(refuses_to_overwrite_existing_project_files)
{
    TestDirectory directory("nomad_project_initializer_conflicts");
    const auto destination = directory.getPath() / "existing";
    const auto projectFile = directory.write("existing/nomad.toml", "existing");

    BOOST_CHECK_EXCEPTION(
        (void)initializeProject(destination, getProjectTemplateDirectory()),
        ProjectInitializationError,
        [](const ProjectInitializationError& error) {
            return NomadString(error.what()).find("Refusing to overwrite existing file") != NomadString::npos;
        }
    );

    const auto content = readFile(projectFile);
    BOOST_TEST(content == "existing");
    BOOST_TEST(!std::filesystem::exists(destination / "res/scripts/init.nomad"));
}

BOOST_AUTO_TEST_CASE(rejects_unknown_template_placeholders_and_rolls_back)
{
    TestDirectory directory("nomad_project_initializer_invalid_template");
    const auto templateDirectory = directory.getPath() / "template";
    directory.write("template/unknown.txt.in", "{{unknown}}");
    const auto destination = directory.getPath() / "project";

    BOOST_CHECK_EXCEPTION(
        (void)initializeProject(destination, templateDirectory),
        ProjectInitializationError,
        [](const ProjectInitializationError& error) {
            return NomadString(error.what()).find("Unknown placeholder '{{unknown}}'") != NomadString::npos;
        }
    );

    BOOST_TEST(!std::filesystem::exists(destination));
}

BOOST_AUTO_TEST_SUITE_END()
