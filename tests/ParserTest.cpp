// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/Tokenizer.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>

using namespace nomad;

BOOST_AUTO_TEST_CASE(parser_reports_expression_errors_without_throwing)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    Tokenizer tokens(&context, "test.nomad", "1 + (2 * 3");

    std::unique_ptr<Expression> expression;
    BOOST_REQUIRE_NO_THROW(expression = parser::parseExpression(&context, nullptr, &tokens));

    BOOST_TEST(expression == nullptr);
    BOOST_REQUIRE_EQUAL(context.getErrorCount(), 1);
    BOOST_TEST(context.getFirstError()->message == "Expected ')'");
}

BOOST_AUTO_TEST_CASE(parser_reports_unknown_statement)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    Tokenizer tokens(&context, "test.nomad", "missing 1");

    auto statement = parser::parseLine(&context, nullptr, &tokens);

    BOOST_TEST(statement == nullptr);
    BOOST_REQUIRE_EQUAL(context.getErrorCount(), 1);
    BOOST_TEST(context.getFirstError()->message == "Unknown statement, native function or function name: 'missing'");
}

BOOST_AUTO_TEST_CASE(parser_recovers_after_errors_inside_blocks)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    Tokenizer tokens(
        &context,
        "test.nomad",
        "if 1 == 1\n"
        "    missing 1\n"
        "    first = 1 +\n"
        "    second = 2\n"
        "end\n"
        "third = 3"
    );

    std::vector<std::unique_ptr<Statement>> statements;
    while (tokens.nextLine()) {
        statements.push_back(parser::parseLine(&context, nullptr, &tokens));
    }

    // The `if` statement is dropped, but its body and `end` are consumed so `third` still parses.
    BOOST_REQUIRE_EQUAL(statements.size(), 2);
    BOOST_TEST(statements[0] == nullptr);
    BOOST_TEST(statements[1] != nullptr);

    BOOST_REQUIRE_EQUAL(context.getErrorCount(), 2);
    const auto diagnostics = context.getDiagnostics();
    BOOST_TEST(diagnostics[0].message == "Unknown statement, native function or function name: 'missing'");
    BOOST_TEST(diagnostics[0].line == 2);
    BOOST_TEST(diagnostics[1].message == "Unexpected end of line");
    BOOST_TEST(diagnostics[1].line == 3);
}

BOOST_AUTO_TEST_CASE(parser_reports_unclosed_blocks)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    Tokenizer tokens(&context, "test.nomad", "if 1 == 1\n    first = 1");

    BOOST_REQUIRE(tokens.nextLine());
    auto statement = parser::parseLine(&context, nullptr, &tokens);

    BOOST_TEST(statement == nullptr);
    BOOST_REQUIRE_EQUAL(context.getErrorCount(), 1);
    BOOST_TEST(context.getFirstError()->message == "Unexpected end of file");
}

BOOST_AUTO_TEST_CASE(compile_functions_reports_parse_errors_with_location)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    compiler->registerScriptFile("broken", "broken.nomad", "total = 1\ntotal = (2");

    CompilerContext context(compiler.get());

    bool compiled = true;
    BOOST_REQUIRE_NO_THROW(compiled = compiler->compileFunctions(&context));

    BOOST_TEST(!compiled);
    BOOST_REQUIRE_EQUAL(context.getErrorCount(), 1);
    const auto* error = context.getFirstError();
    BOOST_TEST(error->message == "Expected ')'");
    BOOST_TEST(error->sourceName == "broken.nomad");
    BOOST_TEST(error->line == 2);
}

BOOST_AUTO_TEST_CASE(interpreter_context_reports_parse_errors)
{
    Runtime runtime;
    Interpreter context(&runtime);

    BOOST_TEST(!context.execute("score = (1 + 2"));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "Expected ')'");
    BOOST_TEST(context.getVariableValue("score") == nullptr);

    BOOST_TEST(!context.execute("1 +"));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "Unexpected end of line");

    BOOST_TEST(context.execute("score = 3"));
    BOOST_TEST(!context.hasError());
}

BOOST_AUTO_TEST_CASE(parser_rejects_tokenizer_from_another_context)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext tokenizerContext(compiler.get());
    CompilerContext parserContext(compiler.get());
    Tokenizer tokens(&tokenizerContext, "test.nomad", "score = 1");

    BOOST_CHECK_THROW((void)parser::parseLine(&parserContext, nullptr, &tokens), NomadBug);
}

BOOST_AUTO_TEST_CASE(compiler_context_reports_at_tokenizer_position)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    Tokenizer tokens(&context, "test.nomad", "first = 1\nsecond = 2");

    BOOST_REQUIRE(tokens.nextLine());
    BOOST_REQUIRE(tokens.nextLine());
    (void)tokens.next();
    context.reportWarning("Warning at tokenizer", &tokens);
    context.reportError("Error at tokenizer", &tokens);

    BOOST_REQUIRE_EQUAL(context.getDiagnostics().size(), 2);
    for (const auto& diagnostic : context.getDiagnostics()) {
        BOOST_TEST(diagnostic.line == tokens.getLineIndex());
        BOOST_TEST(diagnostic.column == tokens.getColumnIndex());
    }
    BOOST_TEST(context.getFirstError()->message == "Error at tokenizer");
}

BOOST_AUTO_TEST_CASE(compile_functions_stops_after_pass_with_errors)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    // The `fun` header is invalid, so its body must not be parsed as top-level code.
    compiler->registerScriptFile("broken", "broken.nomad", "fun 1\n    total = missing\nend");
    compiler->registerScriptFile("other", "other.nomad", "fun 2\nend");
    CompilerContext context(compiler.get());

    BOOST_TEST(!compiler->compileFunctions(&context));

    // Both pre-parse errors are reported; the parse pass does not run.
    BOOST_REQUIRE_EQUAL(context.getErrorCount(), 2);
    BOOST_TEST(context.getDiagnostics()[0].sourceName == "broken.nomad");
    BOOST_TEST(context.getDiagnostics()[0].line == 1);
    BOOST_TEST(context.getDiagnostics()[1].sourceName == "other.nomad");
}
