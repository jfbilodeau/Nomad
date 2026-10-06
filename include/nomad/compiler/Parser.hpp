// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/script/Runtime.hpp>

#include <nomad/compiler/Argument.hpp>
#include <nomad/compiler/Expression.hpp>

#include <nomad/compiler/SyntaxTree.hpp>

namespace nomad {

// Forward declaration
class Compiler;
class CompilerContext;
class Tokenizer;

// Parse functions report errors to their CompilerContext, which must be the tokenizer's context. Functions returning a
// pointer return nullptr on error, functions returning bool return false, and functions returning an id return
// NOMAD_INVALID_ID.
// Pre-parse functions return false on error.
using PreParseStatementFn = bool (*)(CompilerContext*, Function*, Tokenizer*);
// Returns nullptr on error.
using ParseStatementFn = std::unique_ptr<Statement> (*)(CompilerContext*, Function*, Tokenizer*);

namespace parser {

[[nodiscard]] bool expectEndOfLine(CompilerContext* context, Function* function, Tokenizer* tokens);

IdentifierType getIdentifierType(const Compiler* compiler, const NomadString& name, const Function* function = nullptr);

// Returns nullptr for empty lines, for statements that are only handled during pre-parsing, and on error.
// Compare the context error count to detect errors.
std::unique_ptr<Statement> parseLine(CompilerContext* context, Function* function, Tokenizer* tokenizer);
// Parses lines until one of `endTokens` is found. Lines with errors are skipped so the rest of the block is still
// parsed. Returns false if any error was reported.
[[nodiscard]] bool parseBlock(CompilerContext* context, Function* function, Tokenizer* tokens, const std::vector<NomadString>& endTokens, StatementList* statements);
std::unique_ptr<StringLiteral> parseStringExpression(CompilerContext* context, Function* function, Tokenizer* tokens);
std::unique_ptr<Expression> parseExpression(CompilerContext* context, Function* function, Tokenizer* tokens);
std::unique_ptr<Expression> parseRootExpression(CompilerContext* context, Function* function, Tokenizer* tokens, const Statement* statement);
std::unique_ptr<Statement> parseAssignmentStatement(CompilerContext* context, Function* function, Tokenizer* tokens);
std::unique_ptr<Statement> parseStatement(CompilerContext* context, Function* function, Tokenizer* tokenizer);
std::unique_ptr<Statement> parseNativeFunctionStatement(CompilerContext* context, Function* function, Tokenizer* tokens, const NativeFunctionDefinition& nativeFunction);
void parseFunctionParameter(CompilerContext* context, Function* function, Tokenizer* tokens, const NativeFunctionParameterDefinition& parameter);
std::unique_ptr<Statement> parseFunctionCall(CompilerContext* context, Function* function, Tokenizer* tokens, const NomadString& functionName);
std::unique_ptr<Expression> parseLogicalAndExpression(CompilerContext* context, Function* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseLogicalOrExpression(CompilerContext* context, Function* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseRelationalExpression(CompilerContext* context, Function* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseBitwiseAndExpression(CompilerContext* context, Function* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseBitwiseXorExpression(CompilerContext* context, Function* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseBitwiseOrExpression(CompilerContext* context, Function* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseTermExpression(CompilerContext* context, Function* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseProductExpression(CompilerContext* context, Function* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseParenthesesExpression(CompilerContext* context, Function* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseUnaryOperatorExpression(CompilerContext* context, Function* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parsePrimaryExpression(CompilerContext* context, Function* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseFunctionCallExpression(CompilerContext* context, Function* function, Tokenizer* tokens, const Expression* parent, const NomadString& functionName);
[[nodiscard]] bool parseFunctionArguments(CompilerContext* context, Function* function, Tokenizer* tokens, const Function* targetFunction, ArgumentList* arguments);
std::unique_ptr<Expression> parseNativeFunctionCallExpression(CompilerContext* context, Function* function, Tokenizer* tokens, const Expression* parent, const NativeFunctionDefinition& nativeFunction);
[[nodiscard]] bool parseNativeFunctionArguments(CompilerContext* context, Function* function, Tokenizer* tokens, const NativeFunctionDefinition& nativeFunction, ArgumentList* arguments);
[[nodiscard]] NomadId parseCallbackParameter(CompilerContext* context, Function* function, Tokenizer* tokens, const FunctionType* callbackType);
std::unique_ptr<Argument> parseExpressionArgument(
    CompilerContext* context,
    const Type* argumentType,
    Function* function,
    Tokenizer* tokens,
    ParameterAccess parameterAccess = ParameterAccess::Default
);
std::unique_ptr<Argument> parsePredicateArgument(CompilerContext* context, Function* function, Tokenizer* tokens);

} // namespace parser

} // namespace nomad
