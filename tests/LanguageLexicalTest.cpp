// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace nomad;
using namespace nomad::test;

namespace {

struct IntegerCase {
    std::string source;
    NomadInteger expected;
};

struct StringCase {
    std::string source;
    std::string expected;
};

} // namespace

BOOST_AUTO_TEST_SUITE(language_lexical)

BOOST_AUTO_TEST_CASE(comments_are_ignored)
{
    const std::vector<IntegerCase> cases = {
        {"# Leading comment\nreturn 1", 1},
        {"return 2 # Trailing comment", 2},
        {"a = 3 # Comment after assignment\n# Full line\n\nreturn a", 3},
        {"return 4 #", 4},
        {"#comment without space\nreturn 5", 5},
    };

    for (const auto& testCase: cases) {
        BOOST_TEST_CONTEXT(testCase.source) {
            const auto outcome = runSource(testCase.source);

            BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
            BOOST_TEST(outcome.integerValue == testCase.expected);
        }
    }
}

BOOST_AUTO_TEST_CASE(comment_marker_inside_string_is_text)
{
    const auto outcome = runExpression("\"a # b\"");

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.text == "a # b");
}

BOOST_AUTO_TEST_CASE(empty_and_comment_only_functions_return_void)
{
    for (const auto* source: {"", "# only a comment", "\n\n\n", "   \n\t\n"}) {
        BOOST_TEST_CONTEXT("source: '" << source << "'") {
            const auto outcome = runSource(source);

            BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
            BOOST_TEST(outcome.returnTypeName == "void");
            BOOST_TEST(!outcome.fault.has_value());
        }
    }
}

BOOST_AUTO_TEST_CASE(windows_line_endings_are_accepted)
{
    const auto outcome = runSource("a = 20\r\nb = 22\r\nreturn a + b\r\n");

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.integerValue == 42);
}

BOOST_AUTO_TEST_CASE(integer_literals)
{
    const std::vector<IntegerCase> cases = {
        {"0", 0},
        {"7", 7},
        {"42", 42},
        {"-42", -42},
        {"0x10", 16},
        {"0xff", 255},
        {"0xFF", 255},
        {"2147483647", 2147483647},
        {"2147483648", 2147483648LL},
        {"9223372036854775807", std::numeric_limits<NomadInteger>::max()},
        {"-9223372036854775807", -std::numeric_limits<NomadInteger>::max()},
    };

    for (const auto& testCase: cases) {
        BOOST_TEST_CONTEXT(testCase.source) {
            const auto outcome = runExpression(testCase.source);

            BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
            BOOST_TEST(outcome.returnTypeName == "int");
            BOOST_TEST(outcome.integerValue == testCase.expected);
        }
    }
}

BOOST_AUTO_TEST_CASE(integer_literal_out_of_range_is_rejected)
{
    BOOST_TEST(!compileErrors("return 9223372036854775808").empty());
}

BOOST_AUTO_TEST_CASE(binary_operators_do_not_require_spaces)
{
    const std::vector<IntegerCase> cases = {
        {"5+3", 8},
        {"5*3", 15},
        {"a=5\nb=1\nreturn a-b", 4},
        {"a=5\nreturn a+1", 6},
        {"return (5)-(3)", 2},
        {"return 5 - -3", 8},
    };

    for (const auto& testCase: cases) {
        BOOST_TEST_CONTEXT(testCase.source) {
            const auto source = testCase.source.find("return") == std::string::npos
                ? "return " + testCase.source
                : testCase.source;
            const auto outcome = runSource(source);

            BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
            BOOST_TEST(outcome.integerValue == testCase.expected);
        }
    }
}

BOOST_AUTO_TEST_CASE(minus_followed_by_digit_is_a_negative_literal)
{
    // `-` directly followed by a digit belongs to the literal so nativeFunction arguments like `cmd 0.0 -1.0` work.
    for (const auto* source: {"return 5-3", "a = 5\nreturn a -1", "return 5.0-3.0"}) {
        BOOST_TEST_CONTEXT(source) {
            BOOST_TEST(!compileErrors(source).empty());
        }
    }

    const auto outcome = runExpression("-3");
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.integerValue == -3);
}

BOOST_AUTO_TEST_CASE(float_literals)
{
    const std::vector<std::pair<std::string, NomadFloat>> cases = {
        {"0.5", 0.5f},
        {"10.0", 10.0f},
        {"0.0", 0.0f},
        {"-1.25", -1.25f},
        {"123.456", 123.456f},
    };

    for (const auto& [source, expected]: cases) {
        BOOST_TEST_CONTEXT(source) {
            const auto outcome = runExpression(source);

            BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
            BOOST_TEST(outcome.returnTypeName == "float");
            BOOST_TEST(outcome.floatValue == expected);
        }
    }
}

