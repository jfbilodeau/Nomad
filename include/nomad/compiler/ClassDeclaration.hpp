// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/ClassMemberDeclaration.hpp>
#include <nomad/compiler/SyntaxTree.hpp>

namespace nomad {

enum class PreParsePhase;

class ObjectType;
class Tokenizer;

class ClassDeclaration final : public Statement {
public:
    ClassDeclaration(
        NomadIndex line,
        NomadIndex column,
        const ObjectType* type,
        std::vector<std::unique_ptr<ClassMemberDeclaration>> members
    );
    [[nodiscard]] const ObjectType* getType() const;
    [[nodiscard]] const std::vector<std::unique_ptr<ClassMemberDeclaration>>& getMembers() const;
    [[nodiscard]] NomadBoolean isDeclaration() const override;

protected:
    void onResolve(CompilerContext* context, ScriptFunction* function) override;

private:
    const ObjectType* m_type;
    std::vector<std::unique_ptr<ClassMemberDeclaration>> m_members;
};

bool preParseClassStatement(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens,
    PreParsePhase phase
);
bool preParseOpenClassStatement(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens,
    PreParsePhase phase
);
std::unique_ptr<Statement> parseClassStatement(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens);

} // namespace nomad
