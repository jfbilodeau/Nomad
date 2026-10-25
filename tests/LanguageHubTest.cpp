// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/script/Hub.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <type_traits>

using namespace nomad;
using namespace nomad::test;

static_assert(!std::is_convertible_v<NomadId, HubId>);
static_assert(!std::is_convertible_v<HubId, NomadId>);
static_assert(!std::is_convertible_v<CallableId, HubId>);
static_assert(!std::is_convertible_v<HubId, CallableId>);
static_assert(!std::is_invocable_v<decltype(&Runtime::getHub), Runtime&, CallableId>);
static_assert(std::is_same_v<std::variant_alternative_t<0, HubMember>, VariableId>);
static_assert(std::is_same_v<std::variant_alternative_t<1, HubMember>, HubFunctions>);
static_assert(std::is_same_v<std::variant_alternative_t<2, HubMember>, ConstantId>);
static_assert(std::is_same_v<std::variant_alternative_t<3, HubMember>, const Type*>);
static_assert(std::is_same_v<std::variant_alternative_t<4, HubMember>, EventId>);
static_assert(std::is_same_v<std::variant_alternative_t<5, HubMember>, DynamicVariableId>);
static_assert(std::is_same_v<std::variant_alternative_t<6, HubMember>, HubTypeFamily>);

BOOST_AUTO_TEST_SUITE(language_hubs)

BOOST_AUTO_TEST_CASE(declaration_modes_are_consistent_across_files_in_either_order)
{
    for (const auto& modes : {
        std::pair{"hub ", "hub "},
        std::pair{"hub ", "open hub "},
        std::pair{"open hub ", "hub "}
    }) {
        LanguageTestFixture fixture;
        fixture.addFunction("b", NomadString(modes.second) + "example\nsecond = 2\nend");
        fixture.addFunction("a", NomadString(modes.first) + "example\nfirst = 1\nend");
        BOOST_TEST(!fixture.compile());
        BOOST_TEST(fixture.hasError("Cannot declare hub 'example'"));
    }
    LanguageTestFixture fixture;
    fixture.addFunction("b", "open hub example\nsecond = 2\nend");
    fixture.addFunction("a", "open hub example\nfirst = 1\nend");
    fixture.addFunction("main", "example.first = 10\nreturn example.first + example.second");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(fixture.execute("main").integerValue == 12);
}

BOOST_AUTO_TEST_CASE(host_modes_control_script_extension_without_changing_mutability)
{
    for (const auto mode : {HubMode::Opened, HubMode::Closed}) {
        for (const auto* declaration : {"hub example", "open hub example"}) {
            LanguageTestFixture fixture;
            BOOST_REQUIRE(fixture.getRuntime().registerHub("example", mode).isValid());
            fixture.addFunction("main", NomadString(declaration) + "\nvalue = 42\nend");
            const auto allowed = mode == HubMode::Opened && NomadStringView(declaration).starts_with("open");
            BOOST_TEST(fixture.compile() == allowed);
        }
    }
    const auto outcome = runSource("hub example\nvalue = 1\nend\nexample.value = 42\nreturn example.value");
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.integerValue == 42);
    Runtime runtime;
    BOOST_TEST(runtime.registerHub("invalid", static_cast<HubMode>(0)).isInvalid());
    BOOST_TEST(!runtime.isHub("invalid"));
    BOOST_REQUIRE(runtime.registerHub("closed", HubMode::Closed).isValid());
    BOOST_TEST(runtime.registerHub("closed", HubMode::Closed).isInvalid());
    BOOST_TEST(runtime.registerHub("closed", HubMode::Opened).isInvalid());
}

