// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/project/ProjectConfiguration.hpp>
#include <nomad/project/ProjectInitializer.hpp>
#include <nomad/Version.hpp>

#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

#include <fstream>
#include <iterator>

using namespace nomad;
using namespace nomad::test;

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
    const auto destination = directory.getPath() / "Example Game";

    const auto result = initializeProject(destination);

    BOOST_TEST(result.root == std::filesystem::absolute(destination));
    BOOST_REQUIRE(result.createdFiles.size() == 2U);
    BOOST_TEST(std::filesystem::is_regular_file(destination / "nomad.toml"));
    BOOST_TEST(std::filesystem::is_regular_file(destination / "res/scripts/init.nomad"));

    const auto configuration = loadProjectConfiguration(destination / "nomad.toml");
    BOOST_TEST(configuration.project.name == "Example Game");
    BOOST_TEST(configuration.project.identifier == "com.example.example-game");
    BOOST_TEST(configuration.project.executable == "example-game");
    BOOST_TEST(configuration.project.entry == "init");
    BOOST_TEST(configuration.nomad.version == NOMAD_VERSION);
    BOOST_TEST(configuration.resources.directory == NomadPath("res"));
}

BOOST_AUTO_TEST_CASE(refuses_to_overwrite_existing_project_files)
{
    TestDirectory directory("nomad_project_initializer_conflicts");
    const auto destination = directory.getPath() / "existing";
    const auto projectFile = directory.write("existing/nomad.toml", "existing");

    BOOST_CHECK_EXCEPTION(
        (void)initializeProject(destination),
        ProjectInitializationError,
        [](const ProjectInitializationError& error) {
            return NomadString(error.what()).find("Refusing to overwrite existing file") != NomadString::npos;
        }
    );

    std::ifstream input(projectFile, std::ios::binary);
    const NomadString content{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    BOOST_TEST(content == "existing");
    BOOST_TEST(!std::filesystem::exists(destination / "res/scripts/init.nomad"));
}

BOOST_AUTO_TEST_SUITE_END()
