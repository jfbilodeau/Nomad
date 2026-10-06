// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Tokenizer.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>

using namespace nomad;

BOOST_AUTO_TEST_CASE(tokenizer_tokenizes_indented_lines_and_comments)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());

    Tokenizer tokens(&context, "test.nomad", "    score = 1 + 2 # comment  ");

    BOOST_TEST(!context.hasError());
    BOOST_REQUIRE_EQUAL(tokens.getTokenCount(), 5);
    BOOST_TEST(tokens.getTokenAt(0) == "score");
    BOOST_TEST(tokens.getIntegerTokenAt(4) == 2);
    BOOST_TEST(tokens.isReportingTo(&context));
}

BOOST_AUTO_TEST_CASE(tokenizer_requires_compiler_context)
{
    BOOST_CHECK_THROW(Tokenizer(nullptr, "test.nomad", "score = 1"), NomadBug);
}

BOOST_AUTO_TEST_CASE(tokenizer_captures_token_columns)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());

    Tokenizer tokens(&context, "test.nomad", "\tvalue = 12 + value");

    BOOST_REQUIRE(tokens.nextLine());
    BOOST_TEST(tokens.getColumnIndex() == 2);
    BOOST_TEST(tokens.currentToken().column == 2);

    tokens.next();
    BOOST_TEST(tokens.getColumnIndex() == 8);
    tokens.next();
    BOOST_TEST(tokens.getColumnIndex() == 10);
    tokens.next();
    BOOST_TEST(tokens.getColumnIndex() == 13);
    tokens.next();
    BOOST_TEST(tokens.getColumnIndex() == 15);
}

BOOST_AUTO_TEST_CASE(tokenizer_updates_column_when_loading_a_new_line)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());

    Tokenizer tokens(&context, "test.nomad", "first = 1\n    second = 2");

    BOOST_REQUIRE(tokens.nextLine());
    BOOST_TEST(tokens.getColumnIndex() == 1);
    BOOST_REQUIRE(tokens.nextLine());
    BOOST_TEST(tokens.getColumnIndex() == 5);
    BOOST_TEST(tokens.currentToken().column == 5);
}

BOOST_AUTO_TEST_CASE(tokenizer_reports_invalid_line_and_continues_with_following_lines)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());

    Tokenizer tokens(&context, "test.nomad", "first = 1\nsecond = @ 2\nthird = 3");

    BOOST_REQUIRE_EQUAL(context.getErrorCount(), 1);
    const auto* error = context.getFirstError();
    BOOST_REQUIRE(error != nullptr);
    BOOST_TEST(error->message.find("Unrecognized token `@ 2`") != NomadString::npos);
    BOOST_TEST(error->line == 2);
    BOOST_TEST(error->column == 10);

    // The invalid line is treated as empty and skipped.
    BOOST_REQUIRE(tokens.nextLine());
    BOOST_TEST(tokens.currentToken().textValue == "first");
    BOOST_REQUIRE(tokens.nextLine());
    BOOST_TEST(tokens.currentToken().textValue == "third");
    BOOST_TEST(tokens.getLineIndex() == 3);
    BOOST_TEST(!tokens.nextLine());
    BOOST_TEST(tokens.isEndOfFile());
}

BOOST_AUTO_TEST_CASE(compile_functions_reports_tokenizer_errors)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    compiler->registerScriptFile("broken", "broken.nomad", "total = 1\ntotal = \"unterminated");

    CompilerContext context(compiler.get());

    BOOST_TEST(!compiler->compileFunctions(&context));
    BOOST_REQUIRE_EQUAL(context.getErrorCount(), 1);
    const auto* error = context.getFirstError();
    BOOST_TEST(error->message.find("Unrecognized token `\"unterminated`") != NomadString::npos);
    BOOST_TEST(error->sourceName == "broken.nomad");
    BOOST_TEST(error->line == 2);
}

BOOST_AUTO_TEST_CASE(interpreter_context_reports_tokenizer_errors)
{
    Runtime runtime;
    Interpreter context(&runtime);

    BOOST_TEST(!context.execute("score = \"unterminated"));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(context.getError()->find("Unrecognized token `\"unterminated`") != NomadString::npos);
    BOOST_TEST(context.getVariableValue("score") == nullptr);
}
