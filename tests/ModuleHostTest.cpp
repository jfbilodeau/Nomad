// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/Array1Type.hpp>
#include <nomad/collections/Array1dObject.hpp>
#include <nomad/collections/Array2Type.hpp>
#include <nomad/collections/Array2dObject.hpp>
#include <nomad/collections/Array3Type.hpp>
#include <nomad/collections/Array3dObject.hpp>
#include <nomad/collections/ArrayType.hpp>
#include <nomad/collections/KeyValueObject.hpp>
#include <nomad/collections/KeyValueType.hpp>
#include <nomad/collections/ListObject.hpp>
#include <nomad/collections/MapObject.hpp>
#include <nomad/collections/MapType.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/CompilerTools.hpp>
#include <nomad/compiler/SourceLoader.hpp>

#include <nomad/game/Game.hpp>

#include <nomad/log/Logger.hpp>
#include <nomad/log/MemorySink.hpp>

#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/ModuleHost.hpp>
#include <nomad/system/Path.hpp>
#include <nomad/system/ProjectSession.hpp>

#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>

using namespace nomad;
using namespace nomad::test;

namespace {

std::vector<ModuleManifest> fixtureModules() {
    auto modules = resolveModules(getSdkDirectory() / "modules", {"system"});
    modules.push_back({"fixture", NOMAD_TEST_MODULE_DIRECTORY, {"system"}});
    return modules;
}

std::vector<ModuleManifest> scriptFixtureModules(
    const TestDirectory& directory,
    const NomadString& scriptSettings = {}
) {
    const auto moduleDirectory = directory.getPath() / "modules" / "fixture";
    directory.write("modules/fixture/module.toml",
        "schema = 1\n[module]\nname = 'fixture'\n[nomad]\nmodules = ['system']\n" + scriptSettings);
    std::filesystem::copy_file(
        NomadPath(NOMAD_TEST_MODULE_DIRECTORY) / getModuleLibraryName("fixture"),
        moduleDirectory / getModuleLibraryName("fixture")
    );
    auto modules = resolveModules(getSdkDirectory() / "modules", {"system"});
    modules.push_back(loadModuleManifest(moduleDirectory));
    return modules;
}

NomadString events(const TestDirectory& directory) {
    std::ifstream input(directory.getPath() / "module-events.txt");
    return {std::istreambuf_iterator<char>(input), {}};
}

bool compileFixture(
    ModuleHost& host,
    const CompilationTarget target,
    const NomadString& source = "fixture.exit",
    const NomadString& name = "init"
) {
    return host.compileSources(SourceBatch{{{name, {name + ".nomad", source}}}, {}}, target).succeeded();
}

} // namespace

BOOST_AUTO_TEST_SUITE(module_host)

BOOST_AUTO_TEST_CASE(module_script_declarations_are_available_without_running_the_file_body) {
    TestDirectory directory("host-module-script-declarations");
    const auto modules = scriptFixtureModules(directory);
    directory.write("modules/fixture/scripts/init.nomad",
        "open hub sample\n"
        "enum Level\nReady\nend\n"
        "class Configuration\nactive = true\nlevel = sample.Level.Ready\nend\n"
        "end\n"
        "fun moduleAnswer\nreturn 42\nend\n"
        "assert false\n");
    directory.write("scripts/init.nomad",
        "settings = new sample.Configuration\n"
        "assert settings.active\n"
        "assert settings.level == sample.Level.Ready\n"
        "value = moduleAnswer\nassert value == 42\nfixture.exit\n");
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Application);
    const auto result = host->compileProject();
    NomadString diagnostics;
    for (const auto& diagnostic : result.diagnostics) {
        diagnostics += formatDiagnostic(diagnostic) + "\n";
    }
    BOOST_REQUIRE_MESSAGE(result.succeeded(), diagnostics);
    BOOST_TEST(host->getRuntime()->getFunctionId("init").isValid());
    BOOST_TEST(host->getRuntime()->getFunctionId("$fixture.init").isValid());
    BOOST_TEST(host->getRuntime()->getFunctionId("moduleAnswer").isValid());
    BOOST_TEST(host->run() == 7);
}

BOOST_AUTO_TEST_CASE(module_sources_join_explicit_snapshots_and_preserve_compiler_metadata) {
    TestDirectory directory("host-module-script-snapshots");
    const auto modules = scriptFixtureModules(directory, "[scripts]\ndirectory = 'source'\n");
    const auto path = directory.write("modules/fixture/source/nested/main.nomad", "return missing");
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    const std::vector<ScriptSource> overrides{
        {"ignored", {"editor:module", "fun moduleAnswer\nreturn 42\nend", path}}
    };
    const auto batch = host->loadSources(std::vector{path.parent_path(), path}, overrides);
    BOOST_REQUIRE(batch.diagnostics.empty());
    BOOST_REQUIRE(batch.sources.size() == 1U);
    BOOST_TEST(batch.sources[0].scriptName == "$fixture.nested.main");
    auto* compiler = host->getCompiler();
    CompilerContext context(compiler);
    compiler->registerSources(&context, batch);
    const auto id = host->getRuntime()->getFunctionId("$fixture.nested.main");
    BOOST_REQUIRE(id.isValid());
    const auto& script = compiler->getScript(compiler->getScriptId(id));
    BOOST_TEST(script.owner == "fixture");
    BOOST_TEST(script.relativePath == NomadPath("nested/main.nomad"));
    BOOST_TEST(script.source.sourceName == "editor:module");
    BOOST_TEST(compiler->compileScripts(&context, CompilationTarget::Analysis));
}

BOOST_AUTO_TEST_CASE(module_discovery_is_shared_by_explicit_compilation_instruction_dumps_and_documentation) {
    TestDirectory directory("host-module-script-tools");
    const auto modules = scriptFixtureModules(directory);
    directory.write("modules/fixture/scripts/declarations.nomad",
        "fun moduleAnswer\nreturn 42\nend\n");
    const auto application = directory.write("scripts/main.nomad", "return moduleAnswer");
    {
        auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
        const auto result = host->compileSources(SourceBatch{{
            {"main", {"editor:main", "return moduleAnswer"}}
        }, {}}, CompilationTarget::Analysis);
        BOOST_TEST(result.succeeded());
        BOOST_TEST(host->getRuntime()->getFunctionId("$fixture.declarations").isValid());
    }
    {
        auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
        const auto dump = dumpInstructions(std::vector{application}, std::nullopt, *host->getCompiler());
        BOOST_TEST(dump.compilation.succeeded());
        BOOST_TEST(dump.instructions.find("moduleAnswer") != NomadString::npos);
    }
    {
        auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
        const auto documentation = generateDocumentationForPaths(std::vector{application}, *host->getCompiler());
        BOOST_TEST(documentation.compilation.succeeded());
        BOOST_TEST(documentation.documentation.find("moduleAnswer") != NomadString::npos);
        BOOST_TEST(documentation.documentation.find("$fixture.declarations") == NomadString::npos);
    }
}

BOOST_AUTO_TEST_CASE(module_source_errors_keep_paths_and_public_symbol_conflicts_are_not_hidden) {
    TestDirectory directory("host-module-script-conflicts");
    const auto modules = scriptFixtureModules(directory);
    const auto path = directory.write("modules/fixture/scripts/declarations.nomad",
        "fun sharedAnswer\nreturn 42\nend\n");
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    const auto result = host->compileSources(SourceBatch{{
        {"main", {"editor:main", "fun sharedAnswer\nreturn 7\nend"}}
    }, {}}, CompilationTarget::Analysis);
    BOOST_TEST(!result.succeeded());
    BOOST_TEST(std::ranges::any_of(result.diagnostics, [&](const auto& diagnostic) {
        return diagnostic.message.find("sharedAnswer") != NomadString::npos &&
            (diagnostic.sourceName == pathToGenericUtf8(path) || diagnostic.sourceName == "editor:main");
    }));
}

BOOST_AUTO_TEST_CASE(explicit_missing_module_script_directory_is_a_compilation_error) {
    TestDirectory directory("host-module-script-missing");
    const auto modules = scriptFixtureModules(directory, "[scripts]\ndirectory = 'missing'\n");
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    const auto result = host->compileSources({}, CompilationTarget::Analysis);
    BOOST_TEST(!result.succeeded());
    BOOST_REQUIRE(result.diagnostics.size() == 1U);
    BOOST_TEST(result.diagnostics[0].sourceName ==
        pathToGenericUtf8(directory.getPath() / "modules" / "fixture" / "missing"));
    BOOST_TEST(result.diagnostics[0].message.find("fixture") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(project_metadata_is_host_owned_and_defaults_to_blank)
{
    const TestDirectory directory("host-project-metadata");
    auto metadata = ProjectMetadata{"Original project", "org.nomad.test", "1.0.0", "test"};
    auto projectHost = ModuleHost::create({}, directory.getPath(), fixtureModules(), {}, ModuleMode::Application, metadata);
    metadata.name = "Changed by caller";
    const auto projectResult = projectHost->compileSources(SourceBatch{{
        {"init", {"init.nomad", "assert fixture.projectName == \"Original project\"\nfixture.exit"}}
    }, {}});
    BOOST_REQUIRE(projectResult.succeeded());
    BOOST_TEST(projectHost->run() == 7);

    auto standaloneHost = ModuleHost::create({}, directory.getPath(), fixtureModules(), {}, ModuleMode::Application);
    const auto standaloneResult = standaloneHost->compileSources(SourceBatch{{
        {"init", {"init.nomad", "assert fixture.projectName == \"\"\nfixture.exit"}}
    }, {}});
    BOOST_REQUIRE(standaloneResult.succeeded());
    BOOST_TEST(standaloneHost->run() == 7);
}

BOOST_AUTO_TEST_CASE(game_title_defaults_to_nomad_unless_explicitly_configured)
{
    const TestDirectory directory("host-project-session-metadata");
    for (const auto explicitTitle : {false, true}) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
        NomadProject project;
        project.root = directory.getPath();
        project.configuration.project = {"Typed name", "org.nomad.test", "1.0.0", "test"};
        project.configuration.sdk.modules = {"game"};
        project.document = NomadConfiguration::parse(
            "[text]\ndirectories = []\n[audio]\ndirectories = []\n[sprites]\ndirectories = []\n"
        );
        if (explicitTitle) {
            project.document.setString("window.title", "Explicit title");
        }
        const auto host = createProjectSession(project, ModuleMode::Application);
        BOOST_TEST(NomadString(SDL_GetCurrentVideoDriver()) == "dummy");
        auto count = 0;
        auto** windows = SDL_GetWindows(&count);
        BOOST_REQUIRE(windows != nullptr);
        const auto title = count == 1 ? NomadString(SDL_GetWindowTitle(windows[0])) : NomadString{};
        SDL_free(windows);
        BOOST_REQUIRE(count == 1);
        BOOST_TEST(title == (explicitTitle ? "Explicit title" : "Nomad"));
        BOOST_TEST(!project.document.contains("project.name"));
    }
}

BOOST_AUTO_TEST_CASE(host_compiles_configured_files_and_collects_diagnostics_without_running_scripts)
{
    const TestDirectory directory("host-standard-compilation");
    const auto configuration = NomadConfiguration::parse(
        "[scripts]\ndirectories = ['source']\n"
        "[fixture]\nentry = 'launch'\n"
    );
    directory.write("source/launch.nomad", "assert example.value == 42\nfixture.exit");
    directory.write("source/declarations.nomad", "hub example\nvalue = fixture.initialValue\nend");
    auto host = ModuleHost::create(configuration, directory.getPath(), fixtureModules(), {}, ModuleMode::Application);
    auto calls = 0;
    host->getRuntime()->registerNativeFunction(
        "fixture.initialValue",
        [&](VirtualMachine* machine) {
            ++calls;
            machine->setIntegerResult(42);
        },
        {}, host->getRuntime()->getIntegerType(), ""
    );
    const auto result = host->compileProject();
    BOOST_REQUIRE(result.succeeded());
    BOOST_TEST(result.errorCount == 0U);
    BOOST_TEST(result.warningCount == 0U);
    BOOST_TEST(result.diagnostics.empty());
    BOOST_TEST(calls == 0);
    BOOST_TEST(events(directory).find("run\n") == NomadString::npos);
    BOOST_TEST(host->run() == 7);
    BOOST_TEST(calls == 1);
}

BOOST_AUTO_TEST_CASE(host_uses_default_script_paths)
{
    const TestDirectory directory("host-default-script-paths");
    directory.write("scripts/init.nomad", "fixture.exit");
    auto host = ModuleHost::create({}, directory.getPath(), fixtureModules(), {}, ModuleMode::Application);
    const auto result = host->compileProject();
    BOOST_REQUIRE(result.succeeded());
    BOOST_TEST(host->run() == 7);
}

BOOST_AUTO_TEST_CASE(host_default_compilation_uses_analysis_mode_without_installing_initializers)
{
    const TestDirectory directory("host-default-analysis");
    directory.write("scripts/declarations.nomad",
        "fun calculate\nreturn 42\nend\nhub example\nvalue = calculate\nend");
    auto host = ModuleHost::create({}, directory.getPath(), fixtureModules(), {}, ModuleMode::Analysis);
    const auto result = host->compileProject();
    BOOST_REQUIRE(result.succeeded());
    BOOST_CHECK_THROW(host->getRuntime()->initializeScripts(), NomadException);
    BOOST_TEST(events(directory).find("run\n") == NomadString::npos);
}

BOOST_AUTO_TEST_CASE(host_does_not_impose_a_module_entry_convention)
{
    const TestDirectory directory("host-entry-metadata");
    const auto configuration = NomadConfiguration::parse(
        "[fixture]\nentry = 'launch'\n"
    );
    for (const auto target : {CompilationTarget::Analysis, CompilationTarget::Executable}) {
        for (const auto source : {"", "fun launch value:int\nend", "fun launch\nreturn 1\nend"}) {
            auto host = ModuleHost::create(configuration, directory.getPath(), fixtureModules(), {}, ModuleMode::Application);
            const auto result = host->compileSources(SourceBatch{{{"declarations", {"editor:declarations", source}}}, {}}, target);
            BOOST_TEST(result.succeeded());
            BOOST_TEST(result.diagnostics.empty());
            BOOST_TEST(events(directory).find("run\n") == NomadString::npos);
        }
    }
}

BOOST_AUTO_TEST_CASE(failed_standard_compilation_prevents_initialization_and_run)
{
    const TestDirectory directory("host-failed-compilation");
    auto host = ModuleHost::create({}, directory.getPath(), fixtureModules(), {}, ModuleMode::Application);
    const auto result = host->compileSources(SourceBatch{{{"init", {"editor:init", "missing.statement"}}}, {}});
    BOOST_TEST(!result.succeeded());
    BOOST_TEST(result.errorCount > 0U);
    BOOST_REQUIRE(!result.diagnostics.empty());
    BOOST_TEST(result.diagnostics[0].sourceName == "editor:init");
    BOOST_CHECK_THROW((void)host->run(), ModuleException);
    BOOST_TEST(events(directory).find("run\n") == NomadString::npos);
}

BOOST_AUTO_TEST_CASE(source_loading_errors_prevent_run_even_before_compiler_phases)
{
    const TestDirectory directory("host-source-failure");
    auto host = ModuleHost::create({}, directory.getPath(), fixtureModules(), {}, ModuleMode::Application);
    const auto result = host->compileProject();
    BOOST_TEST(!result.succeeded());
    BOOST_TEST(result.errorCount == 1U);
    BOOST_REQUIRE(result.diagnostics.size() == 1U);
    BOOST_TEST(result.diagnostics[0].message.find("Path does not exist") != NomadString::npos);
    BOOST_CHECK_THROW((void)host->run(), ModuleException);
    BOOST_TEST(events(directory).find("run\n") == NomadString::npos);
}

BOOST_AUTO_TEST_CASE(host_preserves_hook_diagnostics_without_spurious_entry_errors)
{
    const TestDirectory directory("host-hook-diagnostics");
    const auto configuration = NomadConfiguration::parse(
        "[fixture]\nfail-hook = true\n"
    );
    auto host = ModuleHost::create(configuration, directory.getPath(), fixtureModules(), {}, ModuleMode::Application);
    const auto result = host->compileSources(SourceBatch{});
    BOOST_TEST(!result.succeeded());
    BOOST_TEST(result.errorCount == 1U);
    BOOST_REQUIRE(result.diagnostics.size() == 1U);
    BOOST_TEST(result.diagnostics[0].message == "Requested hook failure");
    BOOST_CHECK_THROW((void)host->run(), ModuleException);
}

BOOST_AUTO_TEST_CASE(packaging_sessions_reject_all_compilation_overloads)
{
    const TestDirectory directory("host-no-packaging-compilation");
    auto host = ModuleHost::create({}, directory.getPath(), fixtureModules(), {}, ModuleMode::Packaging);
    BOOST_CHECK_THROW((void)host->compileProject(), ModuleException);
    BOOST_CHECK_THROW((void)host->compileSources(SourceBatch{}), ModuleException);
}

BOOST_AUTO_TEST_CASE(script_initialization_runs_after_compilation_before_entry)
{
    const TestDirectory directory("hub-startup");
    auto host = ModuleHost::create({}, directory.getPath(), fixtureModules(), {}, ModuleMode::Application);
    auto prepared = false;
    auto calls = 0;
    BOOST_REQUIRE(host->getRuntime()->registerNativeFunction(
        "fixture.initialValue",
        [&](VirtualMachine* machine) {
            BOOST_TEST(prepared);
            ++calls;
            machine->setIntegerResult(42);
        },
        {}, host->getRuntime()->getIntegerType(), ""
    ).isValid());
    const auto result = host->compileSources(SourceBatch{{
        {"init", {"init.nomad", "hub example\nvalue = fixture.initialValue\nend\nassert example.value == 42\nfixture.exit"}}
    }, {}});
    BOOST_REQUIRE(result.succeeded());
    BOOST_TEST(calls == 0);
    prepared = true;
    BOOST_TEST(host->run() == 7);
    BOOST_TEST(calls == 1);
}

BOOST_AUTO_TEST_CASE(runnable_modules_do_not_require_a_global_script_entry)
{
    const TestDirectory directory("host-no-script-entry");
    auto configuration = NomadConfiguration::parse("[fixture]\nentry = \"\"\n");
    auto host = ModuleHost::create(configuration, directory.getPath(), fixtureModules(), {}, ModuleMode::Application);
    BOOST_REQUIRE(compileFixture(*host, CompilationTarget::Executable,
        "fun calculate\nreturn 42\nend\nhub example\nvalue = calculate\nend", "declarations"));

    BOOST_TEST(host->run() == 0);
    RuntimeValue value;
    host->getRuntime()->getContextVariableValue(host->getRuntime()->getContextVariableId("example.value"), value);
    BOOST_TEST(value.getIntegerValue() == 42);
    BOOST_TEST(events(directory).find("run\n") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(initialization_exit_prevents_the_run_callback)
{
    const TestDirectory directory("host-initialization-exit");
    auto host = ModuleHost::create({}, directory.getPath(), fixtureModules(), {}, ModuleMode::Application);
    BOOST_REQUIRE(compileFixture(*host, CompilationTarget::Executable,
        "fun initialize\nfixture.exit\nreturn 42\nend\nhub example\nvalue = initialize\nend"));

    BOOST_TEST(host->run() == 7);
    BOOST_TEST(events(directory).find("run\n") == NomadString::npos);
}

BOOST_AUTO_TEST_CASE(initialization_failure_prevents_the_run_callback_and_is_not_retried)
{
    const TestDirectory directory("host-initialization-failure");
    auto host = ModuleHost::create({}, directory.getPath(), fixtureModules(), {}, ModuleMode::Application);
    auto calls = 0;
    host->getRuntime()->registerNativeFunction(
        "fixture.fail",
        [&](VirtualMachine*) {
            ++calls;
            throw VirtualMachineException("Requested initialization failure");
        },
        {}, host->getRuntime()->getIntegerType(), ""
    );
    BOOST_REQUIRE(compileFixture(*host, CompilationTarget::Executable, "hub example\nvalue = fixture.fail\nend"));

    BOOST_CHECK_THROW((void)host->run(), VirtualMachineException);
    BOOST_CHECK_THROW((void)host->run(), VirtualMachineException);
    BOOST_TEST(calls == 1);
    BOOST_TEST(events(directory).find("run\n") == NomadString::npos);
}

BOOST_AUTO_TEST_CASE(running_requires_an_application_session_and_a_runnable_module)
{
    const TestDirectory directory("host-run-contract");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"system"});
    auto application = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Application);
    BOOST_CHECK_EXCEPTION((void)application->run(), ModuleException, [](const auto& error) {
        return NomadString(error.what()).find("requires a module with a run callback") != NomadString::npos;
    });
    auto analysis = ModuleHost::create({}, directory.getPath(), fixtureModules(), {}, ModuleMode::Analysis);
    BOOST_CHECK_THROW((void)analysis->run(), ModuleException);
    auto packaging = ModuleHost::create({}, directory.getPath(), fixtureModules(), {}, ModuleMode::Packaging);
    BOOST_CHECK_THROW((void)packaging->run(), ModuleException);
}

