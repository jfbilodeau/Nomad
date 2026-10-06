// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/SyntaxTree.hpp>
#include <nomad/compiler/Tokenizer.hpp>
#include <nomad/script/VirtualMachine.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>

using namespace nomad;

namespace {

class ExecutableTestStatement : public Statement {
public:
    ExecutableTestStatement():
        Statement(0, 0)
    {
    }

protected:
    void onEvaluate(Interpreter& context) const override {
        context.setVoidResult();
    }
};

std::unique_ptr<Statement> parseExecutableTestStatement(
    CompilerContext* /*context*/,
    Function* /*function*/,
    Tokenizer* /*tokens*/
) {
    return std::make_unique<ExecutableTestStatement>();
}

template<typename Node>
void resolveForInterpreter(Compiler* compiler, Node& node) {
    CompilerContext compilerContext(compiler, CompilerMode::Interpreter);
    BOOST_REQUIRE(node.resolve(&compilerContext, nullptr));
}

} // namespace

BOOST_AUTO_TEST_CASE(statement_execution_assigns_interpreter_local_variables)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext compilerContextForStatements(compiler.get(), CompilerMode::Interpreter);
    Interpreter context(&runtime);
    StatementList statements;

    statements.addStatement(std::make_unique<AssignmentStatement>(
        0,
        0,
        "score",
        std::make_unique<BinaryExpression>(
            nullptr,
            0,
            0,
            BinaryOperator::Plus,
            std::make_unique<IntegerLiteral>(nullptr, 0, 0, 12),
            std::make_unique<IntegerLiteral>(nullptr, 0, 0, 5)
        )
    ));

    BOOST_REQUIRE(statements.resolve(&compilerContextForStatements, nullptr));
    statements.evaluate(context);

    const auto* score = context.getVariableValue("score");
    BOOST_REQUIRE(score != nullptr);
    BOOST_TEST(context.getVariableType("score") == runtime.getIntegerType());
    BOOST_TEST(score->getIntegerValue() == 17);
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getIntegerType());
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 17);
}

BOOST_AUTO_TEST_CASE(statement_execution_allows_assignment_type_changes)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    Interpreter context(&runtime);
    context.setVariable("score", runtime.getIntegerType(), RuntimeValue{NomadInteger{12}});

    AssignmentStatement assignment(
        0,
        0,
        "score",
        std::make_unique<BooleanLiteral>(nullptr, 0, 0, true)
    );

    resolveForInterpreter(compiler.get(), assignment);
    assignment.evaluate(context);

    const auto* score = context.getVariableValue("score");
    BOOST_REQUIRE(score != nullptr);
    BOOST_TEST(context.getVariableType("score") == runtime.getBooleanType());
    BOOST_TEST(score->getBooleanValue());
}

BOOST_AUTO_TEST_CASE(statement_evaluation_defaults_to_unsupported)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    compiler->registerParseStatementFn(
        "supported",
        parseExecutableTestStatement,
        nullptr
    );

    Interpreter context(&runtime);
    CompilerContext supportedTokenizerContext(compiler.get(), CompilerMode::Interpreter);
    Tokenizer supportedTokenizer(&supportedTokenizerContext, "test.nomad", "supported");
    auto supported = parser::parseStatement(&supportedTokenizerContext, nullptr, &supportedTokenizer);
    BOOST_REQUIRE(supported != nullptr);
    BOOST_CHECK_NO_THROW(supported->evaluate(context));

    Statement unsupported(0, 0);
    BOOST_CHECK_THROW(unsupported.evaluate(context), AstException);
    BOOST_TEST(context.getResult() == nullptr);
}

BOOST_AUTO_TEST_CASE(statement_execution_calls_compiled_functions_with_interpreter_arguments)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    compiler->registerScriptFile(
        "incrementScore",
        "incrementScore.nomad",
        "params value:int\nreturn value + 1"
    );
    CompilerContext compileContext(compiler.get());
    BOOST_REQUIRE(compiler->compileFunctions(&compileContext));

    Interpreter context(&runtime);

    CompilerContext tokenizerContext(compiler.get(), CompilerMode::Interpreter);
    Tokenizer tokenizer(&tokenizerContext, "test.nomad", "incrementScore 17");
    auto statement = parser::parseLine(&tokenizerContext, nullptr, &tokenizer);
    BOOST_REQUIRE(statement != nullptr);
    resolveForInterpreter(compiler.get(), *statement);
    statement->evaluate(context);
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType()->isVoid());

    context.setVariable("score", runtime.getIntegerType(), RuntimeValue{NomadInteger{17}});
    BOOST_REQUIRE(context.execute("incrementScore score"));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getIntegerType());
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 18);
}

BOOST_AUTO_TEST_CASE(expression_statement_evaluates_and_cannot_compile)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    Interpreter context(&runtime);
    auto statement = std::make_unique<ExpressionStatement>(
        0,
        0,
        std::make_unique<IntegerLiteral>(nullptr, 0, 0, 10)
    );

    resolveForInterpreter(compiler.get(), *statement);
    statement->evaluate(context);
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 10);
    BOOST_CHECK_THROW(statement->compile(compiler.get(), nullptr), AstException);
}

