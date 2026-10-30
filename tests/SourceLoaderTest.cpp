// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compilation.hpp>
#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/SourceLoader.hpp>

#include <nomad/script/Runtime.hpp>

#include <nomad/system/Path.hpp>

#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <vector>

using namespace nomad;
using namespace nomad::test;

BOOST_AUTO_TEST_SUITE(source_overlays)

BOOST_AUTO_TEST_CASE(script_roots_default_to_the_project_scripts_directory_independently_of_game_settings)
{
    const TestDirectory directory("nomad_script_roots_default");
    directory.write("scripts/zebra.nomad", "return 2");
    directory.write("scripts/alpha.nomad", "return 1");
    directory.write("assets/scripts/ignored.nomad", "return 3");
    const auto configuration = NomadConfiguration::parse(
        "[resources]\ndirectory = 'assets'\n[audio]\ndirectories = false\n"
    );
    const auto roots = resolveScriptPaths(configuration, directory.getPath());
    BOOST_REQUIRE(roots.size() == 1U);
    BOOST_TEST(roots[0] == directory.getPath() / "scripts");
    const auto batch = loadSourcePaths(roots);
    BOOST_REQUIRE(batch.diagnostics.empty());
    BOOST_REQUIRE(batch.sources.size() == 2U);
    BOOST_TEST(batch.sources[0].scriptName == "alpha");
    BOOST_TEST(batch.sources[1].scriptName == "zebra");
}

BOOST_AUTO_TEST_CASE(configured_script_roots_are_normalized_and_resolved_relative_to_the_project)
{
    const TestDirectory directory("nomad_script_roots_configured");
    directory.write("res/scripts/main.nomad", "return 1");
    directory.write("mods/answer.nomad", "return 2");
    const auto configuration = NomadConfiguration::parse(
        "[scripts]\ndirectories = ['res\\scripts\\nested\\..', 'mods/']\n"
        "[resources]\ndirectory = 'different-assets'\n"
    );
    const auto directories = getScriptPaths(configuration);
    BOOST_REQUIRE(directories.size() == 2U);
    BOOST_TEST(directories[0] == NomadPath("res/scripts"));
    BOOST_TEST(directories[1] == NomadPath("mods"));
    const auto roots = resolveScriptPaths(configuration, directory.getPath());
    BOOST_REQUIRE(roots.size() == 2U);
    BOOST_TEST(roots[0] == directory.getPath() / "res" / "scripts");
    BOOST_TEST(roots[1] == directory.getPath() / "mods");
    const auto batch = loadSourcePaths(roots);
    BOOST_REQUIRE(batch.diagnostics.empty());
    BOOST_REQUIRE(batch.sources.size() == 2U);
    BOOST_TEST(batch.sources[0].scriptName == "answer");
    BOOST_TEST(batch.sources[1].scriptName == "main");
}

BOOST_AUTO_TEST_CASE(empty_script_roots_disable_disk_discovery)
{
    const auto configuration = NomadConfiguration::parse("[scripts]\ndirectories = []\n");
    BOOST_TEST(getScriptPaths(configuration).empty());
    BOOST_TEST(resolveScriptPaths(configuration, std::filesystem::current_path()).empty());
}

BOOST_AUTO_TEST_CASE(compiler_rejects_invalid_script_sections_and_directory_lists)
{
    for (const auto text : {
        "scripts = false",
        "[scripts]\ndirectories = false",
        "[scripts]\ndirectories = ['scripts', 42]",
        "[scripts]\ndirectories = ['']",
        "[scripts]\ndirectories = ['.']",
        "[scripts]\ndirectories = ['/scripts']",
#ifdef _WIN32
        "[scripts]\ndirectories = ['C:\\scripts']",
        "[scripts]\ndirectories = ['C:scripts']",
#endif
        "[scripts]\ndirectories = ['../scripts']",
        "[scripts]\ndirectories = ['scripts', 'scripts/nested']",
        "[scripts]\ndirectories = ['scripts/nested', 'scripts']",
        "[scripts]\ndirectories = ['Scripts', 'scripts']"
    }) {
        BOOST_TEST_CONTEXT(text) {
            const auto configuration = NomadConfiguration::parse(text);
            BOOST_CHECK_EXCEPTION(
                (void)getScriptPaths(configuration),
                NomadConfigurationException,
                [](const auto& error) {
                    return NomadStringView(error.what()).find("scripts") != NomadStringView::npos;
                }
            );
        }
    }
}

