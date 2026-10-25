// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/SyntaxTree.hpp>

namespace nomad {

class ClassMemberDeclaration;

class ClassFieldInitializerStatement final : public Statement {
public:
    explicit ClassFieldInitializerStatement(ClassMemberDeclaration* member);

protected:
    void onCompile(Compiler* compiler, ScriptCallableBase* function) override;

private:
    ClassMemberDeclaration* m_member;
};

} // namespace nomad
