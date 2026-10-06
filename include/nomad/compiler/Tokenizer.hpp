// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <ostream>
#include <utility>
#include <vector>

namespace nomad {

// Forward declarations
class CompilerContext;

class Runtime;

class Function;

class TokenizerException : public NomadException {
public:
    explicit TokenizerException(const NomadString& message, NomadIndex lineIndex, NomadIndex character);

private:
    NomadIndex m_lineIndex;
    NomadIndex m_character;
};

enum class TokenType {
    Unknown = 1,
    Operator,
    Keyword,
    Identifier,
    Boolean,
    Integer,
    Float,
    String,
    FormatString,
};

struct Token {
    TokenType type = TokenType::Unknown;
    NomadString textValue;
    NomadIndex column = 0;
    union {
        NomadBoolean booleanValue = NOMAD_FALSE;
        NomadInteger integerValue;
        NomadFloat floatValue;
    };

    Token(const TokenType type, NomadString text) :
        type(type),
        textValue(std::move(text))
    {}

    Token(const TokenType type, NomadString text, const NomadBoolean booleanValue) :
        type(type),
        textValue(std::move(text)),
        booleanValue(booleanValue)
    {}

    Token(const TokenType type, NomadString text, const NomadInteger integerValue) :
        type(type),
        textValue(std::move(text)),
        integerValue(integerValue)
    {}

    Token(const TokenType type, NomadString text, const NomadFloat floatValue) :
        type(type),
        textValue(std::move(text)),
        floatValue(floatValue)
    {}

    Token() = default;
};

inline NomadString toString(TokenType type) {
    switch (type) {
        case TokenType::Unknown:
            return "Unknown";
        case TokenType::Operator:
            return "Operator";
        case TokenType::Keyword:
            return "Keyword";
        case TokenType::Identifier:
            return "Identifier";
        case TokenType::Boolean:
            return "Boolean";
        case TokenType::Integer:
            return "Integer";
        case TokenType::Float:
            return "Float";
        case TokenType::String:
            return "String";
        case TokenType::FormatString:
            return "FormatString";
    }
    return "Unknown";
}

inline std::ostream& operator<<(std::ostream& os, const TokenType& type) {
    os << toString(type);

    return os;
}

inline std::ostream& operator<<(std::ostream& os, const Token& token) {
    os << "Token(type: " << token.type
       << ", text: " << token.textValue
       << ", boolean: " << token.booleanValue
       << ", integer: " << token.integerValue
       << ", float: " << token.floatValue
       << ")";
    return os;
}

class Tokenizer {
public:
    // Lexes every line up front. Invalid lines are reported to the context and treated as empty.
    // The source name (usually the file path) identifies the source in diagnostics.
    Tokenizer(CompilerContext* context, NomadString sourceName, const NomadString& source);

    Tokenizer(const Tokenizer&) = delete;
    Tokenizer& operator=(const Tokenizer&) = delete;

    ~Tokenizer() = default;

    void reset();

    // True when this tokenizer reports to the given context.
    [[nodiscard]] bool isReportingTo(const CompilerContext* context) const;

    [[nodiscard]] const NomadString& getSourceName() const;

    [[nodiscard]] const NomadString& getSource() const;

    [[nodiscard]] bool isEndOfFile() const;

    bool nextLine();

    [[nodiscard]] NomadIndex getLineIndex() const;

    [[nodiscard]] NomadIndex getColumnIndex() const;

    [[nodiscard]] const NomadString& getLine() const;

    [[nodiscard]] NomadIndex getLineCount() const;

    [[nodiscard]] const NomadString& getTokenAt(NomadIndex index) const;

    [[nodiscard]] TokenType getTokenTypeAt(NomadIndex index) const;

    [[nodiscard]] NomadInteger getIntegerTokenAt(NomadIndex index) const;
    [[nodiscard]] NomadFloat getFloatTokenAt(NomadIndex index) const;

    [[nodiscard]] const Token& currentToken() const;

    const Token& next();

    // The expect/next helpers below report an error to the context and return false on mismatch.
    [[nodiscard]] bool nextIdentifier(NomadString& identifier);
    [[nodiscard]] bool nextOperator(NomadString& operatorText);
    [[nodiscard]] bool nextInteger(NomadInteger& value);
    [[nodiscard]] bool nextFloat(NomadFloat& value);

    [[nodiscard]] NomadIndex getTokenCount() const;

    [[nodiscard]] bool endOfLine() const;

    [[nodiscard]] bool is(const NomadString& token) const;

    [[nodiscard]] bool is(TokenType type) const;

    // Consumes the token when it matches.
    [[nodiscard]] bool expect(const NomadString& token);

    // Does not consume the token.
    [[nodiscard]] bool expect(TokenType type);

    // Moves to the next line when the current line has been fully consumed.
    [[nodiscard]] bool expectEndOfLine();

private:
    void reportError(const NomadString& message) const;

    void tokenizeLines();

    void loadLine();

    void skipWhiteSpace(const NomadString& line, NomadIndex& index);

    CompilerContext* m_context;
    NomadString m_sourceName;
    NomadString m_source;

    NomadIndex m_lineIndex = 0;
    NomadIndex m_columnIndex = 0;
    std::vector<NomadString> m_lines;
    std::vector<std::vector<Token>> m_lineTokens;
    NomadIndex m_tokenIndex = 0;
    std::vector<Token> m_tokens;
    bool m_endOfFile = false;
};

} // nomad
