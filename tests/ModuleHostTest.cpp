// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>

#include <nomad/system/ModuleHost.hpp>
#include <nomad/system/ProjectSession.hpp>

#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

#include <fstream>
#include <iterator>

using namespace nomad;
using namespace nomad::test;

namespace {

std::vector<ModuleManifest> fixtureModules() {
    auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-system"});
    modules.push_back({"fixture", NOMAD_TEST_MODULE_DIRECTORY, {"nomad-system"}});
    return modules;
}

NomadString events(const TestDirectory& directory) {
    std::ifstream input(directory.getPath() / "module-events.txt");
    return {std::istreambuf_iterator<char>(input), {}};
}

bool compileFixture(ModuleHost& host, const CompilationTarget target) {
    auto compiler = host.getRuntime()->createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"init", {"init.nomad", "fixture.exit"}}}, {}});
    return compiler->compileScripts(&context, target);
}

} // namespace

BOOST_AUTO_TEST_SUITE(module_host)

BOOST_AUTO_TEST_CASE(compiler_resolves_parameterized_annotations_from_collections) {
    for (const auto target : {CompilationTarget::Analysis, CompilationTarget::Executable}) {
        TestDirectory directory("nomad_host_type_annotations");
        const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-collections"});
        ModuleHost host({}, directory.getPath(), directory.getPath(), modules, {}, ModuleMode::Analysis);
        auto& runtime = *host.getRuntime();
        BOOST_REQUIRE(runtime.registerParameterizedType("Pair", 2, [](const auto& arguments) {
            return std::make_unique<ObjectType>(
                "Pair<" + arguments[0]->getTypeName() + " " + arguments[1]->getTypeName() + ">",
                arguments
            );
        }));
        auto compiler = runtime.createCompiler();
        CompilerContext context(compiler.get());
        compiler->registerSources(&context, {{{"init", {"init.nomad",
            "params values:List<int> nested:List<List<string>> pair:Pair<string List<int>>\n"
            "event changed items:List<int>\n"
            "fun identity items:List<int>\n"
            "    return items\n"
            "end\n"
            "return identity values"
        }}}, {}});
        BOOST_REQUIRE(compiler->compileScripts(&context, target));
        const auto* function = runtime.getFunction(runtime.getFunctionId("init"));
        BOOST_REQUIRE(function != nullptr);
        BOOST_TEST(function->getParameterType(0) == runtime.getParameterizedType("List", {runtime.getIntegerType()}));
        BOOST_TEST(function->getParameterType(1)->getTypeName() == "List<List<string>>");
        BOOST_TEST(function->getParameterType(2)->getTypeName() == "Pair<string List<int>>");
    }
}

BOOST_AUTO_TEST_CASE(compiler_reports_invalid_parameterized_annotations) {
    const std::vector<std::pair<NomadString, NomadString>> cases = {
        {"params value:List", "Type arguments required"},
        {"params value:List<>", "expects 1 type arguments, got 0"},
        {"params value:List<int string>", "expects 1 type arguments, got 2"},
        {"params value:List<int", "Expected '>'"},
        {"params value:Missing<int>", "Unknown parameterized type"},
        {"params value:List<Missing>", "Unknown type"},
        {"params value:List<void>", "Invalid type argument"},
        {"params value:List<$file>", "Expected '"},
        {"params value:List<int, string>", "Unrecognized token"},
        {"params value:List<List<int>", "Expected '>'"},
        {"event changed value:List<>", "expects 1 type arguments, got 0"},
        {"fun accept value:List<>\nend", "expects 1 type arguments, got 0"},
        {
            "params value:List<int>\n"
            "fun accept item:List<string>\n    return item\nend\n"
            "return accept value",
            "mismatch"
        },
    };
    for (const auto& [source, expected] : cases) {
        BOOST_TEST_CONTEXT(source) {
            TestDirectory directory("nomad_host_bad_type_annotations");
            const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-collections"});
            ModuleHost host({}, directory.getPath(), directory.getPath(), modules, {}, ModuleMode::Analysis);
            auto compiler = host.getRuntime()->createCompiler();
            CompilerContext context(compiler.get());
            compiler->registerSources(&context, {{{"init", {"init.nomad", source}}}, {}});
            BOOST_TEST(!compiler->compileScripts(&context, CompilationTarget::Analysis));
            bool found = false;
            for (const auto& diagnostic : context.getDiagnostics()) {
                found = found || diagnostic.message.find(expected) != NomadString::npos;
            }
            BOOST_TEST(found, expected);
        }
    }
}

