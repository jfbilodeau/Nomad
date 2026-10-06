// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <boost/test/unit_test.hpp>

#include <string>
#include <vector>

using namespace nomad;
using namespace nomad::test;

namespace {

FunctionOutcome runWithInteger(const std::string& source, NomadInteger argument) {
    LanguageTestFixture fixture;
    fixture.addFunction("main", source);

    if (!fixture.compile()) {
        FunctionOutcome outcome;
        outcome.diagnostics = fixture.getDiagnostics();

        return outcome;
    }

    return fixture.execute("main", {RuntimeValue(argument)});
}

} // namespace

BOOST_AUTO_TEST_SUITE(language_control_flow)

BOOST_AUTO_TEST_CASE(if_executes_body_only_when_true)
{
    const auto source =
        "params value:int\n"
        "result = 0\n"
        "if value > 10\n"
        "    result = 1\n"
        "end\n"
        "return result";

    BOOST_TEST(runWithInteger(source, 11).integerValue == 1);
    BOOST_TEST(runWithInteger(source, 10).integerValue == 0);
}

BOOST_AUTO_TEST_CASE(if_else)
{
    const auto source =
        "params value:int\n"
        "if value == 0\n"
        "    result = \"zero\"\n"
        "else\n"
        "    result = \"other\"\n"
        "end\n"
        "return result";

    const auto zero = runWithInteger(source, 0);
    BOOST_REQUIRE_MESSAGE(zero.compiled, zero.diagnostics);
    BOOST_TEST(zero.text == "zero");
    BOOST_TEST(runWithInteger(source, 5).text == "other");
}

BOOST_AUTO_TEST_CASE(else_if_chain_selects_first_matching_branch)
{
    const auto source =
        "params value:int\n"
        "if value < 0\n"
        "    result = 1\n"
        "else if value == 0\n"
        "    result = 2\n"
        "else if value < 10\n"
        "    result = 3\n"
        "else if value < 100\n"
        "    result = 4\n"
        "else\n"
        "    result = 5\n"
        "end\n"
        "return result";

    const std::vector<std::pair<NomadInteger, NomadInteger>> cases = {
        {-5, 1},
        {0, 2},
        {5, 3},
        {9, 3},
        {50, 4},
        {100, 5},
    };

    for (const auto& [argument, expected]: cases) {
        BOOST_TEST_CONTEXT("value = " << argument) {
            const auto outcome = runWithInteger(source, argument);

            BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
            BOOST_TEST(outcome.integerValue == expected);
        }
    }
}

BOOST_AUTO_TEST_CASE(else_if_without_else)
{
    const auto source =
        "params value:int\n"
        "result = 0\n"
        "if value == 1\n"
        "    result = 10\n"
        "else if value == 2\n"
        "    result = 20\n"
        "end\n"
        "return result";

    BOOST_TEST(runWithInteger(source, 1).integerValue == 10);
    BOOST_TEST(runWithInteger(source, 2).integerValue == 20);
    BOOST_TEST(runWithInteger(source, 3).integerValue == 0);
}

BOOST_AUTO_TEST_CASE(nested_if)
{
    const auto source =
        "params value:int\n"
        "result = 0\n"
        "if value > 0\n"
        "    if value > 10\n"
        "        result = 2\n"
        "    else\n"
        "        result = 1\n"
        "    end\n"
        "else\n"
        "    if value < -10\n"
        "        result = -2\n"
        "    end\n"
        "end\n"
        "return result";

    BOOST_TEST(runWithInteger(source, 20).integerValue == 2);
    BOOST_TEST(runWithInteger(source, 5).integerValue == 1);
    BOOST_TEST(runWithInteger(source, -5).integerValue == 0);
    BOOST_TEST(runWithInteger(source, -20).integerValue == -2);
}

BOOST_AUTO_TEST_CASE(empty_if_blocks_are_allowed)
{
    const auto outcome = runWithInteger(
        "params value:int\n"
        "if value > 0\n"
        "else\n"
        "end\n"
        "return value",
        3
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.integerValue == 3);
}

BOOST_AUTO_TEST_CASE(early_return_exits_function)
{
    const auto source =
        "params value:int\n"
        "if value > 0\n"
        "    return 1\n"
        "end\n"
        "return 0";

    BOOST_TEST(runWithInteger(source, 5).integerValue == 1);
    BOOST_TEST(runWithInteger(source, -5).integerValue == 0);
}

BOOST_AUTO_TEST_CASE(void_return_exits_function)
{
    LanguageTestFixture fixture;
    fixture.addFunction(
        "main",
        "params value:int\n"
        "global.reached = false\n"
        "if value > 0\n"
        "    return\n"
        "end\n"
        "global.reached = true"
    );
    fixture.addFunction("reached", "return global.reached");

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

    const auto skipped = fixture.execute("main", {RuntimeValue(NomadInteger{1})});
    BOOST_TEST(skipped.returnTypeName == "void");
    BOOST_TEST(fixture.execute("reached").booleanValue == false);

    BOOST_TEST(!fixture.execute("main", {RuntimeValue(NomadInteger{-1})}).fault.has_value());
    BOOST_TEST(fixture.execute("reached").booleanValue == true);
}

