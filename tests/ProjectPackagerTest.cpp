// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/project/ProjectConfiguration.hpp>
#include <nomad/project/ProjectPackager.hpp>

#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>

using namespace nomad;
using namespace nomad::test;

namespace {

constexpr auto CONFIGURATION = R"(schema = 1

[project]
name = "Package Test"
identifier = "com.example.package-test"
version = "1.2.3"
executable = "package-test"
entry = "start"

[nomad]
version = "0.1.0"

[resources]
directory = "assets"

[package]
output = "dist"
exclude = ["**/*.aseprite", "**/*.psd", "development/**"]
)";

NomadString readFile(const NomadPath& path) {
    std::ifstream input(path, std::ios::binary);
    return NomadString{
        std::istreambuf_iterator<NomadChar>(input),
        std::istreambuf_iterator<NomadChar>()
    };
}

} // namespace

BOOST_AUTO_TEST_SUITE(project_packager)

BOOST_AUTO_TEST_CASE(dry_run_lists_package_files_without_writing_output)
{
    TestDirectory directory("nomad_project_packager_dry_run");
    const auto projectFile = directory.write(NOMAD_PROJECT_FILE_NAME, CONFIGURATION);
    directory.write("assets/scripts/start.nomad", "return\n");
    directory.write("assets/images/player.png", "png");
    directory.write("assets/images/player.aseprite", "source");
    const auto runtimeDirectory = directory.getPath() / "runtime";
    directory.write("runtime/nomad-runtime.exe", "runtime");
    directory.write("runtime/SDL3.dll", "library");

    const auto configuration = loadProjectConfiguration(projectFile);
    const auto result = packageProject(configuration, runtimeDirectory, false, true);
    const auto output = directory.getPath() / "dist";

    BOOST_TEST(result.output == output);
    BOOST_TEST(result.files.size() == 5U);
    BOOST_TEST(std::ranges::any_of(result.files, [](const auto& file) {
        return file.filename() == "package-test.exe";
    }));
    BOOST_TEST(std::ranges::any_of(result.files, [](const auto& file) {
        return file.filename() == "SDL3.dll";
    }));
    BOOST_TEST(std::ranges::any_of(result.files, [](const auto& file) {
        return file.filename() == "start.nomad";
    }));
    BOOST_TEST(std::ranges::any_of(result.files, [](const auto& file) {
        return file.filename() == NOMAD_PROJECT_FILE_NAME;
    }));
    BOOST_TEST(!std::filesystem::exists(output));
    BOOST_TEST(!std::filesystem::exists(directory.getPath() / "dist.tmp"));
    BOOST_TEST(!std::filesystem::exists(directory.getPath() / "dist.backup"));
}

BOOST_AUTO_TEST_CASE(packages_runtime_resources_and_release_manifest)
{
    TestDirectory directory("nomad_project_packager");
    const auto projectFile = directory.write(NOMAD_PROJECT_FILE_NAME, CONFIGURATION);
    directory.write("assets/scripts/start.nomad", "return\n");
    directory.write("assets/images/player.png", "png");
    directory.write("assets/images/player.aseprite", "source");
    directory.write("assets/art/background.psd", "source");
    directory.write("assets/development/notes.txt", "notes");
    const auto runtimeDirectory = directory.getPath() / "runtime";
    directory.write("runtime/nomad-runtime.exe", "runtime");
    directory.write("runtime/SDL3.dll", "library");

    const auto configuration = loadProjectConfiguration(projectFile);
    const auto result = packageProject(configuration, runtimeDirectory, false);

    const auto output = directory.getPath() / "dist";
    BOOST_TEST(result.output == output);
    BOOST_TEST(std::filesystem::is_regular_file(output / "package-test.exe"));
    BOOST_TEST(std::filesystem::is_regular_file(output / "SDL3.dll"));
    BOOST_TEST(std::filesystem::is_regular_file(output / "res/scripts/start.nomad"));
    BOOST_TEST(std::filesystem::is_regular_file(output / "res/images/player.png"));
    BOOST_TEST(!std::filesystem::exists(output / "res/images/player.aseprite"));
    BOOST_TEST(!std::filesystem::exists(output / "res/art/background.psd"));
    BOOST_TEST(!std::filesystem::exists(output / "res/development/notes.txt"));

    const auto releaseText = readFile(output / NOMAD_PROJECT_FILE_NAME);
    BOOST_TEST(releaseText.find("executable") == NomadString::npos);
    BOOST_TEST(releaseText.find("[resources]") == NomadString::npos);
    BOOST_TEST(releaseText.find("[package]") == NomadString::npos);

    const auto release = loadProjectConfiguration(output / NOMAD_PROJECT_FILE_NAME);
    BOOST_TEST(
        static_cast<int>(release.type) ==
        static_cast<int>(ProjectConfigurationType::Release)
    );
    BOOST_TEST(resolveProjectResourcePath(release) == output / "res");
}

BOOST_AUTO_TEST_CASE(requires_force_to_replace_nonempty_output)
{
    TestDirectory directory("nomad_project_packager_force");
    const auto projectFile = directory.write(NOMAD_PROJECT_FILE_NAME, CONFIGURATION);
    directory.write("assets/scripts/start.nomad", "return\n");
    const auto runtimeDirectory = directory.getPath() / "runtime";
    directory.write("runtime/nomad-runtime", "runtime");
    directory.write("dist/old.txt", "old");
    const auto configuration = loadProjectConfiguration(projectFile);

    BOOST_CHECK_EXCEPTION(
        (void)packageProject(configuration, runtimeDirectory, false),
        ProjectPackagingError,
        [](const ProjectPackagingError& error) {
            return NomadString(error.what()).find("use --force") != NomadString::npos;
        }
    );
    BOOST_TEST(std::filesystem::is_regular_file(directory.getPath() / "dist/old.txt"));

    const auto result = packageProject(configuration, runtimeDirectory, true);
    BOOST_TEST(result.output == directory.getPath() / "dist");
    BOOST_TEST(!std::filesystem::exists(directory.getPath() / "dist/old.txt"));
    BOOST_TEST(std::filesystem::is_regular_file(directory.getPath() / "dist/package-test"));
}

BOOST_AUTO_TEST_CASE(cleans_staging_directory_after_failure)
{
    TestDirectory directory("nomad_project_packager_rollback");
    const auto projectFile = directory.write(NOMAD_PROJECT_FILE_NAME, CONFIGURATION);
    directory.write("assets/scripts/start.nomad", "return\n");
    const auto runtimeDirectory = directory.getPath() / "runtime";
    directory.write("runtime/not-the-runtime.txt", "wrong");
    const auto configuration = loadProjectConfiguration(projectFile);

    BOOST_CHECK_THROW(
        (void)packageProject(configuration, runtimeDirectory, false),
        ProjectPackagingError
    );
    BOOST_TEST(!std::filesystem::exists(directory.getPath() / "dist"));
    BOOST_TEST(!std::filesystem::exists(directory.getPath() / "dist.tmp"));
}

BOOST_AUTO_TEST_SUITE_END()
