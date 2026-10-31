// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/ClassFieldInitializerStatement.hpp>

#include <nomad/compiler/ClassMemberDeclaration.hpp>
#include <nomad/compiler/Compiler.hpp>

#include <nomad/script/Type.hpp>

namespace nomad {

ClassFieldInitializerStatement::ClassFieldInitializerStatement(ClassMemberDeclaration* member):
    Statement(member->getLine(), member->getColumn()),
    m_member(member)
{}

void ClassFieldInitializerStatement::onCompile(Compiler* compiler, ScriptCallableBase* function) {
    auto* initializer = m_member->getInitializer();
    const auto* receiverType = function->getClassType();
    initializer->compile(compiler, function, ExpressionTarget::Result);
    compiler->addLoadReceiver(function, ExpressionTarget::Intermediate);
    compiler->addOpCode(op_native_property_set);
    compiler->addId(m_member->getPropertyId().id);
    initializer->getType()->emitFreeResult(*compiler);
    receiverType->emitFreeIntermediate(*compiler);
}

} // namespace nomad
