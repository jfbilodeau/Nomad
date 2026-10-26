// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include "nomad/Nomad.hpp"

#include "nomad/compiler/SyntaxTree.hpp"

#include <memory>

namespace nomad {

// Forward declaration
class Compiler;
class Expression;
class ScriptCallableBase;
class Statement;
class SyntaxTree;
class Tokenizer;

class ReturnStatementNode final : public Statement {
public:
    explicit ReturnStatementNode(NomadIndex line, NomadIndex column, std::unique_ptr<Expression> expression);

    [[nodiscard]] NomadBoolean alwaysReturns() const override;

protected:
    void onResolve(CompilerContext* context, ScriptCallableBase* function) override;
    void onCompile(Compiler* compiler, ScriptCallableBase* function) override;

private:
    std::unique_ptr<Expression> m_expression;
};

std::unique_ptr<Statement> parseReturnStatement(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens);

} // namespace nomad
