// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <boost/test/unit_test.hpp>

#include <string>
#include <vector>

using namespace nomad;
using namespace nomad::test;

BOOST_AUTO_TEST_SUITE(language_functions)

BOOST_AUTO_TEST_CASE(function_with_parameters_returns_value)
{
    const auto outcome = runSource(
        "fun add v1:int v2:int\n"
        "    return v1 + v2\n"
        "end\n"
        "return add 10 20"
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.returnTypeName == "int");
    BOOST_TEST(outcome.integerValue == 30);
}

BOOST_AUTO_TEST_CASE(function_can_be_called_before_its_declaration)
{
    const auto outcome = runSource(
        "total = twice 21\n"
        "return total\n"
        "fun twice value:int\n"
        "    return value * 2\n"
        "end"
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.integerValue == 42);
}

BOOST_AUTO_TEST_CASE(function_parameters_of_each_type)
{
    const auto outcome = runSource(
        "fun describe count:int ratio:float flag:bool name:string\n"
        "    if flag\n"
        "        return $\"{name}:{count}\"\n"
        "    end\n"
        "    return name\n"
        "end\n"
        "return describe 3 0.5 true \"item\""
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.text == "item:3");
}

BOOST_AUTO_TEST_CASE(function_arguments_accept_expressions)
{
    const auto outcome = runSource(
        "fun add v1:int v2:int\n"
        "    return v1 + v2\n"
        "end\n"
        "a = 4\n"
        "return add (a * 2) (a - 1)"
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.integerValue == 11);
}

BOOST_AUTO_TEST_CASE(function_call_result_in_expression)
{
    const auto outcome = runSource(
        "fun three\n"
        "    return 3\n"
        "end\n"
        "return three * 2 + three"
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.integerValue == 9);
}

BOOST_AUTO_TEST_CASE(nested_function_calls)
{
    const auto outcome = runSource(
        "fun add v1:int v2:int\n"
        "    return v1 + v2\n"
        "end\n"
        "fun square value:int\n"
        "    return value * value\n"
        "end\n"
        "return square (add 1 2)"
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.integerValue == 9);
}

BOOST_AUTO_TEST_CASE(void_functions)
{
    LanguageTestFixture fixture;
    fixture.addFunction(
        "main",
        "fun noReturn\n"
        "    global.first = 1\n"
        "end\n"
        "fun emptyReturn\n"
        "    global.second = 2\n"
        "    if global.second == 2\n"
        "        return\n"
        "    end\n"
        "    global.second = 3\n"
        "end\n"
        "noReturn\n"
        "emptyReturn\n"
        "return global.first + global.second"
    );

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

    const auto outcome = fixture.execute("main");
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 3);

    BOOST_TEST(fixture.getRuntime().getFunction(fixture.getRuntime().getFunctionId("noReturn"))->getReturnType()->isVoid());
}

BOOST_AUTO_TEST_CASE(void_function_cannot_be_used_as_value)
{
    const auto errors = compileErrors(
        "fun noReturn\n"
        "    a = 1\n"
        "end\n"
        "value = noReturn"
    );

    BOOST_TEST(!errors.empty());
}

BOOST_AUTO_TEST_CASE(function_variables_are_local)
{
    const auto errors = compileErrors(
        "fun setLocal\n"
        "    localValue = 1\n"
        "end\n"
        "setLocal\n"
        "return localValue"
    );

    BOOST_TEST(errors.find("Unknown identifier") != std::string::npos, errors);

    const auto callerErrors = compileErrors(
        "callerValue = 1\n"
        "fun readCaller\n"
        "    return callerValue\n"
        "end"
    );

    BOOST_TEST(!callerErrors.empty());
}

BOOST_AUTO_TEST_CASE(same_local_name_in_different_functions)
{
    const auto outcome = runSource(
        "fun first\n"
        "    value = 1\n"
        "    return value\n"
        "end\n"
        "fun second\n"
        "    value = \"text\"\n"
        "    return value\n"
        "end\n"
        "value = 2.5\n"
        "return $\"{first} {second}\""
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.text == "1 text");
}

BOOST_AUTO_TEST_CASE(recursion)
{
    const auto outcome = runSource(
        "fun countDown value:int\n"
        "    global.calls = global.calls + 1\n"
        "    if value > 0\n"
        "        countDown (value - 1)\n"
        "    end\n"
        "end\n"
        "global.calls = 0\n"
        "countDown 5\n"
        "return global.calls"
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 6);
}