BOOST_AUTO_TEST_CASE(game_module_executes_its_configured_entry_after_initialization)
{
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    const TestDirectory directory("host-game-entry");
    auto configuration = NomadConfiguration::parse(
        "[project]\nname = \"Test\"\n[game]\nentry = \"launch\"\n"
        "[text]\ndirectories = []\n[audio]\ndirectories = []\n[sprites]\ndirectories = []\n"
    );
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"game"});
    auto host = ModuleHost::create(configuration, directory.getPath(), modules, {}, ModuleMode::Application);
    auto calls = 0;
    host->getRuntime()->registerNativeFunction(
        "test.initialValue",
        [&](VirtualMachine* machine) {
            ++calls;
            machine->setIntegerResult(42);
        },
        {}, host->getRuntime()->getIntegerType(), ""
    );
    const auto result = host->compileSources(SourceBatch{{{"launch", {"launch.nomad",
        "hub example\nvalue = test.initialValue\nend\n"
        "assert example.value == 42\nexample.value = 43\nsystem.exit"}}}, {}});
    BOOST_REQUIRE(result.succeeded());
    BOOST_TEST(calls == 0);
    BOOST_TEST(host->run() == 0);
    BOOST_TEST(calls == 1);
    RuntimeValue value;
    host->getRuntime()->getContextVariableValue(host->getRuntime()->getContextVariableId("example.value"), value);
    BOOST_TEST(value.getIntegerValue() == 43);
}

BOOST_AUTO_TEST_CASE(game_entry_is_not_required_during_analysis_or_executable_compilation)
{
    const TestDirectory directory("host-game-entry-validation");
    const auto configuration = NomadConfiguration::parse(
        "[project]\nname = \"Test\"\n[game]\nentry = \"launch\"\n"
        "[text]\ndirectories = []\n[audio]\ndirectories = []\n[sprites]\ndirectories = []\n"
    );
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"game"});
    for (const auto target : {CompilationTarget::Analysis, CompilationTarget::Executable}) {
        for (const auto& source : {"", "fun launch value:int\nend", "fun launch\nreturn 1\nend"}) {
            auto host = ModuleHost::create(configuration, directory.getPath(), modules, {}, ModuleMode::Analysis);
            auto compiler = host->getRuntime()->createCompiler();
            CompilerContext context(compiler.get());
            compiler->registerSources(&context, {{{"other", {"other.nomad", source}}}, {}});
            BOOST_TEST(compiler->compileScripts(&context, target));
            BOOST_TEST(context.getFirstError() == nullptr);
        }
    }
}

BOOST_AUTO_TEST_CASE(missing_game_entry_is_reported_during_startup)
{
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    const TestDirectory directory("host-missing-game-entry");
    const auto configuration = NomadConfiguration::parse(
        "[game]\nentry = 'missing'\n[text]\ndirectories = []\n"
        "[audio]\ndirectories = []\n[sprites]\ndirectories = []\n"
    );
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"game"});
    auto host = ModuleHost::create(configuration, directory.getPath(), modules, {}, ModuleMode::Application);
    const auto result = host->compileSources(SourceBatch{});
    BOOST_REQUIRE(result.succeeded());
    BOOST_CHECK_EXCEPTION((void)host->run(), GameException, [](const auto& error) {
        return NomadStringView(error.what()).find("Configured entry function 'missing' was not found")
            != NomadStringView::npos;
    });
}

BOOST_AUTO_TEST_CASE(range_overloads_are_reusable_iterables_in_compiler_and_interpreter) {
    Runtime runtime;
    const NomadString source =
        "total = 0\n"
        "sequence = range 4\n"
        "for value in sequence\n"
        "    total = total + value\n"
        "end\n"
        "for value in range 2 5\n"
        "    total = total + value\n"
        "end\n"
        "for value in range 5 0 (-2)\n"
        "    total = total + value\n"
        "end\n"
        "for outer in sequence\n"
        "    for inner in sequence\n"
        "        total = total + 1\n"
        "    end\n"
        "end\n"
        "for value in range 0\n"
        "    total = total + 1000\n"
        "end\n"
        "for value in range 5 0\n"
        "    total = total + 1000\n"
        "end\n"
        "for value in range 0 5 (-1)\n"
        "    total = total + 1000\n"
        "end\n"
        "cursor = sequence.iterator\n"
        "sequence = range 0\n"
        "cursor.moveNext\n"
        "total = total + cursor.current\n";
    auto compiler = runtime.createCompiler();
    CompilerContext compilation(compiler.get());
    compiler->registerSources(&compilation, {{{"main", {"main.nomad", source + "return total"}}}, {}});
    const auto compiled = compiler->compileScripts(&compilation, CompilationTarget::Executable);
    BOOST_REQUIRE_MESSAGE(compiled,
        (compilation.getFirstError() ? formatDiagnostic(*compilation.getFirstError()) : "Compilation failed"));
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(result.getIntegerValue() == 40);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        const auto succeeded = Interpreter{}.execute(source, context);
        BOOST_REQUIRE_MESSAGE(succeeded, (context.getError() ? *context.getError() : "Interpreter failed"));
        BOOST_REQUIRE(Interpreter{}.evaluateExpression("total", context));
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 40);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(range_handles_integer_limits_and_rejects_zero_step_at_runtime) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext compilation(compiler.get());
    const NomadString source =
        "count = 0\n"
        "for value in range 9223372036854775806 9223372036854775807 2\n"
        "    count = count + 1\n"
        "end\n"
        "for value in range (-9223372036854775807) (-9223372036854775808) (-2)\n"
        "    count = count + 1\n"
        "end\n"
        "for value in range 9223372036854775807 (-9223372036854775808) (-9223372036854775808)\n"
        "    count = count + 1\n"
        "end\n";
    compiler->registerSources(&compilation, {{{
        "main", {"main.nomad", source + "return count"}
    }, {
        "invalid", {"invalid.nomad", "return range 0 5 0"}
    }}, {}});
    const auto compiled = compiler->compileScripts(&compilation, CompilationTarget::Executable);
    BOOST_REQUIRE_MESSAGE(compiled,
        (compilation.getFirstError() ? formatDiagnostic(*compilation.getFirstError()) : "Compilation failed"));
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(result.getIntegerValue() == 4);
    BOOST_CHECK_EXCEPTION(runtime.executeFunction(runtime.getFunctionId("invalid"), {}, result),
        NomadException, [](const NomadException& error) {
            return NomadString(error.what()).find("Range step must not be zero") != NomadString::npos;
        });
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);
    BOOST_REQUIRE(Interpreter{}.execute(source, context));
    BOOST_REQUIRE(Interpreter{}.evaluateExpression("count", context));
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 4);
    BOOST_REQUIRE(Interpreter{}.execute("zero = 0", context));
    BOOST_CHECK_THROW((void)Interpreter{}.evaluateExpression("range 0 5 zero", context), NomadException);
    BOOST_TEST(!Interpreter{}.evaluateExpression("range \"invalid\"", context));
    BOOST_TEST(!Interpreter{}.evaluateExpression("range", context));
    BOOST_TEST(!Interpreter{}.evaluateExpression("range 1 2 3 4", context));
}

BOOST_AUTO_TEST_CASE(range_arguments_are_evaluated_once_and_loop_controls_use_normal_iteration) {
    Runtime runtime;
    auto calls = 0;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "test.bound",
        [&calls](VirtualMachine* machine) {
            ++calls;
            machine->setIntegerResult(machine->getIntegerParameter(0));
        },
        {defParameter("value", runtime.getIntegerType(), "")}, runtime.getIntegerType(), ""
    ).isValid());
    const NomadString source =
        "total = 0\n"
        "sequence = range (test.bound 0) (test.bound 10) (test.bound 1)\n"
        "for value in sequence\n"
        "    if value == 1 continue\n"
        "    if value == 4 break\n"
        "    total = total + value\n"
        "end\n";
    auto compiler = runtime.createCompiler();
    CompilerContext compilation(compiler.get());
    compiler->registerSources(&compilation, {{{"main", {"main.nomad", source + "return total"}}}, {}});
    const auto compiled = compiler->compileScripts(&compilation, CompilationTarget::Executable);
    BOOST_REQUIRE_MESSAGE(compiled,
        (compilation.getFirstError() ? formatDiagnostic(*compilation.getFirstError()) : "Compilation failed"));
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(result.getIntegerValue() == 5);
    BOOST_TEST(calls == 3);
    calls = 0;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);
    BOOST_REQUIRE(Interpreter{}.execute(source, context));
    BOOST_REQUIRE(Interpreter{}.evaluateExpression("total", context));
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 5);
    BOOST_TEST(calls == 3);
}

BOOST_AUTO_TEST_CASE(collection_loops_run_in_compiler_and_interpreter) {
    TestDirectory directory("nomad_host_collection_loops");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    const NomadString source =
        "items = new List<int>\n"
        "items.append 1\nitems.append 2\nitems.append 3\n"
        "total = 0\n"
        "for value in items\n"
        "    if value == 2 continue\n"
        "    total = total + value\n"
        "    for nested in items\n"
        "        total = total + nested\n"
        "    end\n"
        "end\n"
        "array = new Array<int> 2\n"
        "array[0] = 10\narray[1] = 20\n"
        "for value in array\n"
        "    total = total + value\n"
        "end\n"
        "map = new Map<string int>\n"
        "map[\"one\"] = 100\nmap[\"two\"] = 200\n"
        "for entry in map.entries\n"
        "    total = total + entry.value\n"
        "end\n"
        "for value in range 1 3\n"
        "    text = $\"{value} {value + 1}\"\n"
        "    total = total + value\n"
        "end\n";
    auto compiler = runtime.createCompiler();
    CompilerContext compilation(compiler.get());
    compiler->registerSources(&compilation, {{{"main", {"main.nomad", source + "return total"}}}, {}});
    const auto compiled = compiler->compileScripts(&compilation, CompilationTarget::Executable);
    BOOST_REQUIRE_MESSAGE(compiled,
        (compilation.getFirstError() ? formatDiagnostic(*compilation.getFirstError()) : "Compilation failed"));
    RuntimeValue empty;
    runtime.getStringType()->initValue(empty);
    runtime.getStringType()->freeValue(empty);
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(result.getIntegerValue() == 349);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        const auto succeeded = Interpreter{}.execute(source, context);
        BOOST_REQUIRE_MESSAGE(succeeded, (context.getError() ? *context.getError() : "Interpreter failed"));
        BOOST_REQUIRE(Interpreter{}.evaluateExpression("total", context));
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 349);
        BOOST_REQUIRE(Interpreter{}.evaluateExpression("value", context));
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 2);
        BOOST_REQUIRE(Interpreter{}.evaluateExpression("text", context));
        BOOST_TEST(context.getResult()->getValue().getStringValue() == "2 3");
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(collection_iterators_keep_sources_and_obey_current_and_completion_contracts) {
    TestDirectory directory("nomad_host_iterator_contract");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    RuntimeValue empty;
    runtime.getStringType()->initValue(empty);
    runtime.getStringType()->freeValue(empty);
    {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        BOOST_REQUIRE(Interpreter{}.execute("items = new List<string>\nitems.append \"first\"", context));
        BOOST_REQUIRE(Interpreter{}.execute("cursor = items.iterator", context));
        BOOST_CHECK_THROW((void)Interpreter{}.evaluateExpression("cursor.current", context), NomadException);
        BOOST_REQUIRE(Interpreter{}.execute("items = new List<string>", context));
        BOOST_REQUIRE(Interpreter{}.evaluateExpression("cursor.moveNext", context));
        BOOST_TEST(context.getResult()->getValue().getBooleanValue());
        BOOST_REQUIRE(Interpreter{}.evaluateExpression("cursor.current", context));
        BOOST_TEST(context.getResult()->getValue().getStringValue() == "first");
        BOOST_REQUIRE(Interpreter{}.evaluateExpression("cursor.moveNext", context));
        BOOST_TEST(!context.getResult()->getValue().getBooleanValue());
        BOOST_CHECK_THROW((void)Interpreter{}.evaluateExpression("cursor.current", context), NomadException);
        BOOST_REQUIRE(Interpreter{}.evaluateExpression("cursor.moveNext", context));
        BOOST_TEST(!context.getResult()->getValue().getBooleanValue());
    }
    auto compiler = runtime.createCompiler();
    CompilerContext compilation(compiler.get());
    const NomadString source =
        "items = new List<string>\nitems.append \"kept\"\n"
        "for value in items\nreturn value\nend\nreturn \"\"";
    compiler->registerSources(&compilation, {{{"earlyReturn", {"earlyReturn.nomad", source}}}, {}});
    const auto compiled = compiler->compileScripts(&compilation, CompilationTarget::Executable);
    BOOST_REQUIRE_MESSAGE(compiled,
        (compilation.getFirstError() ? formatDiagnostic(*compilation.getFirstError()) : "Compilation failed"));
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("earlyReturn"), {}, result);
    BOOST_TEST(result.getStringValue() == "kept");
    runtime.getStringType()->freeValue(result);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(collection_loops_allow_replacement_failed_removal_and_mutation_before_break) {
    TestDirectory directory("nomad_host_collection_mutation");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    const NomadString source =
        "items = new List<int>\nitems.append 1\nitems.append 2\n"
        "total = 0\n"
        "for value in items\n"
        "    items[1] = 10\n"
        "    items.remove 100\n"
        "    total = total + value\n"
        "end\n"
        "for value in items\n"
        "    items.append 3\n"
        "    break\n"
        "end\n"
        "empty = new List<int>\n"
        "for value in empty\n"
        "    total = 1000\n"
        "end\n";
    auto compiler = runtime.createCompiler();
    CompilerContext compilation(compiler.get());
    compiler->registerSources(&compilation, {{{"main", {"main.nomad", source + "return total"}}}, {}});
    const auto compiled = compiler->compileScripts(&compilation, CompilationTarget::Executable);
    BOOST_REQUIRE_MESSAGE(compiled,
        (compilation.getFirstError() ? formatDiagnostic(*compilation.getFirstError()) : "Compilation failed"));
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(result.getIntegerValue() == 11);
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);
    const auto succeeded = Interpreter{}.execute(source, context);
    BOOST_REQUIRE_MESSAGE(succeeded, (context.getError() ? *context.getError() : "Interpreter failed"));
    BOOST_REQUIRE(Interpreter{}.evaluateExpression("total", context));
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 11);
}

