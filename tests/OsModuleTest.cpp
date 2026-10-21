// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/Array1dObject.hpp>
#include <nomad/collections/MapObject.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/script/Interpreter.hpp>
#include <nomad/script/InterpreterContext.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>

#include <nomad/system/ModuleHost.hpp>
#include <nomad/system/Path.hpp>
#include <nomad/system/ProjectSession.hpp>

#include <TestDirectory.hpp>

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/nowide/cstdlib.hpp>
#include <boost/test/unit_test.hpp>

#include <filesystem>
#include <optional>
#include <vector>

using namespace nomad;
using namespace nomad::test;

namespace {

constexpr auto ENVIRONMENT_NAME = "NOMAD_OS_SNAPSHOT_TEST";

class EnvironmentRestore {
public:
    EnvironmentRestore() {
        if (const auto* value = boost::nowide::getenv(ENVIRONMENT_NAME)) {
            m_previous = value;
        }
    }

    ~EnvironmentRestore() {
        if (m_previous) {
            (void)boost::nowide::setenv(ENVIRONMENT_NAME, m_previous->c_str(), 1);
        } else {
            (void)boost::nowide::unsetenv(ENVIRONMENT_NAME);
        }
    }

private:
    std::optional<NomadString> m_previous;
};

RuntimeValue constant(Runtime& runtime, const NomadString& name) {
    const auto id = runtime.getConstantId(name);
    BOOST_REQUIRE_MESSAGE(id != NOMAD_INVALID_ID, name);
    RuntimeValue value;
    runtime.getConstantValue(id, value);
    return value;
}

bool compile(Runtime& runtime, const NomadString& source) {
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"main", {"main.nomad", source}}}, {}});
    const auto compiled = compiler->compileScripts(&context, CompilationTarget::Executable);
    if (!compiled && context.getFirstError() != nullptr) {
        BOOST_TEST_MESSAGE(formatDiagnostic(*context.getFirstError()));
    }
    return compiled;
}

} // namespace

BOOST_AUTO_TEST_SUITE(os_module)

BOOST_AUTO_TEST_CASE(application_snapshots_are_owned_utf8_and_isolated_from_the_host) {
    EnvironmentRestore restore;
    BOOST_REQUIRE(boost::nowide::setenv(ENVIRONMENT_NAME, "value 日本語=with equals", 1) == 0);
    TestDirectory directory("nomad_os_snapshot");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-os"});
    BOOST_REQUIRE_EQUAL(modules.size(), 2U);
    BOOST_TEST(modules.front().name == "nomad-collections");
    auto host = ModuleHost::create({}, directory.getPath(), modules,
        {"--project-path", "ignored", "--debug", "--", "", "Player 日本語", "--help", "--"},
        ModuleMode::Application);
    auto& runtime = *host->getRuntime();
    BOOST_TEST(runtime.isHub("os"));
    const auto argumentsValue = constant(runtime, "os.argv");
    auto* arguments = argumentsValue.asObject<Array1dObject>();
    BOOST_REQUIRE(arguments != nullptr);
    BOOST_TEST(arguments->getReferenceCount() == 1U);
    BOOST_TEST(arguments->getCount() == 4U);
    const std::vector<NomadString> expected{"", "Player 日本語", "--help", "--"};
    for (auto index = NomadIndex{0}; index < expected.size(); ++index) {
        RuntimeValue value;
        arguments->getValue(static_cast<NomadInteger>(index), value);
        BOOST_TEST(NomadString(value.getStringValue()) == expected[index]);
        value.freeObjectValue();
    }
    auto* environment = constant(runtime, "os.env").asObject<MapObject>();
    BOOST_REQUIRE(environment != nullptr);
    BOOST_TEST(environment->getReferenceCount() == 1U);
    BOOST_REQUIRE(boost::nowide::setenv(ENVIRONMENT_NAME, "changed host", 1) == 0);
    RuntimeValue captured;
    environment->getValue(ENVIRONMENT_NAME, captured);
    BOOST_TEST(NomadString(captured.getStringValue()) == "value 日本語=with equals");
    captured.freeObjectValue();
    RuntimeValue replacement(NomadString("changed snapshot"));
    environment->setValue(ENVIRONMENT_NAME, replacement);
    arguments->setValue(1, replacement);
    replacement.freeObjectValue();
    BOOST_TEST(NomadString(boost::nowide::getenv(ENVIRONMENT_NAME)) == "changed host");
    BOOST_TEST(NomadString(constant(runtime, "os.currentDirectory").getStringValue()) ==
        pathToUtf8(std::filesystem::current_path()));
    BOOST_TEST(NomadString(constant(runtime, "os.executablePath").getStringValue()) ==
        pathToUtf8(NomadPath(boost::dll::program_location().native())));
    BOOST_TEST(NomadString(constant(runtime, "os.tempDirectory").getStringValue()) ==
        pathToUtf8(std::filesystem::temp_directory_path()));
}

