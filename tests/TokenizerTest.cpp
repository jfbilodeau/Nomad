// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/compiler/Compilation.hpp>
#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/interpreter/Interpreter.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <cmath>
#include <stdexcept>

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

BOOST_AUTO_TEST_CASE(tokenizer_source_origins_preserve_physical_text_navigation_and_reset)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    const NomadString source{"\n# comment\n  first = 1\n\nsecond = 2"};
    for (const auto firstLine : {NomadIndex{1}, NomadIndex{1000000000}}) {
        CompilerContext context(compiler.get());
        Tokenizer tokens(&context, "fragment.nomad", source, firstLine);
        BOOST_TEST(tokens.getSource() == source);
        BOOST_TEST(tokens.getLineCount() == 5U);
        BOOST_TEST(tokens.getLineIndex() == firstLine - 1);
        for (auto iteration = 0; iteration < 2; ++iteration) {
            BOOST_REQUIRE(tokens.nextLine());
            BOOST_TEST(tokens.getLineIndex() == firstLine + 2);
            BOOST_TEST(tokens.getLine() == "  first = 1");
            BOOST_TEST(tokens.getColumnIndex() == 3U);
            BOOST_REQUIRE(tokens.nextLine());
            BOOST_TEST(tokens.getLineIndex() == firstLine + 4);
            BOOST_TEST(tokens.getLine() == "second = 2");
            BOOST_TEST(tokens.getColumnIndex() == 1U);
            BOOST_TEST(!tokens.nextLine());
            BOOST_TEST(tokens.isEndOfFile());
            BOOST_TEST(tokens.getLineIndex() == firstLine + 4);
            tokens.reset();
            BOOST_TEST(tokens.getLineIndex() == firstLine - 1);
        }

        BOOST_TEST(!context.hasError());
        Tokenizer empty(&context, "empty.nomad", "", firstLine);
        BOOST_TEST(empty.getLineCount() == 0U);
        BOOST_TEST(!empty.nextLine());
        BOOST_TEST(empty.getLineIndex() == firstLine - 1);
    }

    CompilerContext context(compiler.get());
    Tokenizer implicitOrigin(&context, "default.nomad", source);
    Tokenizer explicitOrigin(&context, "default.nomad", source, 1);
    while (implicitOrigin.nextLine()) {
        BOOST_REQUIRE(explicitOrigin.nextLine());
        BOOST_TEST(implicitOrigin.getLineIndex() == explicitOrigin.getLineIndex());
        BOOST_TEST(implicitOrigin.getColumnIndex() == explicitOrigin.getColumnIndex());
        BOOST_TEST(implicitOrigin.getLine() == explicitOrigin.getLine());
    }

    BOOST_TEST(!explicitOrigin.nextLine());
}

BOOST_AUTO_TEST_CASE(tokenizer_source_origins_apply_to_lexical_parser_and_expression_locations)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    const auto firstLine = NomadIndex{1000000000};
    CompilerContext lexical(compiler.get());
    Tokenizer invalid(&lexical, "fragment.nomad", "# comment\n  @\nvalid", firstLine);
    BOOST_REQUIRE_EQUAL(lexical.getDiagnostics().size(), 1U);
    const auto& lexicalError = lexical.getDiagnostics().front();
    BOOST_TEST(lexicalError.sourceName == "fragment.nomad");
    BOOST_TEST(lexicalError.line == firstLine + 1);
    BOOST_TEST(lexicalError.column == 3U);
    BOOST_REQUIRE(invalid.nextLine());
    BOOST_TEST(invalid.getLineIndex() == firstLine + 2);
    BOOST_TEST(invalid.currentToken().textValue == "valid");

    CompilerContext parsing(compiler.get());
    Tokenizer incomplete(&parsing, "parse-fragment.nomad", "\n1 +", firstLine);
    BOOST_REQUIRE(incomplete.nextLine());
    BOOST_TEST(parser::parseExpression(&parsing, nullptr, &incomplete) == nullptr);
    BOOST_REQUIRE(!parsing.getDiagnostics().empty());
    for (const auto& diagnostic : parsing.getDiagnostics()) {
        BOOST_TEST(diagnostic.sourceName == "parse-fragment.nomad");
        BOOST_TEST(diagnostic.line == firstLine + 1);
    }

    CompilerContext valid(compiler.get());
    Tokenizer literal(&valid, "literal.nomad", "\n  17", firstLine);
    BOOST_REQUIRE(literal.nextLine());
    const auto expression = parser::parseExpression(&valid, nullptr, &literal);
    BOOST_REQUIRE(expression != nullptr);
    BOOST_TEST(expression->getLine() == firstLine + 1);
    CompilerContext defaultContext(compiler.get());
    Tokenizer defaultLiteral(&defaultContext, "literal.nomad", "\n  17");
    BOOST_REQUIRE(defaultLiteral.nextLine());
    const auto defaultExpression = parser::parseExpression(&defaultContext, nullptr, &defaultLiteral);
    BOOST_REQUIRE(defaultExpression != nullptr);
    BOOST_TEST(expression->getColumn() == defaultExpression->getColumn());
}

