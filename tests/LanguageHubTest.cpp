// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

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

BOOST_AUTO_TEST_CASE(variables_initialize_before_any_entry_and_persist)
{
    LanguageTestFixture fixture;
    fixture.addFunction("declarations",
        "hub example\n"
        "    x = 10\n"
        "    name = \"test\"\n"
        "    fun increment\n"
        "        example.x = x + 1\n"
        "        return x\n"
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
    fixture.addFunction("a", "hub example\nfun first\nreturn second + value\nend\nend");
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
        "if value == State.Ready\nreturn $\"{name}:{state == State.Ready}\"\nend\n"
        "return \"busy\"\nend\nend\n"
        "return example.describe example.State.Ready");
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.text == "test:true");
}

BOOST_AUTO_TEST_CASE(initializers_execute_once_in_declaration_order)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main",
        "hub example\nfirst = 10\nsecond = first + 2\nend\n"
        "example.first = example.first + 1\n"
        "return example.first + example.second");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(fixture.execute("main").integerValue == 23);
    BOOST_TEST(fixture.execute("main").integerValue == 24);
}

BOOST_AUTO_TEST_CASE(uninitialized_dependencies_fault_instead_of_reading_defaults)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main", "hub example\nfirst = second\nsecond = 10\nend\nreturn example.first");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto outcome = fixture.execute("main");
    BOOST_REQUIRE(outcome.fault.has_value());
    BOOST_TEST(outcome.fault->find("read before initialization") != NomadString::npos);
    BOOST_TEST(fixture.execute("main").fault.has_value());
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
    BOOST_TEST(!compileErrors("hub example\nx = 1\nfun update\nx = 2\nend\nend").empty());
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

BOOST_AUTO_TEST_SUITE_END()
