// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/script/Interpreter.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

using namespace nomad;
using namespace nomad::test;

BOOST_AUTO_TEST_SUITE(language_hubs)

BOOST_AUTO_TEST_CASE(native_registration_is_idempotent_and_reserves_names)
{
    Runtime runtime;
    BOOST_REQUIRE(runtime.registerHub("example"));
    BOOST_REQUIRE(runtime.registerHub("example"));
    BOOST_REQUIRE(runtime.registerHub("example.inner"));
    BOOST_TEST(runtime.isHub("example.inner"));
    BOOST_TEST(runtime.registerConstant("example", RuntimeValue(NomadInteger{1}), runtime.getIntegerType()) == NOMAD_INVALID_ID);
    BOOST_REQUIRE(runtime.registerConstant("occupied", RuntimeValue(NomadInteger{1}), runtime.getIntegerType()) != NOMAD_INVALID_ID);
    BOOST_TEST(!runtime.registerHub("occupied.child"));
    BOOST_TEST(!runtime.isHub("occupied"));
    BOOST_TEST(!runtime.registerHub("invalid."));
    BOOST_TEST(!runtime.registerHub("invalid..name"));
    BOOST_TEST(!runtime.registerHub("fun"));
}

BOOST_AUTO_TEST_CASE(hub_qualified_dynamic_variables_remain_readable_and_writable)
{
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    NomadFloat volume = 0.25f;

    BOOST_REQUIRE(runtime.registerHub("game"));
    BOOST_REQUIRE(runtime.registerDynamicVariable(
        "game.volume",
        [&volume](VirtualMachine*, const RuntimeValue& value) {
            volume = value.getFloatValue();
        },
        [&volume](VirtualMachine*, RuntimeValue& value) {
            value.setFloatValue(volume);
        },
        runtime.getFloatType(),
        NomadDoc("Test game volume.")
    ) != NOMAD_INVALID_ID);

    fixture.addFunction("main", "game.volume = 0.75\nreturn game.volume");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

    const auto outcome = fixture.execute("main");
    BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
    BOOST_TEST(outcome.floatValue == 0.75f);
    BOOST_TEST(volume == 0.75f);

    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);
    BOOST_TEST(Interpreter{}.execute("game.volume = 0.5", context));
    BOOST_TEST(volume == 0.5f);
    BOOST_TEST(!Interpreter{}.execute("game.volume = \"wrong\"", context));
    BOOST_TEST(context.hasError());
    BOOST_TEST(volume == 0.5f);
}

BOOST_AUTO_TEST_CASE(hub_qualified_read_only_dynamic_variables_remain_unwritable)
{
    Runtime runtime;

    BOOST_REQUIRE(runtime.registerHub("game"));
    BOOST_REQUIRE(runtime.registerDynamicVariable(
        "game.frame",
        nullptr,
        [](VirtualMachine*, RuntimeValue& value) {
            value.setIntegerValue(42);
        },
        runtime.getIntegerType(),
        NomadDoc("Test read-only game frame.")
    ) != NOMAD_INVALID_ID);

    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);

    BOOST_TEST(Interpreter{}.execute("game.frame = 10", context) == false);
    BOOST_TEST(context.hasError());
}

BOOST_AUTO_TEST_CASE(hub_qualified_unknown_members_remain_undeclarable_by_assignment)
{
    LanguageTestFixture fixture;
    BOOST_REQUIRE(fixture.getRuntime().registerHub("game"));

    fixture.addFunction("main", "game.unknown = 10");
    BOOST_TEST(!fixture.compile());
    BOOST_TEST(fixture.hasError("Hub members must be declared inside a hub block"));
}

BOOST_AUTO_TEST_CASE(variables_initialize_before_any_entry_and_persist)
{
    LanguageTestFixture fixture;
    fixture.addFunction("declarations",
        "hub example\n"
        "    x = 10\n"
        "    name = \"test\"\n"
        "    fun increment\n"
        "        example.x = example.x + 1\n"
        "        return example.x\n"
        "    end\n"
        "end");
    fixture.addFunction("main", "return example.increment");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(fixture.execute("main").integerValue == 11);
    BOOST_TEST(fixture.execute("main").integerValue == 12);
    BOOST_TEST(fixture.execute("example.increment").integerValue == 13);
    RuntimeValue name;
    fixture.getRuntime().getContextVariableValue(fixture.getRuntime().getContextVariableId("example.name"), name);
    BOOST_TEST(name.getStringValue() == "test");
}

