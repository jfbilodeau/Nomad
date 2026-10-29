// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/BindingGenerator.hpp>

#include <BindingTestFixture.hpp>

#include <boost/test/unit_test.hpp>

#include <algorithm>

using namespace nomad;
using namespace nomad::test;

BOOST_AUTO_TEST_SUITE(binding_generator)

BOOST_AUTO_TEST_CASE(generation_is_deterministic_and_preserves_unchanged_files) {
    const BindingTestFixture fixture;
    const auto model = analyzeModuleBindings(fixture.getOptions());
    BOOST_REQUIRE(model.classes.size() == 1U);
    BOOST_TEST(model.module == "test-module");
    const auto first = renderModuleBindings(model);
    const auto second = renderModuleBindings(analyzeModuleBindings(fixture.getOptions()));
    BOOST_REQUIRE(first.size() == 3U);
    BOOST_REQUIRE(second.size() == first.size());
    for (auto index = NomadIndex{0}; index < first.size(); ++index) {
        BOOST_TEST(first[index].path == second[index].path);
        BOOST_TEST(first[index].content == second[index].content);
    }
    BOOST_TEST(first[0].content.find("nomad::generated::test_module") != NomadString::npos);
    BOOST_TEST(first[0].content.find("nomad::NomadFloat getRatio") != NomadString::npos);
    const auto written = generateModuleBindings(fixture.getOptions());
    BOOST_TEST(!written.fresh);
    BOOST_TEST(written.changedFiles.size() == 3U);
    const auto header = fixture.getDirectory() / "generated" / first[0].path;
    const auto timestamp = std::filesystem::last_write_time(header);
    BOOST_TEST(generateModuleBindings(fixture.getOptions()).fresh);
    BOOST_CHECK(std::filesystem::last_write_time(header) == timestamp);
}

BOOST_AUTO_TEST_CASE(check_is_nonmutating_and_detects_missing_and_stale_outputs) {
    const BindingTestFixture fixture;
    auto options = fixture.getOptions();
    options.check = true;
    BOOST_TEST(!generateModuleBindings(options).fresh);
    BOOST_TEST(!std::filesystem::exists(fixture.getDirectory() / "generated"));
    options.check = false;
    (void)generateModuleBindings(options);
    const auto source = NomadPath("generated") / "src" / "Bindings.cpp";
    fixture.write(source, "stale");
    std::filesystem::remove(fixture.getDirectory() / "generated" / "nomad-module.cmake");
    options.check = true;
    const auto result = generateModuleBindings(options);
    BOOST_TEST(!result.fresh);
    BOOST_TEST(result.changedFiles.size() == 2U);
    BOOST_TEST(fixture.read(source) == "stale");
    BOOST_TEST(!std::filesystem::exists(fixture.getDirectory() / "generated" / "nomad-module.cmake"));
}

BOOST_AUTO_TEST_CASE(invalid_scripts_and_unsupported_shapes_do_not_write_outputs) {
    const BindingTestFixture fixture;
    for (const auto& script : {
        "class Settings\nvalue = unknownIdentifier\nend\n",
        "class Other\nend\nclass Settings\nvalue = new Other\nend\n",
        "class Settings\ninit value:int\nthis.value = value\nend\nend\n",
        "class Settings\nvalue = 1\nfun read\nreturn this.value\nend\nend\n"
    }) {
        fixture.setScript(script);
        BOOST_CHECK_THROW((void)generateModuleBindings(fixture.getOptions()), NomadException);
        BOOST_TEST(!std::filesystem::exists(fixture.getDirectory() / "generated"));
    }
}

BOOST_AUTO_TEST_CASE(compiler_resolves_initializers_without_executing_them) {
    const BindingTestFixture fixture;
    fixture.setScript(
        "fun calculate divisor:int\nreturn 1 / divisor\nend\n"
        "class Settings\nvalue = calculate 0\nend\n"
        "trigger = calculate 0\n"
    );
    const auto model = analyzeModuleBindings(fixture.getOptions());
    BOOST_REQUIRE(model.classes.size() == 1U);
    BOOST_REQUIRE(model.classes.front().fields.size() == 1U);
    BOOST_TEST(model.classes.front().fields.front().type == "int");
    BOOST_TEST(!generateModuleBindings(fixture.getOptions()).fresh);
}

