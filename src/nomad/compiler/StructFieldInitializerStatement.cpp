// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/StructFieldInitializerStatement.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/StructMemberDeclaration.hpp>

#include <nomad/script/Type.hpp>

namespace nomad {

StructFieldInitializerStatement::StructFieldInitializerStatement(StructMemberDeclaration* member):
    Statement(member->getLine(), member->getColumn()),
    m_member(member)
{}

void StructFieldInitializerStatement::onResolve(CompilerContext* context, ScriptCallableBase* function) {
    auto* initializer = m_member->getInitializer();
    if (!initializer->resolve(context, function)) {
        return;
    }
    if (initializer->getType() != nullptr && initializer->getType()->asStructType() != nullptr &&
        m_stagingVariable.isInvalid()) {
        m_stagingVariable = function->registerVariable(
            "$fieldInitializer" + std::to_string(function->getVariableCount()), initializer->getType());
    }
}

void StructFieldInitializerStatement::onCompile(Compiler* compiler, ScriptCallableBase* function) {
    IdentifierDefinition receiver;
    receiver.identifierType = IdentifierType::Receiver;
    receiver.valueType = function->getClassType();
    const auto* property = compiler->getRuntime()->getProperty(m_member->getPropertyId());
    StorageLocation(receiver).getProperty(*property).emitAssignment(
        *compiler, function, *m_member->getInitializer(), m_stagingVariable);
}

} // namespace nomad
