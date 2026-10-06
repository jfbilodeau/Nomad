// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/script/OpCode.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <cmath>
#include <string>
#include <vector>

using namespace nomad;
using namespace nomad::test;

namespace {

struct IntegerBinaryCase {
    NomadInteger lhs;
    std::string op;
    NomadInteger rhs;
    NomadInteger expected;
};

struct FloatBinaryCase {
    NomadFloat lhs;
    std::string op;
    NomadFloat rhs;
    NomadFloat expected;
};

struct ComparisonCase {
    std::string lhs;
    std::string op;
    std::string rhs;
    bool expected;
};

struct PrecedenceCase {
    std::string expression;
    std::string expected;
};

std::string literal(NomadInteger value) {
    return std::to_string(value);
}

std::string literal(NomadFloat value) {
    auto text = std::to_string(value);

    return text;
}

// Executes `lhs op rhs` with operands passed as function parameters so the compiler cannot fold the expression.
FunctionOutcome runWithParameters(
    const std::string& typeName,
    const std::string& op,
    const RuntimeValue& lhs,
    const RuntimeValue& rhs
) {
    LanguageTestFixture fixture;
    fixture.addFunction("main", "params lhs:" + typeName + " rhs:" + typeName + "\nreturn lhs " + op + " rhs");

    if (!fixture.compile()) {
        FunctionOutcome outcome;
        outcome.diagnostics = fixture.getDiagnostics();

        return outcome;
    }

    return fixture.execute("main", {lhs, rhs});
}

FunctionOutcome runUnaryWithParameter(const std::string& typeName, const std::string& op, const RuntimeValue& operand) {
    LanguageTestFixture fixture;
    fixture.addFunction("main", "params operand:" + typeName + "\nreturn " + op + " operand");

    if (!fixture.compile()) {
        FunctionOutcome outcome;
        outcome.diagnostics = fixture.getDiagnostics();

        return outcome;
    }

    return fixture.execute("main", {operand});
}

bool closeTo(NomadFloat actual, NomadFloat expected) {
    return std::fabs(actual - expected) <= 1e-5f * std::fmax(1.0f, std::fabs(expected));
}

} // namespace

BOOST_AUTO_TEST_SUITE(language_operators)

BOOST_AUTO_TEST_CASE(integer_binary_operators)
{
    const std::vector<IntegerBinaryCase> cases = {
        {7, "+", 3, 10},
        {7, "-", 3, 4},
        {3, "-", 7, -4},
        {7, "*", 3, 21},
        {-7, "*", 3, -21},
        {7, "/", 3, 2},
        {-7, "/", 2, -3},
        {17, "%", 5, 2},
        {-7, "%", 3, -1},
        {6, "&", 3, 2},
        {6, "|", 3, 7},
        {6, "^", 3, 5},
        {5, "^", 5, 0},
        {0x7FFFFFFFFFFFFFFFLL, "&", 0xFF, 0xFF},
        {1LL << 40, "+", 1, (1LL << 40) + 1},
    };

    for (const auto& testCase: cases) {
        const auto expression = "(" + literal(testCase.lhs) + ") " + testCase.op + " (" + literal(testCase.rhs) + ")";

        BOOST_TEST_CONTEXT("folded: " << expression) {
            const auto outcome = runExpression(expression);

            BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
            BOOST_TEST(outcome.returnTypeName == "int");
            BOOST_TEST(outcome.integerValue == testCase.expected);
        }

        BOOST_TEST_CONTEXT("runtime: " << expression) {
            const auto outcome = runWithParameters(
                "int",
                testCase.op,
                RuntimeValue(testCase.lhs),
                RuntimeValue(testCase.rhs)
            );

            BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
            BOOST_TEST(!outcome.fault.has_value());
            BOOST_TEST(outcome.integerValue == testCase.expected);
        }
    }
}

BOOST_AUTO_TEST_CASE(float_binary_operators)
{
    const std::vector<FloatBinaryCase> cases = {
        {7.5f, "+", 2.5f, 10.0f},
        {7.5f, "-", 2.5f, 5.0f},
        {2.5f, "-", 7.5f, -5.0f},
        {7.5f, "*", 2.5f, 18.75f},
        {7.5f, "/", 2.5f, 3.0f},
        {1.0f, "/", 4.0f, 0.25f},
    };

    for (const auto& testCase: cases) {
        const auto expression = "(" + literal(testCase.lhs) + ") " + testCase.op + " (" + literal(testCase.rhs) + ")";

        BOOST_TEST_CONTEXT("folded: " << expression) {
            const auto outcome = runExpression(expression);

            BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
            BOOST_TEST(outcome.returnTypeName == "float");
            BOOST_TEST(closeTo(outcome.floatValue, testCase.expected), outcome.floatValue << " != " << testCase.expected);
        }

        BOOST_TEST_CONTEXT("runtime: " << expression) {
            const auto outcome = runWithParameters(
                "float",
                testCase.op,
                RuntimeValue(testCase.lhs),
                RuntimeValue(testCase.rhs)
            );

            BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
            BOOST_TEST(closeTo(outcome.floatValue, testCase.expected), outcome.floatValue << " != " << testCase.expected);
        }
    }
}

