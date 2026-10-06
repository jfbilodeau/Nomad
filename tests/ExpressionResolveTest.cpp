// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/compiler/Argument.hpp>
#include <nomad/compiler/CallNode.hpp>
#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Expression.hpp>
#include <nomad/compiler/SyntaxTree.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/Function.hpp>

using namespace nomad;

BOOST_AUTO_TEST_CASE(expression_resolve_reports_unknown_identifier_without_throwing)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    Function function(NOMAD_INVALID_ID, "test", "", "");
    CompilerContext context(compiler.get());

    IdentifierExpression expression(nullptr, 3, 7, "missing");

    NomadBoolean resolved = true;
    BOOST_REQUIRE_NO_THROW(resolved = expression.resolve(&context, &function));
    BOOST_TEST(!resolved);
    BOOST_REQUIRE_EQUAL(context.getErrorCount(), 1);

    const auto& diagnostic = context.getDiagnostics().front();
    BOOST_TEST((diagnostic.severity == DiagnosticSeverity::Error));
    BOOST_TEST(diagnostic.message == "Unknown identifier: missing");
    BOOST_TEST(diagnostic.line == 3);
    BOOST_TEST(diagnostic.column == 7);
}

BOOST_AUTO_TEST_CASE(statement_resolve_reports_errors_in_call_arguments)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    Function function(NOMAD_INVALID_ID, "test", "", "");
    CompilerContext context(compiler.get());

    NativeFunctionDefinition definition{};
    definition.id = 0;
    definition.name = "log";
    definition.returnType = runtime.getVoidType();

    NativeFunctionStatementNode statement(1, 0, definition);
    statement.addArgument(createExpressionArgument(
        1,
        4,
        runtime.getIntegerType(),
        std::make_unique<IdentifierExpression>(nullptr, 1, 4, "missing")
    ));

    NomadBoolean resolved = true;
    BOOST_REQUIRE_NO_THROW(resolved = statement.resolve(&context, &function));
    BOOST_TEST(!resolved);
    BOOST_REQUIRE_EQUAL(context.getErrorCount(), 1);

    const auto* error = context.getFirstError();
    BOOST_REQUIRE(error != nullptr);
    BOOST_TEST(error->message == "Unknown identifier: missing");
    BOOST_TEST(error->line == 1);
    BOOST_TEST(error->column == 4);
}

BOOST_AUTO_TEST_CASE(statement_list_resolve_reports_every_failing_statement)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    Function function(NOMAD_INVALID_ID, "test", "", "");
    CompilerContext context(compiler.get());

    StatementList statements;
    statements.addStatement(std::make_unique<AssignmentStatement>(
        0, 0, "first", std::make_unique<IdentifierExpression>(nullptr, 0, 8, "missingFirst")
    ));
    statements.addStatement(std::make_unique<AssignmentStatement>(
        1, 0, "second", std::make_unique<IdentifierExpression>(nullptr, 1, 9, "missingSecond")
    ));

    BOOST_TEST(!statements.resolve(&context, &function));
    BOOST_REQUIRE_EQUAL(context.getErrorCount(), 2);
    BOOST_TEST(context.getDiagnostics()[0].message == "Unknown identifier: missingFirst");
    BOOST_TEST(context.getDiagnostics()[1].message == "Unknown identifier: missingSecond");
}

BOOST_AUTO_TEST_CASE(assignment_resolve_reports_function_variable_type_mismatch)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    Function function(NOMAD_INVALID_ID, "test", "", "");
    function.registerVariable("score", runtime.getIntegerType());
    CompilerContext context(compiler.get());

    AssignmentStatement statement(2, 3, "score", std::make_unique<StringLiteral>(nullptr, 2, 11, "ten"));

    NomadBoolean resolved = true;
    BOOST_REQUIRE_NO_THROW(resolved = statement.resolve(&context, &function));
    BOOST_TEST(!resolved);
    BOOST_REQUIRE_EQUAL(context.getErrorCount(), 1);

    const auto* error = context.getFirstError();
    BOOST_REQUIRE(error != nullptr);
    BOOST_TEST(error->message.find("Cannot assign value of type") != NomadString::npos);
    BOOST_TEST(error->line == 2);
    BOOST_TEST(error->column == 3);
}