BOOST_AUTO_TEST_CASE(editor_text_replaces_disk_snapshot_and_keeps_discovered_script_name)
{
    TestDirectory directory("nomad_overlay_existing");
    const auto path = directory.write("entities/player.nomad", "return missing");
    const std::vector<ScriptSource> overrides{
        {"ignored.for.discovered.file", {"editor:player", "return 42", path}}
    };
    const auto batch = loadSourcePaths(std::vector{directory.getPath()}, overrides);

    BOOST_REQUIRE(batch.diagnostics.empty());
    BOOST_REQUIRE(batch.sources.size() == 1U);
    BOOST_TEST(batch.sources[0].scriptName == "entities.player");
    BOOST_TEST(batch.sources[0].source.sourceName == "editor:player");
    BOOST_TEST(batch.sources[0].source.text == "return 42");
    BOOST_REQUIRE(batch.sources[0].source.filePath.has_value());
    BOOST_TEST(*batch.sources[0].source.filePath == path);
    Runtime editorRuntime;
    auto editorCompiler = editorRuntime.createCompiler();
    BOOST_TEST(compileSources(batch, *editorCompiler).succeeded());
    Runtime diskRuntime;
    auto diskCompiler = diskRuntime.createCompiler();
    BOOST_TEST(!compileSources(loadSourcePaths(std::vector{path}), *diskCompiler).succeeded());
}

BOOST_AUTO_TEST_CASE(overrides_include_new_file_backed_and_untitled_scripts)
{
    TestDirectory directory("nomad_overlay_new");
    directory.write("init.nomad", "return helpers.answer + bonus");
    const auto path = directory.getPath() / "helpers" / "answer.nomad";
    const std::vector<ScriptSource> overrides{
        {"helpers.answer", {"editor:answer", "return 40", path}},
        {"bonus", {"untitled:bonus", "return 2"}}
    };
    const auto batch = loadSourcePaths(std::vector{directory.getPath()}, overrides);

    BOOST_REQUIRE(batch.diagnostics.empty());
    BOOST_REQUIRE(batch.sources.size() == 3U);
    BOOST_TEST(batch.sources[0].scriptName == "bonus");
    BOOST_TEST(!batch.sources[0].source.filePath.has_value());
    BOOST_TEST(batch.sources[1].scriptName == "helpers.answer");
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    BOOST_TEST(compileSources(batch, *compiler).succeeded());
    BOOST_TEST(!std::filesystem::exists(path));
}

BOOST_AUTO_TEST_CASE(explicit_missing_file_uses_editor_snapshot_without_loading_error)
{
    TestDirectory directory("nomad_overlay_deleted");
    const auto path = directory.getPath() / "answer.nomad";
    const std::vector<ScriptSource> overrides{{"other", {"editor:answer", "return 42", path}}};
    const auto batch = loadSourcePaths(std::vector{path}, overrides);

    BOOST_REQUIRE(batch.diagnostics.empty());
    BOOST_REQUIRE(batch.sources.size() == 1U);
    BOOST_TEST(batch.sources[0].scriptName == "answer");
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    BOOST_TEST(compileSources(batch, *compiler).succeeded());
    BOOST_TEST(!std::filesystem::exists(path));
}

