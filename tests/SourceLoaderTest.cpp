// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/CompilerTools.hpp>
#include <nomad/compiler/SourceLoader.hpp>

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
    BOOST_TEST(checkSources(batch).succeeded());
    BOOST_TEST(!checkPath(path).succeeded());
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
    BOOST_TEST(checkSources(batch).succeeded());
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
    BOOST_TEST(checkSources(batch).succeeded());
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
    BOOST_TEST(checkSources(batch).succeeded());
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
    BOOST_TEST(!checkSources(batch).succeeded());
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
    BOOST_TEST(!checkSources(batch).succeeded());
}

BOOST_AUTO_TEST_CASE(different_documents_with_same_script_name_remain_a_compilation_collision)
{
    TestDirectory directory("nomad_overlay_collision");
    directory.write("answer.nomad", "return 1");
    const std::vector<ScriptSource> overrides{{"answer", {"untitled:answer", "return 2"}}};
    const auto batch = loadSourcePaths(std::vector{directory.getPath()}, overrides);

    BOOST_TEST(batch.diagnostics.empty());
    BOOST_REQUIRE(batch.sources.size() == 2U);
    const auto result = checkSources(batch);
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
    BOOST_TEST(!checkSources(batch).succeeded());
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
    BOOST_TEST(!checkSources(batch).succeeded());
}

BOOST_AUTO_TEST_CASE(removing_override_restores_disk_snapshot)
{
    TestDirectory directory("nomad_overlay_close");
    const auto path = directory.write("answer.nomad", "return missing");
    const std::vector<ScriptSource> overrides{{"answer", {"editor:answer", "return 42", path}}};
    BOOST_TEST(checkSources(loadSourcePaths(std::vector{directory.getPath()}, overrides)).succeeded());
    BOOST_TEST(!checkSources(loadSourcePaths(std::vector{directory.getPath()})).succeeded());
}

BOOST_AUTO_TEST_CASE(overlay_diagnostics_use_editor_source_name_and_current_text)
{
    TestDirectory directory("nomad_overlay_diagnostics");
    const auto path = directory.write("answer.nomad", "return 42");
    const std::vector<ScriptSource> overrides{
        {"answer", {"editor:answer", "value = 1\nreturn missing", path}}
    };
    const auto result = checkSources(loadSourcePaths(std::vector{directory.getPath()}, overrides));

    BOOST_TEST(!result.succeeded());
    BOOST_REQUIRE(result.diagnostics.size() == 1U);
    BOOST_TEST(result.diagnostics[0].sourceName == "editor:answer");
    BOOST_TEST(result.diagnostics[0].line == 2U);
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
    BOOST_TEST(checkSources(batch).succeeded());
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
    BOOST_TEST(checkSources(batch).succeeded());
    BOOST_TEST(!checkPath(path).succeeded());
}
#else
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