BOOST_AUTO_TEST_CASE(cpp_keywords_flattened_names_and_accessor_collisions_are_rejected) {
    const BindingTestFixture fixture;
    for (const auto& script : {
        "enum Mode\ntemplate\nend\n",
        "class Settings\nactive = true\nActive = false\nend\n",
        "class Settings\nobject = 1\nend\n",
        "hub one\nclass Two\nend\nend\nclass one_Two\nend\n"
    }) {
        fixture.setScript(script);
        BOOST_CHECK_THROW((void)generateModuleBindings(fixture.getOptions()), NomadException);
    }
}

BOOST_AUTO_TEST_CASE(output_overlap_and_collisions_are_validated_before_writing) {
    const BindingTestFixture fixture;
    auto options = fixture.getOptions();
    for (const auto& directory : {fixture.getDirectory(), fixture.getDirectory() / "scripts",
        fixture.getDirectory() / "scripts" / "generated", fixture.getDirectory().parent_path()}) {
        options.outputDirectory = directory;
        BOOST_CHECK_THROW((void)generateModuleBindings(options), NomadException);
    }
    options.outputDirectory.reset();
    fixture.write(NomadPath("generated") / "src" / "Bindings.cpp", "unrelated");
    BOOST_CHECK_THROW((void)generateModuleBindings(options), NomadException);
    BOOST_TEST(!std::filesystem::exists(fixture.getDirectory() / "generated" / "include"));
    BOOST_TEST(fixture.read(NomadPath("generated") / "src" / "Bindings.cpp") == "unrelated");
    std::filesystem::remove(fixture.getDirectory() / "generated" / "src" / "Bindings.cpp");
    std::filesystem::remove(fixture.getDirectory() / "generated" / "src");
    fixture.write(NomadPath("generated") / "src", "not a directory");
    BOOST_CHECK_THROW((void)generateModuleBindings(options), NomadException);
    BOOST_TEST(!std::filesystem::exists(fixture.getDirectory() / "generated" / "include"));
}

BOOST_AUTO_TEST_CASE(dependencies_use_only_the_explicit_sdk_script_roots) {
    const BindingTestFixture fixture;
    fixture.write("module.toml",
        "schema = 1\n[module]\nname = 'test-module'\n[nomad]\nmodules = ['dependency']\n");
    fixture.write(NomadPath("sdk") / "modules" / "dependency" / "module.toml",
        "schema = 1\n[module]\nname = 'dependency'\n[scripts]\ndirectory = 'declarations'\n");
    fixture.write(NomadPath("sdk") / "modules" / "dependency" / "declarations" / "types.nomad",
        "hub dependency\nenum Mode\nFirst\nSecond\nend\nclass NotSelected\nend\nend\n");
    fixture.setScript("class Settings\nmode = dependency.Mode.Second\nend\n");
    auto options = fixture.getOptions();
    BOOST_CHECK_THROW((void)analyzeModuleBindings(options), NomadException);
    options.sdkDirectory = fixture.getDirectory() / "sdk";
    const auto model = analyzeModuleBindings(options);
    BOOST_REQUIRE(model.classes.size() == 1U);
    BOOST_TEST(model.classes.front().name == "Settings");
    BOOST_REQUIRE(model.enums.size() == 1U);
    BOOST_TEST(model.enums.front().name == "dependency.Mode");
}

BOOST_AUTO_TEST_CASE(retained_log_outputs_are_current) {
    BindingGenerationOptions options;
    options.moduleDirectory = getLogBindingModuleDirectory();
    options.check = true;
    BOOST_TEST(generateModuleBindings(options).fresh);
}

BOOST_AUTO_TEST_SUITE_END()
