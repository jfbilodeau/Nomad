// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/GamePackaging.hpp>

#include <nomad/compiler/SourceLoader.hpp>

#include <nomad/Version.hpp>

#include <nomad/project/ProjectConfiguration.hpp>
#include <nomad/project/ProjectPackager.hpp>

#include <nomad/game/resource/SpriteCatalog.hpp>
#include <nomad/game/resource/SpritePacker.hpp>

#include <nomad/script/Runtime.hpp>

#include <nomad/system/Path.hpp>
#include <nomad/system/ProjectSession.hpp>

#include <TestDirectory.hpp>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <boost/test/unit_test.hpp>

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>

using namespace nomad;
using namespace nomad::test;

namespace {

constexpr auto CONFIGURATION = R"(schema = 1

[project]
name = "Package Test"
identifier = "com.example.package-test"
version = "1.2.3"
executable = "package-test"

[game]
entry = "start"

[nomad]
version = "0.1.0"
modules = []

[scripts]
directories = ["assets"]

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
        if (archive_entry_filetype(entry) == AE_IFDIR) {
            std::filesystem::create_directories(path);
            continue;
        }
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

void stageSystemModule(TestDirectory& directory, const NomadStringView nativeContents) {
    const auto library = getModuleLibraryName("system");
    const auto source = getSdkDirectory() / "modules" / "system" / library;
    BOOST_REQUIRE_MESSAGE(std::filesystem::is_regular_file(source), "SDK must stage the system module for packaging tests");
    const auto destination = directory.getPath() / "modules" / "system";
    std::filesystem::create_directories(destination);
    std::filesystem::copy_file(source, destination / library);
    directory.write("modules/system/module.toml", "schema = 1\n[module]\nname = \"system\"\n");
    directory.write("modules/system/shared.dll", NomadString(nativeContents));
    directory.write("modules/system/runtime.json",
        R"({"schema":1,"files":[{"path":"shared.dll","role":"library"}]})");
}

NomadString configurationWithSystemModule() {
    auto text = NomadString(CONFIGURATION);
    text.replace(text.find("modules = []"), NomadStringView("modules = []").size(), "modules = [\"system\"]");
    return text;
}

void stageFixtureModule(TestDirectory& directory, const NomadString& scriptSettings) {
    const auto library = getModuleLibraryName("fixture");
    const auto destination = directory.getPath() / "modules" / "fixture";
    directory.write("modules/fixture/module.toml",
        "schema = 1\n[module]\nname = 'fixture'\n[nomad]\nmodules = ['system']\n" + scriptSettings);
    std::filesystem::copy_file(NomadPath(NOMAD_TEST_MODULE_DIRECTORY) / library, destination / library);
    directory.write("modules/fixture/runtime.json",
        "{\"schema\":1,\"files\":[{\"path\":\"" + pathToGenericUtf8(library) + "\",\"role\":\"library\"}]}");
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

    const auto configuration = loadNomadProject(projectFile);
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

    const auto configuration = loadNomadProject(projectFile);
    const auto result = packageProject(configuration, runtimeDirectory, false);

    const auto output = directory.getPath() / "extracted";
    BOOST_TEST(result.output == archivePath(directory));
    extractArchive(result.output, output);
    BOOST_TEST(std::filesystem::is_regular_file(output / "package-test.exe"));
    BOOST_TEST(std::filesystem::is_regular_file(output / "SDL3.dll"));
    BOOST_TEST(std::filesystem::is_regular_file(output / "assets/scripts/start.nomad"));
    BOOST_TEST(std::filesystem::is_regular_file(output / "assets/images/player.png"));
    BOOST_TEST(!std::filesystem::exists(output / "assets/images/player.aseprite"));
    BOOST_TEST(!std::filesystem::exists(output / "assets/art/background.psd"));
    BOOST_TEST(!std::filesystem::exists(output / "assets/development/notes.txt"));
    BOOST_TEST(readFile(output / "licenses/Nomad.txt") == "license");
    BOOST_TEST(!std::filesystem::exists(output / "runtime.json"));

    const auto releaseText = readFile(output / NOMAD_PROJECT_FILE_NAME);
    BOOST_TEST(releaseText.find("executable") == NomadString::npos);
    BOOST_TEST(releaseText.find("[resources]") != NomadString::npos);
    BOOST_TEST(releaseText.find("[package]") == NomadString::npos);

    const auto release = loadNomadProject(output / NOMAD_PROJECT_FILE_NAME);
    BOOST_TEST(
        static_cast<int>(release.configuration.type) ==
        static_cast<int>(ProjectConfigurationType::Release)
    );
    BOOST_TEST(release.document.requireString("resources.directory") == "assets");

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
    const auto configuration = loadNomadProject(projectFile);
    const auto original = packageProject(configuration, runtimeDirectory, false);
    const auto originalBytes = readFile(original.output);

    BOOST_CHECK_EXCEPTION(
        (void)packageProject(configuration, runtimeDirectory, false),
        ProjectPackagingException,
        [](const ProjectPackagingException& error) {
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
    const auto configuration = loadNomadProject(projectFile);

    BOOST_CHECK_THROW(
        (void)packageProject(configuration, runtimeDirectory, false),
        ProjectPackagingException
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
    const auto configuration = loadNomadProject(projectFile);
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
    const auto configuration = loadNomadProject(projectFile);
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
    BOOST_CHECK_THROW((void)packageProject(configuration, runtime, false, true), ProjectPackagingException);
    for (const auto& manifest : manifests) {
        directory.write("runtime/runtime.json", manifest);
        BOOST_CHECK_THROW((void)packageProject(configuration, runtime, false, true), ProjectPackagingException);
        BOOST_CHECK_THROW((void)packageProject(configuration, runtime, false), ProjectPackagingException);
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
    const auto configuration = loadNomadProject(projectFile);
    BOOST_CHECK_THROW(
        (void)packageProject(configuration, directory.getPath() / "runtime", false, true),
        ProjectPackagingException
    );
    BOOST_TEST(!std::filesystem::exists(directory.getPath() / "dist"));
}

BOOST_AUTO_TEST_CASE(requires_configured_modules_without_a_legacy_runtime_fallback)
{
    TestDirectory directory("nomad_package_missing_module");
    const auto projectFile = directory.write(NOMAD_PROJECT_FILE_NAME, configurationWithSystemModule());
    directory.write("assets/scripts/start.nomad", "return\n");
    directory.write("runtime/engine", "runtime");
    writeRuntimeManifest(directory, "engine");
    const auto project = loadNomadProject(projectFile);
    BOOST_CHECK_THROW(
        (void)packageProject(project, directory.getPath() / "runtime", false, true),
        ProjectPackagingException
    );
    BOOST_TEST(!std::filesystem::exists(directory.getPath() / "dist"));
}

BOOST_AUTO_TEST_CASE(packages_only_selected_modules_and_deduplicates_identical_native_dependencies)
{
    TestDirectory directory("nomad_package_modules");
    const auto projectFile = directory.write(
        NOMAD_PROJECT_FILE_NAME,
        configurationWithSystemModule() + "\n[custom-module]\nsetting = \"preserved\"\n"
    );
    directory.write("assets/scripts/start.nomad", "return\n");
    directory.write("runtime/engine", "runtime");
    directory.write("runtime/shared.dll", "shared native dependency");
    writeRuntimeManifest(directory, "engine");
    directory.write("runtime/runtime.json",
        "{\"schema\":1,\"version\":\"0.1.0\",\"target\":\"" + NomadString(getPackagePlatform()) + "\",\"files\":["
        "{\"path\":\"engine\",\"role\":\"runtime\"},"
        "{\"path\":\"shared.dll\",\"role\":\"library\"},"
        "{\"path\":\"licenses/Nomad.txt\",\"role\":\"license\"}]}");
    stageSystemModule(directory, "shared native dependency");
    directory.write("modules/unselected/module.toml", "must not ship");
    directory.write(
        NomadPath("modules") / "game" / getModuleLibraryName("game"),
        "unselected game module must not ship"
    );
    const auto project = loadNomadProject(projectFile);
    const auto runtime = directory.getPath() / "runtime";
    const auto preview = packageProject(project, runtime, false, true);
    BOOST_TEST(!std::filesystem::exists(directory.getPath() / "dist"));
    const auto result = packageProject(project, runtime, false);
    BOOST_TEST(preview.files == result.files, boost::test_tools::per_element());
    BOOST_TEST(std::ranges::count(result.files, NomadPath("shared.dll")) == 1);
    const auto output = directory.getPath() / "extracted";
    extractArchive(result.output, output);
    BOOST_TEST(std::filesystem::is_regular_file(output / "modules" / "system" / getModuleLibraryName("system")));
    BOOST_TEST(std::filesystem::is_regular_file(output / "modules" / "system" / "module.toml"));
    BOOST_TEST(!std::filesystem::exists(output / getModuleLibraryName("system")));
    BOOST_TEST(!std::filesystem::exists(output / getModuleLibraryName("game")));
    BOOST_TEST(!std::filesystem::exists(output / "modules" / "unselected"));
    BOOST_TEST(!std::filesystem::exists(output / "modules" / "game"));
    BOOST_TEST(!std::filesystem::exists(output / "module.toml"));
    BOOST_TEST(!std::filesystem::exists(output / "runtime.json"));
    const auto release = NomadConfiguration::readFile(output / NOMAD_PROJECT_FILE_NAME);
    BOOST_TEST(release.requireString("custom-module.setting") == "preserved");
    const auto modules = release.requireStringArray("nomad.modules");
    BOOST_REQUIRE_EQUAL(modules.size(), 1U);
    BOOST_TEST(modules.front() == "system");

    const auto previousArchive = readFile(result.output);
    directory.write("modules/system/shared.dll", "different native dependency");
    BOOST_CHECK_THROW((void)packageProject(project, runtime, true, true), ProjectPackagingException);
    BOOST_CHECK_THROW((void)packageProject(project, runtime, true), ProjectPackagingException);
    BOOST_TEST(readFile(result.output) == previousArchive);
    BOOST_TEST(!std::filesystem::exists(NomadPath(pathToUtf8(result.output) + ".staging")));
}

BOOST_AUTO_TEST_CASE(module_scripts_keep_their_ownership_after_packaging_and_relocation)
{
    for (const auto& scripts : {"scripts", "source/library"}) {
        TestDirectory directory("nomad_module_scripts_package");
        auto text = NomadString(CONFIGURATION);
        text.replace(text.find("modules = []"), NomadStringView("modules = []").size(),
            "modules = ['fixture', 'system']");
        text.replace(text.find("directories = [\"assets\"]"), NomadStringView("directories = [\"assets\"]").size(),
            "directories = ['assets', 'modules/fixture/" + NomadString(scripts) + "']");
        const auto projectFile = directory.write(NOMAD_PROJECT_FILE_NAME, text);
        directory.write("assets/main.nomad",
            "settings = new packaged.Configuration\n"
            "assert settings.answer == 40\n"
            "value = moduleAnswer\nassert value == 42\n");
        directory.write("runtime/engine", "runtime");
        writeRuntimeManifest(directory, "engine");
        stageSystemModule(directory, "shared");
        stageFixtureModule(directory, NomadString(scripts) == "scripts" ?
            NomadString{} : "[scripts]\ndirectory = '" + NomadString(scripts) + "'\n");
        const auto moduleScript = NomadPath("modules/fixture") / scripts / "main.nomad";
        directory.write(moduleScript,
            "open hub packaged\nclass Configuration\nanswer = dependencyAnswer\nend\nend\n"
            "fun moduleAnswer\nreturn 42\nend\nassert false\n");
        directory.write("modules/system/scripts/main.nomad",
            "fun dependencyAnswer\nreturn 40\nend\nassert false\n");
        directory.write(NomadPath("modules/fixture") / scripts / "ignored.txt", "not a source");
        if (NomadString(scripts) != "scripts") {
            directory.write("modules/fixture/scripts/ignored.nomad", "not the configured script directory");
        }
        const auto project = loadNomadProject(projectFile);
        const auto runtime = directory.getPath() / "runtime";
        const auto preview = packageProject(project, runtime, false, true);
        BOOST_TEST(!std::filesystem::exists(directory.getPath() / "dist"));
        BOOST_TEST(!std::filesystem::exists(directory.getPath() / "module-events.txt"));
        const auto result = packageProject(project, runtime, false);
        BOOST_TEST(preview.files == result.files, boost::test_tools::per_element());
        const auto bytes = readFile(result.output);
        (void)packageProject(project, runtime, true);
        BOOST_TEST(readFile(result.output) == bytes);
        const auto output = directory.getPath() / "relocated";
        extractArchive(result.output, output);
        BOOST_TEST(readFile(output / moduleScript) == readFile(directory.getPath() / moduleScript));
        BOOST_TEST(readFile(output / "modules/fixture/module.toml") ==
            readFile(directory.getPath() / "modules/fixture/module.toml"));
        BOOST_TEST(!std::filesystem::exists(output / "modules/fixture/runtime.json"));
        BOOST_TEST(!std::filesystem::exists(output / getModuleLibraryName("fixture")));
        BOOST_TEST(readFile(output / "modules/fixture" / scripts / "ignored.txt") == "not a source");
        if (NomadString(scripts) != "scripts") {
            BOOST_TEST(!std::filesystem::exists(output / "modules/fixture/scripts"));
        }
        const auto release = loadNomadProject(output / NOMAD_PROJECT_FILE_NAME);
        BOOST_TEST(release.configuration.sdk.modules == std::vector<NomadString>({"system", "fixture"}),
            boost::test_tools::per_element());
        const auto roots = getScriptPaths(release.document);
        BOOST_REQUIRE(roots.size() == 2U);
        BOOST_TEST(roots[1] == NomadPath("modules/fixture") / scripts);
        const auto modules = resolveProjectModules(release, directory.getPath() / "unavailable-sdk");
        auto host = ModuleHost::create(release.document, output, modules, {}, ModuleMode::Analysis);
        const auto batch = host->loadSources(resolveScriptPaths(release.document, output));
        BOOST_REQUIRE(batch.diagnostics.empty());
        BOOST_TEST(batch.sources.size() == 3U);
        BOOST_TEST(std::ranges::count(batch.sources, NomadString("$fixture.main"), &ScriptSource::scriptName) == 1);
        const auto compilation = host->compileSources(batch, CompilationTarget::Analysis);
        NomadString diagnostics;
        for (const auto& diagnostic : compilation.diagnostics) {
            diagnostics += formatDiagnostic(diagnostic) + "\n";
        }
        BOOST_REQUIRE_MESSAGE(compilation.succeeded(), diagnostics);
        BOOST_TEST(host->getRuntime()->getFunctionId("$system.main").isValid());
        BOOST_TEST(host->getRuntime()->getFunctionId("$fixture.main").isValid());
        BOOST_TEST(host->getRuntime()->getFunctionId("main").isValid());
        host.reset();
        std::filesystem::remove(output / "modules/fixture/module.toml");
        BOOST_CHECK_THROW((void)resolveProjectModules(release, directory.getPath()), ModuleException);
    }
}

BOOST_AUTO_TEST_CASE(empty_explicit_module_script_directories_survive_packaging)
{
    TestDirectory directory("nomad_empty_module_scripts_package");
    const auto projectFile = directory.write(NOMAD_PROJECT_FILE_NAME, configurationWithSystemModule());
    directory.write("assets/main.nomad", "return\n");
    directory.write("runtime/engine", "runtime");
    writeRuntimeManifest(directory, "engine");
    stageSystemModule(directory, "shared");
    directory.write("modules/system/module.toml",
        "schema = 1\n[module]\nname = 'system'\n[scripts]\ndirectory = 'source/library'\n");
    std::filesystem::create_directories(directory.getPath() / "modules/system/source/library");
    const auto project = loadNomadProject(projectFile);
    const auto runtime = directory.getPath() / "runtime";
    const auto preview = packageProject(project, runtime, false, true);
    const auto result = packageProject(project, runtime, false);
    BOOST_TEST(preview.files == result.files, boost::test_tools::per_element());
    const auto output = directory.getPath() / "extracted";
    extractArchive(result.output, output);
    BOOST_TEST(std::filesystem::is_directory(output / "modules/system/source/library"));
    const auto release = loadNomadProject(output / NOMAD_PROJECT_FILE_NAME);
    const auto modules = resolveProjectModules(release, directory.getPath() / "missing-sdk");
    const auto batch = loadSourceRoots(getModuleScriptRoots(modules));
    BOOST_TEST(batch.diagnostics.empty());
    BOOST_TEST(batch.sources.empty());
}

BOOST_AUTO_TEST_CASE(invalid_module_script_roots_fail_packaging_and_dry_runs)
{
    TestDirectory directory("nomad_invalid_module_scripts_package");
    const auto projectFile = directory.write(NOMAD_PROJECT_FILE_NAME, configurationWithSystemModule());
    directory.write("assets/main.nomad", "return\n");
    directory.write("runtime/engine", "runtime");
    writeRuntimeManifest(directory, "engine");
    stageSystemModule(directory, "shared");
    directory.write("modules/system/module.toml",
        "schema = 1\n[module]\nname = 'system'\n[scripts]\ndirectory = 'missing'\n");
    const auto project = loadNomadProject(projectFile);
    const auto runtime = directory.getPath() / "runtime";
    BOOST_CHECK_THROW((void)packageProject(project, runtime, false, true), ProjectPackagingException);
    BOOST_CHECK_THROW((void)packageProject(project, runtime, false), ProjectPackagingException);
    BOOST_TEST(!std::filesystem::exists(directory.getPath() / "dist"));
}

BOOST_AUTO_TEST_CASE(game_packaging_hook_plans_generated_sprites_without_writing_during_dry_run)
{
    TestDirectory directory("nomad_game_packaging_hook");
    const auto sprite = directory.getPath() / "assets" / "sprites" / "player.png";
    std::filesystem::create_directories(sprite.parent_path());
    SdlSurfacePtr surface(SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32));
    BOOST_REQUIRE(surface != nullptr);
    BOOST_REQUIRE(SDL_FillSurfaceRect(
        surface.get(), nullptr, SDL_MapSurfaceRGBA(surface.get(), 255, 0, 0, 255)
    ));
    BOOST_REQUIRE(IMG_SavePNG(surface.get(), pathToUtf8(sprite).c_str()));
    const auto source = NomadConfiguration::parse(
        "[sprites]\ndirectories = [\"sprites\"]\nmax-texture-size = 32\npadding = 1\n"
    );
    auto release = source;
    std::set<NomadPath> planned;
    std::set<NomadPath> excluded;
    ModulePackageContext context{};
    context.source = &source;
    context.release = &release;
    context.resourceDirectory = directory.getPath() / "assets";
    context.stagingDirectory = directory.getPath() / "stage";
    context.dryRun = true;
    context.recordFile = [&](const NomadPath& path) {
        BOOST_REQUIRE(planned.insert(path).second);
    };
    context.excludeResource = [&](const NomadPath& path) {
        excluded.insert(path);
    };
    context.isResourceExcluded = [](const NomadPath&) { return false; };
    packageGameResources(&context);
    BOOST_TEST(!std::filesystem::exists(context.stagingDirectory));
    BOOST_TEST(excluded.contains(NomadPath("sprites") / "player.png"));
    BOOST_TEST(planned.contains(NomadPath("res") / "sprites" / NOMAD_SPRITE_CATALOG_FILE_NAME));
    BOOST_TEST(planned.size() == 2U);

    const auto dryRunPlan = planned;
    planned.clear();
    excluded.clear();
    context.dryRun = false;
    packageGameResources(&context);
    BOOST_TEST(planned == dryRunPlan, boost::test_tools::per_element());
    BOOST_TEST(!std::filesystem::exists(context.stagingDirectory / "res" / "sprites" / "player.png"));
    for (const auto& file : planned) {
        BOOST_TEST(std::filesystem::is_regular_file(context.stagingDirectory / file));
    }

    context.resourceDirectory = context.stagingDirectory / "res";
    context.isResourceExcluded = [](const NomadPath& path) {
        return path.filename() == NOMAD_SPRITE_CATALOG_FILE_NAME;
    };
    BOOST_CHECK_THROW(packageGameResources(&context), ModuleException);
}

BOOST_AUTO_TEST_CASE(failed_game_generation_preserves_previous_archive_and_cleans_staging)
{
    TestDirectory directory("nomad_package_failed_game_generation");
    auto text = NomadString(CONFIGURATION);
    text.replace(text.find("modules = []"), NomadStringView("modules = []").size(), "modules = [\"game\"]");
    text.replace(text.find("directories = [\"assets\"]"), NomadStringView("directories = [\"assets\"]").size(),
        "directories = [\"assets/scripts\", \"scripts\"]");
    const auto sdkVersion = getNomadVersion().toString();
    text.replace(text.find("version = \"0.1.0\""), NomadStringView("version = \"0.1.0\"").size(),
        "version = \"" + sdkVersion + "\"");
    const auto projectFile = directory.write(NOMAD_PROJECT_FILE_NAME, text);
    directory.write("assets/scripts/start.nomad", "return\n");
    directory.write("scripts/answer.nomad", "return 42\n");
    // Exercise real module hooks without copying and compressing the full SDK dependency bundle.
    directory.write("runtime/engine", "runtime");
    directory.write("runtime/licenses/Nomad.txt", "license");
    directory.write("runtime/runtime.json",
        "{\"schema\":1,\"version\":\"" + sdkVersion + "\",\"target\":\"" +
        NomadString(getPackagePlatform()) + "\",\"files\":["
        "{\"path\":\"engine\",\"role\":\"runtime\"},"
        "{\"path\":\"licenses/Nomad.txt\",\"role\":\"license\"}]}");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"game"});
    for (const auto& module : modules) {
        const auto destination = directory.getPath() / "modules" / pathFromUtf8(module.name);
        std::filesystem::create_directories(destination);
        const auto library = getModuleLibraryName(module.name);
        std::filesystem::copy_file(module.directory / library, destination / library);
        std::filesystem::copy_file(module.directory / "module.toml", destination / "module.toml");
        directory.write(
            NomadPath("modules") / pathFromUtf8(module.name) / "runtime.json",
            R"({"schema":1,"files":[]})"
        );
    }
    const auto project = loadNomadProject(projectFile);
    const auto runtime = directory.getPath() / "runtime";
    const auto original = packageProject(project, runtime, false);
    const auto originalBytes = readFile(original.output);
    const auto extracted = directory.getPath() / "extracted";
    extractArchive(original.output, extracted);
    const auto release = loadNomadProject(extracted / NOMAD_PROJECT_FILE_NAME);
    const auto roots = getScriptPaths(release.document);
    BOOST_REQUIRE(roots.size() == 2U);
    BOOST_TEST(roots[0] == NomadPath("res/scripts"));
    BOOST_TEST(roots[1] == NomadPath("scripts"));
    BOOST_TEST(std::filesystem::is_regular_file(extracted / "res" / "scripts" / "start.nomad"));
    BOOST_TEST(std::filesystem::is_regular_file(extracted / "scripts" / "answer.nomad"));
    const auto paths = resolveScriptPaths(release.document, extracted);
    const auto batch = loadSourcePaths(paths);
    BOOST_REQUIRE(batch.diagnostics.empty());
    BOOST_REQUIRE(batch.sources.size() == 2U);
    BOOST_TEST(batch.sources[0].scriptName == "answer");
    BOOST_TEST(batch.sources[1].scriptName == "start");
    directory.write("assets/sprites/broken.png", "invalid PNG");
    const auto reportsSpriteFailure = [](const ProjectPackagingException& error) {
        return NomadStringView(error.what()).find("Failed to compile sprite resources") != NomadStringView::npos;
    };
    BOOST_CHECK_EXCEPTION((void)packageProject(project, runtime, true, true),
        ProjectPackagingException, reportsSpriteFailure);
    BOOST_CHECK_EXCEPTION((void)packageProject(project, runtime, true),
        ProjectPackagingException, reportsSpriteFailure);
    BOOST_TEST(readFile(original.output) == originalBytes);
    BOOST_TEST(!std::filesystem::exists(NomadPath(pathToUtf8(original.output) + ".staging")));
    BOOST_TEST(!std::filesystem::exists(NomadPath(pathToUtf8(original.output) + ".tmp")));
    BOOST_TEST(!std::filesystem::exists(NomadPath(pathToUtf8(original.output) + ".backup")));
}

BOOST_AUTO_TEST_SUITE_END()
