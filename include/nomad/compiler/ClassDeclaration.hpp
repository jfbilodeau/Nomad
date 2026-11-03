// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/ClassMemberDeclaration.hpp>
#include <nomad/compiler/SyntaxTree.hpp>

namespace nomad {

enum class PreParsePhase;

class CompositeType;
class Tokenizer;

class ClassDeclaration final : public Statement {
public:
    ClassDeclaration(
        NomadIndex line,
        NomadIndex column,
        const CompositeType* type,
        std::vector<std::unique_ptr<ClassMemberDeclaration>> members
    );
    [[nodiscard]] const CompositeType* getType() const;
    [[nodiscard]] const std::vector<std::unique_ptr<ClassMemberDeclaration>>& getMembers() const;
    [[nodiscard]] NomadBoolean isDeclaration() const override;

protected:
    void onResolve(CompilerContext* context, ScriptCallableBase* function) override;

private:
    const CompositeType* m_type;
    std::vector<std::unique_ptr<ClassMemberDeclaration>> m_members;
};

bool preParseClassStatement(
    CompilerContext* context,
    ScriptCallableBase* function,
    Tokenizer* tokens,
    PreParsePhase phase
);
bool preParseOpenClassStatement(
    CompilerContext* context,
    ScriptCallableBase* function,
    Tokenizer* tokens,
    PreParsePhase phase
);
std::unique_ptr<Statement> parseClassStatement(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens);
bool preParseStructStatement(
    CompilerContext* context,
    ScriptCallableBase* function,
    Tokenizer* tokens,
    PreParsePhase phase
);
std::unique_ptr<Statement> parseStructStatement(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens);

} // namespace nomad
