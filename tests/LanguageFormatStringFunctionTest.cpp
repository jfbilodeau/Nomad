// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/script/NativeFunction.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <sstream>
#include <string>
#include <vector>

using namespace nomad;
using namespace nomad::test;

namespace {

// Registers `test.add a:int b:int -> int`, `test.echo text:string -> string`, `test.record value:int` and `test.fail`.
void registerTestNativeFunctions(Runtime& runtime, std::vector<NomadInteger>& recorded) {
    runtime.registerNativeFunction(
        "test.add",
        [](VirtualMachine* virtualMachine) {
            virtualMachine->setIntegerResult(
                virtualMachine->getIntegerParameter(0) + virtualMachine->getIntegerParameter(1)
            );
        },
        {
            defParameter("a", runtime.getIntegerType(), NomadParamDoc("First value")),
            defParameter("b", runtime.getIntegerType(), NomadParamDoc("Second value")),
        },
        runtime.getIntegerType(),
        NomadDoc("Add two integers")
    );

    runtime.registerNativeFunction(
        "test.echo",
        [](VirtualMachine* virtualMachine) {
            virtualMachine->setStringResult(virtualMachine->getStringParameter(0));
        },
        {
            defParameter("text", runtime.getStringRefType(), NomadParamDoc("Text to return")),
        },
        runtime.getStringType(),
        NomadDoc("Return the provided string")
    );

    runtime.registerNativeFunction(
        "test.record",
        [&recorded](VirtualMachine* virtualMachine) {
            recorded.push_back(virtualMachine->getIntegerParameter(0));
        },
        {
            defParameter("value", runtime.getIntegerType(), NomadParamDoc("Value to record")),
        },
        runtime.getVoidType(),
        NomadDoc("Record an integer")
    );

    runtime.registerNativeFunction(
        "test.fail",
        [](VirtualMachine* virtualMachine) {
            virtualMachine->fault("test.fail called");
        },
        {},
        runtime.getVoidType(),
        NomadDoc("Fault the virtual machine")
    );
}

} // namespace

BOOST_AUTO_TEST_SUITE(language_format_strings)

BOOST_AUTO_TEST_CASE(format_string_substitutes_variables)
{
    const auto outcome = runSource(
        "name = \"J-F\"\n"
        "score = 10\n"
        "return $\"Player {name} got {score} points\""
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.returnTypeName == "string");
    BOOST_TEST(outcome.text == "Player J-F got 10 points");
}

BOOST_AUTO_TEST_CASE(format_string_forms)
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"return $\"\"", ""},
        {"return $\"no placeholders\"", "no placeholders"},
        {"a = 1\nreturn $\"{a}\"", "1"},
        {"a = 1\nb = 2\nreturn $\"{a}{b}\"", "12"},
        {"a = -3\nreturn $\"[{a}]\"", "[-3]"},
        {"a = 1\nreturn $\"{a} and {a}\"", "1 and 1"},
        {"flag = true\nreturn $\"{flag}\"", "true"},
        {"text = \"inner\"\nreturn $\"<{text}>\"", "<inner>"},
        {"return $\"line\\nbreak\"", "line\nbreak"},
        {"global.formatValue = 5\nreturn $\"{global.formatValue}\"", "5"},
        {"const LIMIT = 9\nreturn $\"{LIMIT}\"", "9"},
    };

    for (const auto& [source, expected]: cases) {
        BOOST_TEST_CONTEXT(source) {
            const auto outcome = runSource(source);

            BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
            BOOST_TEST(outcome.text == expected);
        }
    }
}

BOOST_AUTO_TEST_CASE(format_string_uses_current_values)
{
    const auto outcome = runSource(
        "fun describe value:int\n"
        "    return $\"value={value}\"\n"
        "end\n"
        "first = describe 1\n"
        "second = describe 2\n"
        "return $\"{first},{second}\""
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.text == "value=1,value=2");
}

