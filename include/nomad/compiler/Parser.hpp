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
using PreParseStatementFn = bool (*)(CompilerContext*, ScriptFunction*, Tokenizer*);
// Returns nullptr on error.
using ParseStatementFn = std::unique_ptr<Statement> (*)(CompilerContext*, ScriptFunction*, Tokenizer*);

namespace parser {

[[nodiscard]] const Type* parseType(CompilerContext* context, Tokenizer* tokens);

[[nodiscard]] bool expectEndOfLine(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens);
[[nodiscard]] bool hasAssignmentOperator(const Tokenizer* tokens);

IdentifierType getIdentifierType(const Compiler* compiler, const NomadString& name, const ScriptFunction* function = nullptr);
[[nodiscard]] bool isNativeMemberCandidate(const Compiler* compiler, const ScriptFunction* function,
                                               const NomadString& name);

// Returns nullptr for empty lines, for statements that are only handled during pre-parsing, and on error.
// Compare the context error count to detect errors.
std::unique_ptr<Statement> parseLine(CompilerContext* context, ScriptFunction* function, Tokenizer* tokenizer);
// Parses lines until one of `endTokens` is found. Lines with errors are skipped so the rest of the block is still
// parsed. Returns false if any error was reported.
[[nodiscard]] bool parseBlock(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const std::vector<NomadString>& endTokens, StatementList* statements);
std::unique_ptr<StringLiteral> parseStringExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens);
std::unique_ptr<Expression> parseExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens);
std::unique_ptr<Expression> parseRootExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const Statement* statement);
std::unique_ptr<Statement> parseAssignmentStatement(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens);
std::unique_ptr<Statement> parseStatement(CompilerContext* context, ScriptFunction* function, Tokenizer* tokenizer);
std::unique_ptr<Statement> parseNativeFunctionStatement(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const NativeFunction& nativeFunction);
void parseFunctionParameter(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const ParameterDefinition& parameter);
std::unique_ptr<Statement> parseFunctionCall(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const NomadString& functionName);
std::unique_ptr<Expression> parseLogicalAndExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseLogicalOrExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseRelationalExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseBitwiseAndExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseBitwiseXorExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseBitwiseOrExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseTermExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseProductExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseParenthesesExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseUnaryOperatorExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parsePostfixExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseNewExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parsePrimaryExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseFunctionCallExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const Expression* parent, const NomadString& functionName);
[[nodiscard]] bool parseFunctionArguments(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const ScriptFunction* targetFunction, ArgumentList* arguments);
std::unique_ptr<Expression> parseNativeFunctionCallExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const Expression* parent, const NativeFunction& nativeFunction);
[[nodiscard]] bool parseNativeFunctionArguments(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens,
    const Callable& nativeFunction,
    ArgumentList* arguments,
    bool allowOmittedArguments = false
);
[[nodiscard]] NomadId parseCallbackParameter(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const FunctionType* callbackType);
std::unique_ptr<Argument> parseExpressionArgument(
    CompilerContext* context,
    const Type* argumentType,
    ScriptFunction* function,
    Tokenizer* tokens
);
std::unique_ptr<Argument> parsePredicateArgument(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens);

} // namespace parser

} // namespace nomad
