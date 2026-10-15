// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/FormatStringCompiler.hpp>

#include <boost/test/unit_test.hpp>

using namespace nomad;

BOOST_AUTO_TEST_SUITE(format_template_parser)

BOOST_AUTO_TEST_CASE(decodes_literal_escapes_without_decoding_placeholder_expressions)
{
    const auto decoded = decodeFormatStringLiteral(R"(line\n{echo "a\"}"} {$"inner\n{score}"})");
    const NomadString expected = "line\n" R"({echo "a\"}"} {$"inner\n{score}"})";

    BOOST_TEST(decoded == expected);
    BOOST_TEST(decodeFormatStringLiteral("").empty());
}

BOOST_AUTO_TEST_CASE(decodes_prefix_before_unclosed_placeholder)
{
    const auto decoded = decodeFormatStringLiteral(R"(line\n{echo "unfinished\n)");
    const NomadString expected = "line\n" R"({echo "unfinished\n)";

    BOOST_TEST(decoded == expected);
}

BOOST_AUTO_TEST_CASE(scans_literals_and_placeholders_without_resolving)
{
    const auto parsed = parseFormatTemplate("Score: {score + bonus}!");

    BOOST_TEST(parsed.diagnostics.empty());
    BOOST_REQUIRE_EQUAL(parsed.segments.size(), 3);
    BOOST_CHECK(parsed.segments[0].type == FormatTemplateSegmentType::Literal);
    BOOST_TEST(parsed.segments[0].text == "Score: ");
    BOOST_TEST(parsed.segments[0].offset == 0);
    BOOST_TEST(parsed.segments[0].length == 7);
    BOOST_CHECK(parsed.segments[1].type == FormatTemplateSegmentType::Placeholder);
    BOOST_TEST(parsed.segments[1].text == "score + bonus");
    BOOST_TEST(parsed.segments[1].offset == 7);
    BOOST_TEST(parsed.segments[1].length == 15);
    BOOST_TEST(!parsed.segments[1].diagnosticIndex.has_value());
    BOOST_TEST(parsed.segments[2].text == "!");
    BOOST_TEST(parsed.segments[2].offset == 22);
}

BOOST_AUTO_TEST_CASE(preserves_template_values_and_adjacent_placeholders)
{
    const auto parsed = parseFormatTemplate(R"(line\n{x}{global.y}})");

    BOOST_TEST(parsed.diagnostics.empty());
    BOOST_REQUIRE_EQUAL(parsed.segments.size(), 4);
    BOOST_TEST(parsed.segments[0].text == R"(line\n)");
    BOOST_TEST(parsed.segments[1].text == "x");
    BOOST_TEST(parsed.segments[2].text == "global.y");
    BOOST_TEST(parsed.segments[3].text == "}");
    BOOST_TEST(parseFormatTemplate("").segments.empty());
}

BOOST_AUTO_TEST_CASE(ignores_braces_in_expression_strings)
{
    const auto parsed = parseFormatTemplate(R"(value={echo "a\"} {b"}; {score})");

    BOOST_TEST(parsed.diagnostics.empty());
    BOOST_REQUIRE_EQUAL(parsed.segments.size(), 4);
    const NomadString expected = R"(echo "a\"} {b")";
    BOOST_TEST(parsed.segments[1].text == expected);
    BOOST_TEST(parsed.segments[3].text == "score");
}

BOOST_AUTO_TEST_CASE(scans_nested_format_strings)
{
    const auto parsed = parseFormatTemplate(R"(outer {$"inner {echo "}"} {$"deep {score}"}"} tail)");

    BOOST_TEST(parsed.diagnostics.empty());
    BOOST_REQUIRE_EQUAL(parsed.segments.size(), 3);
    BOOST_TEST(parsed.segments[1].text == R"($"inner {echo "}"} {$"deep {score}"}")");
    BOOST_TEST(parsed.segments[2].text == " tail");
}