BOOST_AUTO_TEST_CASE(tokenizer_rejects_invalid_source_origins_and_overflowing_line_ranges)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    for (const auto firstLine : {NomadIndex{0}, NOMAD_INVALID_INDEX}) {
        BOOST_CHECK_THROW(Tokenizer(&context, "invalid.nomad", "1", firstLine), std::invalid_argument);
    }

    BOOST_CHECK_THROW(
        Tokenizer(&context, "overflow.nomad", "1\n2", NOMAD_INVALID_INDEX - 1),
        std::invalid_argument
    );
    Tokenizer lastValid(&context, "last.nomad", "1", NOMAD_INVALID_INDEX - 1);
    BOOST_REQUIRE(lastValid.nextLine());
    BOOST_TEST(lastValid.getLineIndex() == NOMAD_INVALID_INDEX - 1);
    BOOST_TEST(!lastValid.nextLine());
    BOOST_TEST(lastValid.getLineIndex() == NOMAD_INVALID_INDEX - 1);
    BOOST_TEST(!context.hasError());
}

BOOST_AUTO_TEST_CASE(tokenizer_accepts_letters_and_underscores_after_identifier_dots)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    for (const auto* name : {"player.score1", "player._score", "player1.child2._value3"}) {
        CompilerContext context(compiler.get());
        Tokenizer tokens(&context, "identifier.nomad", name);

        BOOST_TEST(!context.hasError());
        BOOST_REQUIRE_EQUAL(tokens.getTokenCount(), 1);
        BOOST_TEST(tokens.getTokenTypeAt(0) == TokenType::Identifier);
        BOOST_TEST(tokens.getTokenAt(0) == name);
    }
}

BOOST_AUTO_TEST_CASE(tokenizer_rejects_digits_after_identifier_dots_and_recovers)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    for (const auto* name : {"player.1", "player.1score", "player.child.2value"}) {
        CompilerContext context(compiler.get());
        Tokenizer tokens(&context, "identifier.nomad", NomadString("    ") + name + " = 1\nnext = 2");

        BOOST_REQUIRE_EQUAL(context.getErrorCount(), 1);
        const auto* error = context.getFirstError();
        BOOST_REQUIRE(error != nullptr);
        BOOST_TEST(error->message.find("Unrecognized token") != NomadString::npos);
        BOOST_TEST(error->sourceName == "identifier.nomad");
        BOOST_TEST(error->line == 1);
        BOOST_TEST(error->column == 5);
        BOOST_TEST(tokens.getTokenCount() == 0);
        BOOST_REQUIRE(tokens.nextLine());
        BOOST_TEST(tokens.currentToken().textValue == "next");
        BOOST_TEST(tokens.getLineIndex() == 2);
    }
}

BOOST_AUTO_TEST_CASE(tokenizer_preserves_decimal_literals)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    Tokenizer tokens(&context, "decimal.nomad", "1.25 .5 -2.75");

    BOOST_TEST(!context.hasError());
    BOOST_REQUIRE_EQUAL(tokens.getTokenCount(), 3);
    BOOST_TEST(tokens.getTokenTypeAt(0) == TokenType::Float);
    BOOST_TEST(tokens.getTokenTypeAt(1) == TokenType::Float);
    BOOST_TEST(tokens.getTokenTypeAt(2) == TokenType::Float);
}

BOOST_AUTO_TEST_CASE(tokenizer_does_not_consume_special_float_prefixes_in_identifiers)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    for (const auto* name : {"Info", "Information", "infiniteCount", "InfinityValue", "NaNValue", "nanoseconds"}) {
        CompilerContext context(compiler.get());
        Tokenizer tokens(&context, "identifier.nomad", name);

        BOOST_TEST(!context.hasError());
        BOOST_REQUIRE_EQUAL(tokens.getTokenCount(), 1);
        BOOST_TEST(tokens.getTokenTypeAt(0) == TokenType::Identifier);
        BOOST_TEST(tokens.getTokenAt(0) == name);
    }
}

