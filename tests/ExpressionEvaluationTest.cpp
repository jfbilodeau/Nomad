// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/compiler/Argument.hpp>
#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Expression.hpp>

#include <nomad/interpreter/Interpreter.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/StructType.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <optional>
#include <vector>

using namespace nomad;

BOOST_AUTO_TEST_CASE(expression_values_do_not_use_the_published_context_result)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    InterpreterContext context(interpreter);
    context.setResult(runtime.getIntegerType(), RuntimeValue{NomadInteger{99}});
    std::vector<NomadInteger> order;
    BOOST_REQUIRE(runtime.registerNativeFunction("ast.left", [&](VirtualMachine* active) {
        BOOST_REQUIRE(context.getResult() != nullptr);
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 99);
        order.push_back(1);
        active->setIntegerResult(17);
    }, {}, runtime.getIntegerType(), "").isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction("ast.right", [&](VirtualMachine* active) {
        BOOST_REQUIRE(context.getResult() != nullptr);
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 99);
        order.push_back(2);
        active->setIntegerResult(5);
    }, {}, runtime.getIntegerType(), "").isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction("ast.sum", [&](VirtualMachine* active) {
        BOOST_REQUIRE(context.getResult() != nullptr);
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 99);
        order.push_back(3);
        active->setIntegerResult(active->getIntegerParameter(0) + active->getIntegerParameter(1));
    }, {defParameter("left", runtime.getIntegerType(), ""), defParameter("right", runtime.getIntegerType(), "")},
        runtime.getIntegerType(), "").isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction("ast.fail", [&](VirtualMachine*) {
        order.push_back(4);
        throw NomadException("Expected AST failure");
    }, {}, runtime.getIntegerType(), "").isValid());
    CompilerContext resolution(compiler.get(), CompilerMode::Interpreter, &context);
    FunctionCallExpression call(nullptr, 1, 1, "ast.sum");
    call.addArgument(createExpressionArgument(1, 1, runtime.getIntegerType(),
        std::make_unique<FunctionCallExpression>(nullptr, 1, 1, "ast.left")));
    call.addArgument(createExpressionArgument(1, 1, runtime.getIntegerType(),
        std::make_unique<FunctionCallExpression>(nullptr, 1, 1, "ast.right")));
    BOOST_REQUIRE(call.resolve(&resolution, nullptr));
    const auto instructionsBefore = runtime.getInstructions().size();
    const auto result = call.evaluate(context);
    BOOST_TEST(result.getValue().getIntegerValue() == 22);
    BOOST_TEST(order == std::vector<NomadInteger>({2, 1, 3}), boost::test_tools::per_element());
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 99);
    order.clear();
    BinaryExpression binary(nullptr, 1, 1, BinaryOperator::Plus,
        std::make_unique<FunctionCallExpression>(nullptr, 1, 1, "ast.left"),
        std::make_unique<FunctionCallExpression>(nullptr, 1, 1, "ast.right"));
    BOOST_REQUIRE(binary.resolve(&resolution, nullptr));
    BOOST_TEST(binary.evaluate(context).getValue().getIntegerValue() == 22);
    BOOST_TEST(order == std::vector<NomadInteger>({1, 2}), boost::test_tools::per_element());
    order.clear();
    FunctionCallExpression failing(nullptr, 1, 1, "ast.sum");
    failing.addArgument(createExpressionArgument(1, 1, runtime.getIntegerType(),
        std::make_unique<FunctionCallExpression>(nullptr, 1, 1, "ast.left")));
    failing.addArgument(createExpressionArgument(1, 1, runtime.getIntegerType(),
        std::make_unique<FunctionCallExpression>(nullptr, 1, 1, "ast.fail")));
    BOOST_REQUIRE(failing.resolve(&resolution, nullptr));
    BOOST_CHECK_THROW(static_cast<void>(failing.evaluate(context)), NomadException);
    BOOST_TEST(order == std::vector<NomadInteger>({4}), boost::test_tools::per_element());
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 99);
    BOOST_TEST(!context.hasError());
    BOOST_TEST(runtime.getInstructions().size() == instructionsBefore);
    BOOST_TEST(!interpreter.execute("ast.fail", context));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "Expected AST failure");
    BOOST_TEST(context.getResult() == nullptr);
}