BOOST_AUTO_TEST_CASE(comparison_operators)
{
    const std::vector<ComparisonCase> cases = {
        {"3", "<", "7", true},
        {"7", "<", "3", false},
        {"7", "<", "7", false},
        {"3", "<=", "7", true},
        {"7", "<=", "7", true},
        {"8", "<=", "7", false},
        {"7", ">", "3", true},
        {"3", ">", "7", false},
        {"7", ">", "7", false},
        {"7", ">=", "3", true},
        {"7", ">=", "7", true},
        {"6", ">=", "7", false},
        {"7", "==", "7", true},
        {"7", "==", "3", false},
        {"7", "!=", "3", true},
        {"7", "!=", "7", false},
        {"-1", "<", "0", true},
        {"1.5", "<", "2.5", true},
        {"2.5", "<", "1.5", false},
        {"2.5", "<=", "2.5", true},
        {"2.5", ">", "1.5", true},
        {"1.5", ">=", "2.5", false},
        {"2.5", "==", "2.5", true},
        {"2.5", "!=", "2.5", false},
        {"true", "==", "true", true},
        {"true", "==", "false", false},
        {"true", "!=", "false", true},
        {"false", "!=", "false", false},
        {"\"abc\"", "==", "\"abc\"", true},
        {"\"abc\"", "==", "\"abd\"", false},
        {"\"abc\"", "==", "\"ab\"", false},
        {"\"\"", "==", "\"\"", true},
        {"\"abc\"", "!=", "\"abd\"", true},
        {"\"abc\"", "!=", "\"abc\"", false},
        {"\"Abc\"", "!=", "\"abc\"", true},
    };

    for (const auto& testCase: cases) {
        const auto expression = testCase.lhs + " " + testCase.op + " " + testCase.rhs;

        BOOST_TEST_CONTEXT(expression) {
            const auto folded = runExpression(expression);

            BOOST_REQUIRE_MESSAGE(folded.compiled, folded.diagnostics);
            BOOST_TEST(folded.returnTypeName == "bool");
            BOOST_TEST(folded.booleanValue == testCase.expected);

            const auto runtime = runSource(
                "lhs = " + testCase.lhs + "\nrhs = " + testCase.rhs + "\nreturn lhs " + testCase.op + " rhs"
            );

            BOOST_REQUIRE_MESSAGE(runtime.compiled, runtime.diagnostics);
            BOOST_TEST(runtime.booleanValue == testCase.expected);
        }
    }
}