BOOST_AUTO_TEST_CASE(dotted_and_nested_namespaces_have_independent_modes)
{
    for (const auto* source : {
        "hub game.audio\nvolume = 1\nend\nhub game\nvalue = 2\nend\nreturn game.audio.volume + game.value",
        "hub game\nvalue = 2\nhub audio\nvolume = 1\nend\nend\nreturn game.audio.volume + game.value"
    }) {
        LanguageTestFixture fixture;
        fixture.addFunction("main", source);
        BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
        BOOST_TEST(fixture.execute("main").integerValue == 3);
        const auto& runtime = fixture.getRuntime();
        BOOST_TEST(runtime.getHub(runtime.getHubId("game"))->getMember("audio") == nullptr);
    }
    LanguageTestFixture fixture;
    fixture.addFunction("main", "hub game.audio\nvolume = 1\nend\nreturn game.audio.volume");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(!fixture.getRuntime().isHub("game"));
    BOOST_TEST(fixture.execute("main").integerValue == 1);
    BOOST_TEST(!compileErrors(
        "open hub game\nhub audio\nend\nend\nhub game.audio\nend"
    ).empty());
}

BOOST_AUTO_TEST_CASE(hub_references_require_qualification_in_every_context)
{
    for (const auto* source : {
        "hub game\nconst value = 1\ncopy = value\nend",
        "hub game\nconst value = 1\nfun read\nreturn value\nend\nend",
        "hub game\nfun first\nreturn 1\nend\nfun second\nreturn first\nend\nend",
        "hub game\nenum State\nReady\nend\nfun read value:State\nreturn value\nend\nend",
        "hub game\nconst value = 1\nhub audio\ncopy = value\nend\nend",
        "open hub game\nend\ngame.missing = 1",
        "fun read\nopen hub game\nend\nend",
        "if true\nopen hub game\nend\nend"
    }) {
        BOOST_TEST_CONTEXT(source) {
            BOOST_TEST(!compileErrors(source).empty());
        }
    }
    const auto outcome = runSource(
        "argument = 10\nhub game\nconst value = 1\ncopy = game.value\n"
        "fun read value:int\nlocal = value\nreturn local + game.copy\nend\nend\n"
        "return game.read argument"
    );
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.integerValue == 11);
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);
    BOOST_TEST(!Interpreter{}.execute("open hub game\nend", context));
    BOOST_TEST(!runtime.isHub("game"));
}

BOOST_AUTO_TEST_CASE(open_is_reserved_and_requires_a_supported_declaration)
{
    Runtime runtime;
    BOOST_TEST(runtime.getKeywordId("open").isValid());
    auto compiler = runtime.createCompiler();
    ParseStatementFn parser;
    StatementAvailability availability;
    BOOST_REQUIRE(compiler->getParseStatementFn("hub", parser, availability));
    BOOST_TEST(parser != nullptr);
    BOOST_TEST((availability == StatementAvailability::Compiler));
    BOOST_REQUIRE(compiler->getParseStatementFn("open", parser, availability));
    BOOST_TEST(parser != nullptr);
    BOOST_TEST((availability == StatementAvailability::Compiler));
    PreParseStatementFn preParser;
    for (const auto* name : {"hub", "open"}) {
        BOOST_REQUIRE(compiler->getPreParseStatementFn(name, preParser));
        BOOST_TEST(preParser != nullptr);
    }
    for (const auto* source : {
        "open = 10", "const open = 10", "fun open\nend", "fun read open:int\nend",
        "hub example\nconst open = 10\nend",
        "open", "open example\nend", "open hub", "open hub example trailing\nend"
    }) {
        BOOST_TEST_CONTEXT(source) {
            BOOST_TEST(!compileErrors(source).empty());
        }
    }
}

