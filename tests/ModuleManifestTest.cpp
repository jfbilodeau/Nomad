// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/system/ModuleManifest.hpp>

#include <TestDirectory.hpp>

#include <boost/predef/os.h>
#include <boost/test/unit_test.hpp>

using namespace nomad;
using namespace nomad::test;

BOOST_AUTO_TEST_SUITE(module_manifest)

BOOST_AUTO_TEST_CASE(native_library_names_include_the_nomad_marker_and_platform_conventions) {
    for (const auto* name : {"collections", "game", "log", "os", "system", "custom-module", "lib", "library", "lib-custom"}) {
#if BOOST_OS_WINDOWS
        const auto expected = NomadString(name) + ".nomad.dll";
#elif BOOST_OS_MACOS || BOOST_OS_IOS
        const auto expected = "lib" + NomadString(name) + ".nomad.dylib";
#else
        const auto expected = "lib" + NomadString(name) + ".nomad.so";
#endif
        BOOST_TEST(getModuleLibraryName(name).string() == expected);
    }
    for (const auto* name : {"", "../log", "Log", "log.nomad", "log/module"}) {
        BOOST_CHECK_THROW(getModuleLibraryName(name), ModuleException);
    }
}

BOOST_AUTO_TEST_CASE(resolves_dependencies_before_dependents_once_in_declared_order) {
    TestDirectory directory("nomad_module_dependencies");
    directory.write("system/module.toml",
        "schema = 1\n[module]\nname = 'system'\n");
    directory.write("game/module.toml",
        "schema = 1\n[module]\nname = 'game'\n[nomad]\nmodules = ['system']\n");
    directory.write("telemetry/module.toml",
        "schema = 1\n[module]\nname = 'telemetry'\n[nomad]\nmodules = ['system']\n");
    const auto modules = resolveModules(directory.getPath(), {"game", "telemetry"});
    BOOST_REQUIRE_EQUAL(modules.size(), 3U);
    BOOST_TEST(modules[0].name == "system");
    BOOST_TEST(modules[1].name == "game");
    BOOST_TEST(modules[2].name == "telemetry");
}

BOOST_AUTO_TEST_CASE(reports_cycles_with_dependency_chain) {
    TestDirectory directory("nomad_module_cycle");
    directory.write("first/module.toml",
        "schema = 1\n[module]\nname = 'first'\n[nomad]\nmodules = ['second']\n");
    directory.write("second/module.toml",
        "schema = 1\n[module]\nname = 'second'\n[nomad]\nmodules = ['first']\n");
    BOOST_CHECK_EXCEPTION(
        resolveModules(directory.getPath(), {"first"}),
        ModuleException,
        [](const auto& error) {
            return NomadString(error.what()).find("first -> second -> first") != NomadString::npos;
        }
    );
}

BOOST_AUTO_TEST_CASE(resolves_transitive_dependencies_and_explicit_roots_without_duplicates) {
    TestDirectory directory("nomad_module_transitive");
    directory.write("base/module.toml", "schema = 1\n[module]\nname = 'base'\n");
    directory.write("system/module.toml",
        "schema = 1\n[module]\nname = 'system'\n[nomad]\nmodules = ['base']\n");
    directory.write("game/module.toml",
        "schema = 1\n[module]\nname = 'game'\n[nomad]\nmodules = ['system']\n");
    const auto modules = resolveModules(directory.getPath(), {"game", "base", "system"});
    BOOST_REQUIRE_EQUAL(modules.size(), 3U);
    BOOST_TEST(modules[0].name == "base");
    BOOST_TEST(modules[1].name == "system");
    BOOST_TEST(modules[2].name == "game");
}

