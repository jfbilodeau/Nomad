// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/Function.hpp>

#if defined(NOMAD_DEBUG)
//#define BOOST_SPIRIT_DEBUG
#endif

#define BOOST_NO_CXX98_FUNCTION_BASE
#include "boost/spirit/include/qi.hpp"
#include "boost/phoenix.hpp"

#include <sstream>

BOOST_FUSION_ADAPT_STRUCT(
    nomad::Token,
    (nomad::TokenType, type)
        (nomad::NomadString, text)
        (nomad::NomadBoolean, boolean_value)
        (nomad::NomadInteger, integer_value)
        (nomad::NomadFloat, float_value)
)

namespace nomad {

namespace qi = boost::spirit::qi;
namespace phoenix = boost::phoenix;

template<typename Iterator>
struct CommentSkipper : qi::grammar<Iterator> {
    CommentSkipper() : CommentSkipper::base_type(skip) {
//        BOOST_SPIRIT_DEBUG_NODE(skip);
    }

    qi::rule<Iterator> skip =
        qi::space |
        ('#' >> *(qi::char_ - qi::eol) >> (qi::eol | qi::eoi));
};

template<typename Iterator>
struct LineGrammar : boost::spirit::qi::grammar<Iterator, std::vector<Token>()> {
    LineGrammar() :
        LineGrammar::base_type(start) {

//        BOOST_SPIRIT_DEBUG_NODE(hex_value);
//        BOOST_SPIRIT_DEBUG_NODE(decimal_value);
//        BOOST_SPIRIT_DEBUG_NODE(value);
//        BOOST_SPIRIT_DEBUG_NODE(escape_sequence);
//        BOOST_SPIRIT_DEBUG_NODE(quoted_content);
//        BOOST_SPIRIT_DEBUG_NODE(format_content);
//        BOOST_SPIRIT_DEBUG_NODE(literal_string);
//        BOOST_SPIRIT_DEBUG_NODE(format_string);
//        BOOST_SPIRIT_DEBUG_NODE(operators);
//        BOOST_SPIRIT_DEBUG_NODE(keyword);
//        BOOST_SPIRIT_DEBUG_NODE(identifier);
//        BOOST_SPIRIT_DEBUG_NODE(basic_token);
//        BOOST_SPIRIT_DEBUG_NODE(token);
//        BOOST_SPIRIT_DEBUG_NODE(start);
//        BOOST_SPIRIT_DEBUG_NODE(skipper);
    }

    CommentSkipper<Iterator> skipper;

//    qi::rule<Iterator, NomadBoolean()>
//        boolean_value = qi::bool_;

    template<typename T>
    struct nomad_real_policies : qi::real_policies<T>
    {
        static bool const expect_dot = true;
    };

    qi::real_parser<NomadFloat, nomad_real_policies<NomadFloat>> float_parser;

    // A leading `+` is never part of a literal (`5+3` is an addition); a leading `-` is.
    qi::rule<Iterator, NomadFloat()>
        float_value = !qi::lit('+') >> float_parser;

    qi::rule<Iterator, NomadInteger()>
        hex_value = qi::lit("0x") >> qi::uint_parser<NomadInteger, 16>(),
        decimal_value = !qi::lit('+') >> qi::long_long,
        integer_value = hex_value | decimal_value;

    qi::rule<Iterator, std::string()>
        escape_sequence = qi::lit('\\') >> (
            qi::char_("\"\\") |
            (qi::lit('n') >> qi::attr('\n')) |
            (qi::lit('t') >> qi::attr('\t')) |
            (qi::lit('r') >> qi::attr('\r'))
        ),
        quoted_content = escape_sequence | (qi::char_ - '"'),
        format_content = escape_sequence | (qi::char_ - '"'),
        literal_string = qi::lexeme['"' >> *quoted_content >> '"'],
        format_string = qi::lexeme["$\"" >> *format_content >> '"'],
        operators = qi::lexeme[
            qi::string("==") |
            qi::string("!=") |
            qi::string(">=") |
            qi::string("<=") |
            qi::string("&&") |
            qi::string("||") |
            qi::string("&") |
            qi::string("^") |
            qi::string("|") |
            qi::string("!") |
            qi::string("=") |
            qi::string("+") |
            qi::string("-") |
            qi::string("*") |
            qi::string("/") |
            qi::string("%") |
            qi::string(">") |
            qi::string("<") |
            qi::string(":") |
            qi::string("(") |
            qi::string(")")
        ],
//        keyword = qi::string("true") | qi::string("false") | qi::string("fun"),
        identifier = (qi::alpha | qi::char_("_")) >>
            *(qi::alnum | qi::char_("_") | (qi::char_('.') >> &(qi::alnum | qi::char_('_'))));

