// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/SyntaxTree.hpp>

namespace nomad {

class ClassMemberDeclaration;

class ClassFieldInitializerStatement final : public Statement {
public:
    explicit ClassFieldInitializerStatement(ClassMemberDeclaration* member);

protected:
    void onResolve(CompilerContext* context, ScriptCallableBase* function) override;
    void onCompile(Compiler* compiler, ScriptCallableBase* function) override;

private:
    ClassMemberDeclaration* m_member;
    VariableId m_stagingVariable = VariableId::INVALID;
};

} // namespace nomad