BOOST_AUTO_TEST_CASE(rejects_invalid_missing_and_mismatched_modules) {
    TestDirectory directory("nomad_module_invalid");
    BOOST_CHECK_THROW(resolveModules(directory.getPath(), {"../outside"}), ModuleException);
    BOOST_CHECK_THROW(resolveModules(directory.getPath(), {"missing"}), ModuleException);
    directory.write("wrong/module.toml", "schema = 1\n[module]\nname = 'different'\n");
    BOOST_CHECK_THROW(resolveModules(directory.getPath(), {"wrong"}), ModuleException);
}

BOOST_AUTO_TEST_CASE(rejects_invalid_manifest_fields) {
    TestDirectory directory("nomad_module_fields");
    for (const auto* content : {
        "schema = 2\n[module]\nname = 'module'\n",
        "schema = 1\n[module]\nname = 'module'\n[nomad]\nmodules = 'other'\n",
        "schema = 1\n[module]\nname = 'module'\n[nomad]\nmodules = [1]\n",
        "schema = 1\n[module]\nname = 'module'\n[nomad]\nmodules = ['other', 'other']\n"
    }) {
        directory.write("module/module.toml", content);
        BOOST_CHECK_THROW(resolveModules(directory.getPath(), {"module"}), ModuleException);
    }
}

BOOST_AUTO_TEST_CASE(empty_requests_resolve_to_no_modules) {
    TestDirectory directory("nomad_module_empty");
    BOOST_TEST(resolveModules(directory.getPath(), {}).empty());
}

BOOST_AUTO_TEST_CASE(module_script_directory_defaults_and_explicit_paths_are_retained) {
    TestDirectory directory("nomad_module_script_directory");
    directory.write("module.toml", "schema = 1\n[module]\nname = 'example'\n");
    const auto defaults = loadModuleManifest(directory.getPath());
    BOOST_REQUIRE(defaults.scriptsDirectory.has_value());
    BOOST_TEST(*defaults.scriptsDirectory == NomadPath("scripts"));
    BOOST_TEST(!defaults.scriptsDirectoryRequired);
    directory.write("module.toml",
        "schema = 1\n[module]\nname = 'example'\n[scripts]\ndirectory = 'source\\nested\\..'\n");
    const auto configured = loadModuleManifest(directory.getPath());
    BOOST_REQUIRE(configured.scriptsDirectory.has_value());
    BOOST_TEST(*configured.scriptsDirectory == NomadPath("source"));
    BOOST_TEST(configured.scriptsDirectoryRequired);
}

BOOST_AUTO_TEST_CASE(invalid_module_script_directories_are_manifest_errors) {
    TestDirectory directory("nomad_module_script_directory_invalid");
    for (const auto* setting : {
        "scripts = false\n",
        "[scripts]\ndirectory = false\n",
        "[scripts]\ndirectory = ['source']\n",
        "[scripts]\ndirectory = ''\n",
        "[scripts]\ndirectory = '.'\n",
        "[scripts]\ndirectory = '../outside'\n",
        "[scripts]\ndirectory = '/outside'\n"
    }) {
        directory.write("module.toml", NomadString("schema = 1\n") + setting + "[module]\nname = 'example'\n");
        BOOST_CHECK_EXCEPTION((void)loadModuleManifest(directory.getPath()), ModuleException, [](const auto& error) {
            return NomadStringView(error.what()).find("scripts") != NomadStringView::npos;
        });
    }
}

#if !BOOST_OS_WINDOWS
BOOST_AUTO_TEST_CASE(module_script_directories_cannot_escape_through_directory_links) {
    TestDirectory directory("nomad_module_script_directory_link");
    directory.write("module/module.toml", "schema = 1\n[module]\nname = 'example'\n");
    directory.write("outside/main.nomad", "return 42");
    std::filesystem::create_directory_symlink(
        directory.getPath() / "outside", directory.getPath() / "module" / "scripts"
    );
    BOOST_CHECK_THROW((void)loadModuleManifest(directory.getPath() / "module"), ModuleException);
}
#endif

BOOST_AUTO_TEST_SUITE_END()
