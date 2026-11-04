// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/StructMemberDeclaration.hpp>
#include <nomad/compiler/SyntaxTree.hpp>

namespace nomad {

enum class PreParsePhase;

class StructType;
class Tokenizer;

class StructDeclaration final : public Statement {
public:
    StructDeclaration(
        NomadIndex line,
        NomadIndex column,
        const StructType* type,
        std::vector<std::unique_ptr<StructMemberDeclaration>> members
    );
    [[nodiscard]] const StructType* getType() const;
    [[nodiscard]] const std::vector<std::unique_ptr<StructMemberDeclaration>>& getMembers() const;
    [[nodiscard]] NomadBoolean isDeclaration() const override;

protected:
    void onResolve(CompilerContext* context, ScriptCallableBase* function) override;

private:
    const StructType* m_type;
    std::vector<std::unique_ptr<StructMemberDeclaration>> m_members;
};

bool preParseStructStatement(
    CompilerContext* context,
    ScriptCallableBase* function,
    Tokenizer* tokens,
    PreParsePhase phase
);
std::unique_ptr<Statement> parseStructStatement(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens);

} // namespace nomad