BOOST_AUTO_TEST_CASE(tokenizer_preserves_special_float_literals)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    Tokenizer tokens(&context, "float.nomad", "inf -inf infinity nan");

    BOOST_REQUIRE(!context.hasError());
    BOOST_REQUIRE_EQUAL(tokens.getTokenCount(), 4);
    for (auto index = NomadIndex{0}; index < tokens.getTokenCount(); ++index) {
        BOOST_TEST(tokens.getTokenTypeAt(index) == TokenType::Float);
    }
    BOOST_TEST(std::isinf(tokens.getFloatTokenAt(0)));
    BOOST_TEST(std::isinf(tokens.getFloatTokenAt(1)));
    BOOST_TEST(tokens.getFloatTokenAt(1) < 0);
    BOOST_TEST(std::isinf(tokens.getFloatTokenAt(2)));
    BOOST_TEST(std::isnan(tokens.getFloatTokenAt(3)));
}

BOOST_AUTO_TEST_CASE(enum_members_can_use_names_with_special_float_prefixes)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    const auto result = compileSources(SourceBatch{{
        {"configuration", {"configuration.nomad",
            "open hub example\n"
            "enum Level\nDebug\nInfo\nWarning\nend\n"
            "class Configuration\nlevel = example.Level.Info\nend\n"
            "end\n"}}
    }, {}}, *compiler);
    for (const auto& diagnostic : result.diagnostics) {
        BOOST_TEST_MESSAGE(formatDiagnostic(diagnostic));
    }
    BOOST_TEST(result.succeeded());
}

BOOST_AUTO_TEST_CASE(tokenizer_preserves_expression_strings_inside_format_tokens)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    Tokenizer tokens(
        &context,
        "nested.nomad",
        R"(return $"line\n{echo "a\"}"} {$"inner {score}"}" == "after")"
    );

    BOOST_TEST(!context.hasError());
    BOOST_REQUIRE_EQUAL(tokens.getTokenCount(), 4);
    BOOST_TEST(tokens.getTokenTypeAt(1) == TokenType::FormatString);
    const NomadString expected = "line\n" R"({echo "a\"}"} {$"inner {score}"})";
    BOOST_TEST(tokens.getTokenAt(1) == expected);
    BOOST_TEST(tokens.getTokenAt(2) == "==");
    BOOST_TEST(tokens.getTokenAt(3) == "after");
}

BOOST_AUTO_TEST_CASE(tokenizer_preserves_format_token_column_after_decoding)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    const NomadString source = R"(    $"a\n{x}")";
    Tokenizer tokens(&context, "offset.nomad", source);

    BOOST_TEST(!context.hasError());
    BOOST_REQUIRE_EQUAL(tokens.getTokenCount(), 1);
    const auto& token = tokens.currentToken();
    BOOST_TEST(token.textValue == "a\n{x}");
    BOOST_TEST(token.column == 5);
}

BOOST_AUTO_TEST_CASE(tokenizer_preserves_legacy_literal_escape_decoding)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    Tokenizer tokens(&context, "escape.nomad", R"($"a\n\r\t\"\\\q")");

    BOOST_TEST(!context.hasError());
    BOOST_REQUIRE_EQUAL(tokens.getTokenCount(), 1);
    BOOST_TEST(tokens.getTokenAt(0) == "a\n\r\t\"\\\\q");
}

BOOST_AUTO_TEST_CASE(tokenizer_retains_malformed_placeholder_for_compiler_diagnostics)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    Tokenizer tokens(&context, "broken.nomad", R"(return $"Score: {score")");

    BOOST_TEST(!context.hasError());
    BOOST_REQUIRE_EQUAL(tokens.getTokenCount(), 2);
    BOOST_TEST(tokens.getTokenAt(1) == "Score: {score");
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
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"broken", {"broken.nomad", "total = 1\ntotal = \"unterminated"}}}, {}});

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
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    BOOST_TEST(!context.getInterpreter().execute("score = \"unterminated", context));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(context.getError()->find("Unrecognized token `\"unterminated`") != NomadString::npos);
    BOOST_TEST(context.getVariableValue("score") == nullptr);
}
