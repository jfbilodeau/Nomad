// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/system/ModuleManifest.hpp>

#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

using namespace nomad;
using namespace nomad::test;

BOOST_AUTO_TEST_SUITE(module_manifest)

BOOST_AUTO_TEST_CASE(resolves_dependencies_before_dependents_once_in_declared_order) {
    TestDirectory directory("nomad_module_dependencies");
    directory.write("nomad-system/module.toml",
        "schema = 1\n[module]\nname = 'nomad-system'\n");
    directory.write("nomad-game/module.toml",
        "schema = 1\n[module]\nname = 'nomad-game'\n[nomad]\nmodules = ['nomad-system']\n");
    directory.write("telemetry/module.toml",
        "schema = 1\n[module]\nname = 'telemetry'\n[nomad]\nmodules = ['nomad-system']\n");
    const auto modules = resolveModules(directory.getPath(), {"nomad-game", "telemetry"});
    BOOST_REQUIRE_EQUAL(modules.size(), 3U);
    BOOST_TEST(modules[0].name == "nomad-system");
    BOOST_TEST(modules[1].name == "nomad-game");
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
        ModuleError,
        [](const auto& error) {
            return NomadString(error.what()).find("first -> second -> first") != NomadString::npos;
        }
    );
}

BOOST_AUTO_TEST_CASE(resolves_transitive_dependencies_and_explicit_roots_without_duplicates) {
    TestDirectory directory("nomad_module_transitive");
    directory.write("base/module.toml", "schema = 1\n[module]\nname = 'base'\n");
    directory.write("nomad-system/module.toml",
        "schema = 1\n[module]\nname = 'nomad-system'\n[nomad]\nmodules = ['base']\n");
    directory.write("nomad-game/module.toml",
        "schema = 1\n[module]\nname = 'nomad-game'\n[nomad]\nmodules = ['nomad-system']\n");
    const auto modules = resolveModules(directory.getPath(), {"nomad-game", "base", "nomad-system"});
    BOOST_REQUIRE_EQUAL(modules.size(), 3U);
    BOOST_TEST(modules[0].name == "base");
    BOOST_TEST(modules[1].name == "nomad-system");
    BOOST_TEST(modules[2].name == "nomad-game");
}

BOOST_AUTO_TEST_CASE(rejects_invalid_missing_and_mismatched_modules) {
    TestDirectory directory("nomad_module_invalid");
    BOOST_CHECK_THROW(resolveModules(directory.getPath(), {"../outside"}), ModuleError);
    BOOST_CHECK_THROW(resolveModules(directory.getPath(), {"missing"}), ModuleError);
    directory.write("wrong/module.toml", "schema = 1\n[module]\nname = 'different'\n");
    BOOST_CHECK_THROW(resolveModules(directory.getPath(), {"wrong"}), ModuleError);
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
        BOOST_CHECK_THROW(resolveModules(directory.getPath(), {"module"}), ModuleError);
    }
}

BOOST_AUTO_TEST_CASE(empty_requests_resolve_to_no_modules) {
    TestDirectory directory("nomad_module_empty");
    BOOST_TEST(resolveModules(directory.getPath(), {}).empty());
}

BOOST_AUTO_TEST_SUITE_END()