BOOST_AUTO_TEST_CASE(expression_struct_results_own_their_values_after_ast_and_vm_destruction)
{
    Runtime runtime;
    const auto typeId = runtime.registerStruct("AstOwnedValue");
    const auto* type = runtime.getTypeByName("AstOwnedValue");
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(typeId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(typeId, "text", runtime.getStringType()).isValid());
    runtime.getTypeManager().finalizeCompositeFieldLayout(typeId);
    BOOST_REQUIRE(runtime.registerNativeFunction("ast.makeValue", [](VirtualMachine* active) {
        auto* destination = &active->getStorageParameter(0, 0);
        destination[0].setIntegerValue(17);
        destination[1].setStringValue("owned AST result");
    }, {}, type, "").isValid());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    {
        std::optional<InterpreterValue> retained;
        {
            auto compiler = runtime.createCompiler();
            auto machine = runtime.createVirtualMachine();
            const Interpreter interpreter(*machine);
            InterpreterContext context(interpreter);
            context.setResult(runtime.getIntegerType(), RuntimeValue{NomadInteger{99}});
            CompilerContext resolution(compiler.get(), CompilerMode::Interpreter, &context);
            FunctionCallExpression expression(nullptr, 1, 1, "ast.makeValue");
            BOOST_REQUIRE(expression.resolve(&resolution, nullptr));
            retained = expression.evaluate(context);
            BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 99);
        }
        BOOST_TEST(retained->getType() == type);
        BOOST_REQUIRE_EQUAL(retained->getStorage().size(), 2U);
        BOOST_TEST(retained->getStorage()[0].getIntegerValue() == 17);
        BOOST_TEST(retained->getStorage()[1].getStringValue() == "owned AST result");
        const auto snapshot = *retained;
        BOOST_TEST(snapshot.getStorage().data() != retained->getStorage().data());
        BOOST_TEST(snapshot.getStorage()[1].getStringValue() == "owned AST result");
    }
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
}

BOOST_AUTO_TEST_CASE(expression_evaluation_reads_persistent_interpreter_variables)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);
    context.setVariable("score", runtime.getIntegerType(), RuntimeValue{NomadInteger{12}});

    BOOST_REQUIRE(context.getInterpreter().execute("score + 5", context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getIntegerType());
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 17);
}

BOOST_AUTO_TEST_CASE(expression_evaluation_reuses_compile_time_folded_values)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext compilerContext(compiler.get());
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

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

    const auto result = expression.evaluate(context);
    BOOST_TEST(result.getValue().getIntegerValue() == 42);
    BOOST_TEST(context.getResult() == nullptr);
}

BOOST_AUTO_TEST_CASE(expression_evaluation_short_circuits_boolean_operators)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    // Division by a variable is not folded at resolve time, so it would fail if evaluated.
    BOOST_REQUIRE(context.getInterpreter().execute("zero = 0", context));

    BOOST_REQUIRE(context.getInterpreter().execute("false && 1 / zero > 0", context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getBooleanType());
    BOOST_TEST(!context.getResult()->getValue().getBooleanValue());

    BOOST_REQUIRE(context.getInterpreter().execute("true || 1 / zero > 0", context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getValue().getBooleanValue());
}

BOOST_AUTO_TEST_CASE(expression_resolution_rejects_invalid_operator_types)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext compilerContext(compiler.get());
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

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
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    IntegerLiteral expression(nullptr, 0, 0, 42);

    BOOST_CHECK_THROW(expression.evaluate(context), AstException);
    BOOST_TEST(context.getResult() == nullptr);
}