BOOST_AUTO_TEST_CASE(compiler_requires_registered_parameterized_family) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"init", {"init.nomad", "params value:List<int>"}}}, {}});
    BOOST_TEST(!compiler->compileScripts(&context, CompilationTarget::Analysis));
    BOOST_REQUIRE(context.getFirstError() != nullptr);
    BOOST_TEST(context.getFirstError()->message.find("Unknown parameterized type 'List'") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(compiler_limits_type_nesting) {
    for (const auto nesting : {63, 64}) {
        TestDirectory directory("nomad_host_type_nesting");
        const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-collections"});
        ModuleHost host({}, directory.getPath(), directory.getPath(), modules, {}, ModuleMode::Analysis);
        auto compiler = host.getRuntime()->createCompiler();
        CompilerContext context(compiler.get());
        NomadString typeName = "int";
        for (auto index = 0; index < nesting; ++index) {
            typeName = "List<" + typeName + ">";
        }
        compiler->registerSources(&context, {{{"init", {"init.nomad", "params value:" + typeName}}}, {}});
        BOOST_TEST(compiler->compileScripts(&context, CompilationTarget::Analysis) == (nesting == 63));
        if (nesting == 64) {
            BOOST_REQUIRE(context.getFirstError() != nullptr);
            BOOST_TEST(context.getFirstError()->message.find("maximum depth of 64") != NomadString::npos);
        }
    }
}

BOOST_AUTO_TEST_CASE(collections_interns_list_specializations) {
    TestDirectory directory("nomad_host_list_types");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-collections"});
    ModuleHost host({}, directory.getPath(), directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host.getRuntime();
    BOOST_TEST(runtime.getTypeByName("List<int>") == nullptr);
    const auto* integers = runtime.getParameterizedType("List", {runtime.getIntegerType()});
    BOOST_REQUIRE(integers != nullptr);
    BOOST_TEST(integers == runtime.getParameterizedType("List", {runtime.getIntegerType()}));
    BOOST_TEST(integers == runtime.getTypeByName("List<int>"));
    const auto* descriptor = dynamic_cast<const ObjectType*>(integers);
    BOOST_REQUIRE(descriptor != nullptr);
    BOOST_REQUIRE_EQUAL(descriptor->getTypeArguments().size(), 1u);
    BOOST_TEST(descriptor->getTypeArguments()[0] == runtime.getIntegerType());
    const auto* strings = runtime.getParameterizedType("List", {runtime.getStringType()});
    BOOST_REQUIRE(strings != nullptr);
    BOOST_TEST(integers != strings);
    BOOST_TEST(!integers->sameType(strings));
    const auto* nested = runtime.getParameterizedType("List", {integers});
    BOOST_REQUIRE(nested != nullptr);
    BOOST_TEST(nested->getTypeName() == "List<List<int>>");
    BOOST_TEST(runtime.getParameterizedType("List", {}) == nullptr);
    BOOST_TEST(runtime.getParameterizedType("List", {runtime.getVoidType()}) == nullptr);
    BOOST_TEST(runtime.getParameterizedType("List", {nullptr}) == nullptr);
    Runtime other;
    BOOST_TEST(runtime.getParameterizedType("List", {other.getIntegerType()}) == nullptr);
    BOOST_TEST(runtime.getParameterizedType("Missing", {}) == nullptr);
    BOOST_TEST(!runtime.registerParameterizedType("List", 1, [](const auto&) {
        return std::make_unique<ObjectType>("Duplicate");
    }));
}

BOOST_AUTO_TEST_CASE(parameterized_types_validate_factories_and_cache_by_arguments) {
    Runtime runtime;
    NomadIndex calls = 0;
    BOOST_TEST(runtime.registerParameterizedType("Example", 1, [&calls](const auto& arguments) {
        ++calls;
        return std::make_unique<ObjectType>("Example<" + arguments[0]->getTypeName() + ">", arguments);
    }));
    const auto* first = runtime.getParameterizedType("Example", {runtime.getIntegerType()});
    BOOST_REQUIRE(first != nullptr);
    BOOST_TEST(first == runtime.getParameterizedType("Example", {runtime.getIntegerType()}));
    BOOST_TEST(calls == 1u);
    BOOST_TEST(!runtime.registerParameterizedType("Empty", 1, {}));
    BOOST_TEST(!runtime.registerParameterizedType("", 1, {}));
    BOOST_TEST(runtime.registerParameterizedType("Broken", 1, [](const auto&) {
        return std::unique_ptr<Type>{};
    }));
    BOOST_TEST(runtime.getParameterizedType("Broken", {runtime.getIntegerType()}) == nullptr);
    BOOST_TEST(runtime.registerParameterizedType("Collision", 1, [](const auto&) {
        return std::make_unique<ObjectType>("int");
    }));
    BOOST_TEST(runtime.getParameterizedType("Collision", {runtime.getIntegerType()}) == nullptr);
}

BOOST_AUTO_TEST_CASE(collections_module_loads_in_all_modes) {
    TestDirectory directory("nomad_host_collections");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-collections"});
    BOOST_REQUIRE_EQUAL(modules.size(), 1u);
    BOOST_TEST(modules.front().name == "nomad-collections");

    for (const auto mode : {ModuleMode::Application, ModuleMode::Analysis, ModuleMode::Packaging}) {
        ModuleHost host({}, directory.getPath(), directory.getPath(), modules, {}, mode);
        BOOST_TEST((host.getRuntime() != nullptr) == (mode != ModuleMode::Packaging));
    }
}

BOOST_AUTO_TEST_CASE(loads_registers_compiles_and_shuts_down_in_lifetime_order) {
    TestDirectory directory("nomad_host_lifecycle");
    {
        ModuleHost host({}, directory.getPath(), directory.getPath(), fixtureModules(),
            {"--custom", "value"}, ModuleMode::Application);
        BOOST_REQUIRE(compileFixture(host, CompilationTarget::Executable));
        BOOST_TEST(host.run("init") == 7);
        BOOST_TEST(host.pollExit());
    }
    BOOST_TEST(events(directory) == "register\npreParse\nemit\ncompleted\nexit\nshutdown\ndestroy\n");
}

BOOST_AUTO_TEST_CASE(analysis_skips_emission_and_execution) {
    TestDirectory directory("nomad_host_analysis");
    {
        ModuleHost host({}, directory.getPath(), directory.getPath(), fixtureModules(), {}, ModuleMode::Analysis);
        BOOST_REQUIRE(compileFixture(host, CompilationTarget::Analysis));
    }
    BOOST_TEST(events(directory) == "register\npreParse\ncompleted\nshutdown\ndestroy\n");
}

BOOST_AUTO_TEST_CASE(hook_failure_prevents_later_hooks) {
    TestDirectory directory("nomad_host_hook_failure");
    {
        auto configuration = NomadConfiguration::parse("[fixture]\nfail-hook = true\n");
        ModuleHost host(configuration, directory.getPath(), directory.getPath(), fixtureModules(), {}, ModuleMode::Analysis);
        BOOST_TEST(!compileFixture(host, CompilationTarget::Executable));
    }
    BOOST_TEST(events(directory) == "register\npreParse\nshutdown\ndestroy\n");
}

BOOST_AUTO_TEST_CASE(packaging_uses_context_without_runtime_registration) {
    TestDirectory directory("nomad_host_package");
    auto source = NomadConfiguration::parse("[fixture]\ndevelopment = true\n");
    auto release = source;
    auto generated = NomadString{};
    ModuleHost host(source, directory.getPath(), directory.getPath(), fixtureModules(), {}, ModuleMode::Packaging);
    BOOST_TEST(host.getRuntime() == nullptr);
    ModulePackageContext context{};
    context.source = &source;
    context.release = &release;
    context.writeFile = [&](const NomadPath& path, NomadStringView value) {
        BOOST_TEST(path == NomadPath("generated.txt"));
        generated = value;
    };
    host.package(context);
    BOOST_TEST(source.contains("fixture.development"));
    BOOST_TEST(!release.contains("fixture.development"));
    BOOST_TEST(generated == "generated by fixture\n");
    BOOST_TEST(events(directory).empty());
}

BOOST_AUTO_TEST_CASE(missing_library_is_an_explicit_startup_failure) {
    TestDirectory directory("nomad_host_missing");
    BOOST_CHECK_THROW(
        ModuleHost({}, directory.getPath(), directory.getPath(),
            {{"missing", directory.getPath(), {}}}, {}, ModuleMode::Application),
        ModuleException
    );
}

BOOST_AUTO_TEST_CASE(registration_failure_is_not_reported_as_success) {
    TestDirectory directory("nomad_host_registration_failure");
    auto configuration = NomadConfiguration::parse("[fixture]\nfail-registration = true\n");
    BOOST_CHECK_EXCEPTION(
        ModuleHost(configuration, directory.getPath(), directory.getPath(),
            fixtureModules(), {}, ModuleMode::Analysis),
        NomadException,
        [](const auto& error) {
            return NomadString(error.what()).find("Requested registration failure") != NomadString::npos;
        }
    );
    BOOST_TEST(events(directory) == "register\n");
}

BOOST_AUTO_TEST_CASE(missing_registration_entry_is_an_explicit_startup_failure) {
    TestDirectory directory("nomad_host_missing_entry");
    BOOST_CHECK_EXCEPTION(
        ModuleHost({}, directory.getPath(), directory.getPath(),
            {{"nomad-language", NOMAD_TEST_CORE_DIRECTORY, {}}}, {}, ModuleMode::Analysis),
        ModuleException,
        [](const auto& error) {
            return NomadString(error.what()).find("does not export registerModule") != NomadString::npos;
        }
    );
}

BOOST_AUTO_TEST_CASE(module_arguments_are_provided_during_registration) {
    TestDirectory directory("nomad_host_arguments");
    ModuleHost host({}, directory.getPath(), directory.getPath(), fixtureModules(),
        {"--custom", "value"}, ModuleMode::Analysis);
    RuntimeValue value;
    host.getRuntime()->getConstantValue(host.getRuntime()->getConstantId("fixture.argumentCount"), value);
    BOOST_TEST(value.getIntegerValue() == 2);
}

BOOST_AUTO_TEST_SUITE_END()