BOOST_AUTO_TEST_CASE(format_string_calls_parameterless_functions)
{
    const auto outcome = runSource(
        "fun answer\n"
        "    return 42\n"
        "end\n"
        "fun label\n"
        "    return \"x\"\n"
        "end\n"
        "return $\"{label}={answer}\""
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.text == "x=42");
}

BOOST_AUTO_TEST_CASE(format_string_function_preserves_caller_state)
{
    const auto outcome = runSource(
        "fun answer\n"
        "    value = 40 + 2\n"
        "    return value\n"
        "end\n"
        "before = 7\n"
        "text = $\"{answer}\"\n"
        "after = before + 1\n"
        "return $\"{before}:{text}:{after}\""
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.text == "7:42:8");
}

BOOST_AUTO_TEST_CASE(format_string_rejects_unknown_identifiers)
{
    const auto errors = compileErrors("return $\"{missing}\"");

    BOOST_TEST(errors.find("missing") != std::string::npos, errors);
}

BOOST_AUTO_TEST_CASE(format_string_rejects_functions_with_parameters)
{
    const auto errors = compileErrors("fun double value:int\n    return value * 2\nend\nreturn $\"{double}\"");

    BOOST_TEST(errors.find("double") != std::string::npos, errors);
}

BOOST_AUTO_TEST_CASE(format_string_rejects_malformed_placeholders)
{
    for (const auto* source: {"a = 1\nreturn $\"{a\"", "return $\"{}\"", "return $\"unterminated"}) {
        BOOST_TEST_CONTEXT(source) {
            BOOST_TEST(!compileErrors(source).empty());
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(language_nativeFunctions)

BOOST_AUTO_TEST_CASE(nativeFunction_returns_value)
{
    std::vector<NomadInteger> recorded;
    LanguageTestFixture fixture;
    registerTestNativeFunctions(fixture.getRuntime(), recorded);
    fixture.addFunction("main", "return test.add 40 2");

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

    const auto outcome = fixture.execute("main");
    BOOST_TEST(outcome.returnTypeName == "int");
    BOOST_TEST(outcome.integerValue == 42);
}

BOOST_AUTO_TEST_CASE(nativeFunction_arguments_are_expressions)
{
    std::vector<NomadInteger> recorded;
    LanguageTestFixture fixture;
    registerTestNativeFunctions(fixture.getRuntime(), recorded);
    fixture.addFunction(
        "main",
        "a = 3\n"
        "test.record (a * 2)\n"
        "test.record a\n"
        "test.record (test.add a 4)\n"
        "return test.echo $\"a={a}\""
    );

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

    const auto outcome = fixture.execute("main");
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.text == "a=3");
    BOOST_TEST(recorded == (std::vector<NomadInteger>{6, 3, 7}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(nativeFunction_string_results_are_owned_values)
{
    std::vector<NomadInteger> recorded;
    LanguageTestFixture fixture;
    registerTestNativeFunctions(fixture.getRuntime(), recorded);
    fixture.addFunction(
        "main",
        "fun wrap text:string\n"
        "    return $\"[{text}]\"\n"
        "end\n"
        "name = \"n\"\n"
        "nested = test.echo (test.echo (test.echo \"deep\"))\n"
        "wrapped = wrap (test.echo \"w\")\n"
        "formatted = test.echo $\"f={name}\"\n"
        "same = (test.echo \"x\") == (test.echo \"x\")\n"
        "different = (test.echo \"x\") != $\"{name}\"\n"
        "if same && different\n"
        "    return $\"{nested} {wrapped} {formatted}\"\n"
        "end\n"
        "return \"comparison failed\""
    );

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

    const auto outcome = fixture.execute("main");
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.text == "deep [w] f=n");
}

BOOST_AUTO_TEST_CASE(nativeFunctions_execute_in_order)
{
    std::vector<NomadInteger> recorded;
    LanguageTestFixture fixture;
    registerTestNativeFunctions(fixture.getRuntime(), recorded);
    fixture.addFunction("main", "test.record 1\ntest.record 2\ntest.record 3");

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(!fixture.execute("main").fault.has_value());
    BOOST_TEST(recorded == (std::vector<NomadInteger>{1, 2, 3}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(calls_with_more_than_three_arguments_pop_the_stack)
{
    std::vector<NomadInteger> recorded;
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    registerTestNativeFunctions(runtime, recorded);
    runtime.registerNativeFunction(
        "test.digits",
        [](VirtualMachine* virtualMachine) {
            virtualMachine->setIntegerResult(
                virtualMachine->getIntegerParameter(0) * 1000 + virtualMachine->getIntegerParameter(1) * 100
                + virtualMachine->getIntegerParameter(2) * 10 + virtualMachine->getIntegerParameter(3)
            );
        },
        {
            defParameter("a", runtime.getIntegerType(), NomadParamDoc("Thousands")),
            defParameter("b", runtime.getIntegerType(), NomadParamDoc("Hundreds")),
            defParameter("c", runtime.getIntegerType(), NomadParamDoc("Tens")),
            defParameter("d", runtime.getIntegerType(), NomadParamDoc("Units")),
        },
        runtime.getIntegerType(),
        NomadDoc("Combine four digits")
    );

    // Both calls pop four arguments with `op_pop_n`; a wrong count would corrupt `local`.
    fixture.addFunction(
        "main",
        "fun digits a:int b:int c:int d:int\n"
        "    return a * 1000 + b * 100 + c * 10 + d\n"
        "end\n"
        "local = 5\n"
        "first = test.digits 1 2 3 4\n"
        "second = digits 4 3 2 1\n"
        "return first + second + local"
    );

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

    const auto outcome = fixture.execute("main");
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 1234 + 4321 + 5);

    std::ostringstream disassembly;
    runtime.dumpInstructions(disassembly);
    const auto text = disassembly.str();

    std::size_t popCount = 0;
    for (auto position = text.find("op_pop_n\n"); position != std::string::npos;
         position = text.find("op_pop_n\n", position + 1)) {
        const auto operandStart = text.find(": ", position) + 2;
        BOOST_TEST(text.substr(operandStart, text.find('\n', operandStart) - operandStart) == "4");
        ++popCount;
    }
    BOOST_TEST(popCount == 2u);
}

BOOST_AUTO_TEST_CASE(nativeFunction_fault_stops_function)
{
    std::vector<NomadInteger> recorded;
    LanguageTestFixture fixture;
    registerTestNativeFunctions(fixture.getRuntime(), recorded);
    fixture.addFunction("main", "test.record 1\ntest.fail\ntest.record 2");

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

    const auto outcome = fixture.execute("main");
    BOOST_REQUIRE(outcome.fault.has_value());
    BOOST_TEST(*outcome.fault == "test.fail called");
    BOOST_TEST(recorded == (std::vector<NomadInteger>{1}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(invalid_nativeFunction_calls_are_rejected)
{
    const std::vector<std::string> sources = {
        "test.add 1",
        "test.add 1 2 3",
        "test.add 1.0 2",
        "test.add \"1\" 2",
        "test.echo 1",
        "value = test.record 1",
        "test.unknown 1",
        "test.add = 1",
    };

    for (const auto& source: sources) {
        BOOST_TEST_CONTEXT(source) {
            std::vector<NomadInteger> recorded;
            LanguageTestFixture fixture;
            registerTestNativeFunctions(fixture.getRuntime(), recorded);
            fixture.addFunction("main", source);

            BOOST_TEST(!fixture.compile());
            BOOST_TEST(fixture.getErrorCount() > 0u);
        }
    }
}

BOOST_AUTO_TEST_CASE(predefined_nativeFunctions_are_available)
{
    const auto floor = runExpression("math.floor 2.75");
    BOOST_REQUIRE_MESSAGE(floor.compiled, floor.diagnostics);
    BOOST_TEST(floor.floatValue == 2.0f);

    const auto log = runSource("log.info \"language test\"");
    BOOST_REQUIRE_MESSAGE(log.compiled, log.diagnostics);
    BOOST_TEST(!log.fault.has_value());
}

BOOST_AUTO_TEST_SUITE_END()