BOOST_AUTO_TEST_CASE(compile_functions_reports_resolve_errors_from_every_function)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    compiler->registerScriptFile("broken", "broken.nomad", "total = missing + 1");
    compiler->registerScriptFile("other", "other.nomad", "count = 1\ncount = alsoMissing");
    CompilerContext context(compiler.get());

    bool compiled = true;
    BOOST_REQUIRE_NO_THROW(compiled = compiler->compileFunctions(&context));

    BOOST_TEST(!compiled);
    BOOST_REQUIRE_EQUAL(context.getErrorCount(), 2);
    BOOST_TEST(context.getDiagnostics()[0].message == "Unknown identifier: missing");
    BOOST_TEST(context.getDiagnostics()[0].sourceName == "broken.nomad");
    BOOST_TEST(context.getDiagnostics()[1].message == "Unknown identifier: alsoMissing");
    BOOST_TEST(context.getDiagnostics()[1].sourceName == "other.nomad");
    BOOST_TEST(context.getDiagnostics()[1].line == 2);
}

namespace {

[[nodiscard]] bool startsWith(const NomadString& text, const NomadString& prefix) {
    return text.rfind(prefix, 0) == 0;
}

} // namespace

BOOST_AUTO_TEST_CASE(compile_functions_infers_return_type_of_function_resolved_later)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    compiler->registerScriptFile("a", "a.nomad", "total = b\nnext = total + 1");
    compiler->registerScriptFile("b", "b.nomad", "return 42");
    CompilerContext context(compiler.get());

    BOOST_TEST(compiler->compileFunctions(&context));
    BOOST_TEST(context.getErrorCount() == 0);
    BOOST_TEST(runtime.getFunction(runtime.getFunctionId("b"))->getReturnType() == runtime.getIntegerType());
}

BOOST_AUTO_TEST_CASE(compile_functions_infers_recursive_return_type_from_later_return)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    compiler->registerScriptFile("countdown", "countdown.nomad", "value = countdown\nreturn 1");
    CompilerContext context(compiler.get());

    BOOST_TEST(compiler->compileFunctions(&context));
    BOOST_TEST(context.getErrorCount() == 0);
}

BOOST_AUTO_TEST_CASE(compile_functions_reports_recursive_function_without_typed_return)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    compiler->registerScriptFile("loop", "loop.nomad", "return loop");
    CompilerContext context(compiler.get());

    BOOST_TEST(!compiler->compileFunctions(&context));
    BOOST_REQUIRE_EQUAL(context.getErrorCount(), 1);
    BOOST_TEST(startsWith(context.getDiagnostics()[0].message, "Cannot infer return type of recursive function 'loop'"));
    BOOST_TEST(context.getDiagnostics()[0].sourceName == "loop.nomad");
}

BOOST_AUTO_TEST_CASE(compile_functions_reports_return_type_mismatch_while_resolving)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    compiler->registerScriptFile("mixed", "mixed.nomad", "if true\n    return 1\nend\nreturn true");
    CompilerContext context(compiler.get());

    BOOST_TEST(!compiler->compileFunctions(&context));
    BOOST_REQUIRE_EQUAL(context.getErrorCount(), 1);
    BOOST_TEST(startsWith(context.getDiagnostics()[0].message, "Invalid return statement"));
    BOOST_TEST(context.getDiagnostics()[0].line == 4);
}

BOOST_AUTO_TEST_CASE(compile_functions_treats_function_without_return_as_void)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    compiler->registerScriptFile("noValue", "noValue.nomad", "x = 1");
    compiler->registerScriptFile("user", "user.nomad", "y = noValue");
    CompilerContext context(compiler.get());

    BOOST_TEST(!compiler->compileFunctions(&context));
    BOOST_REQUIRE_EQUAL(context.getErrorCount(), 1);
    BOOST_TEST(context.getDiagnostics()[0].message == "Function 'noValue' does not return a value");
    BOOST_TEST(runtime.getFunction(runtime.getFunctionId("noValue"))->getReturnType() == runtime.getVoidType());
}

