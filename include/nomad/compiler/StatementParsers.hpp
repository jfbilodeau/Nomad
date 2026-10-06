// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/compiler/SyntaxTree.hpp>

#include <memory>
#include <vector>

namespace nomad {

// Forward declarations
class Compiler;
class FunStatementNode;
class Tokenizer;
class FunctionBuilder;
class Statement;
class StatementList;

NomadString generateFunFunctionName(CompilerContext* context, Function* function, NomadIndex line);

class FunStatementNode : public Statement {
public:
    FunStatementNode(
        NomadIndex col, NomadIndex row, Function* function, std::unique_ptr<Expression> ptr
    );

    void addParameter(const NomadString& parameter_name);
    [[nodiscard]] NomadIndex getParameterCount() const;

    [[nodiscard]] Function* getFunction() const;
    [[nodiscard]] const NomadString& getFunctionName() const;
    StatementList& getBody();

protected:
    void onCompile(Compiler* compiler, Function* function) override;

private:
    Function* m_function;
    std::vector<NomadString> m_parameters;
    StatementList m_body;
};

// Compile the parameter list of a `fun`
void parseFunParameters(Function* function, CompilerContext* context, Tokenizer* tokens);

// Compile the parameter list of a callback `fun`. Returns false on error.
[[nodiscard]] bool parseCallbackFunParameters(CompilerContext* context, Function* function, Tokenizer* tokens, const FunctionType* callbackType);

// Not a nativeFunction. Just a helper to compile the body of a function. Consumes the closing `end`. Returns nullptr on error.
std::unique_ptr<FunctionNode> parseFunBody(CompilerContext* context, Function* function, Tokenizer* tokens);

// Helper to compile the body of a `then` callback (consumes remaining function body). Returns nullptr on error.
std::unique_ptr<FunctionNode> parseThenBody(CompilerContext* context, Function* function, Tokenizer* tokens);

// Statement parsers. Errors are reported to the tokenizer's context; pre-parse functions return false and parse
// functions return nullptr on error.
bool preParseFunStatement(CompilerContext* context, Function* function, Tokenizer* tokens);
std::unique_ptr<Statement> parseFunStatement(CompilerContext* context, Function* function, Tokenizer* tokens);
std::unique_ptr<Statement> parseAssertStatement(CompilerContext* context, Function* function, Tokenizer* tokens);
bool preParseConstStatement(CompilerContext* context, Function* function, Tokenizer* tokens);
std::unique_ptr<Statement> parseConstStatement(CompilerContext* context, Function* function, Tokenizer* tokens);
std::unique_ptr<Statement> parseIfStatement(CompilerContext* context, Function* function, Tokenizer* tokens);
bool preParseParamsStatement(CompilerContext* context, Function* function, Tokenizer* tokens);
std::unique_ptr<Statement> parseEventStatement(CompilerContext* context, Function* function, Tokenizer* tokens);
std::unique_ptr<Statement> parseOnStatement(CompilerContext* context, Function* function, Tokenizer* tokens);

} // nomad
