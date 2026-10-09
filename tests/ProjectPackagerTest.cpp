// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/project/ProjectConfiguration.hpp>
#include <nomad/project/ProjectPackager.hpp>

#include <nomad/system/Path.hpp>

#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <memory>

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

NomadPath archivePath(const TestDirectory& directory) {
    return directory.getPath() / "dist" /
        ("package-test-" + NomadString(getPackagePlatform()) + "-1.2.3.zip");
}

void extractArchive(const NomadPath& source, const NomadPath& destination) {
    const auto bytes = readFile(source);
    const std::unique_ptr<archive, decltype(&archive_read_free)> reader(archive_read_new(), &archive_read_free);
    BOOST_REQUIRE_EQUAL(archive_read_support_format_zip(reader.get()), ARCHIVE_OK);
    BOOST_REQUIRE_EQUAL(archive_read_open_memory(reader.get(), bytes.data(), bytes.size()), ARCHIVE_OK);
    archive_entry* entry = nullptr;
    auto result = ARCHIVE_OK;
    while ((result = archive_read_next_header(reader.get(), &entry)) == ARCHIVE_OK) {
        const auto path = destination / std::filesystem::u8path(archive_entry_pathname(entry));
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary);
        char buffer[4096];
        la_ssize_t size;
        while ((size = archive_read_data(reader.get(), buffer, sizeof(buffer))) > 0) {
            output.write(buffer, size);
        }
        BOOST_REQUIRE_EQUAL(size, 0);
        BOOST_REQUIRE(output.good());
    }
    BOOST_REQUIRE_EQUAL(result, ARCHIVE_EOF);
}

void writeRuntimeManifest(TestDirectory& directory, const NomadString& executable, const bool library = false,
    const NomadString& target = NomadString(getPackagePlatform())) {
    directory.write("runtime/licenses/Nomad.txt", "license");
    directory.write("runtime/runtime.json",
        "{\"schema\":1,\"version\":\"0.1.0\",\"target\":\"" + target + "\",\"files\":["
        "{\"path\":\"" + executable + "\",\"role\":\"runtime\"},"
        "{\"path\":\"licenses/Nomad.txt\",\"role\":\"license\"}" +
        (library ? ",{\"path\":\"SDL3.dll\",\"role\":\"library\"}" : "") + "]}");
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
    writeRuntimeManifest(directory, "nomad-runtime.exe", true);
    directory.write("runtime/ignored.txt", "must not ship");

    const auto configuration = loadProjectConfiguration(projectFile);
    const auto result = packageProject(configuration, runtimeDirectory, false, true);
    const auto output = archivePath(directory);

    BOOST_TEST(result.output == output);
    BOOST_TEST(result.files.size() == 6U);
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
    BOOST_TEST(!std::filesystem::exists(directory.getPath() / "dist"));
    auto staging = output;
    staging += ".staging";
    auto temporaryArchive = output;
    temporaryArchive += ".tmp";
    BOOST_TEST(!std::filesystem::exists(staging));
    BOOST_TEST(!std::filesystem::exists(temporaryArchive));
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
    writeRuntimeManifest(directory, "nomad-runtime.exe", true);

    const auto configuration = loadProjectConfiguration(projectFile);
    const auto result = packageProject(configuration, runtimeDirectory, false);

    const auto output = directory.getPath() / "extracted";
    BOOST_TEST(result.output == archivePath(directory));
    extractArchive(result.output, output);
    BOOST_TEST(std::filesystem::is_regular_file(output / "package-test.exe"));
    BOOST_TEST(std::filesystem::is_regular_file(output / "SDL3.dll"));
    BOOST_TEST(std::filesystem::is_regular_file(output / "res/scripts/start.nomad"));
    BOOST_TEST(std::filesystem::is_regular_file(output / "res/images/player.png"));
    BOOST_TEST(!std::filesystem::exists(output / "res/images/player.aseprite"));
    BOOST_TEST(!std::filesystem::exists(output / "res/art/background.psd"));
    BOOST_TEST(!std::filesystem::exists(output / "res/development/notes.txt"));
    BOOST_TEST(readFile(output / "licenses/Nomad.txt") == "license");
    BOOST_TEST(!std::filesystem::exists(output / "runtime.json"));

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

    const auto firstArchive = readFile(result.output);
    const auto repeated = packageProject(configuration, runtimeDirectory, true);
    BOOST_TEST(readFile(repeated.output) == firstArchive);
    BOOST_TEST(!std::filesystem::exists(NomadPath(pathToUtf8(result.output) + ".staging")));
}