BOOST_AUTO_TEST_CASE(malformed_if_statements_are_rejected)
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"if true\n    a = 1", ""},
        {"if\n    a = 1\nend", ""},
        {"if true false\n    a = 1\nend", ""},
        {"if true\n    a = 1\nelse 1\n    a = 2\nend", ""},
        {"if true\nelse\nelse\nend", ""},
        {"if 1\nend", "must be a boolean"},
        {"if true\nelse if 1\nend", "must be a boolean"},
        {"else\nend", ""},
        {"end", ""},
    };

    for (const auto& [source, fragment]: cases) {
        BOOST_TEST_CONTEXT(source) {
            const auto errors = compileErrors(source);

            BOOST_TEST(!errors.empty());
            BOOST_TEST(errors.find(fragment) != std::string::npos, errors);
        }
    }
}

BOOST_AUTO_TEST_CASE(loops_are_not_supported)
{
    BOOST_TEST(!compileErrors("while true\nend").empty());
    BOOST_TEST(!compileErrors("for i = 1\nend").empty());
}

BOOST_AUTO_TEST_CASE(return_types_must_be_consistent)
{
    const std::vector<std::string> sources = {
        "params value:int\nif value > 0\n    return 1\nend\nreturn",
        "params value:int\nif value > 0\n    return\nend\nreturn 1",
        "params value:int\nif value > 0\n    return 1\nend\nreturn 1.0",
        "params value:int\nif value > 0\n    return \"a\"\nend\nreturn true",
    };

    for (const auto& source: sources) {
        BOOST_TEST_CONTEXT(source) {
            BOOST_TEST(!compileErrors(source).empty());
        }
    }
}

BOOST_AUTO_TEST_CASE(value_returning_functions_must_return_on_every_path)
{
    const std::vector<std::string> sources = {
        // The example from the language guide.
        "fun f x:int\n    if x > 10\n        return 1\n    end\nend\nreturn f 1",
        // Top-level function.
        "params value:int\nif value > 0\n    return 1\nend",
        // `if` without `else`, even when every branch returns.
        "params value:int\nif value > 0\n    return 1\nelse if value < 0\n    return -1\nend",
        // One branch of an `if`/`else` falls through.
        "params value:int\nif value > 0\n    return 1\nelse\n    value2 = 2\nend",
        "params value:int\nif value > 0\n    return 1\nelse if value < 0\n    x = 1\nelse\n    return 0\nend",
        // Nested `if` that falls through.
        "params value:int\nif value > 0\n    if value > 10\n        return 2\n    end\nelse\n    return 0\nend",
        // String return type.
        "fun f x:int\n    if x > 10\n        return \"big\"\n    end\nend\nreturn f 1",
    };

    for (const auto& source: sources) {
        BOOST_TEST_CONTEXT(source) {
            const auto errors = compileErrors(source);

            BOOST_TEST(errors.find("Missing `return`") != std::string::npos, errors);
        }
    }
}

BOOST_AUTO_TEST_CASE(missing_return_is_reported_at_the_end_of_the_function)
{
    const auto funErrors = compileErrors("fun f x:int\n    if x > 10\n        return 1\n    end\nend\nreturn f 1");
    BOOST_TEST(funErrors.find("main.nomad:5:") != std::string::npos, funErrors);
    BOOST_TEST(funErrors.find("'f'") != std::string::npos, funErrors);
    BOOST_TEST(funErrors.find("`int`") != std::string::npos, funErrors);

    const auto functionErrors = compileErrors("params value:int\nif value > 0\n    return 1\nend");
    BOOST_TEST(functionErrors.find("main.nomad:4:") != std::string::npos, functionErrors);
}

BOOST_AUTO_TEST_CASE(functions_returning_on_every_path_are_accepted)
{
    const std::vector<std::pair<std::string, NomadInteger>> cases = {
        {"params value:int\nif value > 0\n    return 1\nend\nreturn 0", 1},
        {"params value:int\nif value > 0\n    return 1\nelse\n    return 0\nend", 1},
        {"params value:int\nif value < 0\n    return -1\nelse if value == 0\n    return 0\nelse\n    return 1\nend", 1},
        {"params value:int\nif value > 0\n    if value > 10\n        return 2\n    else\n        return 1\n    end\nelse\n    return 0\nend", 1},
        {"params value:int\nfun f x:int\n    if x > 10\n        return 1\n    end\n    return 0\nend\nreturn f (value + 6)", 1},
        // An `if`/`else` that returns on every branch ends a `fun` without a final `return`.
        {"params value:int\nfun sign x:int\n    if x > 0\n        return 1\n    else\n        return 0\n    end\nend\nreturn sign value", 1},
    };

    for (const auto& [source, expected]: cases) {
        BOOST_TEST_CONTEXT(source) {
            const auto outcome = runWithInteger(source, 5);

            BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
            BOOST_TEST(outcome.integerValue == expected);
        }
    }
}

