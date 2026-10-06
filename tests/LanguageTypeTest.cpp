// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Type.hpp>

#include <boost/test/unit_test.hpp>

#include <cmath>
#include <numbers>
#include <string>
#include <vector>

using namespace nomad;
using namespace nomad::test;

namespace {

struct TypeCase {
    std::string source;
    std::string typeName;
    std::string text;
};

} // namespace

BOOST_AUTO_TEST_SUITE(language_types)

BOOST_AUTO_TEST_CASE(variable_type_is_inferred_from_first_assignment)
{
    const std::vector<TypeCase> cases = {
        {"value = 10\nreturn value", "int", "10"},
        {"ratio = 0.5\nreturn ratio", "float", ""},
        {"text = \"hello\"\nreturn text", "string", "hello"},
        {"flag = false\nreturn flag", "bool", ""},
        {"flag = 1 < 2\nreturn flag", "bool", ""},
        {"copy = 3\nother = copy\nreturn other", "int", "3"},
    };

    for (const auto& testCase: cases) {
        BOOST_TEST_CONTEXT(testCase.source) {
            const auto outcome = runSource(testCase.source);

            BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
            BOOST_TEST(outcome.returnTypeName == testCase.typeName);

            if (!testCase.text.empty()) {
                BOOST_TEST(outcome.text == testCase.text);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(reassignment_with_same_type_is_allowed)
{
    const auto outcome = runSource("value = 1\nvalue = 2\nvalue = value + 3\nreturn value");

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.integerValue == 5);
}

BOOST_AUTO_TEST_CASE(compiled_variables_cannot_change_type)
{
    const std::vector<std::string> sources = {
        "value = 10\nvalue = 1.0",
        "value = 10\nvalue = \"text\"",
        "value = 10\nvalue = true",
        "value = 1.0\nvalue = 1",
        "value = \"text\"\nvalue = 1",
        "value = true\nvalue = 0",
        "global.typed = 10\nglobal.typed = \"text\"",
    };

    for (const auto& source: sources) {
        BOOST_TEST_CONTEXT(source) {
            const auto errors = compileErrors(source);

            BOOST_TEST(!errors.empty());
        }
    }
}

BOOST_AUTO_TEST_CASE(unknown_identifier_is_rejected)
{
    const auto errors = compileErrors("return missing");

    BOOST_TEST(errors.find("Unknown identifier") != std::string::npos, errors);
}

BOOST_AUTO_TEST_CASE(implicit_conversions_are_rejected)
{
    const std::vector<std::string> expressions = {
        "1.2 + 3",
        "3 + 1.2",
        "10 > \"20\"",
        "true == 1",
        "1 == 1.0",
        "\"a\" + 1",
        "true + 1",
    };

    for (const auto& expression: expressions) {
        BOOST_TEST_CONTEXT(expression) {
            BOOST_TEST(!compileErrors("return " + expression).empty());
        }
    }
}

BOOST_AUTO_TEST_CASE(if_condition_is_not_converted_to_bool)
{
    for (const auto* condition: {"1", "0", "1.0", "\"true\""}) {
        BOOST_TEST_CONTEXT(condition) {
            const auto errors = compileErrors(std::string("if ") + condition + "\n    a = 1\nend");

            BOOST_TEST(errors.find("must be a boolean") != std::string::npos, errors);
        }
    }
}

BOOST_AUTO_TEST_CASE(conversion_nativeFunctions)
{
    const auto toFloat = runExpression("toFloat 10");
    BOOST_REQUIRE_MESSAGE(toFloat.compiled, toFloat.diagnostics);
    BOOST_TEST(toFloat.returnTypeName == "float");
    BOOST_TEST(toFloat.floatValue == 10.0f);

    const auto toInt = runExpression("toInt 13.2");
    BOOST_REQUIRE_MESSAGE(toInt.compiled, toInt.diagnostics);
    BOOST_TEST(toInt.returnTypeName == "int");
    BOOST_TEST(toInt.integerValue == 13);

    const auto toBool = runSource("x = 1\nf = x == 1\nreturn f");
    BOOST_REQUIRE_MESSAGE(toBool.compiled, toBool.diagnostics);
    BOOST_TEST(toBool.returnTypeName == "bool");
    BOOST_TEST(toBool.booleanValue == true);

    const auto toText = runSource("score = 10\nreturn $\"{score}\"");
    BOOST_REQUIRE_MESSAGE(toText.compiled, toText.diagnostics);
    BOOST_TEST(toText.returnTypeName == "string");
    BOOST_TEST(toText.text == "10");
}

BOOST_AUTO_TEST_CASE(conversion_nativeFunctions_reject_wrong_argument_types)
{
    BOOST_TEST(!compileErrors("return toFloat 1.0").empty());
    BOOST_TEST(!compileErrors("return toInt 1").empty());
    BOOST_TEST(!compileErrors("return toInt true").empty());
}

BOOST_AUTO_TEST_CASE(conversion_nativeFunctions_are_overloaded_on_argument_type)
{
    const auto fromFloat = runSource("return toInt 1.9");
    BOOST_REQUIRE_MESSAGE(fromFloat.compiled, fromFloat.diagnostics);
    BOOST_TEST(fromFloat.returnTypeName == "int");
    BOOST_TEST(fromFloat.integerValue == 1);

    const auto fromString = runSource("return toInt \"42\"");
    BOOST_REQUIRE_MESSAGE(fromString.compiled, fromString.diagnostics);
    BOOST_TEST(fromString.returnTypeName == "int");
    BOOST_TEST(fromString.integerValue == 42);

    const auto floatFromString = runSource("return toFloat \"2.5\"");
    BOOST_REQUIRE_MESSAGE(floatFromString.compiled, floatFromString.diagnostics);
    BOOST_TEST(floatFromString.returnTypeName == "float");
    BOOST_TEST(floatFromString.floatValue == NomadFloat{2.5});
}

BOOST_AUTO_TEST_CASE(pi_is_predefined)
{
    const auto outcome = runExpression("pi");

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.returnTypeName == "float");
    BOOST_TEST(outcome.floatValue == std::numbers::pi_v<NomadFloat>);
}

BOOST_AUTO_TEST_CASE(constants_of_each_type)
{
    const std::vector<TypeCase> cases = {
        {"const MAX_SCORE = 1000\nreturn MAX_SCORE", "int", "1000"},
        {"const HALF = 0.5\nreturn HALF", "float", ""},
        {"const NAME = \"nomad\"\nreturn NAME", "string", "nomad"},
        {"const ENABLED = true\nreturn ENABLED", "bool", ""},
        {"const HEX = 0x20\nreturn HEX", "int", "32"},
    };

    for (const auto& testCase: cases) {
        BOOST_TEST_CONTEXT(testCase.source) {
            const auto outcome = runSource(testCase.source);

            BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
            BOOST_TEST(outcome.returnTypeName == testCase.typeName);

            if (!testCase.text.empty()) {
                BOOST_TEST(outcome.text == testCase.text);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(constant_expressions_follow_operator_rules)
{
    const std::vector<std::pair<std::string, NomadInteger>> cases = {
        {"2 + 3 * 4", 14},
        {"10 - 2 - 3", 5},
        {"100 / 10 / 2", 5},
        {"(1 + 2) * 3", 9},
        {"-5", -5},
        {"17 % 5", 2},
        {"6 ^ 3", 5},
        {"6 & 3", 2},
        {"6 | 3", 7},
        {"OTHER + 1", 42},
    };

    for (const auto& [expression, expected]: cases) {
        BOOST_TEST_CONTEXT(expression) {
            const auto outcome = runSource("const OTHER = 41\nconst VALUE = " + expression + "\nreturn VALUE");

            BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
            BOOST_TEST(outcome.integerValue == expected);
        }
    }
}

BOOST_AUTO_TEST_CASE(constants_are_globally_visible)
{
    LanguageTestFixture fixture;
    fixture.addFunction("constants", "const SHARED = 12");
    fixture.addFunction("user", "return SHARED + 1");

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

    const auto outcome = fixture.execute("user");
    BOOST_TEST(outcome.integerValue == 13);
}

BOOST_AUTO_TEST_CASE(invalid_constants_are_rejected)
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"const A = 1\nconst A = 2", "already defined"},
        {"const A = 1\nA = 2", ""},
        {"const pi = 3.0", ""},
        {"const A = missing", "Unknown constant"},
        {"const A =", ""},
        {"const = 1", ""},
        {"const A = 1 + 1.0", ""},
        {"x = 1\nconst A = x", ""},
    };

    for (const auto& [source, fragment]: cases) {
        BOOST_TEST_CONTEXT(source) {
            const auto errors = compileErrors(source);

            BOOST_TEST(!errors.empty());
            BOOST_TEST(errors.find(fragment) != std::string::npos, errors);
        }
    }
}

BOOST_AUTO_TEST_CASE(context_variables_keep_type_across_functions)
{
    LanguageTestFixture fixture;
    fixture.addFunction("writer", "global.score = 25");
    fixture.addFunction("reader", "return global.score + 1");

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

    BOOST_TEST(!fixture.execute("writer").fault.has_value());

    const auto outcome = fixture.execute("reader");
    BOOST_TEST(outcome.returnTypeName == "int");
    BOOST_TEST(outcome.integerValue == 26);
}

BOOST_AUTO_TEST_CASE(context_variables_reject_conflicting_types_across_functions)
{
    LanguageTestFixture fixture;
    fixture.addFunction("first", "global.shared = 1");
    fixture.addFunction("second", "global.shared = \"text\"");

    BOOST_TEST(!fixture.compile());
}

BOOST_AUTO_TEST_CASE(interpreter_locals_can_change_type)
{
    Runtime runtime;
    Interpreter interpreter(&runtime);

    BOOST_REQUIRE(interpreter.execute("value = 10"));
    BOOST_TEST(interpreter.getVariableType("value") == runtime.getIntegerType());

    BOOST_REQUIRE_MESSAGE(
        interpreter.execute("value = \"text\""),
        (interpreter.getError() ? *interpreter.getError() : NomadString{})
    );
    BOOST_TEST(interpreter.getVariableType("value") == runtime.getStringType());
    BOOST_TEST(NomadString(interpreter.getVariableValue("value")->getStringValue()) == "text");

    BOOST_REQUIRE(interpreter.execute("value = 1.5"));
    BOOST_TEST(interpreter.getVariableType("value") == runtime.getFloatType());
}

BOOST_AUTO_TEST_CASE(interpreter_context_variables_keep_their_type)
{
    Runtime runtime;
    Interpreter interpreter(&runtime);

    BOOST_REQUIRE(interpreter.execute("global.interpreterTyped = 10"));
    BOOST_TEST(!interpreter.execute("global.interpreterTyped = \"text\""));
    BOOST_TEST(interpreter.hasError());
}

BOOST_AUTO_TEST_SUITE_END()