BOOST_AUTO_TEST_CASE(logical_operators_truth_table)
{
    for (const auto lhs: {false, true}) {
        for (const auto rhs: {false, true}) {
            BOOST_TEST_CONTEXT(lhs << " && / || " << rhs) {
                const auto andOutcome = runWithParameters("bool", "&&", RuntimeValue(lhs), RuntimeValue(rhs));
                const auto orOutcome = runWithParameters("bool", "||", RuntimeValue(lhs), RuntimeValue(rhs));

                BOOST_REQUIRE_MESSAGE(andOutcome.compiled, andOutcome.diagnostics);
                BOOST_REQUIRE_MESSAGE(orOutcome.compiled, orOutcome.diagnostics);
                BOOST_TEST(andOutcome.booleanValue == (lhs && rhs));
                BOOST_TEST(orOutcome.booleanValue == (lhs || rhs));

                const auto lhsText = std::string(lhs ? "true" : "false");
                const auto rhsText = std::string(rhs ? "true" : "false");
                const auto foldedAnd = runExpression(lhsText + " && " + rhsText);
                const auto foldedOr = runExpression(lhsText + " || " + rhsText);

                BOOST_REQUIRE_MESSAGE(foldedAnd.compiled, foldedAnd.diagnostics);
                BOOST_REQUIRE_MESSAGE(foldedOr.compiled, foldedOr.diagnostics);
                BOOST_TEST(foldedAnd.booleanValue == (lhs && rhs));
                BOOST_TEST(foldedOr.booleanValue == (lhs || rhs));
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(logical_operators_short_circuit)
{
    LanguageTestFixture fixture;
    fixture.addFunction(
        "main",
        "fun markTrue\n"
        "    global.called = true\n"
        "    return true\n"
        "end\n"
        "global.called = false\n"
        "a = false && markTrue\n"
        "b = true || markTrue\n"
        "return global.called"
    );

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

    const auto outcome = fixture.execute("main");
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.booleanValue == false);
}

BOOST_AUTO_TEST_CASE(unary_operators)
{
    const auto negateInteger = runUnaryWithParameter("int", "-", RuntimeValue(NomadInteger{5}));
    BOOST_REQUIRE_MESSAGE(negateInteger.compiled, negateInteger.diagnostics);
    BOOST_TEST(negateInteger.integerValue == -5);

    const auto negateFloat = runUnaryWithParameter("float", "-", RuntimeValue(NomadFloat{2.5f}));
    BOOST_REQUIRE_MESSAGE(negateFloat.compiled, negateFloat.diagnostics);
    BOOST_TEST(negateFloat.floatValue == -2.5f);

    const auto notTrue = runUnaryWithParameter("bool", "!", RuntimeValue(true));
    BOOST_REQUIRE_MESSAGE(notTrue.compiled, notTrue.diagnostics);
    BOOST_TEST(notTrue.booleanValue == false);

    const auto notFalse = runUnaryWithParameter("bool", "!", RuntimeValue(false));
    BOOST_TEST(notFalse.booleanValue == true);

    const auto foldedNegate = runExpression("-(3 + 4)");
    BOOST_REQUIRE_MESSAGE(foldedNegate.compiled, foldedNegate.diagnostics);
    BOOST_TEST(foldedNegate.integerValue == -7);

    const auto foldedNot = runExpression("!(1 < 2)");
    BOOST_REQUIRE_MESSAGE(foldedNot.compiled, foldedNot.diagnostics);
    BOOST_TEST(foldedNot.booleanValue == false);
}

BOOST_AUTO_TEST_CASE(unary_plus_is_absolute_value)
{
    const auto runtimeInteger = runUnaryWithParameter("int", "+", RuntimeValue(NomadInteger{-5}));
    BOOST_REQUIRE_MESSAGE(runtimeInteger.compiled, runtimeInteger.diagnostics);
    BOOST_TEST(runtimeInteger.integerValue == 5);

    const auto runtimePositive = runUnaryWithParameter("int", "+", RuntimeValue(NomadInteger{7}));
    BOOST_TEST(runtimePositive.integerValue == 7);

    const auto runtimeFloat = runUnaryWithParameter("float", "+", RuntimeValue(NomadFloat{-2.5f}));
    BOOST_REQUIRE_MESSAGE(runtimeFloat.compiled, runtimeFloat.diagnostics);
    BOOST_TEST(runtimeFloat.floatValue == 2.5f);

    const auto foldedInteger = runExpression("+(0 - 5)");
    BOOST_REQUIRE_MESSAGE(foldedInteger.compiled, foldedInteger.diagnostics);
    BOOST_TEST(foldedInteger.integerValue == 5);

    const auto foldedFloat = runExpression("+(0.0 - 1.5)");
    BOOST_REQUIRE_MESSAGE(foldedFloat.compiled, foldedFloat.diagnostics);
    BOOST_TEST(foldedFloat.floatValue == 1.5f);
}

BOOST_AUTO_TEST_CASE(trigonometric_operators)
{
    const std::vector<std::pair<std::string, NomadFloat>> cases = {
        {"sin", std::sin(0.5f)},
        {"cos", std::cos(0.5f)},
        {"tan", std::tan(0.5f)},
    };

    for (const auto& [op, expected]: cases) {
        BOOST_TEST_CONTEXT(op) {
            const auto runtime = runUnaryWithParameter("float", op, RuntimeValue(NomadFloat{0.5f}));
            BOOST_REQUIRE_MESSAGE(runtime.compiled, runtime.diagnostics);
            BOOST_TEST(runtime.returnTypeName == "float");
            BOOST_TEST(closeTo(runtime.floatValue, expected));

            const auto folded = runExpression(op + " 0.5");
            BOOST_REQUIRE_MESSAGE(folded.compiled, folded.diagnostics);
            BOOST_TEST(closeTo(folded.floatValue, expected));

            const auto parenthesized = runExpression(op + "(0.25 + 0.25)");
            BOOST_REQUIRE_MESSAGE(parenthesized.compiled, parenthesized.diagnostics);
            BOOST_TEST(closeTo(parenthesized.floatValue, expected));
        }
    }
}

BOOST_AUTO_TEST_CASE(string_equality_with_parameters)
{
    RuntimeValue same;
    same.setStringValue("same");
    RuntimeValue other;
    other.setStringValue("other");

    const auto equal = runWithParameters("string", "==", same, same);
    BOOST_REQUIRE_MESSAGE(equal.compiled, equal.diagnostics);
    BOOST_TEST(equal.booleanValue == true);

    const auto notEqual = runWithParameters("string", "!=", same, other);
    BOOST_REQUIRE_MESSAGE(notEqual.compiled, notEqual.diagnostics);
    BOOST_TEST(notEqual.booleanValue == true);

    same.freeStringValue();
    other.freeStringValue();
}

BOOST_AUTO_TEST_CASE(string_equality_with_non_terminal_operands)
{
    const auto outcome = runSource(
        "fun greet name:string\n"
        "    return $\"hi {name}\"\n"
        "end\n"
        "name = \"bob\"\n"
        "a = (greet name) == $\"hi {name}\"\n"
        "b = (greet \"x\") != (greet \"x\")\n"
        "c = (greet name) == \"hi bob\"\n"
        "if a && !b && c\n"
        "    return \"ok\"\n"
        "end\n"
        "return \"fail\""
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.text == "ok");
}

BOOST_AUTO_TEST_CASE(repeated_string_equality_does_not_corrupt_state)
{
    const auto outcome = runSource(
        "fun countMatches remaining:int text:string\n"
        "    if remaining == 0\n"
        "        return 0\n"
        "    end\n"
        "    if text == \"x\"\n"
        "        return 1 + (countMatches (remaining - 1) text)\n"
        "    end\n"
        "    return countMatches (remaining - 1) text\n"
        "end\n"
        "return countMatches 50 \"x\""
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 50);
}

BOOST_AUTO_TEST_CASE(compiled_instructions_match_registered_operands)
{
    LanguageTestFixture fixture;
    // Non-terminal right operands are compiled into `i`, which emits `op_copy_r_to_i`.
    fixture.addFunction(
        "main",
        "params a:bool b:bool c:int d:int\n"
        "flag = true == (a && b)\n"
        "total = c + (c * d)\n"
        "if flag\n"
        "    return total\n"
        "end\n"
        "return d"
    );

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

    const auto& runtime = fixture.getRuntime();
    const auto& instructions = runtime.getInstructions();
    const auto copyResultId = runtime.getInstructionId(OpCodes::op_copy_r_to_i);
    auto copyResultCount = 0;

    // Walking the stream by registered operand counts must land on an opcode after every instruction's operands.
    NomadIndex index = 0;
    while (index < instructions.size()) {
        const auto instructionId = runtime.getInstructionId(instructions[index].fn);
        BOOST_REQUIRE_MESSAGE(instructionId != NOMAD_INVALID_ID, "No opcode at index " << index);

        if (instructionId == copyResultId) {
            copyResultCount++;
        }

        const auto operandCount = runtime.getInstructionOperands(instructionId).size();

        for (NomadIndex operand = 1; operand <= operandCount; ++operand) {
            BOOST_REQUIRE(index + operand < instructions.size());
            BOOST_TEST_CONTEXT(runtime.getInstructionName(instructionId) << " at " << index) {
                BOOST_TEST(runtime.getInstructionId(instructions[index + operand].fn) == NOMAD_INVALID_ID);
            }
        }

        index += 1 + operandCount;
    }

    BOOST_TEST(copyResultCount >= 2);
}

BOOST_AUTO_TEST_CASE(invalid_operand_types_are_rejected)
{
    const std::vector<std::string> expressions = {
        // Modulo and bitwise are integer only.
        "7.0 % 2.0",
        "6.0 & 3.0",
        "6.0 | 3.0",
        "6.0 ^ 3.0",
        "true & false",
        "true | false",
        "true ^ false",
        // Arithmetic is numeric only.
        "true + false",
        "true - false",
        "true * false",
        "true / false",
        "\"a\" + \"b\"",
        "\"a\" - \"b\"",
        "\"a\" * \"b\"",
        // Strings only support equality.
        "\"a\" < \"b\"",
        "\"a\" >= \"b\"",
        "\"a\" == 1",
        "1 != \"a\"",
        // Ordering is numeric only.
        "true < false",
        "true >= false",
        // Logical operators are boolean only.
        "1 && 0",
        "1 || 0",
        "1.0 && 0.0",
        "\"a\" && \"b\"",
        // Mixed operand types.
        "1 + 1.0",
        "1.0 * 2",
        "1 == 1.0",
        "1 < 1.0",
        "true == 1",
        "1 & true",
        // Unary operators.
        "!1",
        "!1.0",
        "!\"text\"",
        "-true",
        "-\"text\"",
        "sin 1",
        "cos true",
        "tan \"text\"",
    };

    for (const auto& expression: expressions) {
        BOOST_TEST_CONTEXT(expression) {
            BOOST_TEST(!compileErrors("return " + expression).empty());
        }
    }
}

BOOST_AUTO_TEST_CASE(invalid_operand_types_are_rejected_for_variables)
{
    const std::vector<std::string> sources = {
        "a = 1\nb = 1.0\nreturn a + b",
        "a = 1.0\nb = 2.0\nreturn a % b",
        "a = true\nb = false\nreturn a + b",
        "a = \"x\"\nb = \"y\"\nreturn a + b",
        "a = 1\nreturn !a",
        "a = true\nreturn -a",
    };

    for (const auto& source: sources) {
        BOOST_TEST_CONTEXT(source) {
            BOOST_TEST(!compileErrors(source).empty());
        }
    }
}

BOOST_AUTO_TEST_CASE(precedence_and_associativity)
{
    const std::vector<PrecedenceCase> cases = {
        // Multiplication, division and modulo before addition and subtraction.
        {"10 + 2 * 3", "16"},
        {"2 * 3 + 10", "16"},
        {"(10 + 2) * 3", "36"},
        {"10 - 6 / 2", "7"},
        {"10 + 7 % 4", "13"},
        {"2 * 3 % 4", "2"},
        // Left associativity.
        {"10 - 4 - 3", "3"},
        {"100 / 10 / 2", "5"},
        {"20 % 7 % 4", "2"},
        {"2 * 3 * 4", "24"},
        // Arithmetic before bitwise.
        {"1 + 2 | 4", "7"},
        {"4 | 1 + 2", "7"},
        // `|` before `^` before `&`.
        {"1 | 2 ^ 3", "0"},
        {"6 & 3 ^ 1", "2"},
        {"6 ^ 3 & 5", "5"},
        // Bitwise before comparison.
        {"6 & 3 == 2", "true"},
        // Arithmetic before comparison.
        {"1 + 2 > 2", "true"},
        {"2 * 3 == 6", "true"},
        // Comparison before logical operators.
        {"1 < 2 && 2 < 3", "true"},
        {"1 > 2 || 2 < 3", "true"},
        // `||` before `&&`.
        {"true || false && false", "false"},
        {"false && false || true", "false"},
        // Parentheses override precedence.
        {"true || (false && false)", "true"},
        {"(1 | 2) ^ 3", "0"},
        {"1 | (2 ^ 3)", "1"},
        {"((((1 + 2))))", "3"},
        // Unary operators bind tighter than binary operators.
        {"-2 * 3", "-6"},
        {"- 2 * 3", "-6"},
        {"!false && false", "false"},
        {"!(false && false)", "true"},
    };

    for (const auto& testCase: cases) {
        BOOST_TEST_CONTEXT(testCase.expression) {
            auto outcome = runExpression(testCase.expression);

            BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);

            if (testCase.expected == "true" || testCase.expected == "false") {
                BOOST_TEST(outcome.returnTypeName == "bool");
                BOOST_TEST(outcome.booleanValue == (testCase.expected == "true"));
            } else {
                BOOST_TEST(outcome.text == testCase.expected);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(precedence_matches_at_runtime)
{
    const auto outcome = runSource(
        "a = 10\nb = 2\nc = 3\nd = 4\n"
        "return a + b * c - d / b"
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.integerValue == 14);

    const auto subtraction = runSource("a = 10\nb = 4\nc = 3\nreturn a - b - c");
    BOOST_REQUIRE_MESSAGE(subtraction.compiled, subtraction.diagnostics);
    BOOST_TEST(subtraction.integerValue == 3);
}

BOOST_AUTO_TEST_CASE(malformed_expressions_are_rejected)
{
    const std::vector<std::string> sources = {
        "return 1 +",
        "return * 2",
        "return (1 + 2",
        "return 1 + 2)",
        "return ()",
        "return 1 2",
        "return 1 * * 2",
        "a =",
        "= 1",
        "return 1 == == 1",
    };

    for (const auto& source: sources) {
        BOOST_TEST_CONTEXT(source) {
            BOOST_TEST(!compileErrors(source).empty());
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