BOOST_AUTO_TEST_CASE(compile_functions_reports_non_boolean_if_condition_while_resolving)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    compiler->registerScriptFile("condition", "condition.nomad", "x = 1\nif x\nend");
    CompilerContext context(compiler.get());

    BOOST_TEST(!compiler->compileFunctions(&context));
    BOOST_REQUIRE_EQUAL(context.getErrorCount(), 1);
    BOOST_TEST(context.getDiagnostics()[0].message == "If statement condition must be a boolean expression");
    BOOST_TEST(context.getDiagnostics()[0].line == 2);
}

BOOST_AUTO_TEST_CASE(compile_functions_reports_format_string_errors_with_location)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    compiler->registerScriptFile("format", "format.nomad", "x = 1\ntext = $\"value {missing}\"");
    CompilerContext context(compiler.get());

    BOOST_TEST(!compiler->compileFunctions(&context));
    BOOST_REQUIRE_EQUAL(context.getErrorCount(), 1);
    BOOST_TEST(context.getDiagnostics()[0].message == "Unknown identifier in format string: missing");
    BOOST_TEST(context.getDiagnostics()[0].sourceName == "format.nomad");
    BOOST_TEST(context.getDiagnostics()[0].line == 2);
}

BOOST_AUTO_TEST_CASE(expression_resolve_reports_unknown_nativeFunction)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());

    CallNativeFunctionExpression expression(nullptr, 1, 2, "missingNativeFunction");

    BOOST_TEST(!expression.resolve(&context, nullptr));
    BOOST_REQUIRE_EQUAL(context.getErrorCount(), 1);
    BOOST_TEST(context.getDiagnostics().front().message == "Unknown native function 'missingNativeFunction'");
}

BOOST_AUTO_TEST_CASE(expression_resolve_reports_unknown_function)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());

    FunctionCallExpression expression(nullptr, 1, 2, "missingFunction");

    BOOST_TEST(!expression.resolve(&context, nullptr));
    BOOST_REQUIRE_EQUAL(context.getErrorCount(), 1);
    BOOST_TEST(context.getDiagnostics().front().message == "Unknown function 'missingFunction'");
}

BOOST_AUTO_TEST_CASE(expression_resolve_reports_invalid_binary_fold)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());

    BinaryExpression expression(
        nullptr,
        4,
        5,
        BinaryOperator::Plus,
        std::make_unique<BooleanLiteral>(nullptr, 4, 5, true),
        std::make_unique<IntegerLiteral>(nullptr, 4, 12, 1)
    );

    NomadBoolean resolved = true;
    BOOST_REQUIRE_NO_THROW(resolved = expression.resolve(&context, nullptr));
    BOOST_TEST(!resolved);
    BOOST_TEST(!expression.isParsed());
    BOOST_REQUIRE_EQUAL(context.getErrorCount(), 1);

    const auto& diagnostic = context.getDiagnostics().front();
    BOOST_TEST(diagnostic.message.starts_with("Invalid binary operator '"));
    BOOST_TEST(diagnostic.line == 4);
    BOOST_TEST(diagnostic.column == 5);
}

BOOST_AUTO_TEST_CASE(expression_resolve_folds_constant_expressions)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());

    BinaryExpression expression(
        nullptr,
        0,
        0,
        BinaryOperator::Star,
        std::make_unique<IntegerLiteral>(nullptr, 0, 0, 6),
        std::make_unique<IntegerLiteral>(nullptr, 0, 0, 7)
    );

    BOOST_TEST(expression.resolve(&context, nullptr));
    BOOST_TEST(!context.hasError());
    BOOST_TEST(expression.isParsed());
    BOOST_TEST(expression.getValue().getIntegerValue() == 42);
}