    qi::rule<Iterator, Token()> basic_token = (
//        boolean_value[qi::_val = phoenix::construct<Token>(phoenix::val(TokenType::Boolean), phoenix::val(""), qi::_1)] |
        float_value[qi::_val = phoenix::construct<Token>(phoenix::val(TokenType::Float), phoenix::val(""), qi::_1)] |
        integer_value[qi::_val = phoenix::construct<Token>(phoenix::val(TokenType::Integer), phoenix::val(""), qi::_1)] |
        literal_string[qi::_val = phoenix::construct<Token>(phoenix::val(TokenType::String), qi::_1, phoenix::val(0.0f))] |
        format_string[qi::_val = phoenix::construct<Token>(phoenix::val(TokenType::FormatString), qi::_1, phoenix::val(0.0f))] |
        operators[qi::_val = phoenix::construct<Token>(phoenix::val(TokenType::Operator), qi::_1, phoenix::val(0.0f))] |
//        keyword[qi::_val = phoenix::construct<Token>(phoenix::val(TokenType::Keyword), qi::_1, phoenix::val(0.0))] |
        identifier[qi::_val = phoenix::construct<Token>(phoenix::val(TokenType::Identifier), qi::_1, phoenix::val(0.0f))]
    );

    qi::rule<Iterator, Token()> token = basic_token >> *skipper;

    // Consumes as many tokens as possible so the caller can locate the first invalid character.
    qi::rule<Iterator, std::vector<Token>()> tokens = *token;