BOOST_AUTO_TEST_CASE(analysis_exposes_exact_enum_types_without_host_data) {
    TestDirectory directory("nomad_os_analysis");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-os"});
    auto host = ModuleHost::create({}, directory.getPath(), modules,
        {"--", "must not be captured"}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    BOOST_TEST(constant(runtime, "os.argv").asObject<Array1dObject>()->getCount() == 0U);
    BOOST_TEST(constant(runtime, "os.env").asObject<MapObject>()->getCount() == 0U);
    BOOST_TEST(runtime.getConstantType(runtime.getConstantId("os.argv"))->getTypeName() == "ReadOnlyArray1<string>");
    BOOST_TEST(runtime.getConstantType(runtime.getConstantId("os.env"))->getTypeName() == "ReadOnlyMap<string string>");
    for (const auto* name : {"os.currentDirectory", "os.executablePath", "os.tempDirectory"}) {
        BOOST_TEST(NomadString(constant(runtime, name).getStringValue()).empty());
    }
    const auto* platform = runtime.getTypeByName("os.Platform")->asEnum();
    const auto* architecture = runtime.getTypeByName("os.Architecture")->asEnum();
    BOOST_REQUIRE(platform != nullptr);
    BOOST_REQUIRE(architecture != nullptr);
    BOOST_TEST(platform->getValueCount() == 3U);
    BOOST_TEST(architecture->getValueCount() == 4U);
    const std::vector<NomadString> platforms{"Windows", "Linux", "MacOS"};
    const std::vector<NomadString> architectures{"X86", "X64", "Arm32", "Arm64"};
    for (auto index = NomadIndex{0}; index < platforms.size(); ++index) {
        BOOST_TEST(platform->getValue(index).name == platforms[index]);
        BOOST_TEST(platform->getValue(index).value == static_cast<NomadInteger>(index + 1));
    }
    for (auto index = NomadIndex{0}; index < architectures.size(); ++index) {
        BOOST_TEST(architecture->getValue(index).name == architectures[index]);
        BOOST_TEST(architecture->getValue(index).value == static_cast<NomadInteger>(index + 1));
    }
    BOOST_TEST(runtime.getConstantType(runtime.getConstantId("os.platform")) == platform);
    BOOST_TEST(runtime.getConstantType(runtime.getConstantId("os.architecture")) == architecture);
#if defined(_WIN32)
    BOOST_TEST(constant(runtime, "os.platform").getIntegerValue() == 1);
#elif defined(__linux__)
    BOOST_TEST(constant(runtime, "os.platform").getIntegerValue() == 2);
#elif defined(__APPLE__)
    BOOST_TEST(constant(runtime, "os.platform").getIntegerValue() == 3);
#endif
#if defined(_M_ARM64) || defined(_M_ARM64EC) || defined(__aarch64__)
    BOOST_TEST(constant(runtime, "os.architecture").getIntegerValue() == 4);
#elif defined(_M_ARM) || defined(__arm__)
    BOOST_TEST(constant(runtime, "os.architecture").getIntegerValue() == 3);
#elif defined(_M_X64) || defined(__x86_64__)
    BOOST_TEST(constant(runtime, "os.architecture").getIntegerValue() == 2);
#elif defined(_M_IX86) || defined(__i386__)
    BOOST_TEST(constant(runtime, "os.architecture").getIntegerValue() == 1);
#endif
    BOOST_REQUIRE(compile(runtime,
        "assert os.platform == os.Platform.Windows || os.platform == os.Platform.Linux "
            "|| os.platform == os.Platform.MacOS\n"
        "assert os.architecture == os.Architecture.X86 || os.architecture == os.Architecture.X64 "
            "|| os.architecture == os.Architecture.Arm32 || os.architecture == os.Architecture.Arm64\n"
        "assert os.argv.count == 0\n"
        "assert os.env.count == 0\n"));
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
}

BOOST_AUTO_TEST_CASE(bindings_and_collection_contents_are_read_only_in_both_execution_modes) {
    TestDirectory directory("nomad_os_read_only");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-os"});
    const std::vector<NomadString> assignments{
        "os.argv = new Array<string> 1",
        "os.env = new Map<string string>",
        "os.platform = os.Platform.Windows",
        "os.architecture = os.Architecture.X64",
        "os.currentDirectory = \"elsewhere\"",
        "os.executablePath = \"elsewhere\"",
        "os.tempDirectory = \"elsewhere\"",
        "os.argv[0] = \"updated\"",
        "os.env[\"snapshot\"] = \"updated\"",
        "os.env.remove \"snapshot\"",
        "os.env.clear"
    };
    for (const auto& assignment : assignments) {
        BOOST_TEST_CONTEXT(assignment) {
            auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
            BOOST_TEST(!compile(*host->getRuntime(), assignment));
            auto machine = host->getRuntime()->createVirtualMachine();
            InterpreterContext context(*machine);
            BOOST_TEST(!Interpreter{}.execute(assignment, context));
            BOOST_TEST(context.hasError());
        }
    }
    auto host = ModuleHost::create({}, directory.getPath(), modules,
        {"--", "original"}, ModuleMode::Application);
    auto& runtime = *host->getRuntime();
    BOOST_REQUIRE(compile(runtime,
        "assert os.argv[0] == \"original\"\n"
        "for argument in os.argv\nassert argument == \"original\"\nend\n"));
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
}

BOOST_AUTO_TEST_CASE(no_separator_means_no_application_arguments_and_packaging_has_no_runtime) {
    TestDirectory directory("nomad_os_modes");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-os"});
    {
        auto host = ModuleHost::create({}, directory.getPath(), modules,
            {"--debug", "runtime data"}, ModuleMode::Application);
        BOOST_TEST(constant(*host->getRuntime(), "os.argv").asObject<Array1dObject>()->getCount() == 0U);
    }
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Packaging);
    BOOST_TEST(host->getRuntime() == nullptr);
}