BOOST_AUTO_TEST_CASE(collection_iterators_detect_structural_mutation_on_next_advance) {
    TestDirectory directory("nomad_host_iteration_invalidated");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    for (const auto* mutation : {"items.append 3", "items.prepend 3", "items.insert 0 3",
        "items.remove 0", "items.removeFirst", "items.removeLast", "items.clear"}) {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        const auto source = NomadString{
            "items = new List<int>\nitems.append 1\nitems.append 2\n"
            "for value in items\n"
        } + mutation + "\nend";
        BOOST_TEST_CONTEXT(mutation) {
            BOOST_TEST(!Interpreter{}.execute(source, context));
            BOOST_REQUIRE(context.getError() != nullptr);
            BOOST_TEST(context.getError()->find("structurally modified") != NomadString::npos, *context.getError());
        }
    }
}

BOOST_AUTO_TEST_CASE(collection_loops_reject_non_iterables) {
    TestDirectory directory("nomad_host_iteration_invalid_sources");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    for (const auto* source : {
        "for value in 42\nend",
        "map = new Map<string int>\nfor value in map\nend",
        "array = new Array<int int> 2 2\nfor value in array\nend"
    }) {
        auto compiler = runtime.createCompiler();
        CompilerContext compilation(compiler.get());
        compiler->registerSources(&compilation, {{{"rejected", {"rejected.nomad", source}}}, {}});
        BOOST_TEST(!compiler->compileScripts(&compilation, CompilationTarget::Analysis));
    }
}