BOOST_AUTO_TEST_CASE(hubs_reopen_across_scripts_and_allow_forward_calls)
{
    LanguageTestFixture fixture;
    fixture.addFunction("a", "hub example\nfun first\nreturn second + example.value\nend\nend");
    fixture.addFunction("b", "hub example\nvalue = 20\nfun second\nreturn 22\nend\nend");
    fixture.addFunction("main", "return example.first");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(fixture.execute("main").integerValue == 42);
}

BOOST_AUTO_TEST_CASE(nested_hubs_resolve_current_parent_and_global_names)
{
    const auto outcome = runSource(
        "const globalValue = 1\n"
        "hub example\n"
        "const parentValue = 2\n"
        "hub inner\n"
        "const localValue = 3\n"
        "fun calculate\nreturn localValue + parentValue + globalValue\nend\n"
        "end\nend\n"
        "hub example.inner\nfun read\nreturn calculate\nend\nend\n"
        "return example.inner.read");
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.integerValue == 6);
}

BOOST_AUTO_TEST_CASE(hub_enums_and_formatted_expressions_resolve)
{
    const auto outcome = runSource(
        "hub example\n"
        "enum State\nReady\nBusy\nend\n"
        "state = State.Ready\n"
        "name = \"test\"\n"
        "fun describe value:State\n"
        "if value == State.Ready\nreturn $\"{example.name}:{example.state == State.Ready}\"\nend\n"
        "return \"busy\"\nend\nend\n"
        "return example.describe example.State.Ready");
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.text == "test:true");
}

BOOST_AUTO_TEST_CASE(initializers_execute_once_in_declaration_order)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main",
        "hub example\nfirst = 10\nsecond = example.first + 2\nend\n"
        "example.first = example.first + 1\n"
        "return example.first + example.second");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(fixture.execute("main").integerValue == 23);
    BOOST_TEST(fixture.execute("main").integerValue == 24);
}

BOOST_AUTO_TEST_CASE(forward_static_values_are_available_before_runtime_initialization)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main",
        "fun readSecond\nreturn example.second\nend\n"
        "hub example\nfirst = readSecond\nsecond = 10\nend\nreturn example.first");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(fixture.execute("main").integerValue == 10);
}

BOOST_AUTO_TEST_CASE(declarations_are_required_and_duplicate_members_are_errors)
{
    BOOST_TEST(!compileErrors("hub example\nend\nexample.missing = 1").empty());
    BOOST_TEST(!compileErrors("hub example\nend\nexample.length = 1").empty());
    BOOST_TEST(!compileErrors("hub example\nx = 1\nend\nhub example\nx = 2\nend").empty());
    BOOST_TEST(!compileErrors("hub example\nx = 1\nend\nhub example\nfun x\nend\nend").empty());
    BOOST_TEST(!compileErrors("hub example\nend\nexample = 1").empty());
    BOOST_TEST(!compileErrors("hub example\nend\nconst example = 1").empty());
    BOOST_TEST(!compileErrors("hub example\nend\nfun example\nend").empty());
    BOOST_TEST(!compileErrors("hub example\nx = 1\nend\nexample.x = \"wrong\"").empty());
    BOOST_TEST(!compileErrors("hub example\nx = 1\ny = x\nend").empty());
}

BOOST_AUTO_TEST_CASE(malformed_or_misplaced_hubs_are_errors)
{
    BOOST_TEST(!compileErrors("hub example\nx = 1").empty());
    BOOST_TEST(!compileErrors("hub example trailing\nend").empty());
    BOOST_TEST(!compileErrors("hub example\nreturn 1\nend").empty());
    BOOST_TEST(!compileErrors("fun inner\nhub example\nend\nend").empty());
    BOOST_TEST(!compileErrors("if true\nhub example\nend\nend").empty());
}

BOOST_AUTO_TEST_CASE(native_hubs_can_be_extended_by_scripts)
{
    LanguageTestFixture fixture;
    BOOST_REQUIRE(fixture.getRuntime().registerHub("example"));
    BOOST_REQUIRE(fixture.getRuntime().registerConstant(
        "example.nativeValue", RuntimeValue(NomadInteger{40}), fixture.getRuntime().getIntegerType()
    ) != NOMAD_INVALID_ID);
    fixture.addFunction("main", "hub example\nextra = 2\nend\nreturn example.nativeValue + example.extra");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(fixture.execute("main").integerValue == 42);
}

