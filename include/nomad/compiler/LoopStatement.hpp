// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/SyntaxTree.hpp>

namespace nomad {

class Tokenizer;

class LoopStatement : public Statement {
public:
    LoopStatement(NomadIndex line, NomadIndex column, bool postTest, bool until);
    [[nodiscard]] StatementList& getBody();
    void setCondition(std::unique_ptr<Expression> condition);
    void setUntil(bool until);
    void compileControl(Compiler* compiler, bool isBreak);
    void checkReachability(CompilerContext* context, const ScriptFunction* function) const override;

protected:
    void patchControls(Compiler* compiler, NomadIndex continuation, NomadIndex end);
    void resetControls();
    void onResolve(CompilerContext* context, ScriptFunction* function) override;
    void onCompile(Compiler* compiler, ScriptFunction* function) override;
    void onEvaluate(InterpreterContext& context) const override;

    StatementList m_body;

private:
    std::unique_ptr<Expression> m_condition;
    bool m_postTest;
    bool m_until;
    std::vector<NomadIndex> m_breaks;
    std::vector<NomadIndex> m_continues;
};

std::unique_ptr<Statement> parseWhileStatement(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens
);
std::unique_ptr<Statement> parseForStatement(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens
);
std::unique_ptr<Statement> parseUntilStatement(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens
);
std::unique_ptr<Statement> parseDoStatement(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens
);
std::unique_ptr<Statement> parseBreakStatement(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens
);
std::unique_ptr<Statement> parseContinueStatement(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens
);
std::unique_ptr<Statement> parseLoopControlStatement(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens
);

} // namespace nomad