BOOST_AUTO_TEST_CASE(empty_environment_values_are_preserved_and_snapshots_are_independent) {
    EnvironmentRestore restore;
    BOOST_REQUIRE(boost::nowide::setenv(ENVIRONMENT_NAME, "", 1) == 0);
    TestDirectory directory("nomad_os_empty_environment");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-os"});
    auto first = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Application);
    auto* firstEnvironment = constant(*first->getRuntime(), "os.env").asObject<MapObject>();
#if !defined(_WIN32)
    // The Windows C runtime may remove variables assigned an empty value.
    BOOST_TEST(firstEnvironment->contains(ENVIRONMENT_NAME));
    RuntimeValue value;
    firstEnvironment->getValue(ENVIRONMENT_NAME, value);
    BOOST_TEST(NomadString(value.getStringValue()).empty());
    value.freeObjectValue();
#endif
    BOOST_REQUIRE(boost::nowide::setenv(ENVIRONMENT_NAME, "second snapshot", 1) == 0);
    auto second = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Application);
    auto* secondEnvironment = constant(*second->getRuntime(), "os.env").asObject<MapObject>();
    BOOST_TEST(firstEnvironment != secondEnvironment);
    (void)firstEnvironment->remove(ENVIRONMENT_NAME);
    BOOST_TEST(!firstEnvironment->contains(ENVIRONMENT_NAME));
    BOOST_TEST(secondEnvironment->contains(ENVIRONMENT_NAME));
}

BOOST_AUTO_TEST_CASE(snapshot_objects_are_released_with_the_runtime) {
    TestDirectory directory("nomad_os_lifetime");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-os"});
    {
        auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    }
    const auto before = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        auto host = ModuleHost::create({}, directory.getPath(), modules,
            {"--", "owned 日本語"}, ModuleMode::Application);
        BOOST_REQUIRE(compile(*host->getRuntime(), "assert os.argv[0] == \"owned 日本語\""));
        RuntimeValue result;
        host->getRuntime()->executeFunction(host->getRuntime()->getFunctionId("main"), {}, result);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(collection_constants_survive_compiler_destruction_and_interpreter_reads) {
    TestDirectory directory("nomad_os_collection_constants");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-os"});
    auto host = ModuleHost::create({}, directory.getPath(), modules,
        {"--", "original"}, ModuleMode::Application);
    auto& runtime = *host->getRuntime();
    RuntimeValue environmentValue(NomadString("original"));
    constant(runtime, "os.env").asObject<MapObject>()->setValue("interpreter", environmentValue);
    environmentValue.freeObjectValue();
    BOOST_REQUIRE(compile(runtime,
        "fun identity values:ReadOnlyArray1<string>\nreturn values\nend\n"
        "values = identity os.argv\n"
        "assert values[0] == \"original\"\n"
        "return os.argv\n"));
    const auto argumentsValue = constant(runtime, "os.argv");
    auto* arguments = argumentsValue.asObject<Array1dObject>();
    const auto references = arguments->getReferenceCount();
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(result.getObjectValue() == arguments);
    runtime.getConstantType(runtime.getConstantId("os.argv"))->freeValue(result);
    BOOST_TEST(arguments->getReferenceCount() == references);
    {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        BOOST_REQUIRE(Interpreter{}.evaluateExpression("os.argv[0]", context));
        BOOST_TEST(NomadString(context.getResult()->getValue().getStringValue()) == "original");
        BOOST_TEST(!Interpreter{}.execute("os.argv[0] = \"updated\"", context));
        BOOST_TEST(!Interpreter{}.execute("os.env[\"interpreter\"] = \"updated\"", context));
        BOOST_REQUIRE(Interpreter{}.evaluateExpression("os.env[\"interpreter\"]", context));
        BOOST_TEST(NomadString(context.getResult()->getValue().getStringValue()) == "original");
    }
    BOOST_TEST(arguments->getReferenceCount() == references);
}

BOOST_AUTO_TEST_SUITE_END()