BOOST_AUTO_TEST_CASE(hub_objects_index_members_without_duplicating_callable_ownership)
{
    Runtime runtime;
    BOOST_REQUIRE(runtime.registerHub("example"));
    const auto id = runtime.getHubId("example");
    const auto* hub = runtime.getHub(id);
    BOOST_REQUIRE(hub != nullptr);
    BOOST_REQUIRE(runtime.registerHub("example"));
    BOOST_TEST(runtime.getHub(id) == hub);
    const auto functionId = runtime.registerNativeFunction(
        "example.answer", [](VirtualMachine* machine) { machine->setIntegerResult(42); },
        {}, runtime.getIntegerType(), ""
    );
    BOOST_REQUIRE(functionId.isValid());
    const auto overloadId = runtime.registerNativeFunction(
        "example.answer", [](VirtualMachine* machine) { machine->setIntegerResult(machine->getIntegerParameter(0)); },
        {defParameter("value", runtime.getIntegerType(), "")}, runtime.getIntegerType(), ""
    );
    BOOST_REQUIRE(overloadId.isValid());
    const auto* member = hub->getMember("answer");
    BOOST_REQUIRE(member != nullptr);
    const auto* functions = std::get_if<HubFunctions>(member);
    BOOST_REQUIRE(functions != nullptr);
    BOOST_REQUIRE(functions->ids.size() == 2U);
    BOOST_TEST((functions->ids[0] == functionId));
    BOOST_TEST((functions->ids[1] == overloadId));
    BOOST_TEST(runtime.getFunction(functions->ids[0])->getName() == "example.answer");
    BOOST_REQUIRE(runtime.registerEnum("example.State", {{"Ready", 1}}) != nullptr);
    BOOST_REQUIRE(hub->getMember("State") != nullptr);
    BOOST_TEST(std::holds_alternative<HubType>(*hub->getMember("State")));
    BOOST_REQUIRE(hub->getMember("State.Ready") != nullptr);
    BOOST_TEST(std::holds_alternative<HubConstant>(*hub->getMember("State.Ready")));
    BOOST_REQUIRE(runtime.registerEvent("example.changed", {}) != NOMAD_INVALID_ID);
    BOOST_REQUIRE(hub->getMember("changed") != nullptr);
    BOOST_TEST(std::holds_alternative<HubEvent>(*hub->getMember("changed")));
    BOOST_REQUIRE(runtime.registerHub("example.child"));
    const auto childId = runtime.getHubId("example.child");
    BOOST_TEST(runtime.getHub(childId)->getParentId() == id);
    BOOST_REQUIRE(hub->getMember("child") != nullptr);
    BOOST_TEST(std::get<HubChild>(*hub->getMember("child")).id == childId);
}

BOOST_AUTO_TEST_CASE(explicit_hubs_adopt_existing_qualified_native_registrations)
{
    Runtime runtime;
    const auto constantId = runtime.registerConstant(
        "example.child.value", RuntimeValue(NomadInteger{42}), runtime.getIntegerType()
    );
    BOOST_REQUIRE(constantId != NOMAD_INVALID_ID);
    BOOST_REQUIRE(runtime.registerHub("example"));
    const auto* parent = runtime.getHub(runtime.getHubId("example"));
    BOOST_REQUIRE(parent->getMember("child.value") != nullptr);
    BOOST_REQUIRE(runtime.registerHub("example.child"));
    BOOST_TEST(parent->getMember("child.value") == nullptr);
    const auto* child = runtime.getHub(runtime.getHubId("example.child"));
    BOOST_REQUIRE(child->getMember("value") != nullptr);
    BOOST_TEST(std::get<HubConstant>(*child->getMember("value")).id == constantId);
}