BOOST_AUTO_TEST_CASE(requires_force_to_replace_nonempty_output)
{
    TestDirectory directory("nomad_project_packager_force");
    const auto projectFile = directory.write(NOMAD_PROJECT_FILE_NAME, CONFIGURATION);
    directory.write("assets/scripts/start.nomad", "return\n");
    const auto runtimeDirectory = directory.getPath() / "runtime";
    directory.write("runtime/nomad-runtime", "runtime");
    writeRuntimeManifest(directory, "nomad-runtime");
    directory.write("dist/keep.txt", "unrelated");
    const auto configuration = loadProjectConfiguration(projectFile);
    const auto original = packageProject(configuration, runtimeDirectory, false);
    const auto originalBytes = readFile(original.output);

    BOOST_CHECK_EXCEPTION(
        (void)packageProject(configuration, runtimeDirectory, false),
        ProjectPackagingError,
        [](const ProjectPackagingError& error) {
            return NomadString(error.what()).find("use --force") != NomadString::npos;
        }
    );
    BOOST_TEST(readFile(original.output) == originalBytes);
    const auto preview = packageProject(configuration, runtimeDirectory, true, true);
    BOOST_TEST(preview.output == original.output);
    BOOST_TEST(readFile(original.output) == originalBytes);

    const auto result = packageProject(configuration, runtimeDirectory, true);
    BOOST_TEST(result.output == archivePath(directory));
    BOOST_TEST(readFile(directory.getPath() / "dist/keep.txt") == "unrelated");
    extractArchive(result.output, directory.getPath() / "extracted");
    BOOST_TEST(std::filesystem::is_regular_file(directory.getPath() / "extracted/package-test"));
}

BOOST_AUTO_TEST_CASE(cleans_staging_directory_after_failure)
{
    TestDirectory directory("nomad_project_packager_rollback");
    const auto projectFile = directory.write(NOMAD_PROJECT_FILE_NAME, CONFIGURATION);
    directory.write("assets/scripts/start.nomad", "return\n");
    const auto runtimeDirectory = directory.getPath() / "runtime";
    directory.write("runtime/not-the-runtime.txt", "wrong");
    writeRuntimeManifest(directory, "nomad-runtime");
    const auto configuration = loadProjectConfiguration(projectFile);

    BOOST_CHECK_THROW(
        (void)packageProject(configuration, runtimeDirectory, false),
        ProjectPackagingError
    );
    const auto output = archivePath(directory);
    BOOST_TEST(!std::filesystem::exists(output));
    auto staging = output;
    staging += ".staging";
    auto temporaryArchive = output;
    temporaryArchive += ".tmp";
    BOOST_TEST(!std::filesystem::exists(staging));
    BOOST_TEST(!std::filesystem::exists(temporaryArchive));
}

BOOST_AUTO_TEST_CASE(uses_manifest_roles_and_target_and_ignores_undeclared_files)
{
    TestDirectory directory("nomad_manifest_roles");
    const auto projectFile = directory.write(NOMAD_PROJECT_FILE_NAME, CONFIGURATION);
    directory.write("assets/scripts/start.nomad", "return\n");
    directory.write("runtime/engine", "runtime");
    directory.write("runtime/undeclared.dll", "not distributable");
    writeRuntimeManifest(directory, "engine", false, "linux-arm64");
    const auto configuration = loadProjectConfiguration(projectFile);
    const auto preview = packageProject(configuration, directory.getPath() / "runtime", false, true);
    BOOST_TEST(preview.output.filename() == "package-test-linux-arm64-1.2.3.zip");
    const auto result = packageProject(configuration, directory.getPath() / "runtime", false);
    BOOST_TEST(preview.files == result.files, boost::test_tools::per_element());
    extractArchive(result.output, directory.getPath() / "extracted");
    BOOST_TEST(readFile(directory.getPath() / "extracted/package-test") == "runtime");
    BOOST_TEST(!std::filesystem::exists(directory.getPath() / "extracted/undeclared.dll"));
}

