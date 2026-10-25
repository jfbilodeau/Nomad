// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/script/Runtime.hpp>

#include <nomad/compiler/Argument.hpp>
#include <nomad/compiler/Expression.hpp>
#include <nomad/compiler/Identifier.hpp>

#include <nomad/compiler/SyntaxTree.hpp>

namespace nomad {

// Forward declaration
class Compiler;
class CompilerContext;
class Tokenizer;

enum class StatementAvailability {
    Compiler = 1,
    Interpreter,
    Both
};

enum class PreParsePhase {
    Declarations = 1,
    Signatures
};

// Parse functions report errors to their CompilerContext, which must be the tokenizer's context. Functions returning a
// pointer return nullptr on error, functions returning bool return false, and functions returning an id return
// NOMAD_INVALID_ID.
// Pre-parse functions return false on error.
using PreParseStatementFn = bool (*)(CompilerContext*, ScriptCallableBase*, Tokenizer*, PreParsePhase);
// Returns nullptr on error.
using ParseStatementFn = std::unique_ptr<Statement> (*)(CompilerContext*, ScriptCallableBase*, Tokenizer*);

namespace parser {

[[nodiscard]] const Type* parseType(CompilerContext* context, Tokenizer* tokens);
[[nodiscard]] bool parseParameters(
    CompilerContext* context,
    Tokenizer* tokens,
    std::vector<Parameter>& parameters
);
// Counts end-delimited blocks opened on a line, including inline fun callbacks but not then continuations.
[[nodiscard]] NomadIndex countBlockOpenings(const Tokenizer* tokens);

[[nodiscard]] bool expectEndOfLine(
    CompilerContext* context,
    ScriptCallableBase* function,
    Tokenizer* tokens,
    NomadIndex statementLine = NOMAD_INVALID_INDEX
);
[[nodiscard]] bool hasAssignmentOperator(const Tokenizer* tokens);

IdentifierType getIdentifierType(const Compiler* compiler, const NomadString& name, const ScriptCallableBase* function = nullptr);
[[nodiscard]] bool isNativeMemberCandidate(const Compiler* compiler, const ScriptCallableBase* function,
                                               const NomadString& name);

// Returns nullptr for empty lines, for statements that are only handled during pre-parsing, and on error.
// Compare the context error count to detect errors.
std::unique_ptr<Statement> parseLine(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokenizer);
// Parses lines until one of `endTokens` is found. Lines with errors are skipped so the rest of the block is still
// parsed. Returns false if any error was reported.
[[nodiscard]] bool parseBlock(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens, const std::vector<NomadString>& endTokens, StatementList* statements);
std::unique_ptr<StringLiteral> parseStringExpression(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens);
std::unique_ptr<Expression> parseExpression(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens);
std::unique_ptr<Expression> parseRootExpression(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens, const Statement* statement);
std::unique_ptr<Statement> parseAssignmentStatement(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens);
std::unique_ptr<Statement> parseStatement(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokenizer);
void parseFunctionParameter(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens, const Parameter& parameter);
std::unique_ptr<Statement> parseFunctionCall(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens, const NomadString& functionName);
std::unique_ptr<Expression> parseLogicalAndExpression(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseLogicalOrExpression(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseRelationalExpression(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseBitwiseAndExpression(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseBitwiseXorExpression(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseBitwiseOrExpression(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseTermExpression(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseProductExpression(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseParenthesesExpression(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseUnaryOperatorExpression(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parsePostfixExpression(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseNewExpression(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parsePrimaryExpression(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens, const Expression* parent);
std::unique_ptr<Expression> parseFunctionCallExpression(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens, const Expression* parent, const NomadString& functionName);
[[nodiscard]] bool parseCallableArguments(
    CompilerContext* context,
    ScriptCallableBase* function,
    Tokenizer* tokens,
    const std::vector<Parameter>& parameters,
    ArgumentList* arguments,
    bool allowOmittedArguments = false
);
[[nodiscard]] bool parseCallableArguments(
    CompilerContext* context,
    ScriptCallableBase* function,
    Tokenizer* tokens,
    const Callable& callable,
    ArgumentList* arguments,
    bool allowOmittedArguments = false
);
[[nodiscard]] CallableId parseCallbackParameter(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens, const FunctionType* callbackType);
std::unique_ptr<Argument> parseExpressionArgument(
    CompilerContext* context,
    const Type* argumentType,
    ScriptCallableBase* function,
    Tokenizer* tokens
);
std::unique_ptr<Argument> parsePredicateArgument(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens);

} // namespace parser

} // namespace nomad