BOOST_AUTO_TEST_CASE(initialization_is_explicit_and_independent_of_compiler_lifetime)
{
    Runtime runtime;
    auto calls = 0;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "initialValue", [&calls](VirtualMachine* machine) { ++calls; machine->setIntegerResult(42); },
        {}, runtime.getIntegerType(), ""
    ).isValid());
    {
        auto compiler = runtime.createCompiler();
        CompilerContext context(compiler.get());
        compiler->registerSources(&context, {{
            {"main", {"main.nomad", "hub example\nx = initialValue\nend\nreturn example.x"}},
            {"noop", {"noop.nomad", "return 1"}}
        }, {}});
        compiler->registerPhaseHook([&](CompilerContext*, CompilationPhase) { BOOST_TEST(calls == 0); });
        BOOST_REQUIRE_MESSAGE(compiler->compileScripts(&context),
            (context.getFirstError() ? formatDiagnostic(*context.getFirstError()) : "Compilation failed"));
    }
    BOOST_TEST(calls == 0);
    BOOST_CHECK_THROW(runtime.executeFunction(runtime.getFunctionId("main")), VirtualMachineException);
    auto machine = runtime.createVirtualMachine();
    machine->run(runtime.getScriptFunction(runtime.getFunctionId("noop")), {});
    BOOST_TEST(machine->getIntegerResult() == 1);
    BOOST_TEST(calls == 0);
    runtime.initializeScripts();
    runtime.initializeScripts();
    BOOST_TEST(calls == 1);
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(result.getIntegerValue() == 42);
    const auto* hub = runtime.getHub(runtime.getHubId("example"));
    BOOST_REQUIRE(hub != nullptr);
    BOOST_REQUIRE(hub->getMember("x") != nullptr);
    BOOST_TEST(std::holds_alternative<HubVariable>(*hub->getMember("x")));
    BOOST_TEST(hub->getMembers().size() == 1U);
}

BOOST_AUTO_TEST_CASE(analysis_resolves_members_without_installing_or_executing_initializers)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{
        {"main", {"main.nomad", "hub example\nvalue = 42\nend\nreturn example.value"}}
    }, {}});
    const auto instructionCount = runtime.getInstructions().size();
    BOOST_REQUIRE(compiler->compileScripts(&context, CompilationTarget::Analysis));
    BOOST_TEST(runtime.getInstructions().size() == instructionCount);
    BOOST_TEST(runtime.getContextVariableType(runtime.getContextVariableId("example.value")) == runtime.getIntegerType());
    BOOST_CHECK_THROW(runtime.initializeScripts(), VirtualMachineException);
}

BOOST_AUTO_TEST_CASE(reopened_hubs_keep_cross_hub_initialization_order)
{
    const auto outcome = runSource(
        "hub first\nx = 10\nend\n"
        "hub second\ny = first.x + 1\nend\n"
        "hub first\nz = second.y + 1\nend\n"
        "return first.z");
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.integerValue == 12);
}

BOOST_AUTO_TEST_CASE(static_values_are_installed_without_executable_assignments)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main",
        "hub example\nx = 10\ny = example.x + 2\nname = \"test\"\nend\n"
        "example.x = example.x + 1\nreturn example.x + example.y");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    const auto* initializer = runtime.getScriptFunction(runtime.getFunctionId("$init"));
    BOOST_REQUIRE(initializer != nullptr);
    BOOST_TEST(initializer->getFunctionEnd() - initializer->getFunctionStart() == 1U);
    BOOST_TEST(runtime.getFunctionCount() == 2U);
    RuntimeValue value;
    runtime.getContextVariableValue(runtime.getContextVariableId("example.y"), value);
    BOOST_TEST(value.getIntegerValue() == 12);
    BOOST_TEST(fixture.execute("main").integerValue == 23);
    BOOST_TEST(fixture.execute("main").integerValue == 24);
}

BOOST_AUTO_TEST_CASE(runtime_initializers_follow_script_names_not_registration_order)
{
    LanguageTestFixture fixture;
    fixture.addFunction("b", "hub example\nb = initVar\na = 1\nend");
    fixture.addFunction("a",
        "fun initVar\nexample.a = example.a + 1\nreturn example.a\nend\n"
        "hub example\nc = initVar\nend");
    fixture.addFunction("main", "assert example.a == 3\nreturn example.b * 10 + example.c");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(fixture.execute("main").integerValue == 32);
    BOOST_TEST(fixture.execute("main").integerValue == 32);
}

BOOST_AUTO_TEST_CASE(runtime_calls_prevent_stale_initial_value_propagation)
{
    const auto outcome = runSource(
        "fun changeA\nexample.a = example.a + 1\nreturn example.a\nend\n"
        "hub example\na = 10\nb = changeA\nc = example.a\nd = 5\nend\n"
        "assert example.b == 11\nassert example.d == 5\nreturn example.c");
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.integerValue == 11);
}

BOOST_AUTO_TEST_CASE(runtime_dependent_values_have_safe_typed_defaults)
{
    const auto outcome = runSource(
        "fun readLater\nreturn example.later\nend\n"
        "fun calculate\nreturn 42\nend\n"
        "hub example\nfirst = readLater\nlater = calculate\nend\n"
        "assert example.later == 42\nreturn example.first");
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.integerValue == 0);
}