BOOST_AUTO_TEST_CASE(recovers_after_empty_placeholders)
{
    const auto parsed = parseFormatTemplate("a{}b{ \t }c{score}");

    BOOST_REQUIRE_EQUAL(parsed.diagnostics.size(), 2);
    BOOST_REQUIRE_EQUAL(parsed.segments.size(), 6);
    BOOST_CHECK(parsed.segments[1].type == FormatTemplateSegmentType::Error);
    BOOST_REQUIRE(parsed.segments[1].diagnosticIndex.has_value());
    BOOST_TEST(*parsed.segments[1].diagnosticIndex == 0);
    BOOST_TEST(parsed.diagnostics[0].message == "Empty placeholder in format string");
    BOOST_TEST(parsed.diagnostics[0].offset == 1);
    BOOST_TEST(parsed.diagnostics[0].length == 2);
    BOOST_CHECK(parsed.segments[3].type == FormatTemplateSegmentType::Error);
    BOOST_REQUIRE(parsed.segments[3].diagnosticIndex.has_value());
    BOOST_TEST(*parsed.segments[3].diagnosticIndex == 1);
    BOOST_TEST(parsed.diagnostics[1].offset == 4);
    BOOST_TEST(parsed.diagnostics[1].length == 5);
    BOOST_TEST(parsed.segments[5].text == "score");
}

BOOST_AUTO_TEST_CASE(retains_prefix_and_stops_at_an_unclosed_placeholder)
{
    const auto parsed = parseFormatTemplate("Score: {score} next: {bonus");

    BOOST_REQUIRE_EQUAL(parsed.segments.size(), 4);
    BOOST_REQUIRE_EQUAL(parsed.diagnostics.size(), 1);
    BOOST_TEST(parsed.segments[1].text == "score");
    BOOST_TEST(parsed.segments[2].text == " next: ");
    BOOST_CHECK(parsed.segments[3].type == FormatTemplateSegmentType::Error);
    BOOST_TEST(parsed.segments[3].text == "bonus");
    BOOST_TEST(parsed.diagnostics[0].message == "Missing '}' in format string");
    BOOST_TEST(parsed.diagnostics[0].offset == 21);
    BOOST_TEST(parsed.diagnostics[0].length == 6);
}

BOOST_AUTO_TEST_CASE(unclosed_expression_strings_do_not_invent_a_recovery_boundary)
{
    const auto parsed = parseFormatTemplate(R"(prefix {echo "broken} then {score})");

    BOOST_REQUIRE_EQUAL(parsed.segments.size(), 2);
    BOOST_REQUIRE_EQUAL(parsed.diagnostics.size(), 1);
    BOOST_TEST(parsed.segments[0].text == "prefix ");
    BOOST_CHECK(parsed.segments[1].type == FormatTemplateSegmentType::Error);
    BOOST_TEST(parsed.segments[1].text == R"(echo "broken} then {score})");
}

BOOST_AUTO_TEST_CASE(finds_nested_literal_end_without_consuming_following_tokens)
{
    const NomadString source = R"(return $"outer {$"inner {echo "}"}"}" == "after")";
    const auto end = findFormatStringLiteralEnd(source, 7);

    BOOST_REQUIRE(end.has_value());
    BOOST_TEST(source.substr(*end + 1) == R"( == "after")");
    BOOST_TEST(!findFormatStringLiteralEnd(R"($"unclosed {score")", 0).has_value());
    BOOST_TEST(!findFormatStringLiteralEnd(R"($"unclosed)", 0).has_value());
    BOOST_CHECK_THROW(findFormatStringLiteralEnd("ordinary", 0), NomadBug);
    BOOST_CHECK_THROW(findFormatStringLiteralEnd("", NOMAD_INVALID_INDEX), NomadBug);
}

BOOST_AUTO_TEST_CASE(preserves_legacy_brace_behavior_after_unknown_escapes)
{
    const auto parsed = parseFormatTemplate(R"(prefix \{score})");

    BOOST_TEST(parsed.diagnostics.empty());
    BOOST_REQUIRE_EQUAL(parsed.segments.size(), 2);
    BOOST_TEST(parsed.segments[0].text == R"(prefix \)");
    BOOST_TEST(parsed.segments[1].text == "score");
    const auto end = findFormatStringLiteralEnd(R"($"prefix \{echo "}"}" after)", 0);
    BOOST_REQUIRE(end.has_value());
    BOOST_TEST(*end == 20);
}

BOOST_AUTO_TEST_SUITE_END()