BOOST_AUTO_TEST_CASE(hub_ids_support_independent_namespaces_and_catalog_growth)
{
    Runtime runtime;
    BOOST_TEST(runtime.getHubId("missing") == HubId::INVALID);
    BOOST_TEST(runtime.getHub(HubId::INVALID) == nullptr);
    BOOST_TEST(runtime.getHub(HubId{-2}) == nullptr);
    BOOST_TEST(runtime.getHub(HubId{NOMAD_ID_MAX}) == nullptr);
    BOOST_TEST(runtime.getHub(HubId{999999}) == nullptr);

    const auto registeredId = runtime.registerHub("example.child", HubMode::Opened);
    BOOST_REQUIRE(registeredId.isValid());
    BOOST_TEST(runtime.getHubId("example") == HubId::INVALID);
    BOOST_REQUIRE(runtime.registerHub("example", HubMode::Closed).isValid());
    const auto parentId = runtime.getHubId("example");
    const auto childId = runtime.getHubId("example.child");
    BOOST_TEST(registeredId == childId);
    BOOST_REQUIRE(parentId.isValid());
    BOOST_REQUIRE(childId.isValid());
    BOOST_TEST(parentId != childId);
    const auto* parent = runtime.getHub(parentId);
    const auto* child = runtime.getHub(childId);
    BOOST_REQUIRE(parent != nullptr);
    BOOST_REQUIRE(child != nullptr);
    BOOST_TEST((parent->getMode() == HubMode::Closed));
    BOOST_TEST((child->getMode() == HubMode::Opened));

    BOOST_TEST(runtime.registerHub("example.child", HubMode::Opened) == childId);
    for (auto index = 0; index < 64; ++index) {
        BOOST_REQUIRE(runtime.registerHub("additional" + std::to_string(index), HubMode::Opened).isValid());
    }

    BOOST_TEST(runtime.getHubId("example") == parentId);
    BOOST_TEST(runtime.getHubId("example.child") == childId);
    BOOST_TEST(runtime.getHub(parentId) == parent);
    BOOST_TEST(runtime.getHub(childId) == child);
    BOOST_TEST(parent->getMember("child") == nullptr);
}

BOOST_AUTO_TEST_CASE(native_registration_is_idempotent_and_reserves_names)
{
    Runtime runtime;
    BOOST_REQUIRE(runtime.registerHub("example", HubMode::Opened).isValid());
    BOOST_REQUIRE(runtime.registerHub("example", HubMode::Opened).isValid());
    BOOST_REQUIRE(runtime.registerHub("example.inner", HubMode::Opened).isValid());
    BOOST_TEST(runtime.isHub("example.inner"));
    BOOST_TEST(runtime.registerConstant("example", RuntimeValue(NomadInteger{1}), runtime.getIntegerType()).isInvalid());
    BOOST_REQUIRE(runtime.registerConstant("occupied", RuntimeValue(NomadInteger{1}), runtime.getIntegerType()).isValid());
    BOOST_TEST(runtime.registerHub("occupied", HubMode::Opened) == HubId::INVALID);
    BOOST_TEST(!runtime.isHub("occupied"));
    BOOST_TEST(runtime.registerHub("", HubMode::Opened) == HubId::INVALID);
    BOOST_TEST(runtime.registerHub(".invalid", HubMode::Opened) == HubId::INVALID);
    BOOST_TEST(runtime.registerHub("invalid.", HubMode::Opened) == HubId::INVALID);
    BOOST_TEST(runtime.registerHub("invalid..name", HubMode::Opened) == HubId::INVALID);
    BOOST_TEST(!runtime.isHub("invalid"));
}

BOOST_AUTO_TEST_CASE(native_hub_names_are_not_restricted_to_script_identifiers)
{
    Runtime runtime;
    for (const auto* name : {"fun", "123", "with spaces", "with-hyphens", "9 host.child-name"}) {
        const auto id = runtime.registerHub(name, HubMode::Opened);
        BOOST_REQUIRE(id.isValid());
        BOOST_TEST(runtime.registerHub(name, HubMode::Opened) == id);
        BOOST_TEST(runtime.getHubId(name) == id);
        const auto* hub = runtime.getHub(id);
        BOOST_REQUIRE(hub != nullptr);
        BOOST_TEST(hub->getName() == name);
    }
    const auto parentId = runtime.getHubId("9 host");
    BOOST_TEST(parentId == HubId::INVALID);
    const auto childId = runtime.getHubId("9 host.child-name");
    BOOST_REQUIRE(childId.isValid());
    BOOST_TEST((runtime.getHub(childId)->getMode() == HubMode::Opened));
}

