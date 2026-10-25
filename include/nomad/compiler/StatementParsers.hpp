// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/compiler/SyntaxTree.hpp>

#include <memory>
#include <vector>

namespace nomad {

enum class PreParsePhase;

// Forward declarations
class Compiler;
class FunStatementNode;
class Tokenizer;
class FunctionBuilder;
class Statement;
class StatementList;

NomadString generateFunFunctionName(CompilerContext* context, ScriptCallableBase* function, NomadIndex line);

class FunStatementNode : public Statement {
public:
    FunStatementNode(
        NomadIndex col, NomadIndex row, ScriptCallableBase* function, std::unique_ptr<Expression> ptr
    );

    void addParameter(const NomadString& parameter_name);
    [[nodiscard]] NomadIndex getParameterCount() const;

    [[nodiscard]] ScriptCallableBase* getFunction() const;
    [[nodiscard]] const NomadString& getFunctionName() const;
    StatementList& getBody();

protected:
    void onCompile(Compiler* compiler, ScriptCallableBase* function) override;

private:
    ScriptCallableBase* m_function;
    std::vector<NomadString> m_parameters;
    StatementList m_body;
};

// Compile the parameter list of a `fun`
void parseFunParameters(ScriptCallableBase* function, CompilerContext* context, Tokenizer* tokens);

// Compile the parameter list of a callback `fun`. Returns false on error.
[[nodiscard]] bool parseCallbackFunParameters(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens, const FunctionType* callbackType);

// Not a nativeFunction. Just a helper to compile the body of a function. Consumes the closing `end`. Returns nullptr on error.
std::unique_ptr<FunctionNode> parseFunBody(
    CompilerContext* context,
    ScriptCallableBase* function,
    Tokenizer* tokens,
    NomadIndex startLine
);

// Helper to compile the body of a `then` callback (consumes remaining function body). Returns nullptr on error.
std::unique_ptr<FunctionNode> parseThenBody(
    CompilerContext* context,
    ScriptCallableBase* function,
    Tokenizer* tokens,
    NomadIndex startLine
);

// Statement parsers. Errors are reported to the tokenizer's context; pre-parse functions return false and parse
// functions return nullptr on error.
std::unique_ptr<Statement> parseHubStatement(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens);
bool preParseHubStatement(
    CompilerContext* context,
    ScriptCallableBase* function,
    Tokenizer* tokens,
    PreParsePhase phase
);
std::unique_ptr<Statement> parseOpenStatement(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens);
bool preParseOpenStatement(
    CompilerContext* context,
    ScriptCallableBase* function,
    Tokenizer* tokens,
    PreParsePhase phase
);
bool preParseFunStatement(
    CompilerContext* context,
    ScriptCallableBase* function,
    Tokenizer* tokens,
    PreParsePhase phase
);
std::unique_ptr<Statement> parseFunStatement(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens);
std::unique_ptr<Statement> parseAssertStatement(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens);
bool preParseConstStatement(
    CompilerContext* context,
    ScriptCallableBase* function,
    Tokenizer* tokens,
    PreParsePhase phase
);
std::unique_ptr<Statement> parseConstStatement(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens);
bool preParseEnumStatement(
    CompilerContext* context,
    ScriptCallableBase* function,
    Tokenizer* tokens,
    PreParsePhase phase
);
std::unique_ptr<Statement> parseEnumStatement(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens);
std::unique_ptr<Statement> parseIfStatement(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens);
bool preParseParamsStatement(
    CompilerContext* context,
    ScriptCallableBase* function,
    Tokenizer* tokens,
    PreParsePhase phase
);
std::unique_ptr<Statement> parseEventStatement(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens);
std::unique_ptr<Statement> parseOnStatement(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens);

} // nomad