BOOST_AUTO_TEST_CASE(runtime_dependent_strings_have_initialized_empty_storage)
{
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "readLater",
        [](VirtualMachine* machine) {
            RuntimeValue value;
            auto* runtime = machine->getRuntime();
            runtime->getContextVariableValue(runtime->getContextVariableId("example.later"), value);
            BOOST_REQUIRE(value.getObjectValue() != nullptr);
            machine->setStringResult(value.getStringValue());
        },
        {}, runtime.getStringType(), ""
    ).isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "initialText", [](VirtualMachine* machine) { machine->setStringResult("ready"); },
        {}, runtime.getStringType(), ""
    ).isValid());
    fixture.addFunction("main",
        "hub example\nfirst = readLater\nlater = initialText\nend\n"
        "assert example.later == \"ready\"\nreturn example.first");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(fixture.execute("main").text.empty());
}

BOOST_AUTO_TEST_CASE(initializer_type_cycles_and_void_values_are_compile_errors)
{
    BOOST_TEST(!compileErrors(
        "hub example\na = initA\nend\nfun initA\nreturn example.a + 1\nend"
    ).empty());
    BOOST_TEST(!compileErrors("fun noValue\nend\nhub example\na = noValue\nend").empty());
}

BOOST_AUTO_TEST_CASE(hub_variable_reads_are_qualified_and_locals_still_shadow_them)
{
    const auto outcome = runSource(
        "hub example\na = 2\nfun read a:int\nreturn a + example.a\nend\nend\n"
        "return example.read 1");
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.integerValue == 3);
    BOOST_TEST(!compileErrors("hub example\na = 2\nfun read\nreturn a\nend\nend").empty());
    BOOST_TEST(!compileErrors("a = 1\nhub example\nb = a\nend").empty());
    BOOST_TEST(!compileErrors("a = 1\nhub example\nb = $\"{a}\"\nend").empty());
    BOOST_TEST(!compileErrors("a = 1\nhub example\nb = $\"{a + 1}\"\nend").empty());
}

BOOST_AUTO_TEST_CASE(initializer_format_strings_keep_their_declaring_hub)
{
    const auto outcome = runSource(
        "hub first\nconst title = \"first\"\nvalue = 10\n"
        "text = $\"{title}:{first.value + 1}\"\nend\n"
        "hub second\nconst title = \"second\"\nvalue = 20\n"
        "text = $\"{title}:{second.value + 1}\"\nend\n"
        "return $\"{first.text}/{second.text}\"");
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
    BOOST_TEST(outcome.text == "first:11/second:21");
}

BOOST_AUTO_TEST_CASE(simple_initializer_format_strings_do_not_share_different_hub_scopes)
{
    const auto outcome = runSource(
        "hub first\nconst title = \"first\"\ntext = $\"{title}\"\nend\n"
        "hub second\nconst title = \"second\"\ntext = $\"{title}\"\nend\n"
        "return $\"{first.text}/{second.text}\"");
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.text == "first/second");
}

BOOST_AUTO_TEST_CASE(duplicate_initializers_across_scripts_are_errors)
{
    LanguageTestFixture fixture;
    fixture.addFunction("a", "hub example\nx = 10\nend");
    fixture.addFunction("b", "hub example\ny = 11\nx = 10\nend");
    BOOST_TEST(!fixture.compile());
    BOOST_TEST(fixture.getDiagnostics().find("more than one initializer") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(initializer_diagnostics_keep_the_original_source)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{
        {"a", {"a.nomad", "hub first\nvalue = 10\nend"}},
        {"b", {"b.nomad", "hub second\nvalue = missingValue\nend"}}
    }, {}});
    BOOST_TEST(!compiler->compileScripts(&context));
    BOOST_REQUIRE(context.getFirstError() != nullptr);
    BOOST_TEST(context.getFirstError()->sourceName == "b.nomad");
    BOOST_TEST(context.getFirstError()->line == 2U);
}

BOOST_AUTO_TEST_CASE(embedding_initializes_before_executing_functions)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{
        {"main", {"main.nomad",
            "fun calculate\nreturn 42\nend\nhub example\nvalue = calculate\nend\n"
            "assert example.value == 42\nexample.value = example.value + 1"}}
    }, {}});
    BOOST_REQUIRE(compiler->compileScripts(&context));
    compiler.reset();
    runtime.initializeScripts();
    runtime.executeFunction(runtime.getFunctionId("main"));
    RuntimeValue value;
    runtime.getContextVariableValue(runtime.getContextVariableId("example.value"), value);
    BOOST_TEST(value.getIntegerValue() == 43);
}

BOOST_AUTO_TEST_SUITE_END()