BOOST_AUTO_TEST_CASE(path_identity_handles_relative_and_lexically_equivalent_paths)
{
    const auto root = std::filesystem::current_path();
    const auto path = root / "nomad_overlay_normalized" / "answer.nomad";
    const auto relative = path.lexically_relative(root);
    BOOST_REQUIRE(!relative.empty());
    const auto alternate = relative.parent_path() / "unused" / ".." / relative.filename();
    const std::vector<ScriptSource> overrides{{"unused", {"editor:answer", "return 42", alternate}}};
    const auto batch = loadSourcePaths(std::vector{path}, overrides);

    BOOST_REQUIRE(batch.diagnostics.empty());
    BOOST_REQUIRE(batch.sources.size() == 1U);
    BOOST_TEST(batch.sources[0].source.sourceName == "editor:answer");
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    BOOST_TEST(compileSources(batch, *compiler).succeeded());
}

BOOST_AUTO_TEST_CASE(duplicate_file_overrides_report_an_error_instead_of_choosing_a_version)
{
    TestDirectory directory("nomad_overlay_duplicate");
    const auto path = directory.getPath() / "answer.nomad";
    const std::vector<ScriptSource> overrides{
        {"first", {"editor:first", "return 1", path}},
        {"second", {"editor:second", "return 2", path.parent_path() / "." / path.filename()}}
    };
    const auto batch = loadSourcePaths({}, overrides);

    BOOST_REQUIRE(batch.diagnostics.size() == 1U);
    BOOST_TEST(batch.diagnostics[0].sourceName == "editor:second");
    BOOST_TEST(batch.diagnostics[0].message.find("Duplicate source override") != NomadString::npos);
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    BOOST_TEST(!compileSources(batch, *compiler).succeeded());
}

BOOST_AUTO_TEST_CASE(untitled_identity_uses_exact_source_name_not_script_name)
{
    const std::vector<ScriptSource> overrides{
        {"first", {"untitled:document", "return 1"}},
        {"second", {"untitled:document", "return 2"}}
    };
    const auto batch = loadSourcePaths({}, overrides);

    BOOST_REQUIRE(batch.diagnostics.size() == 1U);
    BOOST_TEST(batch.diagnostics[0].sourceName == "untitled:document");
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    BOOST_TEST(!compileSources(batch, *compiler).succeeded());
}