BOOST_AUTO_TEST_CASE(collection_loop_bindings_preserve_values_and_allow_reassignment) {
    TestDirectory directory("nomad_host_mutable_loop_bindings");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    const NomadString source =
        "items = new List<string>\n"
        "items.append \"first\"\nitems.append \"last\"\n"
        "for value in items\n"
        "    value = value.toUpper\n"
        "end\n"
        "empty = new List<string>\n"
        "for value in empty\nend\n"
        "for fresh in empty\nend\n"
        "for stopped in items\n"
        "    stopped = \"break\"\n"
        "    break\n"
        "end\n"
        "number = 20\n"
        "for number in range 0\nend\n"
        "for unused in range 0\nend\n"
        "count = 0\n"
        "for shared in range 2\n"
        "    for shared in range 3\n"
        "        shared = 100\n"
        "        count = count + 1\n"
        "    end\n"
        "end\n"
        "text = $\"{value}{fresh}{stopped}{number} {unused} {count} {shared}\"\n";
    auto compiler = runtime.createCompiler();
    CompilerContext compilation(compiler.get());
    compiler->registerSources(&compilation, {{{"main", {"main.nomad", source + "return text"}}}, {}});
    const auto compiled = compiler->compileScripts(&compilation, CompilationTarget::Executable);
    BOOST_REQUIRE_MESSAGE(compiled,
        (compilation.getFirstError() ? formatDiagnostic(*compilation.getFirstError()) : "Compilation failed"));
    RuntimeValue emptyString;
    runtime.getStringType()->initValue(emptyString);
    runtime.getStringType()->freeValue(emptyString);
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(result.getStringValue() == "LASTbreak20 0 6 100");
    runtime.getStringType()->freeValue(result);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        const auto succeeded = Interpreter{}.execute(source, context);
        BOOST_REQUIRE_MESSAGE(succeeded, (context.getError() ? *context.getError() : "Interpreter failed"));
        BOOST_REQUIRE(Interpreter{}.evaluateExpression("text", context));
        BOOST_TEST(context.getResult()->getValue().getStringValue() == "LASTbreak20 0 6 100");
        BOOST_REQUIRE(Interpreter{}.execute("value = \"still here\"", context));
        BOOST_REQUIRE(Interpreter{}.evaluateExpression("value", context));
        BOOST_TEST(context.getResult()->getValue().getStringValue() == "still here");
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(object_parameter_reassignment_preserves_caller_references_and_shared_mutations) {
    TestDirectory directory("nomad_host_mutable_object_parameters");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    auto compiler = runtime.createCompiler();
    CompilerContext compilation(compiler.get());
    const NomadString source =
        "fun change items:List<int>\n"
        "    items.append 1\n"
        "    items = new List<int>\n"
        "    items.append 2\n"
        "    return items[0]\n"
        "end\n"
        "items = new List<int>\n"
        "items.append 10\n"
        "result = change items\n"
        "return result * 100 + items.count * 10 + items[1]";
    compiler->registerSources(&compilation, {{{"main", {"main.nomad", source}}}, {}});
    const auto compiled = compiler->compileScripts(&compilation, CompilationTarget::Executable);
    BOOST_REQUIRE_MESSAGE(compiled,
        (compilation.getFirstError() ? formatDiagnostic(*compilation.getFirstError()) : "Compilation failed"));
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(result.getIntegerValue() == 221);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(collections_key_values_construct_read_and_share_references) {
    TestDirectory directory("nomad_host_key_value");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    const std::vector<NomadString> sources = {
        "entry = new KeyValue<string int> \"score\" 100",
        "assert entry.key == \"score\"",
        "assert entry.value == 100",
        "flags = new KeyValue<int bool> 123 true",
        "assert flags.key == 123",
        "assert flags.value",
        "words = new KeyValue<string string> \"key\" \"value\"",
        "savedKey = words.key",
        "savedValue = words.value",
        "words = new KeyValue<string string> \"other\" \"replacement\"",
        "assert savedKey == \"key\"",
        "assert savedValue == \"value\"",
        "numbers = new List<int>",
        "numbers.append 7",
        "shared = new KeyValue<List<int> List<int>> numbers numbers",
        "alias = shared",
        "shared.key.append 8",
        "alias.value.append 9",
        "assert numbers.count == 3",
        "assert numbers[1] == 8",
        "assert numbers[2] == 9",
        "savedList = shared.value",
        "shared = new KeyValue<List<int> List<int>> (new List<int>) (new List<int>)",
        "alias = shared",
        "assert savedList[0] == 7",
        "nested = new KeyValue<string KeyValue<int bool>> \"nested\" flags",
        "assert nested.value.key == 123",
        "assert nested.value.value",
        "pairs = new Array<KeyValue<string List<int>>> 2",
        "assert pairs[0].key == \"\"",
        "assert pairs[0].value.count == 0",
        "pairs[0].value.append 42",
        "assert pairs[1].value.count == 0",
        "assert pairs[0].value[0] == 42",
        "pairs[1] = new KeyValue<string List<int>> \"numbers\" numbers",
        "assert pairs[1].key == \"numbers\"",
        "assert pairs[1].value.count == 3",
        "map = new Map<string KeyValue<string int>>",
        "map[\"entry\"] = entry",
        "assert map[\"entry\"].key == \"score\"",
        "assert map[\"entry\"].value == 100"
    };
    NomadString script;
    for (const auto& source : sources) {
        script += source + "\n";
    }
    auto compiler = runtime.createCompiler();
    CompilerContext compilation(compiler.get());
    compiler->registerSources(&compilation, {{{"main", {"main.nomad", script}}}, {}});
    BOOST_REQUIRE_MESSAGE(compiler->compileScripts(&compilation, CompilationTarget::Executable),
        (compilation.getFirstError() ? formatDiagnostic(*compilation.getFirstError()) : "Compilation failed"));
    RuntimeValue empty;
    runtime.getStringType()->initValue(empty);
    runtime.getStringType()->freeValue(empty);
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    {
        VirtualMachine machine(&runtime);
        InterpreterContext context(machine);
        for (const auto& source : sources) {
            BOOST_TEST_CONTEXT(source) {
                const auto isAssertion = source.starts_with("assert ");
                BOOST_REQUIRE_MESSAGE(Interpreter{}.execute(isAssertion ? source.substr(7) : source, context),
                    (context.getError() ? *context.getError() : source));
                if (isAssertion) {
                    BOOST_TEST(context.getResult()->getValue().getBooleanValue());
                }
            }
        }
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    const auto* type = runtime.getParameterizedType("KeyValue", {runtime.getStringType(), runtime.getIntegerType()});
    BOOST_TEST(type == runtime.getTypeByName("KeyValue<string int>"));
    BOOST_TEST(type == runtime.getParameterizedType("KeyValue", {runtime.getStringType(), runtime.getIntegerType()}));
    BOOST_TEST(runtime.getConstructorTemplate("KeyValue").has_value());
    std::ostringstream documentation;
    generateTypeDocumentation(&runtime, documentation);
    generateNativeMemberDocumentation(&runtime, documentation);
    BOOST_TEST(documentation.str().find("new KeyValue<string int> key:string value:int") != NomadString::npos);
    BOOST_TEST(documentation.str().find("#### `key` (*read-only property*)") != NomadString::npos);
    BOOST_TEST(documentation.str().find("#### `value` (*read-only property*)") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(collections_key_values_infer_constructor_types) {
    TestDirectory directory("nomad_host_key_value_inference");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    const std::vector<NomadString> sources = {
        "entry = new KeyValue \"score\" 100",
        "assert entry.key == \"score\"",
        "assert entry.value == 100",
        "flags = new KeyValue 123 true",
        "assert flags.key == 123",
        "assert flags.value",
        "key = \"numbers\"",
        "numbers = new List<int>",
        "entryWithList = new KeyValue key numbers",
        "entryWithList.value.append 42",
        "assert numbers[0] == 42",
        "nested = new KeyValue \"outer\" (new KeyValue 123 true)",
        "assert nested.value.key == 123",
        "assert nested.value.value",
        "pairs = new Array<KeyValue<string int>> 1",
        "pairs[0] = new KeyValue \"score\" 100",
        "assert pairs[0].value == 100",
        "explicitEntry = new KeyValue<string int> \"explicit\" 7",
        "explicitEntry = entry",
        "assert explicitEntry.key == \"score\""
    };
    NomadString script;
    for (const auto& source : sources) {
        script += source + "\n";
    }
    auto compiler = runtime.createCompiler();
    CompilerContext compilation(compiler.get());
    compiler->registerSources(&compilation, {{{
        "main", {"main.nomad", script}
    }, {
        "makeEntry", {"makeEntry.nomad", "return new KeyValue \"score\" 100"}
    }, {
        "makeFlags", {"makeFlags.nomad", "return new KeyValue 123 true"}
    }}, {}});
    const auto compiled = compiler->compileScripts(&compilation, CompilationTarget::Executable);
    BOOST_REQUIRE_MESSAGE(compiled,
        (compilation.getFirstError() ? formatDiagnostic(*compilation.getFirstError()) : "Compilation failed"));
    const auto* entryType = runtime.getParameterizedType("KeyValue", {runtime.getStringType(), runtime.getIntegerType()});
    const auto* flagsType = runtime.getParameterizedType("KeyValue", {runtime.getIntegerType(), runtime.getBooleanType()});
    BOOST_TEST(runtime.getScriptFunction(runtime.getFunctionId("makeEntry"))->getReturnType() == entryType);
    BOOST_TEST(runtime.getScriptFunction(runtime.getFunctionId("makeFlags"))->getReturnType() == flagsType);
    RuntimeValue empty;
    runtime.getStringType()->initValue(empty);
    runtime.getStringType()->freeValue(empty);
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    {
        VirtualMachine machine(&runtime);
        InterpreterContext evaluation(machine);
        for (const auto& source : sources) {
            BOOST_TEST_CONTEXT(source) {
                const auto isAssertion = source.starts_with("assert ");
                BOOST_REQUIRE_MESSAGE(Interpreter{}.execute(isAssertion ? source.substr(7) : source, evaluation),
                    (evaluation.getError() ? *evaluation.getError() : source));
                if (isAssertion) {
                    BOOST_TEST(evaluation.getResult()->getValue().getBooleanValue());
                }
            }
        }
        BOOST_REQUIRE(Interpreter{}.execute("entry", evaluation));
        BOOST_TEST(evaluation.getResult()->getType() == entryType);
        BOOST_REQUIRE(Interpreter{}.execute("flags", evaluation));
        BOOST_TEST(evaluation.getResult()->getType() == flagsType);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(collections_key_values_reject_invalid_construction_and_property_writes) {
    TestDirectory directory("nomad_host_key_value_validation");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    VirtualMachine machine(&runtime);
    InterpreterContext evaluation(machine);
    for (const auto* source : {
        "entry = new KeyValue<string int>",
        "entry = new KeyValue<string int> \"key\"",
        "entry = new KeyValue<string int> \"key\" 1 2",
        "entry = new KeyValue<string int> 1 2",
        "entry = new KeyValue<string int> \"key\" true",
        "entry = new KeyValue<string> \"key\"",
        "entry = new KeyValue<string int bool> \"key\" 1",
        "entry = new KeyValue<void int> 0 1",
        "entry = new KeyValue<string void> \"key\" 1",
        "entry = new KeyValue",
        "entry = new KeyValue \"key\"",
        "entry = new KeyValue \"key\" 1 2",
        "entry = new KeyValue \"key\" 1\nentry.key = \"other\"",
        "entry = new KeyValue \"key\" 1\nentry.value = 2",
        "pairs = new Array<KeyValue<string int>> 1\npairs[0] = new KeyValue 1 \"key\"",
        "entry = new KeyValue<string int> \"key\" 1\nentry.key = \"other\"",
        "entry = new KeyValue<string int> \"key\" 1\nentry.value = 2",
        "entry = new KeyValue<string int> \"key\" 1\nentry[0]",
        "entry = new KeyValue<string int> \"key\" 1\nentry.missing"
    }) {
        BOOST_TEST_CONTEXT(source) {
            auto compiler = runtime.createCompiler();
            CompilerContext compilation(compiler.get());
            compiler->registerSources(&compilation, {{{"rejected", {"rejected.nomad", source}}}, {}});
            BOOST_TEST(!compiler->compileScripts(&compilation, CompilationTarget::Executable));
            BOOST_REQUIRE(compilation.getFirstError() != nullptr);
            const NomadString input(source);
            const auto lineBreak = input.find('\n');
            if (lineBreak != NomadString::npos) {
                BOOST_REQUIRE(Interpreter{}.execute(input.substr(0, lineBreak), evaluation));
            }
            BOOST_TEST(!Interpreter{}.execute(
                lineBreak == NomadString::npos ? input : input.substr(lineBreak + 1), evaluation
            ));
            BOOST_TEST(evaluation.hasError());
        }
    }
    BOOST_CHECK_THROW(KeyValueType(&runtime, TypeId::INVALID, "Invalid", ClassMode::Closed, runtime.getFileNameType(), runtime.getIntegerType()), NomadException);
    BOOST_CHECK_THROW(KeyValueType(&runtime, TypeId::INVALID, "Invalid", ClassMode::Closed, runtime.getIntegerType(), runtime.getFileNameType()), NomadException);
    BOOST_CHECK_THROW(KeyValueObject(runtime.getFileNameType(), runtime.getIntegerType()), NomadException);
}

BOOST_AUTO_TEST_CASE(collections_constructs_arrays_from_nomad_and_interpreter) {
    TestDirectory directory("nomad_host_array_construction");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    BOOST_REQUIRE(dynamic_cast<const Array1Type*>(
        runtime.getParameterizedType("Array1", {runtime.getIntegerType()})
    ) != nullptr);
    BOOST_REQUIRE(dynamic_cast<const Array2Type*>(
        runtime.getParameterizedType("Array2", {runtime.getStringType()})
    ) != nullptr);
    BOOST_REQUIRE(dynamic_cast<const Array3Type*>(
        runtime.getParameterizedType("Array3", {runtime.getFloatType()})
    ) != nullptr);
    NomadString calls;
    runtime.registerNativeFunction(
        "size",
        [&calls](VirtualMachine* machine) {
            const auto value = machine->getIntegerParameter(0);
            calls += std::to_string(value);
            machine->setIntegerResult(value);
        },
        {defParameter("value", runtime.getIntegerType(), "")}, runtime.getIntegerType(), ""
    );
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"main", {"main.nomad",
        "line = new Array1<int> 10\n"
        "grid = new Array2<string> (size 2) (size 3)\n"
        "volume = new Array3<float> 2 3 4\n"
        "alias = grid\n"
        "assert alias.count == 6\n"
        "assert line.count == 10\n"
        "assert volume.count == 24\n"
        "assert (new Array1<int> (2 + 3)).count == 5\n"
        "arrays = new List<Array1<int>>\n"
        "arrays.append line\n"
        "assert arrays[0].count == 10\n"
        "return new Array1<Array2<int>> 2"
    }}}, {}});
    const auto compiled = compiler->compileScripts(&context, CompilationTarget::Executable);
    BOOST_REQUIRE_MESSAGE(compiled, (context.getFirstError() ? formatDiagnostic(*context.getFirstError()) : "Compilation failed"));
    const auto* nestedType = runtime.getParameterizedType("Array2", {runtime.getIntegerType()});
    RuntimeValue empty;
    nestedType->initValue(empty);
    nestedType->freeValue(empty);
    // The process-wide empty string is retained after the runtime's arrays are released.
    runtime.getStringType()->initValue(empty);
    runtime.getStringType()->freeValue(empty);
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(calls == "32");
    const auto* outer = result.asObject<Array1dObject>();
    BOOST_TEST(outer->getCount() == 2u);
    RuntimeValue first;
    RuntimeValue second;
    outer->getValue(0, first);
    outer->getValue(1, second);
    BOOST_TEST(first.getObjectValue() == second.getObjectValue());
    BOOST_TEST(first.asObject<Array2dObject>()->getCount() == 0u);
    nestedType->freeValue(first);
    nestedType->freeValue(second);
    result.freeObjectValue();
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    calls.clear();
    {
        VirtualMachine machine(&runtime);
        InterpreterContext evaluation(machine);
        for (const auto* source : {
            "line = new Array1<int> 10", "grid = new Array2<string> (size 2) (size 3)",
            "volume = new Array3<float> 2 3 4", "grid.count"
        }) {
            const auto executed = Interpreter{}.execute(source, evaluation);
            BOOST_REQUIRE_MESSAGE(executed, (evaluation.getError() ? *evaluation.getError() : source));
        }
        BOOST_TEST(calls == "23");
        BOOST_TEST(evaluation.getResult()->getValue().getIntegerValue() == 6);
        const auto* grid = evaluation.getVariableValue("grid")->asObject<Array2dObject>();
        BOOST_TEST(grid->getDimensionSize(0) == 2u);
        BOOST_TEST(grid->getDimensionSize(1) == 3u);
        RuntimeValue element;
        grid->getValue(1, 2, element);
        BOOST_TEST(runtime.getStringType()->toString(element) == "");
        runtime.getStringType()->freeValue(element);
        evaluation.getVariableValue("volume")->asObject<Array3dObject>()->getValue(1, 2, 3, element);
        BOOST_TEST(element.getFloatValue() == 0.0f);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    std::ostringstream documentation;
    generateTypeDocumentation(&runtime, documentation);
    BOOST_TEST(documentation.str().find("new Array2<string> size1:int size2:int") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(array_dimension_values_fail_at_runtime_not_compilation) {
    for (const auto* source : {
        "return new Array1<int> 0", "return new Array2<int> 1 0",
        "return new Array3<int> 1 0 1", "return new Array1<int> -1",
        "return new Array2<int> 9223372036854775807 2"
    }) {
        BOOST_TEST_CONTEXT(source) {
            TestDirectory directory("nomad_host_invalid_array_dimensions");
            const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
            auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
            auto& runtime = *host->getRuntime();
            auto compiler = runtime.createCompiler();
            CompilerContext context(compiler.get());
            compiler->registerSources(&context, {{{"main", {"main.nomad", source}}}, {}});
            const auto compiled = compiler->compileScripts(&context, CompilationTarget::Executable);
            BOOST_REQUIRE_MESSAGE(compiled, (context.getFirstError() ? formatDiagnostic(*context.getFirstError()) : "Compilation failed"));
            const auto before = NomadObject::getLiveObjectCount();
            auto machine = runtime.createVirtualMachine();
            BOOST_CHECK_THROW(machine->run(runtime.getScriptFunction(runtime.getFunctionId("main")), {}), NomadException);
            BOOST_TEST(NomadObject::getLiveObjectCount() == before);
            InterpreterContext evaluation(*machine);
            BOOST_TEST(!Interpreter{}.execute(NomadString(source).substr(7), evaluation));
            BOOST_REQUIRE(evaluation.getError() != nullptr);
            BOOST_TEST(NomadObject::getLiveObjectCount() == before);
        }
    }
}

BOOST_AUTO_TEST_CASE(array_constructor_overloads_return_rank_specific_types) {
    TestDirectory directory("nomad_host_array_overloads");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    NomadString calls;
    runtime.registerNativeFunction(
        "size",
        [&calls](VirtualMachine* machine) {
            const auto value = machine->getIntegerParameter(0);
            calls += std::to_string(value);
            machine->setIntegerResult(value);
        },
        {defParameter("value", runtime.getIntegerType(), "")}, runtime.getIntegerType(), ""
    );
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{
        "main", {"main.nomad",
            "line = new Array<int> (size 2)\n"
            "grid = new Array<int> (size 3) (size 4)\n"
            "volume = new Array<int> (size 5) (size 6) (size 7)\n"
            "assert (lineCount line) == 2\n"
            "assert (gridCount grid) == 12\n"
            "assert (volumeCount volume) == 210\n"
            "return grid"
        }
    }, {
        "lineCount", {"lineCount.nomad", "params value:Array1<int>\nreturn value.count"}
    }, {
        "gridCount", {"gridCount.nomad", "params value:Array2<int>\nreturn value.count"}
    }, {
        "volumeCount", {"volumeCount.nomad", "params value:Array3<int>\nreturn value.count"}
    }}, {}});
    const auto compiled = compiler->compileScripts(&context, CompilationTarget::Executable);
    BOOST_REQUIRE_MESSAGE(compiled, (context.getFirstError() ? formatDiagnostic(*context.getFirstError()) : "Compilation failed"));
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(result.asObject<Array2dObject>()->getCount() == 12u);
    result.freeObjectValue();
    BOOST_TEST(calls == "243765");
    const auto constructorCount = runtime.getConstructors().size();
    BOOST_TEST(runtime.getParameterizedConstructorName("Array", {runtime.getIntegerType()}) == "Array<int>");
    BOOST_TEST(runtime.getConstructors().size() == constructorCount);
    {
        VirtualMachine machine(&runtime);
        InterpreterContext evaluation(machine);
        for (const auto& [source, rank] : std::vector<std::pair<NomadString, NomadIndex>>{
            {"new Array<int> 2", 1}, {"new Array<int> 3 4", 2}, {"new Array<int> 5 6 7", 3}
        }) {
            BOOST_REQUIRE(Interpreter{}.execute(source, evaluation));
            const auto* type = dynamic_cast<const ArrayType*>(evaluation.getResult()->getType());
            BOOST_REQUIRE(type != nullptr);
            BOOST_TEST(type->getDimensionCount() == rank);
        }
        for (const auto* source : {
            "new Array<int>", "new Array<int> 0", "new Array<int> 1 (-1)",
            "new Array<int> 1 2 0", "new Array<int> 1 \"two\"",
            "new Array<int> 1 2 true", "new Array<int> 1 2 3 4",
            "new Array<> 1", "new Array<int string> 1", "new Array<void> 1"
        }) {
            BOOST_TEST_CONTEXT(source) {
                BOOST_TEST(!Interpreter{}.execute(source, evaluation));
                BOOST_REQUIRE(evaluation.getError() != nullptr);
            }
        }
    }
    std::ostringstream documentation;
    generateTypeDocumentation(&runtime, documentation);
    BOOST_TEST(documentation.str().find("new Array<int> size1:int size2:int` -> `Array2<int>") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(array_constructor_signatures_are_checked_by_compiler) {
    for (const auto* source : {
        "return new Array1<int>", "return new Array2<int> 2", "return new Array3<int> 2 3",
        "return new Array1<int> 1 2", "return new Array2<int> 1 \"two\"",
        "return new Array3<int> 1 2 true", "return new Array1<int> 1.0",
        "return new Array1<int string> 2"
    }) {
        BOOST_TEST_CONTEXT(source) {
            TestDirectory directory("nomad_host_array_constructor_types");
            const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
            auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
            auto compiler = host->getRuntime()->createCompiler();
            CompilerContext context(compiler.get());
            compiler->registerSources(&context, {{{"main", {"main.nomad", source}}}, {}});
            BOOST_TEST(!compiler->compileScripts(&context, CompilationTarget::Analysis));
            BOOST_REQUIRE(context.getFirstError() != nullptr);
        }
    }
}

BOOST_AUTO_TEST_CASE(collections_maps_construct_empty_typed_instances_and_defaults) {
    TestDirectory directory("nomad_host_map_construction");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    const auto* mapType = runtime.getParameterizedType("Map", {runtime.getStringType(), runtime.getIntegerType()});
    BOOST_REQUIRE(dynamic_cast<const MapType*>(mapType) != nullptr);
    BOOST_TEST(mapType->getTypeName() == "Map<string int>");
    BOOST_TEST(runtime.getParameterizedType("Map", {runtime.getStringType(), runtime.getIntegerType()}) == mapType);
    BOOST_TEST(!mapType->sameType(runtime.getParameterizedType("Map", {runtime.getStringType(), runtime.getStringType()})));
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue first;
    RuntimeValue second;
    mapType->initValue(first);
    mapType->initValue(second);
    BOOST_TEST(first.getObjectValue() != second.getObjectValue());
    BOOST_TEST(first.asObject<MapObject>()->getCount() == 0u);
    BOOST_TEST(first.asObject<MapObject>()->getValueType() == runtime.getIntegerType());
    mapType->freeValue(first);
    mapType->freeValue(second);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    const std::vector<NomadString> sources = {
        "names = new Map<string string>",
        "scores = new Map<string int>",
        "nested = new Map<string List<int>>",
        "maps = new Map<string Map<string int>>",
        "alias = scores",
        "assert names.count == 0",
        "assert scores.count == 0",
        "assert alias.count == 0",
        "assert nested.count == 0",
        "assert maps.count == 0",
        "defaults = new Array<Map<string int>> 2",
        "assert defaults[0].count == 0",
        "assert defaults[1].count == 0",
        "defaults[0] = scores",
        "assert defaults[0].count == 0"
    };
    NomadString script =
        "fun countEntries values:Map<string int>\n"
        "    return values.count\n"
        "end\n";
    for (const auto& source : sources) {
        script += source + "\n";
    }
    script += "return countEntries scores";
    auto compiler = runtime.createCompiler();
    CompilerContext compilation(compiler.get());
    compiler->registerSources(&compilation, {{{"main", {"main.nomad", script}}}, {}});
    BOOST_REQUIRE_MESSAGE(compiler->compileScripts(&compilation, CompilationTarget::Executable),
        (compilation.getFirstError() ? formatDiagnostic(*compilation.getFirstError()) : "Compilation failed"));
    const auto compiledObjects = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(result.getIntegerValue() == 0);
    BOOST_TEST(NomadObject::getLiveObjectCount() == compiledObjects);
    {
        VirtualMachine machine(&runtime);
        InterpreterContext context(machine);
        for (const auto& source : sources) {
            BOOST_TEST_CONTEXT(source) {
                const auto isAssertion = source.starts_with("assert ");
                BOOST_REQUIRE_MESSAGE(Interpreter{}.execute(isAssertion ? source.substr(7) : source, context),
                    (context.getError() ? *context.getError() : source));
                if (isAssertion) {
                    BOOST_TEST(context.getResult()->getValue().getBooleanValue());
                }
            }
        }
        BOOST_TEST(context.getVariableValue("scores")->getObjectValue() ==
            context.getVariableValue("alias")->getObjectValue());
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == compiledObjects);
    std::ostringstream documentation;
    generateNativeMemberDocumentation(&runtime, documentation);
    BOOST_TEST(documentation.str().find("### `Map<string int>`") != NomadString::npos);
    BOOST_TEST(documentation.str().find("The number of entries in the map.") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(collections_map_indexers_insert_replace_and_preserve_owned_values) {
    TestDirectory directory("nomad_host_map_indexers");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    const std::vector<NomadString> sources = {
        "scores = new Map<string int>",
        "scores[\"hero\"] = 100",
        "scores[\"Hero\"] = 20",
        "scores[\"\"] = 3",
        "assert scores.count == 3",
        "assert scores[\"Hero\"] == 20",
        "assert scores[\"\"] == 3",
        "alias = scores",
        "alias[\"hero\"] = scores[\"hero\"] + 1",
        "assert scores[\"hero\"] == 101",
        "assert scores.count == 3",
        "assert scores[\"missing\"] == 0",
        "assert scores.count == 3",
        "words = new Map<string string>",
        "key = \"name\"",
        "words[key] = \"hello\"",
        "key = \"changed\"",
        "words[\"name\"] = words[\"name\"]",
        "assert words[\"name\"][1] == \"e\"",
        "words[\"name\"] = \"updated\"",
        "assert words[\"name\"] == \"updated\"",
        "assert words[\"missing\"] == \"\"",
        "assert words.count == 1",
        "nested = new Map<string List<int>>",
        "nested[\"list\"] = new List<int>",
        "nested[\"list\"].append 7",
        "saved = nested[\"list\"]",
        "nested[\"list\"] = new List<int>",
        "assert saved[0] == 7",
        "assert nested[\"list\"].count == 0",
        "nested[\"absent\"].append 8",
        "assert nested[\"absent\"].count == 0",
        "assert nested.count == 1",
        "maps = new Map<string Map<string int>>",
        "maps[\"inner\"] = scores",
        "maps[\"inner\"][\"hero\"] = 200",
        "assert scores[\"hero\"] == 200",
        "maps[\"missing\"][\"ignored\"] = 1",
        "assert maps[\"missing\"].count == 0",
        "assert maps.count == 1",
        "holder = new List<Map<string string>>",
        "holder.append words",
        "words = new Map<string string>",
        "assert holder[0][\"name\"] == \"updated\""
    };
    NomadString script;
    for (const auto& source : sources) {
        script += source + "\n";
    }
    script += "return holder[0][\"name\"]";
    auto compiler = runtime.createCompiler();
    CompilerContext compilation(compiler.get());
    compiler->registerSources(&compilation, {{{"main", {"main.nomad", script}}}, {}});
    BOOST_REQUIRE_MESSAGE(compiler->compileScripts(&compilation, CompilationTarget::Executable),
        (compilation.getFirstError() ? formatDiagnostic(*compilation.getFirstError()) : "Compilation failed"));
    RuntimeValue empty;
    runtime.getStringType()->initValue(empty);
    runtime.getStringType()->freeValue(empty);
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(result.getStringObject()->getString() == "updated");
    runtime.getStringType()->freeValue(result);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    {
        VirtualMachine machine(&runtime);
        InterpreterContext context(machine);
        for (const auto& source : sources) {
            BOOST_TEST_CONTEXT(source) {
                const auto isAssertion = source.starts_with("assert ");
                BOOST_REQUIRE_MESSAGE(Interpreter{}.execute(isAssertion ? source.substr(7) : source, context),
                    (context.getError() ? *context.getError() : source));
                if (isAssertion) {
                    BOOST_TEST(context.getResult()->getValue().getBooleanValue());
                }
            }
        }
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    struct LogCapture {
        LogSinkId sinkId = log::createMemorySink();
        MemorySink& sink = *static_cast<MemorySink*>(log::getSink(sinkId));
        LogCapture() {
            log::flush();
            sink.clear();
        }
        ~LogCapture() { log::removeSink(sinkId); }
    } capture;
    MapObject map(runtime.getIntegerType());
    map.getValue("absent", result);
    log::flush();
    BOOST_TEST(result.getIntegerValue() == 0);
    BOOST_TEST(map.getCount() == 0u);
    BOOST_REQUIRE(capture.sink.getEntries().size() == 1u);
    BOOST_CHECK(capture.sink.getEntries()[0].level == LogLevel::Error);
    std::ostringstream documentation;
    generateNativeMemberDocumentation(&runtime, documentation);
    BOOST_TEST(documentation.str().find("Map<string int>[index]") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(collections_maps_contains_and_key_snapshots_do_not_change_the_map) {
    TestDirectory directory("nomad_host_map_contains");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    const std::vector<NomadString> sources = {
        "scores = new Map<string int>",
        "emptyKeys = scores.keys",
        "assert emptyKeys.count == 0",
        "emptyValues = scores.values",
        "emptyEntries = scores.entries",
        "assert emptyValues.count == 0",
        "assert emptyEntries.count == 0",
        "present = scores.contains \"hero\"",
        "assert present == false",
        "assert scores.count == 0",
        "scores[\"hero\"] = 0",
        "scores[\"\"] = 1",
        "present = scores.contains \"hero\"",
        "assert present == true",
        "present = scores.contains \"Hero\"",
        "assert present == false",
        "present = scores.contains \"\"",
        "assert present == true",
        "alias = scores",
        "present = alias.contains \"hero\"",
        "assert present == true",
        "scores[\"hero\"] = 2",
        "present = scores.contains \"hero\"",
        "assert present == true",
        "present = scores.contains \"missing\"",
        "assert present == false",
        "assert scores.count == 2",
        "words = new Map<string string>",
        "words[\"empty\"] = \"\"",
        "present = words.contains \"empty\"",
        "assert present == true",
        "present = words.contains \"missing\"",
        "assert present == false",
        "keys = scores.keys",
        "otherKeys = scores.keys",
        "scoreValues = scores.values",
        "scoreEntries = scores.entries",
        "otherEntries = scores.entries",
        "assert scoreValues.count == 2",
        "assert scoreValues[0] + scoreValues[1] == 3",
        "assert scoreEntries.count == 2",
        "assert scoreEntries[0].key != scoreEntries[1].key",
        "assert scoreEntries[0].value == scores[scoreEntries[0].key]",
        "assert scoreEntries[1].value == scores[scoreEntries[1].key]",
        "savedEntry = scoreEntries[0]",
        "scoreValues[0] = 999",
        "scoreEntries[0] = new KeyValue \"snapshot\" 999",
        "assert scores.count == 2",
        "present = scores.contains \"snapshot\"",
        "assert present == false",
        "assert otherEntries[0].key == savedEntry.key",
        "assert otherEntries[0].value == savedEntry.value",
        "assert keys.count == 2",
        "assert keys[0] != keys[1]",
        "present = scores.contains keys[0]",
        "assert present == true",
        "present = scores.contains keys[1]",
        "assert present == true",
        "originalKey = keys[0]",
        "keys[0] = \"changed snapshot\"",
        "present = scores.contains \"changed snapshot\"",
        "assert present == false",
        "present = scores.contains originalKey",
        "assert present == true",
        "assert otherKeys[0] != \"changed snapshot\"",
        "scores[\"later\"] = 3",
        "assert keys.count == 2",
        "assert otherKeys.count == 2",
        "assert scoreValues.count == 2",
        "assert otherEntries.count == 2",
        "assert emptyValues.count == 0",
        "assert emptyEntries.count == 0",
        "assert emptyKeys.count == 0",
        "removed = scores.remove originalKey",
        "assert removed == true",
        "assert otherKeys.count == 2",
        "alias.clear",
        "assert scores.count == 0",
        "assert otherEntries[0].value + otherEntries[1].value == 3",
        "assert savedEntry.value == 1 || savedEntry.value == 2",
        "assert scoreValues[0] == 999",
        "assert otherKeys[0] != otherKeys[1]",
        "assert otherKeys[0] == \"hero\" || otherKeys[0] == \"\"",
        "assert otherKeys[1] == \"hero\" || otherKeys[1] == \"\"",
        "scores = new Map<string int>",
        "assert otherKeys.count == 2",
        "words.clear",
        "words[\"Hero\"] = \"upper\"",
        "words[\"hero\"] = \"lower\"",
        "wordKeys = words.keys",
        "wordValues = words.values",
        "wordEntries = words.entries",
        "words = new Map<string string>",
        "assert wordValues[0] == \"upper\" || wordValues[0] == \"lower\"",
        "assert wordValues[1] == \"upper\" || wordValues[1] == \"lower\"",
        "assert wordValues[0] != wordValues[1]",
        "assert wordEntries[0].key != wordEntries[1].key",
        "assert wordEntries[0].value != wordEntries[1].value",
        "assert wordKeys.count == 2",
        "assert wordKeys[0] != wordKeys[1]",
        "assert wordKeys[0] == \"Hero\" || wordKeys[0] == \"hero\"",
        "assert wordKeys[1] == \"Hero\" || wordKeys[1] == \"hero\"",
        "lists = new Map<string List<int>>",
        "numbers = new List<int>",
        "numbers.append 7",
        "lists[\"numbers\"] = numbers",
        "listValues = lists.values",
        "listEntries = lists.entries",
        "listValues[0].append 8",
        "listEntries[0].value.append 9",
        "assert lists[\"numbers\"].count == 3",
        "assert numbers[2] == 9",
        "lists[\"numbers\"] = new List<int>",
        "assert lists[\"numbers\"].count == 0",
        "assert listEntries[0].value.count == 3",
        "lists.clear",
        "lists = new Map<string List<int>>",
        "assert listValues[0][0] == 7",
        "assert listEntries[0].key == \"numbers\"",
        "assert listEntries[0].value[1] == 8"
    };
    NomadString script;
    for (const auto& source : sources) {
        script += source + "\n";
    }
    auto compiler = runtime.createCompiler();
    CompilerContext compilation(compiler.get());
    compiler->registerSources(&compilation, {{{"main", {"main.nomad", script}}}, {}});
    BOOST_REQUIRE_MESSAGE(compiler->compileScripts(&compilation, CompilationTarget::Executable),
        (compilation.getFirstError() ? formatDiagnostic(*compilation.getFirstError()) : "Compilation failed"));
    RuntimeValue empty;
    runtime.getStringType()->initValue(empty);
    runtime.getStringType()->freeValue(empty);
    const auto before = NomadObject::getLiveObjectCount();
    struct LogCapture {
        LogSinkId sinkId = log::createMemorySink();
        MemorySink& sink = *static_cast<MemorySink*>(log::getSink(sinkId));
        LogCapture() {
            log::flush();
            sink.clear();
        }
        ~LogCapture() { log::removeSink(sinkId); }
    } capture;
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    {
        VirtualMachine machine(&runtime);
        InterpreterContext context(machine);
        for (const auto& source : sources) {
            BOOST_TEST_CONTEXT(source) {
                const auto isAssertion = source.starts_with("assert ");
                BOOST_REQUIRE_MESSAGE(Interpreter{}.execute(isAssertion ? source.substr(7) : source, context),
                    (context.getError() ? *context.getError() : source));
                if (isAssertion) {
                    BOOST_TEST(context.getResult()->getValue().getBooleanValue());
                }
            }
        }
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    log::flush();
    BOOST_TEST(capture.sink.getEntries().empty());
    std::ostringstream documentation;
    generateNativeMemberDocumentation(&runtime, documentation);
    BOOST_TEST(documentation.str().find("#### `contains` (*method*)") != NomadString::npos);
    BOOST_TEST(documentation.str().find("- `key:string`") != NomadString::npos);
    BOOST_TEST(documentation.str().find("`returns bool`") != NomadString::npos);
    BOOST_TEST(documentation.str().find("#### `keys` (*method*)") != NomadString::npos);
    BOOST_TEST(documentation.str().find("`returns Array1<string>`") != NomadString::npos);
    BOOST_TEST(documentation.str().find("#### `values` (*method*)") != NomadString::npos);
    BOOST_TEST(documentation.str().find("`returns Array1<int>`") != NomadString::npos);
    BOOST_TEST(documentation.str().find("#### `entries` (*method*)") != NomadString::npos);
    BOOST_TEST(documentation.str().find("`returns Array1<KeyValue<string int>>`") != NomadString::npos);
    const auto* mapType = runtime.getParameterizedType("Map", {runtime.getStringType(), runtime.getIntegerType()});
    for (const auto* name : {"keys", "values", "entries"}) {
        const auto memberId = runtime.getNativeMemberId(mapType, name);
        BOOST_REQUIRE((runtime.getMemberType(memberId) == MemberType::Method));
        const auto* method = runtime.getNativeMemberMethod(memberId);
        BOOST_REQUIRE(method != nullptr);
        BOOST_TEST(method->getParameterCount() == 0u);
    }
}

BOOST_AUTO_TEST_CASE(collections_maps_remove_and_clear_without_logging_and_preserve_saved_objects) {
    TestDirectory directory("nomad_host_map_remove");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    const std::vector<NomadString> sources = {
        "scores = new Map<string int>",
        "scores.clear",
        "scores.clear",
        "removed = scores.remove \"missing\"",
        "assert removed == false",
        "assert scores.count == 0",
        "scores[\"hero\"] = 0",
        "scores[\"Hero\"] = 2",
        "scores[\"\"] = 3",
        "alias = scores",
        "removed = alias.remove \"hero\"",
        "assert removed == true",
        "assert scores.count == 2",
        "present = scores.contains \"hero\"",
        "assert present == false",
        "assert scores[\"Hero\"] == 2",
        "removed = scores.remove \"hero\"",
        "assert removed == false",
        "removed = scores.remove \"\"",
        "assert removed == true",
        "assert scores.count == 1",
        "scores[\"hero\"] = 7",
        "assert scores[\"hero\"] == 7",
        "assert scores.count == 2",
        "words = new Map<string string>",
        "alias.clear",
        "assert scores.count == 0",
        "present = scores.contains \"hero\"",
        "assert present == false",
        "present = scores.contains \"Hero\"",
        "assert present == false",
        "scores[\"hero\"] = 8",
        "assert alias[\"hero\"] == 8",
        "assert alias.count == 1",
        "words[\"name\"] = \"hello\"",
        "savedWord = words[\"name\"]",
        "removed = words.remove \"name\"",
        "assert removed == true",
        "assert savedWord == \"hello\"",
        "assert words.count == 0",
        "nested = new Map<string List<int>>",
        "nested[\"saved\"] = new List<int>",
        "nested[\"saved\"].append 9",
        "saved = nested[\"saved\"]",
        "removed = nested.remove \"saved\"",
        "assert removed == true",
        "assert saved[0] == 9",
        "nested[\"discarded\"] = new List<int>",
        "removed = nested.remove \"discarded\"",
        "assert removed == true",
        "assert nested.count == 0",
        "nested[\"saved\"] = saved",
        "nested[\"discarded\"] = new List<int>",
        "nested.clear",
        "assert nested.count == 0",
        "assert saved[0] == 9",
        "words[\"name\"] = savedWord",
        "words[\"other\"] = \"discarded\"",
        "words.clear",
        "words.clear",
        "assert words.count == 0",
        "assert savedWord == \"hello\"",
        "words[\"new\"] = \"again\"",
        "assert words[\"new\"] == \"again\""
    };
    NomadString script;
    for (const auto& source : sources) {
        script += source + "\n";
    }
    auto compiler = runtime.createCompiler();
    CompilerContext compilation(compiler.get());
    compiler->registerSources(&compilation, {{{"main", {"main.nomad", script}}}, {}});
    BOOST_REQUIRE_MESSAGE(compiler->compileScripts(&compilation, CompilationTarget::Executable),
        (compilation.getFirstError() ? formatDiagnostic(*compilation.getFirstError()) : "Compilation failed"));
    RuntimeValue empty;
    runtime.getStringType()->initValue(empty);
    runtime.getStringType()->freeValue(empty);
    const auto before = NomadObject::getLiveObjectCount();
    struct LogCapture {
        LogSinkId sinkId = log::createMemorySink();
        MemorySink& sink = *static_cast<MemorySink*>(log::getSink(sinkId));
        LogCapture() {
            log::flush();
            sink.clear();
        }
        ~LogCapture() { log::removeSink(sinkId); }
    } capture;
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    {
        VirtualMachine machine(&runtime);
        InterpreterContext context(machine);
        for (const auto& source : sources) {
            BOOST_TEST_CONTEXT(source) {
                const auto isAssertion = source.starts_with("assert ");
                BOOST_REQUIRE_MESSAGE(Interpreter{}.execute(isAssertion ? source.substr(7) : source, context),
                    (context.getError() ? *context.getError() : source));
                if (isAssertion) {
                    BOOST_TEST(context.getResult()->getValue().getBooleanValue());
                }
            }
        }
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    log::flush();
    BOOST_TEST(capture.sink.getEntries().empty());
    const auto* listType = runtime.getParameterizedType("List", {runtime.getIntegerType()});
    {
        MapObject map(listType);
        RuntimeValue value;
        listType->initValue(value);
        map.setValue("list", value);
        BOOST_TEST(value.getObjectValue()->getReferenceCount() == 2u);
        BOOST_TEST(map.remove("list"));
        BOOST_TEST(value.getObjectValue()->getReferenceCount() == 1u);
        map.setValue("list", value);
        listType->freeValue(value);
        const auto storedObjects = NomadObject::getLiveObjectCount();
        BOOST_TEST(map.remove("list"));
        BOOST_TEST(NomadObject::getLiveObjectCount() == storedObjects - 1);
        listType->initValue(value);
        map.setValue("first", value);
        map.setValue("second", value);
        BOOST_TEST(value.getObjectValue()->getReferenceCount() == 3u);
        map.clear();
        BOOST_TEST(map.getCount() == 0u);
        BOOST_TEST(value.getObjectValue()->getReferenceCount() == 1u);
        map.setValue("discarded", value);
        listType->freeValue(value);
        const auto beforeClear = NomadObject::getLiveObjectCount();
        map.clear();
        BOOST_TEST(NomadObject::getLiveObjectCount() == beforeClear - 1);
        map.clear();
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    std::ostringstream documentation;
    generateNativeMemberDocumentation(&runtime, documentation);
    BOOST_TEST(documentation.str().find("#### `remove` (*method*)") != NomadString::npos);
    BOOST_TEST(documentation.str().find("#### `clear` (*method*)") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(collections_maps_release_partial_copies_and_preserve_entries_on_failure) {
    Runtime runtime;
    class MapValueType final : public ClassType {
    public:
        explicit MapValueType(Runtime* runtime, TypeId id): ClassType(runtime, id, "MapValue", ClassMode::Closed) {}

        [[nodiscard]] NomadString getTypeName() const override { return "MapValue"; }
        void toString(const RuntimeValue&, NomadString& text) const override { text = getTypeName(); }
        void copyValue(const RuntimeValue& source, RuntimeValue& destination) const override {
            ClassType::copyValue(source, destination);
            if (failCopy) {
                throw NomadException("Map value copy failed");
            }
        }
        bool failCopy = false;
    } type(&runtime, TypeId::INVALID);
    const auto before = NomadObject::getLiveObjectCount();
    {
        MapObject map(&type);
        RuntimeValue first;
        RuntimeValue second;
        first.setObjectValue(new NomadObject);
        second.setObjectValue(new NomadObject);
        type.failCopy = true;
        BOOST_CHECK_THROW(map.setValue("new", first), NomadException);
        BOOST_TEST(map.getCount() == 0u);
        BOOST_TEST(first.getObjectValue()->getReferenceCount() == 1u);
        type.failCopy = false;
        map.setValue("stored", first);
        type.failCopy = true;
        BOOST_CHECK_THROW(map.setValue("stored", second), NomadException);
        BOOST_TEST(map.getCount() == 1u);
        BOOST_TEST(first.getObjectValue()->getReferenceCount() == 2u);
        BOOST_TEST(second.getObjectValue()->getReferenceCount() == 1u);
        RuntimeValue result;
        BOOST_CHECK_THROW(map.getValue("stored", result), NomadException);
        type.freeValue(result);
        BOOST_TEST(first.getObjectValue()->getReferenceCount() == 2u);
        type.failCopy = false;
        map.getValue("stored", result);
        BOOST_TEST(result.getObjectValue() == first.getObjectValue());
        type.freeValue(first);
        type.freeValue(second);
        BOOST_TEST(result.getObjectValue()->getReferenceCount() == 2u);
        type.freeValue(result);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(collections_map_snapshots_own_references_and_release_partial_results) {
    Runtime runtime;
    class SnapshotValueType final : public ClassType {
    public:
        explicit SnapshotValueType(Runtime* runtime, TypeId id): ClassType(runtime, id, "SnapshotValue", ClassMode::Closed) {}
        void initValue(RuntimeValue& value) const override {
            if (failInitialization) {
                value.setObjectValue(new NomadObject);
                throw NomadException("Snapshot value initialization failed");
            }
            ClassType::initValue(value);
        }
        void copyValue(const RuntimeValue& source, RuntimeValue& destination) const override {
            ClassType::copyValue(source, destination);
            if (copiesUntilFailure == 0) {
                throw NomadException("Snapshot value copy failed");
            }
            if (copiesUntilFailure > 0) {
                --copiesUntilFailure;
            }
        }
        mutable NomadInteger copiesUntilFailure = -1;
        bool failInitialization = false;
    } valueType(&runtime, TypeId::INVALID);
    StringType keyType(&runtime, TypeId::INVALID);
    KeyValueType entryType(&runtime, TypeId::INVALID, "KeyValue<string SnapshotValue>", ClassMode::Closed, &keyType, &valueType);
    RuntimeValue empty;
    keyType.initValue(empty);
    keyType.freeValue(empty);
    const auto before = NomadObject::getLiveObjectCount();
    {
        MapObject map(&valueType);
        RuntimeValue value;
        value.setObjectValue(new NomadObject);
        map.setValue("first", value);
        map.setValue("second", value);
        BOOST_TEST(value.getObjectValue()->getReferenceCount() == 3u);
        const auto storedObjects = NomadObject::getLiveObjectCount();
        valueType.failInitialization = true;
        BOOST_CHECK_THROW(static_cast<void>(map.getValues()), NomadException);
        BOOST_TEST(NomadObject::getLiveObjectCount() == storedObjects);
        BOOST_CHECK_THROW(static_cast<void>(map.getEntries(&entryType)), NomadException);
        BOOST_TEST(NomadObject::getLiveObjectCount() == storedObjects);
        valueType.failInitialization = false;
        for (const auto successfulCopies : {0, 1}) {
            valueType.copiesUntilFailure = successfulCopies;
            BOOST_CHECK_THROW(static_cast<void>(map.getValues()), NomadException);
            BOOST_TEST(value.getObjectValue()->getReferenceCount() == 3u);
            BOOST_TEST(NomadObject::getLiveObjectCount() == storedObjects);
            valueType.copiesUntilFailure = successfulCopies;
            BOOST_CHECK_THROW(static_cast<void>(map.getEntries(&entryType)), NomadException);
            BOOST_TEST(value.getObjectValue()->getReferenceCount() == 3u);
            BOOST_TEST(NomadObject::getLiveObjectCount() == storedObjects);
            BOOST_TEST(map.getCount() == 2u);
            BOOST_TEST(map.contains("first"));
            BOOST_TEST(map.contains("second"));
        }
        valueType.copiesUntilFailure = -1;
        {
            auto values = map.getValues();
            auto entries = map.getEntries(&entryType);
            auto otherEntries = map.getEntries(&entryType);
            BOOST_TEST(values->getCount() == 2u);
            BOOST_TEST(entries->getCount() == 2u);
            BOOST_TEST(otherEntries->getCount() == 2u);
            BOOST_TEST(value.getObjectValue()->getReferenceCount() == 9u);
            RuntimeValue firstEntry;
            RuntimeValue otherEntry;
            entries->getValue(0, firstEntry);
            otherEntries->getValue(0, otherEntry);
            BOOST_TEST(firstEntry.getObjectValue() != otherEntry.getObjectValue());
            BOOST_TEST(firstEntry.asObject<KeyValueObject>()->getValue().getObjectValue() == value.getObjectValue());
            BOOST_TEST(otherEntry.asObject<KeyValueObject>()->getValue().getObjectValue() == value.getObjectValue());
            map.clear();
            BOOST_TEST(value.getObjectValue()->getReferenceCount() == 7u);
            entryType.freeValue(firstEntry);
            entryType.freeValue(otherEntry);
        }
        BOOST_TEST(value.getObjectValue()->getReferenceCount() == 1u);
        valueType.freeValue(value);
        BOOST_TEST(map.getValues()->getCount() == 0u);
        BOOST_TEST(map.getEntries(&entryType)->getCount() == 0u);
        BOOST_CHECK_THROW(static_cast<void>(map.getEntries(nullptr)), NomadException);
        IntegerType integerType(&runtime, TypeId::INVALID);
        KeyValueType wrongKeyType(&runtime, TypeId::INVALID, "KeyValue<int SnapshotValue>", ClassMode::Closed, &integerType, &valueType);
        KeyValueType wrongValueType(&runtime, TypeId::INVALID, "KeyValue<string int>", ClassMode::Closed, &keyType, &integerType);
        BOOST_CHECK_THROW(static_cast<void>(map.getEntries(&wrongKeyType)), NomadException);
        BOOST_CHECK_THROW(static_cast<void>(map.getEntries(&wrongValueType)), NomadException);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(collections_maps_reject_unsupported_operations_and_types) {
    TestDirectory directory("nomad_host_map_validation");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    BOOST_CHECK_THROW(MapObject(nullptr), NomadException);
    BOOST_CHECK_THROW(MapObject(runtime.getVoidType()), NomadException);
    BOOST_CHECK_THROW(MapType(&runtime, TypeId::INVALID, "Invalid", ClassMode::Closed, runtime.getIntegerType(), runtime.getIntegerType()), NomadException);
    BOOST_CHECK_THROW(MapType(&runtime, TypeId::INVALID, "Invalid", ClassMode::Closed, runtime.getStringType(), runtime.getFileNameType()), NomadException);
    VirtualMachine machine(&runtime);
    InterpreterContext context(machine);
    for (const auto* source : {
        "values = new Map<int string>",
        "values = new Map<string>",
        "values = new Map<string int int>",
        "values = new Map<string void>",
        "values = new Map<string int> 1",
        "values = new Map<string int>\nvalues.count = 1",
        "values = new Map<string int>\nvalues[1]",
        "values = new Map<string int>\nvalues[\"key\"] = \"wrong\"",
        "values = new Map<string int>\nvalues[\"a\" \"b\"]",
        "values = new Map<string int>\nvalues.contains 1",
        "values = new Map<string int>\nvalues.contains",
        "values = new Map<string int>\nvalues.contains \"a\" \"b\"",
        "values = new Map<string int>\nvalues.remove 1",
        "values = new Map<string int>\nvalues.remove",
        "values = new Map<string int>\nvalues.remove \"a\" \"b\"",
        "values = new Map<string int>\nvalues.clear 1",
        "values = new Map<string int>\nresult = values.clear",
        "values = new Map<string int>\nvalues.keys = new Array<string> 1",
        "values = new Map<string int>\nvalues.keys[0] = 1",
        "values = new Map<string int>\nvalues.keys 1",
        "values = new Map<string int>\nvalues.values 1",
        "values = new Map<string int>\nvalues.entries 1",
        "values = new Map<string int>\nvalues.values = new Array<int> 1",
        "values = new Map<string int>\nvalues.entries = new Array<KeyValue<string int>> 1",
        "values = new Map<string int>\nvalues.values[0] = \"wrong\"",
        "values = new Map<string int>\nvalues.entries[0] = new KeyValue \"wrong\" true",
        "values = new Map<string int>\nvalues.entries[0].key = \"wrong\"",
        "values = new Map<string int>\nvalues.entries[0].value = 2"
    }) {
        BOOST_TEST_CONTEXT(source) {
            auto compiler = runtime.createCompiler();
            CompilerContext compilation(compiler.get());
            compiler->registerSources(&compilation, {{{"rejected", {"rejected.nomad", source}}}, {}});
            BOOST_TEST(!compiler->compileScripts(&compilation, CompilationTarget::Executable));
            BOOST_REQUIRE(compilation.getFirstError() != nullptr);
            const NomadString input(source);
            const auto lineBreak = input.find('\n');
            if (lineBreak != NomadString::npos) {
                BOOST_REQUIRE(Interpreter{}.execute(input.substr(0, lineBreak), context));
            }
            BOOST_TEST(!Interpreter{}.execute(
                lineBreak == NomadString::npos ? input : input.substr(lineBreak + 1), context
            ));
            BOOST_TEST(context.hasError());
            BOOST_REQUIRE(Interpreter{}.execute("recovery = new Map<string int>", context));
        }
    }
}

BOOST_AUTO_TEST_CASE(collections_lists_store_typed_values_and_share_mutations) {
    TestDirectory directory("nomad_host_list_values");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"main", {"main.nomad",
        "fun append values:List<int>\n"
        "    values.append 30\n"
        "end\n"
        "values = new List<int>\n"
        "assert values.count == 0\n"
        "values.append 10\n"
        "alias = values\n"
        "alias.append 20\n"
        "append values\n"
        "assert alias.count == 3\n"
        "words = new List<string>\n"
        "words.append \"hello\"\n"
        "words.append \"world\".toUpper\n"
        "assert words.count == 2\n"
        "nested = new List<List<string>>\n"
        "nested.append words\n"
        "nested.append new List<string>\n"
        "assert nested.count == 2\n"
        "return values.count"
    }}}, {}});
    const auto compiled = compiler->compileScripts(&context, CompilationTarget::Executable);
    BOOST_REQUIRE_MESSAGE(compiled,
        (context.getFirstError() ? formatDiagnostic(*context.getFirstError()) : "Compilation failed"));
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(result.getIntegerValue() == 3);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);

    {
        VirtualMachine machine(&runtime);
        InterpreterContext interpreterContext(machine);
        for (const auto* source : {
            "values = new List<string>", "values.append \"hello\"", "alias = values",
            "alias.append \"world\".toUpper", "values.count"
        }) {
            BOOST_REQUIRE(Interpreter{}.execute(source, interpreterContext));
        }
        BOOST_TEST(interpreterContext.getResult()->getValue().getIntegerValue() == 2);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(collections_lists_reject_invalid_member_operations) {
    for (const auto* source : {
        "values = new List<int>\nvalues.append \"wrong\"",
        "values = new List<string>\nvalues.append 1",
        "values = new List<List<int>>\nvalues.append new List<string>",
        "values = new List<int>\nvalues.count = 10",
        "values = new List<int>\nvalues.append",
        "values = new List<int>\nvalues[0] = \"wrong\"",
        "values = new List<int>\nreturn values[\"key\"]",
        "values = new List<int>\nreturn values[0 1]",
        "values = new List<int>\nreturn values.prepend 1",
        "values = new List<int>\nvalues.push 1",
        "values = new List<int>\nvalues.pop",
        "values = new List<int>\nvalues.prepend \"wrong\"",
        "values = new List<int>\nvalues.insert 0 \"wrong\"",
        "values = new List<int>\nvalues.insert \"zero\" 1",
        "values = new List<int>\nvalues.insert 0",
        "values = new List<int>\nvalues.remove \"zero\"",
        "values = new List<int>\nvalues.remove",
        "values = new List<int>\nvalues.removeFirst 1",
        "values = new List<int>\nvalues.removeLast 1",
        "values = new List<int>\nvalues.clear 1",
        "values = new List<int>\nresult = values.insert 0 1",
        "values = new List<int>\nresult = values.clear",
        "params values:List<int>\nvalues = new List<string>"
    }) {
        BOOST_TEST_CONTEXT(source) {
            TestDirectory directory("nomad_host_bad_list_values");
            const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
            auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
            auto compiler = host->getRuntime()->createCompiler();
            CompilerContext context(compiler.get());
            compiler->registerSources(&context, {{{"main", {"main.nomad", source}}}, {}});
            BOOST_TEST(!compiler->compileScripts(&context, CompilationTarget::Analysis));
            BOOST_TEST(context.hasError());
            const NomadString input(source);
            if (!input.starts_with("params ")) {
                VirtualMachine machine(host->getRuntime());
                InterpreterContext evaluation(machine);
                const auto lineBreak = input.find('\n');
                BOOST_REQUIRE(Interpreter{}.execute(input.substr(0, lineBreak), evaluation));
                BOOST_TEST(!Interpreter{}.execute(input.substr(lineBreak + 1), evaluation));
                BOOST_TEST(evaluation.hasError());
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(collections_lists_mutate_order_return_indices_and_release_values) {
    TestDirectory directory("nomad_host_list_mutation");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    const std::vector<NomadString> sources = {
        "items = new List<int>",
        "removed = items.removeFirst",
        "assert removed == false",
        "removed = items.removeLast",
        "assert removed == false",
        "removed = items.remove 0",
        "assert removed == false",
        "removed = items.remove (-1)",
        "assert removed == false",
        "items.clear",
        "items.insert 0 20",
        "index = items.append 40",
        "assert index == 1",
        "items.prepend 10",
        "items.insert 2 30",
        "items.insert 4 50",
        "assert items.count == 5",
        "assert items[0] == 10",
        "assert items[1] == 20",
        "assert items[2] == 30",
        "assert items[3] == 40",
        "assert items[4] == 50",
        "alias = items",
        "removed = alias.remove 2",
        "assert removed == true",
        "assert items.count == 4",
        "assert items[2] == 40",
        "removed = items.removeFirst",
        "assert removed == true",
        "assert items[0] == 20",
        "removed = items.removeLast",
        "assert removed == true",
        "assert items.count == 2",
        "assert items[1] == 40",
        "removed = items.remove 2",
        "assert removed == false",
        "removed = items.remove 9223372036854775807",
        "assert removed == false",
        "assert items.count == 2",
        "alias.clear",
        "assert items.count == 0",
        "items.clear",
        "index = items.append 9",
        "assert index == 0",
        "removed = items.removeLast",
        "assert removed == true",
        "items.prepend 8",
        "removed = items.removeFirst",
        "assert removed == true",
        "items.insert 0 7",
        "removed = items.remove 0",
        "assert removed == true",
        "assert items.count == 0",
        "words = new List<string>",
        "index = words.append \"hello\"",
        "assert index == 0",
        "words.insert 0 words[0]",
        "words.prepend words[1]",
        "assert words.count == 3",
        "assert words[0] == \"hello\"",
        "savedWord = words[1]",
        "removed = words.remove 1",
        "assert removed == true",
        "words.clear",
        "assert savedWord == \"hello\"",
        "nested = new List<List<int>>",
        "nested.append new List<int>",
        "nested[0].append 42",
        "saved = nested[0]",
        "nested.insert 0 nested[0]",
        "nested.prepend new List<int>",
        "removed = nested.removeLast",
        "assert removed == true",
        "assert nested[1][0] == 42",
        "nested.clear",
        "assert saved[0] == 42"
    };
    NomadString script;
    for (const auto& source : sources) {
        script += source + "\n";
    }
    auto compiler = runtime.createCompiler();
    CompilerContext compilation(compiler.get());
    compiler->registerSources(&compilation, {{{"main", {"main.nomad", script}}}, {}});
    BOOST_REQUIRE_MESSAGE(compiler->compileScripts(&compilation, CompilationTarget::Executable),
        (compilation.getFirstError() ? formatDiagnostic(*compilation.getFirstError()) : "Compilation failed"));
    RuntimeValue empty;
    runtime.getStringType()->initValue(empty);
    runtime.getStringType()->freeValue(empty);
    const auto before = NomadObject::getLiveObjectCount();
    struct LogCapture {
        LogSinkId sinkId = log::createMemorySink();
        MemorySink& sink = *static_cast<MemorySink*>(log::getSink(sinkId));
        LogCapture() {
            log::flush();
            sink.clear();
        }
        ~LogCapture() { log::removeSink(sinkId); }
    } capture;
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    {
        VirtualMachine machine(&runtime);
        InterpreterContext context(machine);
        for (const auto& source : sources) {
            BOOST_TEST_CONTEXT(source) {
                const auto isAssertion = source.starts_with("assert ");
                BOOST_REQUIRE_MESSAGE(Interpreter{}.execute(isAssertion ? source.substr(7) : source, context),
                    (context.getError() ? *context.getError() : source));
                if (isAssertion) {
                    BOOST_TEST(context.getResult()->getValue().getBooleanValue());
                }
            }
        }
        log::flush();
        BOOST_TEST(capture.sink.getEntries().empty());
        for (const auto* source : {"items.insert (-1) 1", "items.insert 1 1"}) {
            BOOST_TEST(!Interpreter{}.execute(source, context));
            BOOST_REQUIRE(context.hasError());
            BOOST_REQUIRE(Interpreter{}.evaluateExpression("items.count", context));
            BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 0);
        }
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    log::flush();
    std::ostringstream documentation;
    generateNativeMemberDocumentation(&runtime, documentation);
    for (const auto* name : {"append", "prepend", "insert", "remove", "removeFirst", "removeLast", "clear"}) {
        BOOST_TEST(documentation.str().find("#### `" + NomadString(name) + "` (*method*)") != NomadString::npos);
    }
}

BOOST_AUTO_TEST_CASE(collections_list_insertion_bounds_fault_without_changing_list) {
    for (const auto* index : {"(-1)", "1", "9223372036854775807"}) {
        BOOST_TEST_CONTEXT(index) {
            TestDirectory directory("nomad_host_list_insertion_bounds");
            const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
            auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
            auto& runtime = *host->getRuntime();
            auto compiler = runtime.createCompiler();
            CompilerContext compilation(compiler.get());
            const auto source = "params items:List<int>\nitems.insert " + NomadString(index) + " 42";
            compiler->registerSources(&compilation, {{{"main", {"main.nomad", source}}}, {}});
            BOOST_REQUIRE_MESSAGE(compiler->compileScripts(&compilation, CompilationTarget::Executable),
                (compilation.getFirstError() ? formatDiagnostic(*compilation.getFirstError()) : "Compilation failed"));
            const auto* listType = runtime.getParameterizedType("List", {runtime.getIntegerType()});
            RuntimeValue value;
            listType->initValue(value);
            const auto before = NomadObject::getLiveObjectCount();
            auto machine = runtime.createVirtualMachine();
            BOOST_CHECK_THROW(machine->run(runtime.getScriptFunction(runtime.getFunctionId("main")), {value}), NomadException);
            BOOST_TEST(value.asObject<ListObject>()->getCount() == 0u);
            BOOST_TEST(NomadObject::getLiveObjectCount() == before);
            listType->freeValue(value);
        }
    }
}

BOOST_AUTO_TEST_CASE(collections_indexers_read_replace_chain_and_preserve_evaluation_order) {
    TestDirectory directory("nomad_host_indexers");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"main", {"main.nomad",
        "fun receiver values:List<int>\n"
        "    values.append 1\n"
        "    return values\n"
        "end\n"
        "fun index values:List<int>\n"
        "    values.append 2\n"
        "    return 0\n"
        "end\n"
        "fun assigned values:List<int>\n"
        "    values.append 3\n"
        "    return 9\n"
        "end\n"
        "fun replace values:List<string>\n"
        "    values[0] = \"changed\"\n"
        "end\n"
        "values = new List<int>\n"
        "values.append 0\n"
        "(receiver values)[index values] = assigned values\n"
        "assert values.count == 4\n"
        "assert values[0] == 9\n"
        "assert values[1] == 1\n"
        "assert values[2] == 3\n"
        "assert values[3] == 2\n"
        "words = new List<string>\n"
        "words.append \"hello\"\n"
        "words[0] = words[0]\n"
        "assert words[0] == \"hello\"\n"
        "replace words\n"
        "assert words[0].length == 7\n"
        "assert words[0][0] == \"c\"\n"
        "nested = new List<List<string>>\n"
        "nested.append words\n"
        "nested[0][0] = \"updated\"\n"
        "assert words[0] == \"updated\"\n"
        "nested[0] = new List<string>\n"
        "nested[0].append \"new\"\n"
        "assert nested[0][0] == \"new\"\n"
        "assert words[0] == \"updated\"\n"
        "return $\"{words[0]}:{values[0]}\""
    }}}, {}});
    const auto compiled = compiler->compileScripts(&context, CompilationTarget::Executable);
    BOOST_REQUIRE_MESSAGE(compiled,
        (context.getFirstError() ? formatDiagnostic(*context.getFirstError()) : "Compilation failed"));
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(result.getStringObject()->getString() == "updated:9");
    runtime.getStringType()->freeValue(result);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(array1_indexers_read_replace_and_preserve_references) {
    TestDirectory directory("nomad_host_array1_indexers");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"main", {"main.nomad",
        "fun replace values:Array1<string>\n"
        "    values[0] = \"changed\"\n"
        "end\n"
        "values = new Array<int> 2\n"
        "assert values[0] == 0\n"
        "values[1] = 123\n"
        "alias = values\n"
        "alias[0] = values[1]\n"
        "assert values[0] == 123\n"
        "assert values.count == 2\n"
        "assert values[-1] == 0\n"
        "assert values[2] == 0\n"
        "values[-1] = 9\n"
        "values[2] = 9\n"
        "assert values[0] == 123\n"
        "assert values[1] == 123\n"
        "words = new Array1<string> 1\n"
        "words[0] = \"hello\"\n"
        "assert words[-1] == \"\"\n"
        "words[1] = \"ignored\"\n"
        "assert words[0] == \"hello\"\n"
        "words[0] = words[0]\n"
        "replace words\n"
        "assert words[0][0] == \"c\"\n"
        "nested = new Array1<List<int>> 1\n"
        "nested[0].append 7\n"
        "saved = nested[0]\n"
        "nested[0] = new List<int>\n"
        "assert saved[0] == 7\n"
        "assert nested[0].count == 0\n"
        "return words[0].length + values[1]"
    }}}, {}});
    const auto compiled = compiler->compileScripts(&context, CompilationTarget::Executable);
    BOOST_REQUIRE_MESSAGE(compiled,
        (context.getFirstError() ? formatDiagnostic(*context.getFirstError()) : "Compilation failed"));
    RuntimeValue empty;
    runtime.getStringType()->initValue(empty);
    runtime.getStringType()->freeValue(empty);
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(result.getIntegerValue() == 130);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(array2_indexers_read_write_chain_and_preserve_reference_semantics) {
    TestDirectory directory("nomad_host_array2_indexers");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"main", {"main.nomad",
        "fun replace grid:Array2<string>\n"
        "    grid[1 2] = \"changed\"\n"
        "end\n"
        "grid = new Array<int> 2 3\n"
        "assert grid.count == 6\n"
        "grid[0 1] = 12\n"
        "grid[1 0] = 34\n"
        "alias = grid\n"
        "alias[1 2] = grid[0 1] + grid[1 0]\n"
        "assert grid[1 2] == 46\n"
        "assert grid[0 0] == 0\n"
        "assert grid[1 1] == 0\n"
        "assert grid[(0 + 1) (1 + 1)] == 46\n"
        "assert grid[-1 0] == 0\n"
        "assert grid[2 0] == 0\n"
        "assert grid[0 (-1)] == 0\n"
        "assert grid[0 3] == 0\n"
        "grid[-1 0] = 99\n"
        "grid[2 0] = 99\n"
        "grid[0 (-1)] = 99\n"
        "grid[0 3] = 99\n"
        "assert grid[0 1] == 12\n"
        "assert grid[1 2] == 46\n"
        "words = new Array2<string> 2 3\n"
        "words[1 2] = \"hello\"\n"
        "words[1 2] = words[1 2]\n"
        "assert words[1 2][0] == \"h\"\n"
        "assert words[2 0] == \"\"\n"
        "replace words\n"
        "assert words[1 2].length == 7\n"
        "nested = new Array2<List<int>> 1 2\n"
        "nested[0 1].append 7\n"
        "saved = nested[0 1]\n"
        "nested[0 1] = new List<int>\n"
        "assert saved[0] == 7\n"
        "assert nested[0 1].count == 0\n"
        "nested[-1 0].append 8\n"
        "assert nested[-1 0].count == 0\n"
        "holder = new List<Array2<string>>\n"
        "holder.append words\n"
        "holder[0][0 1] = \"nested\"\n"
        "assert words[0 1] == \"nested\"\n"
        "return $\"{words[1 2]}:{grid[1 2]}\""
    }}}, {}});
    const auto compiled = compiler->compileScripts(&context, CompilationTarget::Executable);
    BOOST_REQUIRE_MESSAGE(compiled,
        (context.getFirstError() ? formatDiagnostic(*context.getFirstError()) : "Compilation failed"));
    RuntimeValue empty;
    runtime.getStringType()->initValue(empty);
    runtime.getStringType()->freeValue(empty);
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(result.getStringObject()->getString() == "changed:46");
    runtime.getStringType()->freeValue(result);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    std::ostringstream documentation;
    generateNativeMemberDocumentation(&runtime, documentation);
    BOOST_TEST(documentation.str().find("Array2<int>[index1 index2]") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(array2_indexers_work_in_interpreter_and_reject_invalid_syntax_and_types) {
    TestDirectory directory("nomad_host_array2_interpreter");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    RuntimeValue empty;
    runtime.getStringType()->initValue(empty);
    runtime.getStringType()->freeValue(empty);
    const auto before = NomadObject::getLiveObjectCount();
    {
        VirtualMachine machine(&runtime);
        InterpreterContext context(machine);
        for (const auto* source : {
            "grid = new Array<int> 2 3", "grid[1 2] = 123", "alias = grid", "alias[0 1] = grid[1 2]",
            "assert grid[0 1] == 123", "assert grid[1 2] == 123",
            "assert grid[-1 0] == 0", "assert grid[0 (-1)] == 0",
            "assert grid[2 0] == 0", "assert grid[0 3] == 0",
            "grid[2 0] = 9", "grid[0 3] = 9", "assert grid[0 1] == 123",
            "words = new Array2<string> 1 2", "words[0 1] = \"hello\"",
            "words[0 1] = words[0 1]", "assert words[0 1][1] == \"e\"",
            "nested = new Array2<List<int>> 1 2", "nested[0 1].append 7", "saved = nested[0 1]",
            "nested[0 1] = new List<int>", "assert saved[0] == 7", "assert nested[0 1].count == 0",
            "assert nested[-1 0].count == 0"
        }) {
            BOOST_TEST_CONTEXT(source) {
                const auto isAssertion = NomadStringView(source).starts_with("assert ");
                const auto executed = Interpreter{}.execute(isAssertion ? source + 7 : source, context);
                BOOST_REQUIRE_MESSAGE(executed, (context.getError() ? *context.getError() : source));
                if (isAssertion) {
                    BOOST_TEST(context.getResult()->getValue().getBooleanValue());
                }
            }
        }
        for (const auto* source : {
            "grid[]", "grid[0]", "grid[0 1 2]", "grid[0 1", "grid[0, 1]",
            "grid[\"key\" 0]", "grid[0 \"key\"]", "grid[0 1.0]", "grid[false 0]",
            "grid[0 1] = \"wrong\"", "grid[0 1] = false"
        }) {
            BOOST_TEST_CONTEXT(source) {
                BOOST_TEST(!Interpreter{}.execute(source, context));
                BOOST_TEST(context.hasError());
                auto compiler = runtime.createCompiler();
                CompilerContext compilation(compiler.get());
                compiler->registerSources(&compilation, {{{"rejected", {"rejected.nomad",
                    NomadString("grid = new Array2<int> 2 3\n") + source
                }}}, {}});
                BOOST_TEST(!compiler->compileScripts(&compilation, CompilationTarget::Executable));
                BOOST_REQUIRE(compilation.getFirstError() != nullptr);
            }
        }
        BOOST_REQUIRE(Interpreter{}.evaluateExpression("grid[0 1] + grid[1 2]", context));
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 246);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(array3_indexers_work_in_compiled_scripts_and_interpreter) {
    TestDirectory directory("nomad_host_array3_indexers");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    const std::vector<NomadString> statements = {
        "volume = new Array<int> 2 3 4",
        "assert volume.count == 24",
        "assert volume[1 2 3] == 0",
        "volume[0 1 2] = 12",
        "volume[1 0 2] = 34",
        "volume[1 2 0] = 56",
        "alias = volume",
        "alias[1 2 3] = volume[0 1 2] + volume[1 0 2]",
        "assert volume[1 2 3] == 46",
        "assert volume[0 1 2] == 12",
        "assert volume[1 0 2] == 34",
        "assert volume[1 2 0] == 56",
        "assert volume[0 0 0] == 0",
        "words = new Array3<string> 2 3 4",
        "words[1 2 3] = \"hello\"",
        "words[1 2 3] = words[1 2 3]",
        "assert words[1 2 3][1] == \"e\"",
        "assert words[0 0 0] == \"\"",
        "words[0 0 4] = \"ignored\"",
        "assert words[0 0 4] == \"\"",
        "assert words[1 2 3] == \"hello\"",
        "nested = new Array3<List<int>> 1 2 3",
        "nested[0 1 2].append 7",
        "saved = nested[0 1 2]",
        "nested[0 1 2] = new List<int>",
        "assert saved[0] == 7",
        "assert nested[0 1 2].count == 0",
        "nested[0 1 3].append 8",
        "assert nested[0 1 3].count == 0"
    };
    auto sources = statements;
    for (const auto* indexes : {"-1 0 0", "2 0 0", "0 (-1) 0", "0 3 0", "0 0 (-1)", "0 0 4"}) {
        sources.push_back("assert volume[" + NomadString(indexes) + "] == 0");
        sources.push_back("volume[" + NomadString(indexes) + "] = 99");
    }
    sources.emplace_back("assert volume[1 2 3] == 46");
    sources.emplace_back("assert volume[0 0 0] == 0");
    NomadString script;
    for (const auto& source : sources) {
        script += source + "\n";
    }
    auto compiler = runtime.createCompiler();
    CompilerContext compilation(compiler.get());
    compiler->registerSources(&compilation, {{{"main", {"main.nomad", script}}}, {}});
    BOOST_REQUIRE_MESSAGE(compiler->compileScripts(&compilation, CompilationTarget::Executable),
        (compilation.getFirstError() ? formatDiagnostic(*compilation.getFirstError()) : "Compilation failed"));
    RuntimeValue empty;
    runtime.getStringType()->initValue(empty);
    runtime.getStringType()->freeValue(empty);
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    {
        VirtualMachine machine(&runtime);
        InterpreterContext context(machine);
        for (const auto& source : sources) {
            BOOST_TEST_CONTEXT(source) {
                const auto isAssertion = source.starts_with("assert ");
                BOOST_REQUIRE_MESSAGE(Interpreter{}.execute(isAssertion ? source.substr(7) : source, context),
                    (context.getError() ? *context.getError() : source));
                if (isAssertion) {
                    BOOST_TEST(context.getResult()->getValue().getBooleanValue());
                }
            }
        }
        for (const auto* source : {
            "volume[0 0]", "volume[0 0 0 0]", "volume[\"key\" 0 0]",
            "volume[0 \"key\" 0]", "volume[0 0 \"key\"]", "volume[0 0 0] = \"wrong\""
        }) {
            BOOST_TEST_CONTEXT(source) {
                BOOST_TEST(!Interpreter{}.execute(source, context));
                BOOST_TEST(context.hasError());
                auto rejectedCompiler = runtime.createCompiler();
                CompilerContext rejected(rejectedCompiler.get());
                rejectedCompiler->registerSources(&rejected, {{{"rejected", {"rejected.nomad",
                    NomadString("volume = new Array<int> 2 3 4\n") + source
                }}}, {}});
                BOOST_TEST(!rejectedCompiler->compileScripts(&rejected, CompilationTarget::Executable));
                BOOST_REQUIRE(Interpreter{}.evaluateExpression("volume[1 2 3] == 46", context));
                BOOST_TEST(context.getResult()->getValue().getBooleanValue());
            }
        }
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    std::ostringstream documentation;
    generateNativeMemberDocumentation(&runtime, documentation);
    BOOST_TEST(documentation.str().find("Array3<int>[index1 index2 index3]") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(array1_indexers_work_in_interpreter_and_report_bounds_errors) {
    TestDirectory directory("nomad_host_array1_interpreter");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    VirtualMachine machine(&runtime);
    InterpreterContext context(machine);
    for (const auto* source : {
        "values = new Array<int> 2", "values[0] = 123", "alias = values",
        "alias[1] = values[0]", "values[1]"
    }) {
        BOOST_REQUIRE_MESSAGE(Interpreter{}.execute(source, context),
            (context.getError() ? *context.getError() : source));
    }
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 123);
    for (const auto* source : {"values[-1]", "values[2]", "values[-1] = 9", "values[2] = 9"}) {
        BOOST_TEST_CONTEXT(source) {
            BOOST_REQUIRE(Interpreter{}.execute(source, context));
            BOOST_TEST(!context.hasError());
        }
    }
    BOOST_REQUIRE(Interpreter{}.evaluateExpression("values[-1] + values[2]", context));
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 0);
    for (const auto* source : {"values[\"key\"]", "values[0] = \"wrong\"", "values[0 1]"}) {
        BOOST_TEST_CONTEXT(source) {
            BOOST_TEST(!Interpreter{}.execute(source, context));
            BOOST_TEST(context.hasError());
        }
    }
    BOOST_REQUIRE(Interpreter{}.evaluateExpression("values.count", context));
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 2);
    BOOST_REQUIRE(Interpreter{}.evaluateExpression("values[0] + values[1]", context));
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 246);
}

BOOST_AUTO_TEST_CASE(collections_indexers_work_in_interpreter_and_fault_without_growing) {
    TestDirectory directory("nomad_host_interpreter_indexers");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    const auto before = NomadObject::getLiveObjectCount();
    {
        VirtualMachine machine(&runtime);
        InterpreterContext context(machine);
        for (const auto* source : {
            "words = new List<string>", "words.append \"first\"", "alias = words",
            "alias[0] = \"second\"", "words[0] = words[0]", "words[0][0]"
        }) {
            const auto executed = Interpreter{}.execute(source, context);
            BOOST_REQUIRE_MESSAGE(executed,
                (context.getError() ? *context.getError() : "Interpreter failed"));
        }
        BOOST_TEST(context.getResult()->getValue().getStringObject()->getString() == "s");
        for (const auto* source : {"words[-1]", "words[1]", "words[1] = \"third\""}) {
            BOOST_TEST(!Interpreter{}.execute(source, context));
            BOOST_REQUIRE(context.getError() != nullptr);
            BOOST_TEST(context.getError()->find("out of bounds for size 1") != NomadString::npos);
        }
        BOOST_REQUIRE(Interpreter{}.evaluateExpression("words.count", context));
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 1);
        BOOST_REQUIRE(Interpreter{}.execute("empty = new List<int>", context));
        BOOST_TEST(!Interpreter{}.execute("empty[0]", context));
        BOOST_REQUIRE(context.getError() != nullptr);
        BOOST_TEST(context.getError()->find("size 0") != NomadString::npos);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(collections_constructs_distinct_reference_counted_lists) {
    TestDirectory directory("nomad_host_list_construction");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    BOOST_TEST(runtime.getKeywordId("new").isValid());
    const auto* listType = runtime.getParameterizedType("List", {runtime.getIntegerType()});
    BOOST_REQUIRE(listType != nullptr);
    BOOST_REQUIRE(runtime.addProperty(listType->getId(), {
        .valueTypeId = runtime.getTypeId("bool"),
        .name = "testPresent",
        .getter = [](VirtualMachine* machine, RuntimeValue& result) {
            result.setBooleanValue(machine->getThis() != nullptr);
        },
        .setter = {},
        .documentation = ""
    }).isValid());
    runtime.registerNativeFunction(
        "test.distinct",
        [](VirtualMachine* machine) {
            machine->setBooleanResult(
                machine->getParameter(0).getObjectValue() != machine->getParameter(1).getObjectValue()
            );
        },
        {{"left", listType}, {"right", listType}},
        runtime.getBooleanType(),
        ""
    );
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"init", {"init.nomad",
        "fun identity value:List<int>\n"
        "    return value\n"
        "end\n"
        "fun build\n"
        "    first = new List<int>\n"
        "    alias = first\n"
        "    first = new List<int>\n"
        "    unused = new List<List<string>>\n"
        "    return identity alias\n"
        "end\n"
        "return build"
    }}, {"distinct", {"distinct.nomad", "return test.distinct (new List<int>) (new List<int>)"}},
        {"postfix", {"postfix.nomad", "return new List<int>.testPresent"}}}, {}});
    BOOST_REQUIRE(compiler->compileScripts(&context, CompilationTarget::Executable));
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue first;
    RuntimeValue second;
    runtime.executeFunction(runtime.getFunctionId("init"), {}, first);
    BOOST_REQUIRE(first.getObjectValue() != nullptr);
    BOOST_TEST(first.getObjectValue()->getReferenceCount() == 1u);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before + 1);
    runtime.executeFunction(runtime.getFunctionId("init"), {}, second);
    BOOST_TEST(first.getObjectValue() != second.getObjectValue());
    BOOST_TEST(second.getObjectValue()->getReferenceCount() == 1u);
    RuntimeValue distinct;
    runtime.executeFunction(runtime.getFunctionId("distinct"), {}, distinct);
    BOOST_TEST(distinct.getBooleanValue());
    RuntimeValue postfix;
    runtime.executeFunction(runtime.getFunctionId("postfix"), {}, postfix);
    BOOST_TEST(postfix.getBooleanValue());
    listType->freeValue(first);
    listType->freeValue(second);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);

    {
        VirtualMachine machine(&runtime);
        InterpreterContext interpreterContext(machine);
        BOOST_REQUIRE(Interpreter{}.evaluateExpression("new List<int>.testPresent", interpreterContext));
        BOOST_TEST(interpreterContext.getResult()->getValue().getBooleanValue());
        BOOST_REQUIRE(Interpreter{}.execute("value = new List<int>", interpreterContext));
        const auto* original = interpreterContext.getVariableValue("value")->getObjectValue();
        BOOST_REQUIRE(Interpreter{}.execute("alias = value", interpreterContext));
        BOOST_REQUIRE(Interpreter{}.execute("value = new List<int>", interpreterContext));
        BOOST_TEST(original == interpreterContext.getVariableValue("alias")->getObjectValue());
        BOOST_TEST(original != interpreterContext.getVariableValue("value")->getObjectValue());
        BOOST_TEST(NomadObject::getLiveObjectCount() == before + 2);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(compiler_rejects_invalid_object_construction) {
    for (const auto* source : {
        "return new int", "return new string", "return new void", "return new",
        "return new Missing", "return new List", "return new List<>", "return new List<int> 1",
        "return new Unconstructible"
    }) {
        BOOST_TEST_CONTEXT(source) {
            TestDirectory directory("nomad_host_bad_construction");
            const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
            auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
            BOOST_REQUIRE(host->getRuntime()->registerClass(
                "Unconstructible", ClassMode::Closed
            ).isValid());
            auto compiler = host->getRuntime()->createCompiler();
            CompilerContext context(compiler.get());
            compiler->registerSources(&context, {{{"init", {"init.nomad", source}}}, {}});
            BOOST_TEST(!compiler->compileScripts(&context, CompilationTarget::Analysis));
            BOOST_TEST(context.hasError());
        }
    }
}

BOOST_AUTO_TEST_CASE(object_construction_reports_callback_failures) {
    Runtime runtime;
    const auto descriptorId = runtime.registerClass<ClassType>("Broken", ClassMode::Closed);
    BOOST_REQUIRE(descriptorId.isValid());
    const auto* type = runtime.getType(descriptorId).value()->asClassType();
    BOOST_REQUIRE(runtime.addConstructor(type->getId(), {
        .parameters = {},
        .function = [](VirtualMachine*) {},
        .documentation = ""
    }).isValid());
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"main", {"main.nomad", "return new Broken"}}}, {}});
    BOOST_REQUIRE(compiler->compileScripts(&context, CompilationTarget::Executable));
    RuntimeValue result;
    BOOST_CHECK_EXCEPTION(
        runtime.executeFunction(runtime.getFunctionId("main"), {}, result),
        NomadException,
        [](const NomadException& exception) {
            return NomadString(exception.what()).find("returned null") != NomadString::npos;
        }
    );
    VirtualMachine machine(&runtime);
    InterpreterContext interpreterContext(machine);
    BOOST_CHECK_THROW(
        static_cast<void>(Interpreter{}.evaluateExpression("new Broken", interpreterContext)),
        NomadException
    );
    BOOST_TEST(interpreterContext.getResult() == nullptr);
    BOOST_TEST(!Interpreter{}.execute("new Broken", interpreterContext));
    BOOST_REQUIRE(interpreterContext.getError() != nullptr);
    BOOST_TEST(interpreterContext.getError()->find("returned null") != NomadString::npos);
    BOOST_TEST(interpreterContext.getResult() == nullptr);
}

BOOST_AUTO_TEST_CASE(compiler_resolves_parameterized_annotations_from_collections) {
    for (const auto target : {CompilationTarget::Analysis, CompilationTarget::Executable}) {
        TestDirectory directory("nomad_host_type_annotations");
        const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
        auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
        auto& runtime = *host->getRuntime();
        BOOST_REQUIRE(runtime.registerParameterizedType("Pair", 2, [](Runtime* owningRuntime, TypeId id, const auto& arguments) {
            return std::make_unique<ClassType>(owningRuntime, id, "Pair<" + arguments[0]->getTypeName() + " " + arguments[1]->getTypeName() + ">",
                ClassMode::Closed,
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
        const auto* function = runtime.getScriptFunction(runtime.getFunctionId("init"));
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
            const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
            auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
            auto compiler = host->getRuntime()->createCompiler();
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
        const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
        auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
        auto compiler = host->getRuntime()->createCompiler();
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
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    BOOST_TEST(runtime.getTypeByName("List<int>") == nullptr);
    const auto* integers = runtime.getParameterizedType("List", {runtime.getIntegerType()});
    BOOST_REQUIRE(integers != nullptr);
    BOOST_TEST(integers == runtime.getParameterizedType("List", {runtime.getIntegerType()}));
    BOOST_TEST(integers == runtime.getTypeByName("List<int>"));
    const auto* descriptor = dynamic_cast<const ClassType*>(integers);
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
    BOOST_TEST(!runtime.registerParameterizedType("List", 1, [](Runtime* owningRuntime, TypeId id, const auto&) {
        return std::make_unique<ClassType>(owningRuntime, id, "Duplicate", ClassMode::Closed);
    }));
}

BOOST_AUTO_TEST_CASE(collections_list_defaults_are_distinct_empty_instances) {
    TestDirectory directory("nomad_host_list_defaults");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    const auto* type = runtime.getParameterizedType("List", {runtime.getIntegerType()});
    BOOST_REQUIRE(type != nullptr);
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue first;
    RuntimeValue second;
    type->initValue(first);
    type->initValue(second);
    BOOST_REQUIRE(first.getObjectValue() != nullptr);
    BOOST_REQUIRE(second.getObjectValue() != nullptr);
    BOOST_TEST(first.getObjectValue() != second.getObjectValue());
    type->freeValue(first);
    type->freeValue(second);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(collections_native_returns_do_not_allocate_unused_list_defaults) {
    TestDirectory directory("nomad_host_list_native_defaults");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    auto& runtime = *host->getRuntime();
    const auto* type = runtime.getParameterizedType("List", {runtime.getIntegerType()});
    BOOST_REQUIRE(type != nullptr);
    runtime.registerNativeFunction(
        "makeList",
        [&runtime](VirtualMachine* machine) {
            machine->getResult().setObjectValue(new ListObject(runtime.getIntegerType()));
        },
        {}, type, ""
    );
    const auto memberId = runtime.addMethod(type->getId(), {
        .name = "copyReference",
        .parameters = {},
        .returnTypeId = type->getId(),
        .function = [](VirtualMachine* machine) {
            addRef(machine->getThis());
            machine->getResult().setObjectValue(machine->getThis());
        },
        .documentation = ""
    });
    BOOST_REQUIRE(memberId.isValid());
    const auto before = NomadObject::getLiveObjectCount();
    {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        BOOST_REQUIRE(Interpreter{}.execute("list = makeList", context));
        BOOST_REQUIRE(Interpreter{}.execute("alias = list.copyReference", context));
        BOOST_REQUIRE(Interpreter{}.execute("alias.append 42", context));
        BOOST_REQUIRE(Interpreter{}.execute("list[0]", context));
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 42);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(parameterized_types_validate_factories_and_cache_by_arguments) {
    Runtime runtime;
    NomadIndex calls = 0;
    BOOST_TEST(runtime.registerParameterizedType("Example", 1, [&calls](Runtime* owningRuntime, TypeId id, const auto& arguments) {
        ++calls;
        return std::make_unique<ClassType>(owningRuntime, id, "Example<" + arguments[0]->getTypeName() + ">", ClassMode::Closed, arguments);
    }));
    const auto* first = runtime.getParameterizedType("Example", {runtime.getIntegerType()});
    BOOST_REQUIRE(first != nullptr);
    BOOST_TEST(first == runtime.getParameterizedType("Example", {runtime.getIntegerType()}));
    BOOST_TEST(calls == 1u);
    BOOST_TEST(!runtime.registerParameterizedType("Empty", 1, {}));
    BOOST_TEST(!runtime.registerParameterizedType("", 1, {}));
    BOOST_TEST(runtime.registerParameterizedType("Broken", 1, [](Runtime*, TypeId, const auto&) {
        return std::unique_ptr<Type>{};
    }));
    BOOST_TEST(runtime.getParameterizedType("Broken", {runtime.getIntegerType()}) == nullptr);
    BOOST_TEST(runtime.registerParameterizedType("Collision", 1, [](Runtime* owningRuntime, TypeId id, const auto&) {
        return std::make_unique<ClassType>(owningRuntime, id, "int", ClassMode::Closed);
    }));
    BOOST_TEST(runtime.getParameterizedType("Collision", {runtime.getIntegerType()}) == nullptr);
}

BOOST_AUTO_TEST_CASE(collections_module_loads_in_all_modes) {
    TestDirectory directory("nomad_host_collections");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"collections"});
    BOOST_REQUIRE_EQUAL(modules.size(), 1u);
    BOOST_TEST(modules.front().name == "collections");

    for (const auto mode : {ModuleMode::Application, ModuleMode::Analysis, ModuleMode::Packaging}) {
        auto host = ModuleHost::create({}, directory.getPath(), modules, {}, mode);
        BOOST_TEST((host->getRuntime() != nullptr) == (mode != ModuleMode::Packaging));
        BOOST_TEST((host->getCompiler() != nullptr) == (mode != ModuleMode::Packaging));
    }
}

BOOST_AUTO_TEST_CASE(loads_registers_compiles_and_shuts_down_in_lifetime_order) {
    TestDirectory directory("nomad_host_lifecycle");
    {
        auto host = ModuleHost::create({}, directory.getPath(), fixtureModules(),
            {"--custom", "value"}, ModuleMode::Application);
        BOOST_REQUIRE(compileFixture(*host, CompilationTarget::Executable));
        BOOST_TEST(host->run() == 7);
        BOOST_TEST(host->pollExit());
    }
    BOOST_TEST(events(directory) == "register\npreParse\nemit\ncompleted\nrun\nexit\nshutdown\ndestroy\n");
}

BOOST_AUTO_TEST_CASE(analysis_skips_emission_and_execution) {
    TestDirectory directory("nomad_host_analysis");
    {
        auto host = ModuleHost::create({}, directory.getPath(), fixtureModules(), {}, ModuleMode::Analysis);
        BOOST_REQUIRE(compileFixture(*host, CompilationTarget::Analysis));
    }
    BOOST_TEST(events(directory) == "register\npreParse\ncompleted\nshutdown\ndestroy\n");
}

BOOST_AUTO_TEST_CASE(module_registration_and_compilation_share_the_compiler_and_resolve_script_types) {
    for (const auto mode : {ModuleMode::Application, ModuleMode::Analysis}) {
        TestDirectory directory("nomad_host_deferred_types");
        auto configuration = NomadConfiguration::parse("[fixture]\ndeferred = true\n");
        {
            auto host = ModuleHost::create(configuration, directory.getPath(), fixtureModules(), {}, mode);
            const auto* function = host->getRuntime()->getNativeFunction("fixture.echo");
            BOOST_REQUIRE(function != nullptr);
            const auto id = function->getId();
            BOOST_TEST(!function->isSignatureResolved());
            BOOST_REQUIRE(compileFixture(*host,
                mode == ModuleMode::Application ? CompilationTarget::Executable : CompilationTarget::Analysis,
                "open hub fixture\nclass Settings\nanswer = 42\nend\nend\n"
                "original = new fixture.Settings\nupdated = fixture.echo original\n"
                "assert updated.answer == 42\nfixture.exit\n"));
            BOOST_TEST(function->isSignatureResolved());
            BOOST_TEST(function->getId() == id);
            BOOST_TEST(function->getReturnType() == host->getRuntime()->getTypeByName("fixture.Settings"));
            BOOST_TEST(function->getParameters()[0].type == function->getReturnType());
            if (mode == ModuleMode::Application) {
                BOOST_TEST(host->run() == 7);
            }
        }
        const auto recorded = events(directory);
        BOOST_TEST(recorded.find("deferred-resolve\ndeferred-destroy\n") != NomadString::npos);
        BOOST_TEST(recorded.ends_with("shutdown\ndestroy\n"));
    }
}

BOOST_AUTO_TEST_CASE(unresolved_module_targets_are_destroyed_before_runtime_callbacks_and_library_unload) {
    TestDirectory directory("nomad_host_failed_deferred_types");
    auto configuration = NomadConfiguration::parse("[fixture]\ndeferred = true\n");
    {
        auto host = ModuleHost::create(configuration, directory.getPath(), fixtureModules(), {}, ModuleMode::Application);
        BOOST_TEST(!compileFixture(*host, CompilationTarget::Executable));
        BOOST_CHECK_THROW((void)host->run(), ModuleException);
    }
    BOOST_TEST(events(directory) ==
        "register\npreParse\nexit\nshutdown\ndeferred-destroy\ndestroy\n");
}

BOOST_AUTO_TEST_CASE(compiler_tooling_uses_module_registration_compiler_for_deferred_targets) {
    for (const auto documentation : {false, true}) {
        TestDirectory directory("nomad_host_deferred_tooling");
        auto configuration = NomadConfiguration::parse("[fixture]\ndeferred = true\n");
        const auto path = directory.write("main.nomad", "open hub fixture\nclass Settings\nend\nend\nreturn 1\n");
        auto host = ModuleHost::create(configuration, directory.getPath(), fixtureModules(), {}, ModuleMode::Analysis);
        const auto result = documentation
            ? generateDocumentationForPaths(std::vector{path}, *host->getCompiler()).compilation
            : dumpInstructions(std::vector{path}, std::nullopt, *host->getCompiler()).compilation;
        BOOST_TEST(result.succeeded());
        BOOST_TEST(events(directory).find("deferred-resolve\ndeferred-destroy\n") != NomadString::npos);
    }
}

BOOST_AUTO_TEST_CASE(hook_failure_prevents_later_hooks) {
    TestDirectory directory("nomad_host_hook_failure");
    {
        auto configuration = NomadConfiguration::parse("[fixture]\nfail-hook = true\n");
        auto host = ModuleHost::create(configuration, directory.getPath(), fixtureModules(), {}, ModuleMode::Analysis);
        BOOST_TEST(!compileFixture(*host, CompilationTarget::Executable));
    }
    BOOST_TEST(events(directory) == "register\npreParse\nshutdown\ndestroy\n");
}

BOOST_AUTO_TEST_CASE(packaging_uses_context_without_runtime_registration) {
    TestDirectory directory("nomad_host_package");
    auto source = NomadConfiguration::parse("[fixture]\ndevelopment = true\n");
    auto release = source;
    auto generated = NomadString{};
    auto host = ModuleHost::create(source, directory.getPath(), fixtureModules(), {}, ModuleMode::Packaging);
    BOOST_TEST(host->getRuntime() == nullptr);
    ModulePackageContext context{};
    context.source = &source;
    context.release = &release;
    context.writeFile = [&](const NomadPath& path, NomadStringView value) {
        BOOST_TEST(path == NomadPath("generated.txt"));
        generated = value;
    };
    host->package(context);
    BOOST_TEST(source.contains("fixture.development"));
    BOOST_TEST(!release.contains("fixture.development"));
    BOOST_TEST(generated == "generated by fixture\n");
    BOOST_TEST(events(directory).empty());
}

BOOST_AUTO_TEST_CASE(missing_library_is_an_explicit_startup_failure) {
    TestDirectory directory("nomad_host_missing");
    BOOST_CHECK_THROW(
        (void)ModuleHost::create({}, directory.getPath(),
            {{"missing", directory.getPath(), {}}}, {}, ModuleMode::Application),
        ModuleException
    );
}

BOOST_AUTO_TEST_CASE(registration_failure_is_not_reported_as_success) {
    TestDirectory directory("nomad_host_registration_failure");
    auto configuration = NomadConfiguration::parse("[fixture]\nfail-registration = true\n");
    BOOST_CHECK_EXCEPTION(
        (void)ModuleHost::create(configuration, directory.getPath(),
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
    std::filesystem::copy_file(
        NOMAD_TEST_CORE_LIBRARY,
        directory.getPath() / getModuleLibraryName("missing-entry")
    );
    BOOST_CHECK_EXCEPTION(
        (void)ModuleHost::create({}, directory.getPath(),
            {{"missing-entry", directory.getPath(), {}}}, {}, ModuleMode::Analysis),
        ModuleException,
        [](const auto& error) {
            return NomadString(error.what()).find("does not export registerModule") != NomadString::npos;
        }
    );
}

BOOST_AUTO_TEST_CASE(module_arguments_are_provided_during_registration) {
    TestDirectory directory("nomad_host_arguments");
    auto host = ModuleHost::create({}, directory.getPath(), fixtureModules(),
        {"--custom", "value"}, ModuleMode::Analysis);
    RuntimeValue value;
    host->getRuntime()->getConstantValue(host->getRuntime()->getConstantId("fixture.argumentCount"), value);
    BOOST_TEST(value.getIntegerValue() == 2);
}

BOOST_AUTO_TEST_SUITE_END()