    qi::rule<Iterator, std::vector<Token>()> start = *token >> qi::eoi;
};

TokenizerException::TokenizerException(const NomadString& message, NomadIndex lineIndex, NomadIndex character) :
    NomadException(message),
    m_lineIndex(lineIndex),
    m_character(character) {}

Tokenizer::Tokenizer(CompilerContext* context, NomadString sourceName, const NomadString& source) :
    m_context(context),
    m_sourceName(std::move(sourceName)),
    m_source(source) {
    if (m_context == nullptr) {
        throw NomadBug("Tokenizer requires a compiler context");
    }

    std::stringstream stream(source);

    NomadString line;

    while (std::getline(stream, line)) {
        m_lines.push_back(line);
    }

    tokenizeLines();

    m_endOfFile = m_lines.empty();

    if (!isEndOfFile()) {
        loadLine();
    }
}

const NomadString& Tokenizer::getSourceName() const {
    return m_sourceName;
}

bool Tokenizer::isReportingTo(const CompilerContext* context) const {
    return m_context == context;
}

void Tokenizer::reset() {
    m_lineIndex = 0;
    m_tokenIndex = 0;
    m_endOfFile = false;
    m_columnIndex = 0;
    m_tokens.clear();
}

const NomadString& Tokenizer::getSource() const {
    return m_source;
}

bool Tokenizer::isEndOfFile() const {
//    return m_line_index >= (int) m_lines.size();
    return m_endOfFile;
}

bool Tokenizer::nextLine() {
    m_tokens.clear();
    m_tokenIndex = 0;

    do {
        if (m_lineIndex >= m_lines.size()) {
            m_endOfFile = true;

            return false;
        }

        loadLine();

        m_lineIndex++;

        if (getTokenCount() != 0) {
            return true;
        }
    } while (!isEndOfFile());

    return false;
}

bool Tokenizer::is(const NomadString& token) const {
    return !endOfLine() && currentToken().textValue == token;
}

bool Tokenizer::is(TokenType type) const {
    if (endOfLine()) {
        return false;
    }

    return currentToken().type == type;
}

bool Tokenizer::expect(const NomadString& token) {
    if (!is(token)) {
        reportError("Expected '" + token + "'");

        return false;
    }

    next();

    return true;
}

bool Tokenizer::expect(TokenType type) {
    if (!is(type)) {
        reportError("Expected '" + toString(type) + "'");

        return false;
    }

    return true;
}

NomadIndex Tokenizer::getLineIndex() const {
    return m_lineIndex;
}

NomadIndex Tokenizer::getColumnIndex() const {
    return m_columnIndex;
}

const NomadString& Tokenizer::getLine() const {
    return m_lines[m_lineIndex-1];
}

NomadIndex Tokenizer::getLineCount() const {
    return m_lines.size();
}

const NomadString& Tokenizer::getTokenAt(NomadIndex index) const {
    return m_tokens[index].textValue;
}

TokenType Tokenizer::getTokenTypeAt(NomadIndex index) const {
    return m_tokens[index].type;
}

NomadInteger Tokenizer::getIntegerTokenAt(NomadIndex index) const {
    return m_tokens[index].integerValue;
}

NomadFloat Tokenizer::getFloatTokenAt(NomadIndex index) const {
    return m_tokens[index].floatValue;
}


const Token& Tokenizer::currentToken() const {
    return m_tokens[m_tokenIndex];
}

const Token& Tokenizer::next() {
    const Token& tok = m_tokens[m_tokenIndex++];

    if (m_tokenIndex < m_tokens.size()) {
        m_columnIndex = m_tokens[m_tokenIndex].column;
    } else {
        m_columnIndex = 0;
    }

    return tok;
}

bool Tokenizer::nextIdentifier(NomadString& identifier) {
    if (!expect(TokenType::Identifier)) {
        return false;
    }

    identifier = next().textValue;

    return true;
}

bool Tokenizer::nextOperator(NomadString& operatorText) {
    if (!expect(TokenType::Operator)) {
        return false;
    }

    operatorText = next().textValue;

    return true;
}

bool Tokenizer::nextInteger(NomadInteger& value) {
    if (!expect(TokenType::Integer)) {
        return false;
    }

    value = next().integerValue;

    return true;
}

bool Tokenizer::nextFloat(NomadFloat& value) {
    if (!expect(TokenType::Float)) {
        return false;
    }

    value = next().floatValue;

    return true;
}

NomadIndex Tokenizer::getTokenCount() const {
    return m_tokens.size();
}

bool Tokenizer::endOfLine() const {
    return m_tokenIndex >= m_tokens.size();
}

bool Tokenizer::expectEndOfLine() {
    if (!endOfLine()) {
        reportError("Expected end of line.");

        return false;
    }

    nextLine();

    return true;
}

void Tokenizer::reportError(const NomadString& message) const {
    m_context->reportError(message, this);
}

void Tokenizer::tokenizeLines() {
    const LineGrammar<NomadString::const_iterator> lineGrammar;

    m_lineTokens.reserve(m_lines.size());

    for (auto lineIndex = NomadIndex{0}; lineIndex < m_lines.size(); ++lineIndex) {
        const auto& line = m_lines[lineIndex];
        std::vector<Token> lineTokens;

        auto first = line.cbegin();
        auto success = true;

        while (first != line.cend()) {
            qi::skip_over(first, line.cend(), lineGrammar.skipper);
            if (first == line.cend()) {
                break;
            }

            const auto tokenStart = first;
            Token token;
            success = qi::parse(first, line.cend(), lineGrammar.basic_token, token);
            if (!success) {
                first = tokenStart;
                break;
            }

            token.column = static_cast<NomadIndex>(tokenStart - line.cbegin()) + 1;
            lineTokens.push_back(std::move(token));
        }

        if (!success || first != line.cend()) {
            const auto column = static_cast<NomadIndex>(first - line.cbegin()) + 1;
            m_context->reportError(
                "Unrecognized token `" + NomadString(first, line.cend()) + "` in line: `" + line + "`",
                m_sourceName,
                lineIndex + 1,
                column
            );
            lineTokens.clear();
        }

        m_lineTokens.push_back(std::move(lineTokens));
    }
}

void Tokenizer::loadLine() {
    m_tokens = m_lineTokens[m_lineIndex];

    m_tokenIndex = 0;
    if (!m_tokens.empty()) {
        m_columnIndex = m_tokens[0].column;
    } else {
        m_columnIndex = 0;
    }
}

void Tokenizer::skipWhiteSpace(const NomadString& line, NomadIndex& index) {
    while (index < line.size() && line[index] == ' ') {
        ++index;
    }
}

} // nomad