BOOST_AUTO_TEST_CASE(different_documents_with_same_script_name_remain_a_compilation_collision)
{
    TestDirectory directory("nomad_overlay_collision");
    directory.write("answer.nomad", "return 1");
    const std::vector<ScriptSource> overrides{{"answer", {"untitled:answer", "return 2"}}};
    const auto batch = loadSourcePaths(std::vector{directory.getPath()}, overrides);

    BOOST_TEST(batch.diagnostics.empty());
    BOOST_REQUIRE(batch.sources.size() == 2U);
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    const auto result = compileSources(batch, *compiler);
    BOOST_TEST(!result.succeeded());
    BOOST_REQUIRE(!result.diagnostics.empty());
    BOOST_TEST(result.diagnostics[0].message.find("already used") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(unrelated_disk_loading_failures_are_not_suppressed)
{
    TestDirectory directory("nomad_overlay_errors");
    const auto missing = directory.getPath() / "missing.nomad";
    const std::vector<ScriptSource> overrides{{"answer", {"untitled:answer", "return 42"}}};
    const auto batch = loadSourcePaths(std::vector{missing}, overrides);

    BOOST_REQUIRE(batch.diagnostics.size() == 1U);
    BOOST_TEST(batch.diagnostics[0].message == "Path does not exist");
    BOOST_REQUIRE(batch.sources.size() == 1U);
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    BOOST_TEST(!compileSources(batch, *compiler).succeeded());
}

BOOST_AUTO_TEST_CASE(empty_backing_paths_are_errors_not_untitled_documents)
{
    const std::vector<ScriptSource> overrides{
        {"answer", {"editor:answer", "return 42", std::filesystem::path{}}}
    };
    const auto batch = loadSourcePaths({}, overrides);

    BOOST_REQUIRE(batch.diagnostics.size() == 1U);
    BOOST_TEST(batch.diagnostics[0].sourceName == "editor:answer");
    BOOST_TEST(batch.sources.empty());
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    BOOST_TEST(!compileSources(batch, *compiler).succeeded());
}

BOOST_AUTO_TEST_CASE(removing_override_restores_disk_snapshot)
{
    TestDirectory directory("nomad_overlay_close");
    const auto path = directory.write("answer.nomad", "return missing");
    const std::vector<ScriptSource> overrides{{"answer", {"editor:answer", "return 42", path}}};
    Runtime editorRuntime;
    auto editorCompiler = editorRuntime.createCompiler();
    BOOST_TEST(compileSources(loadSourcePaths(std::vector{directory.getPath()}, overrides), *editorCompiler).succeeded());
    Runtime diskRuntime;
    auto diskCompiler = diskRuntime.createCompiler();
    BOOST_TEST(!compileSources(loadSourcePaths(std::vector{directory.getPath()}), *diskCompiler).succeeded());
}

BOOST_AUTO_TEST_CASE(overlay_diagnostics_use_editor_source_name_and_current_text)
{
    TestDirectory directory("nomad_overlay_diagnostics");
    const auto path = directory.write("answer.nomad", "return 42");
    const std::vector<ScriptSource> overrides{
        {"answer", {"editor:answer", "value = 1\nreturn missing", path}}
    };
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    const auto result = compileSources(loadSourcePaths(std::vector{directory.getPath()}, overrides), *compiler);

    BOOST_TEST(!result.succeeded());
    BOOST_REQUIRE(result.diagnostics.size() == 1U);
    BOOST_TEST(result.diagnostics[0].sourceName == "editor:answer");
    BOOST_TEST(result.diagnostics[0].line == 2U);
}

BOOST_AUTO_TEST_CASE(module_sources_keep_owner_relative_path_and_application_file_names) {
    TestDirectory directory("nomad_module_source_identity");
    directory.write("application/main.nomad", "return 1");
    directory.write("os/main.nomad", "return 2");
    const auto modulePath = directory.write("os/utilities/paths.nomad", "return 3");
    const std::vector<SourceRoot> roots{
        {directory.getPath() / "application"},
        {directory.getPath() / "os", "os"}
    };
    const auto batch = loadSourceRoots(roots);
    BOOST_REQUIRE(batch.diagnostics.empty());
    BOOST_REQUIRE(batch.sources.size() == 3U);
    BOOST_TEST(batch.sources[0].scriptName == "$os.main");
    BOOST_TEST(batch.sources[1].scriptName == "$os.utilities.paths");
    BOOST_TEST(batch.sources[1].owner == "os");
    BOOST_TEST(batch.sources[1].relativePath == NomadPath("utilities/paths.nomad"));
    BOOST_TEST(batch.sources[1].source.sourceName == pathToGenericUtf8(modulePath));
    BOOST_TEST(batch.sources[2].scriptName == "main");
    BOOST_TEST(batch.sources[2].owner.empty());
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    BOOST_TEST(compileSources(batch, *compiler).succeeded());
}

BOOST_AUTO_TEST_CASE(module_ownership_wins_over_repeated_or_overlapping_application_roots) {
    TestDirectory directory("nomad_module_source_deduplication");
    const auto modulePath = directory.write("modules/os/scripts/main.nomad", "return 42");
    const auto moduleRoot = modulePath.parent_path();
    const std::vector<SourceRoot> roots{
        {directory.getPath()}, {moduleRoot}, {moduleRoot, "os"}, {moduleRoot, "os"}, {modulePath}
    };
    const auto batch = loadSourceRoots(roots);
    BOOST_REQUIRE(batch.diagnostics.empty());
    BOOST_REQUIRE(batch.sources.size() == 1U);
    BOOST_TEST(batch.sources[0].scriptName == "$os.main");
    BOOST_TEST(batch.sources[0].owner == "os");
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    BOOST_TEST(compileSources(batch, *compiler).succeeded());
}

BOOST_AUTO_TEST_CASE(dotted_module_identity_collisions_and_conflicting_owners_are_errors) {
    TestDirectory directory("nomad_module_source_collisions");
    directory.write("utilities.paths.nomad", "return 1");
    directory.write("utilities/paths.nomad", "return 2");
    const std::vector<SourceRoot> roots{{directory.getPath(), "os"}};
    const auto batch = loadSourceRoots(roots);
    BOOST_REQUIRE(batch.diagnostics.size() == 1U);
    BOOST_TEST(batch.diagnostics[0].message.find("$os.utilities.paths") != NomadString::npos);
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    BOOST_TEST(!compileSources(batch, *compiler).succeeded());
    const std::vector<SourceRoot> conflicting{
        {directory.getPath() / "utilities", "os"},
        {directory.getPath() / "utilities", "other"}
    };
    const auto duplicate = loadSourceRoots(conflicting);
    BOOST_REQUIRE(duplicate.diagnostics.size() == 1U);
    BOOST_TEST(duplicate.diagnostics[0].message.find("both modules") != NomadString::npos);
    Runtime conflictingRuntime;
    auto conflictingCompiler = conflictingRuntime.createCompiler();
    BOOST_TEST(!compileSources(duplicate, *conflictingCompiler).succeeded());
}

BOOST_AUTO_TEST_CASE(module_overrides_preserve_ownership_for_existing_and_unsaved_files) {
    TestDirectory directory("nomad_module_source_overrides");
    const auto path = directory.write("main.nomad", "return missing");
    const auto unsaved = directory.getPath() / "nested" / "new.nomad";
    const std::vector<SourceRoot> roots{{directory.getPath(), "os"}};
    const std::vector<ScriptSource> overrides{
        {"ignored", {"editor:main", "return 42", path}},
        {"also.ignored", {"editor:new", "return 7", unsaved}}
    };
    const auto batch = loadSourceRoots(roots, overrides);
    BOOST_REQUIRE(batch.diagnostics.empty());
    BOOST_REQUIRE(batch.sources.size() == 2U);
    BOOST_TEST(batch.sources[0].scriptName == "$os.main");
    BOOST_TEST(batch.sources[0].source.sourceName == "editor:main");
    BOOST_TEST(batch.sources[1].scriptName == "$os.nested.new");
    BOOST_TEST(batch.sources[1].relativePath == NomadPath("nested/new.nomad"));
    BOOST_TEST(batch.sources[1].owner == "os");
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    BOOST_TEST(compileSources(batch, *compiler).succeeded());
}

BOOST_AUTO_TEST_CASE(missing_default_module_directories_are_optional_but_explicit_roots_are_not) {
    TestDirectory directory("nomad_module_source_missing");
    const auto missing = directory.getPath() / "scripts";
    BOOST_TEST(loadSourceRoots(std::vector<SourceRoot>{{missing, "os", false}}).diagnostics.empty());
    const auto required = loadSourceRoots(std::vector<SourceRoot>{{missing, "os"}});
    BOOST_REQUIRE(required.diagnostics.size() == 1U);
    BOOST_TEST(required.diagnostics[0].message.find("os") != NomadString::npos);
    const auto file = directory.write("file.nomad", "return 42");
    const auto notDirectory = loadSourceRoots(std::vector<SourceRoot>{{file, "os"}});
    BOOST_REQUIRE(notDirectory.diagnostics.size() == 1U);
    BOOST_TEST(notDirectory.diagnostics[0].message.find("script directory") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(module_source_identity_is_stable_when_its_directory_moves) {
    TestDirectory directory("nomad_module_source_relocation");
    const auto original = directory.getPath() / "original";
    const auto relocated = directory.getPath() / "relocated";
    directory.write("original/scripts/nested/main.nomad", "return 42");
    const auto before = loadSourceRoots(std::vector<SourceRoot>{{original / "scripts", "os"}});
    std::filesystem::rename(original, relocated);
    const auto after = loadSourceRoots(std::vector<SourceRoot>{{relocated / "scripts", "os"}});
    BOOST_REQUIRE(before.diagnostics.empty());
    BOOST_REQUIRE(after.diagnostics.empty());
    BOOST_REQUIRE(before.sources.size() == 1U);
    BOOST_REQUIRE(after.sources.size() == 1U);
    BOOST_TEST(before.sources[0].scriptName == after.sources[0].scriptName);
    BOOST_TEST(before.sources[0].relativePath == after.sources[0].relativePath);
    BOOST_TEST(before.sources[0].source.sourceName != after.sources[0].source.sourceName);
}

#ifdef _WIN32
BOOST_AUTO_TEST_CASE(windows_path_matching_is_case_insensitive)
{
    TestDirectory directory("nomad_overlay_case");
    const auto path = directory.write("answer.nomad", "return missing");
    const auto alternate = path.parent_path() / "ANSWER.nomad";
    const std::vector<ScriptSource> overrides{{"answer", {"editor:answer", "return 42", alternate}}};
    const auto batch = loadSourcePaths(std::vector{directory.getPath()}, overrides);

    BOOST_REQUIRE(batch.diagnostics.empty());
    BOOST_REQUIRE(batch.sources.size() == 1U);
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    BOOST_TEST(compileSources(batch, *compiler).succeeded());
}

BOOST_AUTO_TEST_CASE(editor_override_bypasses_exclusively_locked_disk_file)
{
    TestDirectory directory("nomad_overlay_locked");
    const auto path = directory.write("answer.nomad", "return missing");
    struct FileLock {
        HANDLE handle;
        ~FileLock() {
            if (handle != INVALID_HANDLE_VALUE) {
                CloseHandle(handle);
            }
        }
    };
    const FileLock lock{CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr)};
    BOOST_REQUIRE(lock.handle != INVALID_HANDLE_VALUE);

    const std::vector<ScriptSource> overrides{{"answer", {"editor:answer", "return 42", path}}};
    const auto batch = loadSourcePaths(std::vector{directory.getPath()}, overrides);
    BOOST_REQUIRE(batch.diagnostics.empty());
    BOOST_REQUIRE(batch.sources.size() == 1U);
    Runtime editorRuntime;
    auto editorCompiler = editorRuntime.createCompiler();
    BOOST_TEST(compileSources(batch, *editorCompiler).succeeded());
    Runtime diskRuntime;
    auto diskCompiler = diskRuntime.createCompiler();
    BOOST_TEST(!compileSources(loadSourcePaths(std::vector{path}), *diskCompiler).succeeded());
}
#else
BOOST_AUTO_TEST_CASE(directory_aliases_do_not_duplicate_module_sources_or_break_overrides)
{
    TestDirectory directory("nomad_module_directory_alias");
    const auto path = directory.write("source/main.nomad", "return missing");
    const auto alias = directory.getPath() / "alias";
    std::filesystem::create_directory_symlink(path.parent_path(), alias);
    const std::vector<SourceRoot> roots{{alias, "os"}, {path.parent_path()}};
    const std::vector<ScriptSource> overrides{
        {"ignored", {"editor:main", "return 42", path}}
    };
    const auto batch = loadSourceRoots(roots, overrides);
    BOOST_REQUIRE(batch.diagnostics.empty());
    BOOST_REQUIRE(batch.sources.size() == 1U);
    BOOST_TEST(batch.sources[0].scriptName == "$os.main");
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    BOOST_TEST(compileSources(batch, *compiler).succeeded());
}

BOOST_AUTO_TEST_CASE(non_windows_path_matching_preserves_case)
{
    const auto root = std::filesystem::current_path();
    const std::vector<ScriptSource> overrides{
        {"lower", {"editor:lower", "return 1", root / "answer.nomad"}},
        {"upper", {"editor:upper", "return 2", root / "ANSWER.nomad"}}
    };
    const auto batch = loadSourcePaths({}, overrides);
    BOOST_TEST(batch.diagnostics.empty());
    BOOST_REQUIRE(batch.sources.size() == 2U);
}
#endif

BOOST_AUTO_TEST_SUITE_END()