BOOST_AUTO_TEST_CASE(expression_execution_calls_registered_nativeFunctions_without_compiling_input)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    runtime.registerNativeFunction(
        "identity",
        [](VirtualMachine* interpreter) {
            interpreter->setStringResult(interpreter->getStringParameter(0));
        },
        {
            defParameter("value", runtime.getStringRefType(), NomadParamDoc("Value to return"))
        },
        runtime.getStringType(),
        NomadDoc("Return the provided string")
    );
    Interpreter context(&runtime);

    CompilerContext tokenizerContext(compiler.get(), CompilerMode::Interpreter);
    Tokenizer tokenizer(&tokenizerContext, "test.nomad", "identity \"console\"");
    auto expression = parser::parseExpression(&tokenizerContext, nullptr, &tokenizer);
    resolveForInterpreter(compiler.get(), *expression);
    expression->evaluate(context);

    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getStringType());
    BOOST_TEST(context.getResult()->getValue().getStringValue() == "console");
    BOOST_TEST(!context.hasError());
}

BOOST_AUTO_TEST_CASE(statement_execution_calls_registered_void_nativeFunctions)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    NomadInteger calledValue = 0;
    runtime.registerNativeFunction(
        "remember",
        [&calledValue](VirtualMachine* interpreter) {
            calledValue = interpreter->getIntegerParameter(0);
        },
        {
            defParameter("value", runtime.getIntegerType(), NomadParamDoc("Value to remember"))
        },
        runtime.getVoidType(),
        NomadDoc("Remember an integer")
    );
    Interpreter context(&runtime);

    CompilerContext tokenizerContext(compiler.get(), CompilerMode::Interpreter);
    Tokenizer tokenizer(&tokenizerContext, "test.nomad", "remember 42");
    auto statement = parser::parseLine(&tokenizerContext, nullptr, &tokenizer);
    BOOST_REQUIRE(statement != nullptr);
    resolveForInterpreter(compiler.get(), *statement);
    statement->evaluate(context);

    BOOST_TEST(calledValue == 42);
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType()->isVoid());
    BOOST_TEST(!context.hasError());
}

BOOST_AUTO_TEST_CASE(nativeFunction_execution_reports_callback_errors_through_context)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    runtime.registerNativeFunction(
        "fail",
        [](VirtualMachine* interpreter) {
            interpreter->fault("NativeFunction failed");
        },
        {},
        runtime.getIntegerType(),
        NomadDoc("Fail for testing")
    );
    Interpreter context(&runtime);

    CompilerContext tokenizerContext(compiler.get(), CompilerMode::Interpreter);
    Tokenizer tokenizer(&tokenizerContext, "test.nomad", "fail");
    auto expression = parser::parseExpression(&tokenizerContext, nullptr, &tokenizer);
    resolveForInterpreter(compiler.get(), *expression);
    expression->evaluate(context);

    BOOST_TEST(context.hasError());
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "NativeFunction failed");
    BOOST_TEST(context.getResult() == nullptr);
}

BOOST_AUTO_TEST_CASE(nativeFunction_execution_accepts_named_compiled_callback_functions)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    const auto callbackFunctionId = compiler->registerScriptFile(
        "callbackFunction",
        "callbackFunction.nomad",
        "params value:int\nreturn value"
    );
    CompilerContext compileContext(compiler.get());
    BOOST_REQUIRE(compiler->compileFunctions(&compileContext));

    const auto* callbackType = runtime.getCallbackType(
        {runtime.getIntegerType()},
        runtime.getIntegerType()
    );
    NomadId receivedCallbackId = NOMAD_INVALID_ID;
    runtime.registerNativeFunction(
        "acceptCallback",
        [&receivedCallbackId](VirtualMachine* interpreter) {
            receivedCallbackId = interpreter->getIdParameter(0);
        },
        {
            defParameter("callback", callbackType, NomadParamDoc("Callback to accept"))
        },
        runtime.getVoidType(),
        NomadDoc("Accept a callback")
    );
    Interpreter context(&runtime);

    BOOST_TEST(context.execute("acceptCallback callbackFunction"));
    BOOST_TEST(receivedCallbackId == callbackFunctionId);
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType()->isVoid());
    BOOST_TEST(!context.hasError());

    BOOST_TEST(!context.execute("acceptCallback fun"));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "Inline `fun` callbacks are not supported by the interpreter");
    BOOST_TEST(receivedCallbackId == callbackFunctionId);

    BOOST_TEST(!context.execute("acceptCallback then"));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "Inline `then` callbacks are not supported by the interpreter");
    BOOST_TEST(receivedCallbackId == callbackFunctionId);
}

BOOST_AUTO_TEST_CASE(callback_argument_rejects_uncompiled_inline_callback_functions)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    const auto callbackFunctionId = compiler->registerFunctionSource(
        "uncompiledCallback",
        "uncompiledCallback.nomad",
        "params value:int\nreturn value"
    );
    BOOST_REQUIRE(callbackFunctionId != NOMAD_INVALID_ID);
    const auto* callbackType = runtime.getCallbackType(
        {runtime.getIntegerType()},
        runtime.getIntegerType()
    );
    Interpreter context(&runtime);
    auto argument = createCallbackArgument(0, 0, callbackType, callbackFunctionId);

    BOOST_CHECK_THROW(static_cast<void>(argument->evaluate(context)), AstException);
}