BOOST_AUTO_TEST_CASE(recursive_function_with_return_value)
{
    const auto outcome = runSource(
        "fun factorial value:int\n"
        "    if value <= 1\n"
        "        return 1\n"
        "    end\n"
        "    return value * (factorial (value - 1))\n"
        "end\n"
        "return factorial 5"
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.integerValue == 120);
}

BOOST_AUTO_TEST_CASE(function_parameters)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main", "params count:int ratio:float flag:bool name:string\nreturn $\"{name} {count} {flag}\"");

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

    RuntimeValue name(NomadString("n"));

    const auto outcome = fixture.execute(
        "main",
        {RuntimeValue(NomadInteger{4}), RuntimeValue(NomadFloat{0.5f}), RuntimeValue(true), name}
    );

    // The virtual machine borrows its arguments; the caller keeps ownership.
    name.freeStringValue();

    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.text.find("n 4") == 0u, outcome.text);
}

BOOST_AUTO_TEST_CASE(functions_call_other_functions)
{
    LanguageTestFixture fixture;
    fixture.addFunction("helper", "params value:int\nreturn value + 1");
    fixture.addFunction("main", "return helper 41");

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(fixture.execute("main").integerValue == 42);
}

BOOST_AUTO_TEST_CASE(dotted_function_names_can_be_called)
{
    LanguageTestFixture fixture;
    fixture.addFunction("entities.player.speed", "return 7");
    fixture.addFunction("main", "return entities.player.speed * 2");

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(fixture.execute("main").integerValue == 14);
}

BOOST_AUTO_TEST_CASE(functions_are_callable_from_other_functions)
{
    LanguageTestFixture fixture;
    fixture.addFunction("library", "fun triple value:int\n    return value * 3\nend");
    fixture.addFunction("main", "return triple 5");

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(fixture.execute("main").integerValue == 15);
}

BOOST_AUTO_TEST_CASE(invalid_calls_are_rejected)
{
    const auto prefix =
        "fun add v1:int v2:int\n"
        "    return v1 + v2\n"
        "end\n";

    const std::vector<std::string> calls = {
        "return add 1",
        "return add",
        "return add 1 2 3",
        "return add 1 2.0",
        "return add \"1\" 2",
        "return add true 2",
        "value = 1.0\nvalue = add 1 2",
    };

    for (const auto& call: calls) {
        BOOST_TEST_CONTEXT(call) {
            BOOST_TEST(!compileErrors(prefix + call).empty());
        }
    }
}

BOOST_AUTO_TEST_CASE(invalid_declarations_are_rejected)
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"fun\nend", ""},
        {"fun missingEnd\n    a = 1", ""},
        {"fun dup a:int a:int\nend", "already defined"},
        {"fun badType a:unknownType\nend", "Unknown type"},
        {"fun missingType a\nend", ""},
        {"fun missingType a:\nend", ""},
        {"fun same\nend\nfun same\nend", ""},
        {"fun toInt\nend", "already used"},
        {"fun if\nend", ""},
        {"const NAME = 1\nfun NAME\nend", "already used"},
        {"params a:int\nparams b:int", "already has parameters"},
        {"params a:int a:int", "already defined"},
        {"params a:unknownType", "Unknown type"},
        {"params a:int\na = 1.0", ""},
    };

    for (const auto& [source, fragment]: cases) {
        BOOST_TEST_CONTEXT(source) {
            const auto errors = compileErrors(source);

            BOOST_TEST(!errors.empty());
            BOOST_TEST(errors.find(fragment) != std::string::npos, errors);
        }
    }
}

BOOST_AUTO_TEST_CASE(function_name_cannot_shadow_function)
{
    LanguageTestFixture fixture;
    fixture.addFunction("helper", "return 1");
    fixture.addFunction("main", "fun helper\nend");

    BOOST_TEST(!fixture.compile());
    BOOST_TEST(fixture.hasError("already used"), fixture.getDiagnostics());
}

BOOST_AUTO_TEST_CASE(function_return_type_must_be_consistent)
{
    const auto errors = compileErrors(
        "fun mixed value:int\n"
        "    if value > 0\n"
        "        return 1\n"
        "    end\n"
        "    return\n"
        "end"
    );

    BOOST_TEST(!errors.empty());
}

BOOST_AUTO_TEST_SUITE_END()