BOOST_AUTO_TEST_CASE(rejects_invalid_manifest_without_writing_package)
{
    TestDirectory directory("nomad_manifest_invalid");
    const auto projectFile = directory.write(NOMAD_PROJECT_FILE_NAME, CONFIGURATION);
    directory.write("assets/scripts/start.nomad", "return\n");
    directory.write("runtime/engine", "runtime");
    directory.write("runtime/licenses/Nomad.txt", "license");
    const auto configuration = loadProjectConfiguration(projectFile);
    const auto runtime = directory.getPath() / "runtime";
    const std::vector<NomadString> manifests{
        "not JSON",
        R"({"schema":2,"version":"0.1.0","target":"windows-x64","files":[]})",
        R"({"schema":1,"version":"9.0.0","target":"windows-x64","files":[]})",
        R"({"schema":1,"version":"0.1.0","target":"../escape","files":[]})",
        R"({"schema":1,"version":"0.1.0","target":"windows-x64","files":[{"path":"../engine","role":"runtime"}]})",
        R"({"schema":1,"version":"0.1.0","target":"windows-x64","files":[{"path":"C:/engine","role":"runtime"}]})",
        R"({"schema":1,"version":"0.1.0","target":"windows-x64","files":[{"path":"engine","role":"runtime"},{"path":"engine","role":"runtime"}]})",
        R"({"schema":1,"version":"0.1.0","target":"windows-x64","files":[{"path":"engine","role":"runtime"},{"path":"licenses/Nomad.txt","role":"runtime"}]})",
        R"({"schema":1,"version":"0.1.0","target":"windows-x64","files":[{"path":"engine","role":"runtime"},{"path":"licenses/./Nomad.txt","role":"license"}]})",
        R"({"schema":1,"version":"0.1.0","target":"windows-x64","files":[{"path":"engine","role":"unknown"}]})",
        R"({"schema":1,"version":"0.1.0","target":"windows-x64","files":[{"path":"engine","role":"runtime"},{"path":"missing.txt","role":"license"}]})",
        R"({"schema":1,"version":"0.1.0","target":"windows-x64","files":[{"path":"engine","role":"runtime"}]})"
    };
    BOOST_CHECK_THROW((void)packageProject(configuration, runtime, false, true), ProjectPackagingError);
    for (const auto& manifest : manifests) {
        directory.write("runtime/runtime.json", manifest);
        BOOST_CHECK_THROW((void)packageProject(configuration, runtime, false, true), ProjectPackagingError);
        BOOST_CHECK_THROW((void)packageProject(configuration, runtime, false), ProjectPackagingError);
        BOOST_TEST(!std::filesystem::exists(directory.getPath() / "dist"));
    }
}

BOOST_AUTO_TEST_CASE(rejects_renamed_runtime_destination_collision)
{
    TestDirectory directory("nomad_manifest_collision");
    const auto projectFile = directory.write(NOMAD_PROJECT_FILE_NAME, CONFIGURATION);
    directory.write("assets/scripts/start.nomad", "return\n");
    directory.write("runtime/engine.exe", "runtime");
    directory.write("runtime/package-test.exe", "collision");
    directory.write("runtime/licenses/Nomad.txt", "license");
    directory.write("runtime/runtime.json",
        R"({"schema":1,"version":"0.1.0","target":"windows-x64","files":[
            {"path":"engine.exe","role":"runtime"},
            {"path":"package-test.exe","role":"library"},
            {"path":"licenses/Nomad.txt","role":"license"}]})");
    const auto configuration = loadProjectConfiguration(projectFile);
    BOOST_CHECK_THROW(
        (void)packageProject(configuration, directory.getPath() / "runtime", false, true),
        ProjectPackagingError
    );
    BOOST_TEST(!std::filesystem::exists(directory.getPath() / "dist"));
}

BOOST_AUTO_TEST_SUITE_END()
