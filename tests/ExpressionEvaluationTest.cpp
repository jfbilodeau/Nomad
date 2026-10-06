// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Expression.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>

using namespace nomad;

BOOST_AUTO_TEST_CASE(expression_evaluation_reads_persistent_interpreter_variables)
{
    Runtime runtime;
    Interpreter context(&runtime);
    context.setVariable("score", runtime.getIntegerType(), RuntimeValue{NomadInteger{12}});

    BOOST_REQUIRE(context.execute("score + 5"));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getIntegerType());
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 17);
}

BOOST_AUTO_TEST_CASE(expression_evaluation_reuses_compile_time_folded_values)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext compilerContext(compiler.get());
    Interpreter context(&runtime);

    BinaryExpression expression(
        nullptr,
        0,
        0,
        BinaryOperator::Star,
        std::make_unique<IntegerLiteral>(nullptr, 0, 0, 6),
        std::make_unique<IntegerLiteral>(nullptr, 0, 0, 7)
    );

    BOOST_REQUIRE(expression.resolve(&compilerContext, nullptr));
    BOOST_TEST(expression.isParsed());

    expression.evaluate(context);
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 42);
}

BOOST_AUTO_TEST_CASE(expression_evaluation_short_circuits_boolean_operators)
{
    Runtime runtime;
    Interpreter context(&runtime);

    // Division by a variable is not folded at resolve time, so it would fail if evaluated.
    BOOST_REQUIRE(context.execute("zero = 0"));

    BOOST_REQUIRE(context.execute("false && 1 / zero > 0"));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getBooleanType());
    BOOST_TEST(!context.getResult()->getValue().getBooleanValue());

    BOOST_REQUIRE(context.execute("true || 1 / zero > 0"));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getValue().getBooleanValue());
}

BOOST_AUTO_TEST_CASE(expression_resolution_rejects_invalid_operator_types)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext compilerContext(compiler.get());
    Interpreter context(&runtime);

    BinaryExpression expression(
        nullptr,
        0,
        0,
        BinaryOperator::Plus,
        std::make_unique<IntegerLiteral>(nullptr, 0, 0, 1),
        std::make_unique<BooleanLiteral>(nullptr, 0, 0, true)
    );

    BOOST_TEST(!expression.resolve(&compilerContext, nullptr));
    BOOST_REQUIRE(compilerContext.getFirstError() != nullptr);
    BOOST_TEST(compilerContext.getFirstError()->message.starts_with("Invalid binary operator '+'"));

    BOOST_CHECK_THROW(expression.evaluate(context), AstException);
    BOOST_TEST(context.getResult() == nullptr);
}

BOOST_AUTO_TEST_CASE(expression_evaluation_requires_resolution)
{
    Runtime runtime;
    Interpreter context(&runtime);

    IntegerLiteral expression(nullptr, 0, 0, 42);

    BOOST_CHECK_THROW(expression.evaluate(context), AstException);
    BOOST_TEST(context.getResult() == nullptr);
}

BOOST_AUTO_TEST_CASE(expression_evaluation_copies_string_values_safely)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext compilerContext(compiler.get());
    Interpreter context(&runtime);

    StringLiteral expression(nullptr, 0, 0, "nomad");
    BOOST_REQUIRE(expression.resolve(&compilerContext, nullptr));
    expression.evaluate(context);
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getStringType());
    BOOST_TEST(context.getResult()->getValue().getStringValue() == "nomad");

    context.setVariable("greeting", context.getResult()->getType(), context.getResult()->getValue());
    BOOST_TEST(context.getVariableValue("greeting")->getStringValue() == "nomad");
}

BOOST_AUTO_TEST_CASE(expression_evaluation_reads_runtime_constants)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext compilerContext(compiler.get());
    Interpreter context(&runtime);

    IdentifierExpression expression(nullptr, 0, 0, "pi");
    BOOST_REQUIRE(expression.resolve(&compilerContext, nullptr));
    BOOST_TEST(expression.isParsed());

    expression.evaluate(context);
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getFloatType());
    BOOST_TEST(context.getResult()->getValue().getFloatValue() == NOMAD_PI);
}
BOOST_AUTO_TEST_CASE(expression_constant_folding_handles_boolean_equality)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext compilerContext(compiler.get());
    Interpreter context(&runtime);

    BinaryExpression equalExpression(
        nullptr,
        0,
        0,
        BinaryOperator::EqualEqual,
        std::make_unique<BooleanLiteral>(nullptr, 0, 0, false),
        std::make_unique<BooleanLiteral>(nullptr, 0, 0, false)
    );
    BOOST_REQUIRE(equalExpression.resolve(&compilerContext, nullptr));
    equalExpression.evaluate(context);
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getValue().getBooleanValue());

    BinaryExpression equalTrueExpression(
        nullptr,
        0,
        0,
        BinaryOperator::EqualEqual,
        std::make_unique<BooleanLiteral>(nullptr, 0, 0, true),
        std::make_unique<BooleanLiteral>(nullptr, 0, 0, true)
    );
    BOOST_REQUIRE(equalTrueExpression.resolve(&compilerContext, nullptr));
    equalTrueExpression.evaluate(context);
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getValue().getBooleanValue());

    BinaryExpression notEqualExpression(
        nullptr,
        0,
        0,
        BinaryOperator::BangEqual,
        std::make_unique<BooleanLiteral>(nullptr, 0, 0, true),
        std::make_unique<BooleanLiteral>(nullptr, 0, 0, false)
    );
    BOOST_REQUIRE(notEqualExpression.resolve(&compilerContext, nullptr));
    notEqualExpression.evaluate(context);
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getValue().getBooleanValue());

    BinaryExpression notEqualSameTrueExpression(
        nullptr,
        0,
        0,
        BinaryOperator::BangEqual,
        std::make_unique<BooleanLiteral>(nullptr, 0, 0, true),
        std::make_unique<BooleanLiteral>(nullptr, 0, 0, true)
    );
    BOOST_REQUIRE(notEqualSameTrueExpression.resolve(&compilerContext, nullptr));
    notEqualSameTrueExpression.evaluate(context);
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(!context.getResult()->getValue().getBooleanValue());
}

BOOST_AUTO_TEST_CASE(expression_constant_folding_handles_float_comparisons)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext compilerContext(compiler.get());
    Interpreter context(&runtime);

    const auto checkComparison = [&](const BinaryOperator op, const NomadFloat lhs, const NomadFloat rhs, const bool expected) {
        BinaryExpression expression(
            nullptr,
            0,
            0,
            op,
            std::make_unique<FloatLiteral>(nullptr, 0, 0, lhs),
            std::make_unique<FloatLiteral>(nullptr, 0, 0, rhs)
        );
        BOOST_REQUIRE(expression.resolve(&compilerContext, nullptr));
        expression.evaluate(context);
        BOOST_REQUIRE(context.getResult() != nullptr);
        BOOST_TEST(context.getResult()->getType() == runtime.getBooleanType());
        BOOST_TEST(context.getResult()->getValue().getBooleanValue() == expected);
    };

    checkComparison(BinaryOperator::EqualEqual, 1.0, 2.0, false);
    checkComparison(BinaryOperator::BangEqual, 1.0, 2.0, true);
    checkComparison(BinaryOperator::LessThan, 1.0, 2.0, true);
    checkComparison(BinaryOperator::LessThanEqual, 1.0, 2.0, true);
    checkComparison(BinaryOperator::GreaterThan, 1.0, 2.0, false);
    checkComparison(BinaryOperator::GreaterThanEqual, 1.0, 2.0, false);
}