BOOST_AUTO_TEST_CASE(boolean_literals)
{
    const auto trueOutcome = runExpression("true");
    const auto falseOutcome = runExpression("false");

    BOOST_REQUIRE_MESSAGE(trueOutcome.compiled, trueOutcome.diagnostics);
    BOOST_REQUIRE_MESSAGE(falseOutcome.compiled, falseOutcome.diagnostics);
    BOOST_TEST(trueOutcome.returnTypeName == "bool");
    BOOST_TEST(trueOutcome.booleanValue == true);
    BOOST_TEST(falseOutcome.booleanValue == false);
}

BOOST_AUTO_TEST_CASE(string_literals_and_escape_sequences)
{
    const std::vector<StringCase> cases = {
        {R"("hello")", "hello"},
        {R"("")", ""},
        {R"("with spaces  ")", "with spaces  "},
        {R"("Line1\nLine2")", "Line1\nLine2"},
        {R"("Col1\tCol2")", "Col1\tCol2"},
        {R"("CR\r")", "CR\r"},
        {R"("quote \" inside")", "quote \" inside"},
        {R"("back\\slash")", "back\\slash"},
    };

    for (const auto& testCase: cases) {
        BOOST_TEST_CONTEXT(testCase.source) {
            const auto outcome = runExpression(testCase.source);

            BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
            BOOST_TEST(outcome.returnTypeName == "string");
            BOOST_TEST(outcome.text == testCase.expected);
        }
    }
}

BOOST_AUTO_TEST_CASE(malformed_tokens_are_rejected)
{
    const std::vector<std::string> sources = {
        "return 1 @ 2",
        "return \"unterminated",
        "a = 1 ~ 2",
        "return 1 ; 2",
        "return [1]",
    };

    for (const auto& source: sources) {
        BOOST_TEST_CONTEXT(source) {
            BOOST_TEST(!compileErrors(source).empty());
        }
    }
}

BOOST_AUTO_TEST_CASE(unrecognized_token_reports_location)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main", "a = 1\nreturn a @ 2");

    BOOST_REQUIRE(!fixture.compile());

    const auto* error = fixture.findError("Unrecognized token");
    BOOST_REQUIRE_MESSAGE(error != nullptr, fixture.getDiagnostics());
    BOOST_TEST(error->line == 2u);
    BOOST_TEST(error->column == 10u);
}

BOOST_AUTO_TEST_CASE(valid_identifiers)
{
    const std::vector<std::string> identifiers = {
        "player",
        "_player",
        "score_1",
        "camelCase",
        "PascalCase",
        "x",
        "_",
        "a1b2c3",
    };

    for (const auto& identifier: identifiers) {
        BOOST_TEST_CONTEXT(identifier) {
            const auto outcome = runSource(identifier + " = 7\nreturn " + identifier);

            BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
            BOOST_TEST(outcome.integerValue == 7);
        }
    }
}

BOOST_AUTO_TEST_CASE(identifiers_are_case_sensitive)
{
    const auto errors = compileErrors("score = 1\nreturn Score");

    BOOST_TEST(errors.find("Unknown identifier") != std::string::npos, errors);

    const auto outcome = runSource("score = 1\nScore = 2\nreturn score");
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.integerValue == 1);
}

BOOST_AUTO_TEST_CASE(invalid_identifiers_are_rejected)
{
    const std::vector<std::string> sources = {
        "1abc = 1",
        "value. = 1",
        "a-b = 1",
    };

    for (const auto& source: sources) {
        BOOST_TEST_CONTEXT(source) {
            BOOST_TEST(!compileErrors(source).empty());
        }
    }
}

BOOST_AUTO_TEST_CASE(reserved_words_cannot_be_assigned)
{
    const std::vector<std::string> words = {
        "assert", "const", "else", "end", "event", "false", "fun", "if", "params", "pi", "return", "true",
    };

    for (const auto& word: words) {
        BOOST_TEST_CONTEXT(word) {
            BOOST_TEST(!compileErrors(word + " = 1").empty());
        }
    }
}

BOOST_AUTO_TEST_CASE(unknown_statement_is_rejected)
{
    const auto errors = compileErrors("doesNotExist 1 2");

    BOOST_TEST(errors.find("doesNotExist") != std::string::npos, errors);
}

BOOST_AUTO_TEST_SUITE_END()
