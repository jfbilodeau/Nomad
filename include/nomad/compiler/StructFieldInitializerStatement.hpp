// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/SyntaxTree.hpp>

namespace nomad {

class StructMemberDeclaration;

class StructFieldInitializerStatement final : public Statement {
public:
    explicit StructFieldInitializerStatement(StructMemberDeclaration* member);

protected:
    void onResolve(CompilerContext* context, ScriptCallableBase* function) override;
    void onCompile(Compiler* compiler, ScriptCallableBase* function) override;

private:
    StructMemberDeclaration* m_member;
    VariableId m_stagingVariable = VariableId::INVALID;
};

} // namespace nomad