BOOST_AUTO_TEST_CASE(void_functions_may_fall_through)
{
    BOOST_TEST(compileErrors("params value:int\nif value > 0\n    return\nend\nx = 1").empty());
    BOOST_TEST(compileErrors("fun f x:int\n    if x > 10\n        return\n    end\nend\nf 1").empty());
}

BOOST_AUTO_TEST_CASE(unreachable_code_is_rejected)
{
    const std::vector<std::string> sources = {
        // The example from the language guide.
        "fun oops\n    return 1\n    x = 2\nend\nreturn oops",
        // Void `return`.
        "fun f\n    return\n    x = 2\nend\nf",
        "x = 1\nreturn x\nassert x == 1",
        // After an `if`/`else` that returns on every branch.
        "params value:int\nif value > 0\n    return 1\nelse\n    return 0\nend\nx = 1",
        // Inside a branch.
        "params value:int\nif value > 0\n    return 1\n    x = 2\nend\nreturn 0",
        "params value:int\nif value > 0\n    x = 1\nelse\n    return 0\n    x = 2\nend\nreturn 1",
        // An unreachable `if`.
        "params value:int\nreturn 1\nif value > 0\n    x = 1\nend",
    };

    for (const auto& source: sources) {
        BOOST_TEST_CONTEXT(source) {
            const auto errors = compileErrors(source);

            BOOST_TEST(errors.find("Unreachable code") != std::string::npos, errors);
        }
    }
}

BOOST_AUTO_TEST_CASE(unreachable_code_is_reported_at_the_first_unreachable_statement)
{
    const auto funErrors = compileErrors("fun oops\n    return 1\n    x = 2\n    x = 3\nend\nreturn oops");
    BOOST_TEST(funErrors.find("main.nomad:3:") != std::string::npos, funErrors);
    BOOST_TEST(funErrors.find("main.nomad:4:") == std::string::npos, funErrors);

    const auto ifErrors = compileErrors("params value:int\nreturn 1\nif value > 0\n    x = 1\nend");
    BOOST_TEST(ifErrors.find("main.nomad:3:") != std::string::npos, ifErrors);
}

BOOST_AUTO_TEST_CASE(declarations_after_return_are_reachable)
{
    const auto outcome = runWithInteger(
        "params value:int\n"
        "return twice value\n"
        "fun twice x:int\n"
        "    return x * 2\n"
        "end\n"
        "const limit = 10",
        4
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.integerValue == 8);
}

BOOST_AUTO_TEST_CASE(assert_passes_when_true)
{
    const auto outcome = runSource("score = 10\nassert score >= 0\nreturn score");

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 10);
}

BOOST_AUTO_TEST_CASE(assert_fails_when_false)
{
    const auto outcome = runSource("score = -1\nassert score >= 0\nreturn score");

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_REQUIRE(outcome.fault.has_value());
    BOOST_TEST(outcome.fault->find("Assertion failed") != std::string::npos, *outcome.fault);
    BOOST_TEST(outcome.fault->find("main") != std::string::npos, *outcome.fault);
    BOOST_TEST(outcome.fault->find("[2]") != std::string::npos, *outcome.fault);
    BOOST_TEST(outcome.fault->find("assert score >= 0") != std::string::npos, *outcome.fault);
}

BOOST_AUTO_TEST_CASE(assert_stops_execution)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main", "global.after = false\nassert false\nglobal.after = true");
    fixture.addFunction("after", "return global.after");

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(fixture.execute("main").fault.has_value());
    BOOST_TEST(fixture.execute("after").booleanValue == false);
}

BOOST_AUTO_TEST_CASE(assert_evaluates_runtime_values)
{
    const auto source = "params value:int\nassert value % 2 == 0\nreturn value";

    const auto even = runWithInteger(source, 4);
    BOOST_REQUIRE_MESSAGE(even.compiled, even.diagnostics);
    BOOST_TEST(!even.fault.has_value());

    const auto odd = runWithInteger(source, 3);
    BOOST_TEST(odd.fault.has_value());
}

BOOST_AUTO_TEST_CASE(assert_requires_boolean_expression)
{
    for (const auto* source: {"assert 1", "assert 1.0", "assert \"text\"", "assert", "assert missing"}) {
        BOOST_TEST_CONTEXT(source) {
            BOOST_TEST(!compileErrors(source).empty());
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