BOOST_AUTO_TEST_CASE(script_hub_names_require_identifier_tokens)
{
    for (const auto* name : {"123", "example.123", "example.1child", "with spaces", "with-hyphens"}) {
        BOOST_TEST(!compileErrors(NomadString("hub ") + name + "\nend").empty());
    }
    for (const auto* name : {"fun", "example.fun", "reservedword", "example.reservedword", "example._child2"}) {
        LanguageTestFixture fixture;
        auto& runtime = fixture.getRuntime();
        BOOST_REQUIRE(runtime.registerKeyword("reservedword", NomadDoc("Test keyword.")).isValid());
        BOOST_REQUIRE(runtime.registerHub(name, HubMode::Opened).isValid());
        fixture.addFunction("main", NomadString("open hub ") + name + "\nend");
        BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    }
}

BOOST_AUTO_TEST_CASE(hub_qualified_dynamic_variables_remain_readable_and_writable)
{
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    NomadFloat volume = 0.25f;

    BOOST_REQUIRE(runtime.registerHub("game", HubMode::Opened).isValid());
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
    ).isValid());

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

    BOOST_REQUIRE(runtime.registerHub("game", HubMode::Opened).isValid());
    BOOST_REQUIRE(runtime.registerDynamicVariable(
        "game.frame",
        nullptr,
        [](VirtualMachine*, RuntimeValue& value) {
            value.setIntegerValue(42);
        },
        runtime.getIntegerType(),
        NomadDoc("Test read-only game frame.")
    ).isValid());

    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);

    BOOST_TEST(Interpreter{}.execute("game.frame = 10", context) == false);
    BOOST_TEST(context.hasError());
}

BOOST_AUTO_TEST_CASE(hub_qualified_unknown_members_remain_undeclarable_by_assignment)
{
    LanguageTestFixture fixture;
    BOOST_REQUIRE(fixture.getRuntime().registerHub("game", HubMode::Opened).isValid());

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
    fixture.addFunction("a", "open hub example\nfun first\nreturn example.second + example.value\nend\nend");
    fixture.addFunction("b", "open hub example\nvalue = 20\nfun second\nreturn 22\nend\nend");
    fixture.addFunction("main", "return example.first");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(fixture.execute("main").integerValue == 42);
}

BOOST_AUTO_TEST_CASE(nested_hubs_only_prefix_declaration_names)
{
    const auto outcome = runSource(
        "const globalValue = 1\n"
        "hub example\n"
        "const parentValue = 2\n"
        "open hub inner\n"
        "const localValue = 3\n"
        "fun calculate\nreturn example.inner.localValue + example.parentValue + globalValue\nend\n"
        "end\nend\n"
        "open hub example.inner\nfun read\nreturn example.inner.calculate\nend\nend\n"
        "return example.inner.read");
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.integerValue == 6);
}