BOOST_AUTO_TEST_CASE(expression_evaluation_copies_string_values_safely)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext compilerContext(compiler.get());
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    StringLiteral expression(nullptr, 0, 0, "nomad");
    BOOST_REQUIRE(expression.resolve(&compilerContext, nullptr));
    const auto result = expression.evaluate(context);
    BOOST_TEST(result.getType() == runtime.getStringType());
    BOOST_TEST(result.getValue().getStringValue() == "nomad");
    BOOST_TEST(context.getResult() == nullptr);

    context.setVariable("greeting", result.getType(), result.getValue());
    BOOST_TEST(context.getVariableValue("greeting")->getStringValue() == "nomad");
}

BOOST_AUTO_TEST_CASE(expression_evaluation_reads_runtime_constants)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext compilerContext(compiler.get());
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    IdentifierExpression expression(nullptr, 0, 0, "pi");
    BOOST_REQUIRE(expression.resolve(&compilerContext, nullptr));
    BOOST_TEST(expression.isParsed());

    const auto result = expression.evaluate(context);
    BOOST_TEST(result.getType() == runtime.getFloatType());
    BOOST_TEST(result.getValue().getFloatValue() == NOMAD_PI);
}
BOOST_AUTO_TEST_CASE(expression_constant_folding_handles_boolean_equality)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext compilerContext(compiler.get());
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    BinaryExpression equalExpression(
        nullptr,
        0,
        0,
        BinaryOperator::EqualEqual,
        std::make_unique<BooleanLiteral>(nullptr, 0, 0, false),
        std::make_unique<BooleanLiteral>(nullptr, 0, 0, false)
    );
    BOOST_REQUIRE(equalExpression.resolve(&compilerContext, nullptr));
    BOOST_TEST(equalExpression.evaluate(context).getValue().getBooleanValue());

    BinaryExpression equalTrueExpression(
        nullptr,
        0,
        0,
        BinaryOperator::EqualEqual,
        std::make_unique<BooleanLiteral>(nullptr, 0, 0, true),
        std::make_unique<BooleanLiteral>(nullptr, 0, 0, true)
    );
    BOOST_REQUIRE(equalTrueExpression.resolve(&compilerContext, nullptr));
    BOOST_TEST(equalTrueExpression.evaluate(context).getValue().getBooleanValue());

    BinaryExpression notEqualExpression(
        nullptr,
        0,
        0,
        BinaryOperator::BangEqual,
        std::make_unique<BooleanLiteral>(nullptr, 0, 0, true),
        std::make_unique<BooleanLiteral>(nullptr, 0, 0, false)
    );
    BOOST_REQUIRE(notEqualExpression.resolve(&compilerContext, nullptr));
    BOOST_TEST(notEqualExpression.evaluate(context).getValue().getBooleanValue());

    BinaryExpression notEqualSameTrueExpression(
        nullptr,
        0,
        0,
        BinaryOperator::BangEqual,
        std::make_unique<BooleanLiteral>(nullptr, 0, 0, true),
        std::make_unique<BooleanLiteral>(nullptr, 0, 0, true)
    );
    BOOST_REQUIRE(notEqualSameTrueExpression.resolve(&compilerContext, nullptr));
    BOOST_TEST(!notEqualSameTrueExpression.evaluate(context).getValue().getBooleanValue());
}

BOOST_AUTO_TEST_CASE(expression_constant_folding_handles_float_comparisons)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext compilerContext(compiler.get());
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

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
        const auto result = expression.evaluate(context);
        BOOST_TEST(result.getType() == runtime.getBooleanType());
        BOOST_TEST(result.getValue().getBooleanValue() == expected);
    };

    checkComparison(BinaryOperator::EqualEqual, 1.0, 2.0, false);
    checkComparison(BinaryOperator::BangEqual, 1.0, 2.0, true);
    checkComparison(BinaryOperator::LessThan, 1.0, 2.0, true);
    checkComparison(BinaryOperator::LessThanEqual, 1.0, 2.0, true);
    checkComparison(BinaryOperator::GreaterThan, 1.0, 2.0, false);
    checkComparison(BinaryOperator::GreaterThanEqual, 1.0, 2.0, false);
}