BOOST_AUTO_TEST_CASE(hub_enums_and_formatted_expressions_resolve)
{
    const auto outcome = runSource(
        "hub example\n"
        "enum State\nReady\nBusy\nend\n"
        "state = example.State.Ready\n"
        "name = \"test\"\n"
        "fun describe value:example.State\n"
        "if value == example.State.Ready\nreturn $\"{example.name}:{example.state == example.State.Ready}\"\nend\n"
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
    BOOST_REQUIRE(fixture.getRuntime().registerHub("example", HubMode::Opened).isValid());
    BOOST_REQUIRE(fixture.getRuntime().registerConstant(
        "example.nativeValue", RuntimeValue(NomadInteger{40}), fixture.getRuntime().getIntegerType()
    ).isValid());
    fixture.addFunction("main", "open hub example\nextra = 2\nend\nreturn example.nativeValue + example.extra");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(fixture.execute("main").integerValue == 42);
}

BOOST_AUTO_TEST_CASE(hub_objects_index_members_without_duplicating_callable_ownership)
{
    Runtime runtime;
    BOOST_REQUIRE(runtime.registerHub("example", HubMode::Opened).isValid());
    const auto id = runtime.getHubId("example");
    const auto* hub = runtime.getHub(id);
    BOOST_REQUIRE(hub != nullptr);
    BOOST_REQUIRE(runtime.registerHub("example", HubMode::Opened).isValid());
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
    BOOST_REQUIRE(runtime.createEnum("example.State", {{"Ready", 1}}).isValid());
    BOOST_REQUIRE(hub->getMember("State") != nullptr);
    BOOST_TEST(std::get<const Type*>(*hub->getMember("State")) == runtime.getTypeByName("example.State"));
    BOOST_REQUIRE(hub->getMember("State.Ready") != nullptr);
    BOOST_TEST(std::holds_alternative<ConstantId>(*hub->getMember("State.Ready")));
    BOOST_REQUIRE(runtime.registerEvent("example.changed", {}).isValid());
    BOOST_REQUIRE(hub->getMember("changed") != nullptr);
    BOOST_TEST(std::holds_alternative<EventId>(*hub->getMember("changed")));
    BOOST_REQUIRE(runtime.registerHub("example.child", HubMode::Opened).isValid());
    const auto childId = runtime.getHubId("example.child");
    BOOST_REQUIRE(runtime.getHub(childId) != nullptr);
    BOOST_TEST(hub->getMember("child") == nullptr);
}

BOOST_AUTO_TEST_CASE(explicit_hubs_adopt_existing_qualified_native_registrations)
{
    Runtime runtime;
    const auto constantId = runtime.registerConstant(
        "example.child.value", RuntimeValue(NomadInteger{42}), runtime.getIntegerType()
    );
    BOOST_REQUIRE(constantId.isValid());
    BOOST_REQUIRE(runtime.registerHub("example", HubMode::Opened).isValid());
    const auto* parent = runtime.getHub(runtime.getHubId("example"));
    BOOST_REQUIRE(parent->getMember("child.value") != nullptr);
    BOOST_REQUIRE(runtime.registerHub("example.child", HubMode::Opened).isValid());
    BOOST_TEST(parent->getMember("child.value") == nullptr);
    const auto* child = runtime.getHub(runtime.getHubId("example.child"));
    BOOST_REQUIRE(child->getMember("value") != nullptr);
    BOOST_TEST(std::get<ConstantId>(*child->getMember("value")) == constantId);
}

BOOST_AUTO_TEST_CASE(rebuilding_hubs_preserves_native_member_values_and_overloads)
{
    Runtime runtime;
    const auto constantId = runtime.registerConstant(
        "example.child.answer", RuntimeValue(NomadInteger{42}), runtime.getIntegerType()
    );
    const auto typeId = runtime.createEnum("example.child.State", {{"Ready", 1}});
    BOOST_REQUIRE(typeId.isValid());
    const auto* type = runtime.getType(typeId).value();
    const auto eventId = runtime.registerEvent("example.child.changed", {});
    const auto dynamicVariableId = runtime.registerDynamicVariable(
        "example.child.current", nullptr,
        [](VirtualMachine*, RuntimeValue& value) { value.setIntegerValue(42); },
        runtime.getIntegerType(), ""
    );
    const auto functionId = runtime.registerNativeFunction(
        "example.child.read", [](VirtualMachine* machine) { machine->setIntegerResult(42); },
        {}, runtime.getIntegerType(), ""
    );
    const auto overloadId = runtime.registerNativeFunction(
        "example.child.read",
        [](VirtualMachine* machine) { machine->setIntegerResult(machine->getIntegerParameter(0)); },
        {defParameter("value", runtime.getIntegerType(), "")}, runtime.getIntegerType(), ""
    );
    BOOST_REQUIRE(constantId.isValid());
    BOOST_REQUIRE(type != nullptr);
    BOOST_REQUIRE(eventId.isValid());
    BOOST_REQUIRE(dynamicVariableId.isValid());
    BOOST_REQUIRE(functionId.isValid());
    BOOST_REQUIRE(overloadId.isValid());
    BOOST_REQUIRE(runtime.registerParameterizedType(
        "example.child.Box", 1, [](Runtime* owningRuntime, TypeId id, const std::vector<const Type*>& arguments) {
            return std::make_unique<ObjectType>(owningRuntime, id, "example.child.Box<" + arguments[0]->getTypeName() + ">", ObjectTypeMode::Closed, arguments
            );
        }
    ));

    const auto parentId = runtime.registerHub("example", HubMode::Opened);
    BOOST_REQUIRE(parentId.isValid());
    const auto* parent = runtime.getHub(parentId);
    const auto verifyMembers = [&](const Hub* hub, const NomadString& prefix) {
        const auto requireMember = [&](const NomadString& name) {
            const auto* member = hub->getMember(prefix + name);
            BOOST_REQUIRE(member != nullptr);
            return member;
        };
        BOOST_TEST(std::get<ConstantId>(*requireMember("answer")) == constantId);
        BOOST_TEST(std::get<const Type*>(*requireMember("State")) == type);
        BOOST_TEST(std::get<EventId>(*requireMember("changed")) == eventId);
        BOOST_TEST(std::get<DynamicVariableId>(*requireMember("current")) == dynamicVariableId);
        BOOST_TEST(std::get<HubTypeFamily>(*requireMember("Box")).argumentCount == 1U);
        const auto& functions = std::get<HubFunctions>(*requireMember("read")).ids;
        BOOST_REQUIRE(functions.size() == 2U);
        BOOST_TEST(functions[0] == functionId);
        BOOST_TEST(functions[1] == overloadId);
    };
    verifyMembers(parent, "child.");

    const auto childId = runtime.registerHub("example.child", HubMode::Opened);
    BOOST_REQUIRE(childId.isValid());
    BOOST_TEST(parent->getMember("child.answer") == nullptr);
    BOOST_TEST(parent->getMember("child") == nullptr);
    const auto* child = runtime.getHub(childId);
    verifyMembers(child, "");

    BOOST_TEST(runtime.registerHub("example.child.answer", HubMode::Opened) == HubId::INVALID);
    BOOST_TEST(!runtime.isHub("example.child.answer"));
    BOOST_TEST(!runtime.isHub("example.child.answer.nested"));
    BOOST_TEST(runtime.getHub(parentId) == parent);
    BOOST_TEST(runtime.getHub(childId) == child);
    verifyMembers(child, "");
    BOOST_TEST(runtime.registerHub("example.child", HubMode::Opened) == childId);
    BOOST_REQUIRE(runtime.registerHub("unrelated", HubMode::Opened).isValid());
    verifyMembers(child, "");
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
    BOOST_TEST(std::holds_alternative<VariableId>(*hub->getMember("x")));
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
        "open hub first\nx = 10\nend\n"
        "hub second\ny = first.x + 1\nend\n"
        "open hub first\nz = second.y + 1\nend\n"
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
    fixture.addFunction("b", "open hub example\nb = initVar\na = 1\nend");
    fixture.addFunction("a",
        "fun initVar\nexample.a = example.a + 1\nreturn example.a\nend\n"
        "open hub example\nc = initVar\nend");
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
        "text = $\"{first.title}:{first.value + 1}\"\nend\n"
        "hub second\nconst title = \"second\"\nvalue = 20\n"
        "text = $\"{second.title}:{second.value + 1}\"\nend\n"
        "return $\"{first.text}/{second.text}\"");
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
    BOOST_TEST(outcome.text == "first:11/second:21");
}

BOOST_AUTO_TEST_CASE(simple_initializer_format_strings_do_not_share_different_hub_scopes)
{
    const auto outcome = runSource(
        "hub first\nconst title = \"first\"\ntext = $\"{first.title}\"\nend\n"
        "hub second\nconst title = \"second\"\ntext = $\"{second.title}\"\nend\n"
        "return $\"{first.text}/{second.text}\"");
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.text == "first/second");
}

BOOST_AUTO_TEST_CASE(duplicate_initializers_across_scripts_are_errors)
{
    LanguageTestFixture fixture;
    fixture.addFunction("a", "open hub example\nx = 10\nend");
    fixture.addFunction("b", "open hub example\ny = 11\nx = 10\nend");
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
