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

void ClassFieldInitializerStatement::onResolve(CompilerContext* context, ScriptCallableBase* function) {
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

void ClassFieldInitializerStatement::onCompile(Compiler* compiler, ScriptCallableBase* function) {
    auto* initializer = m_member->getInitializer();
    const auto* receiverType = function->getClassType();
    if (receiverType->asStructType() != nullptr) {
        IdentifierDefinition receiver;
        receiver.identifierType = IdentifierType::Receiver;
        receiver.valueType = receiverType;
        const auto* property = compiler->getRuntime()->getProperty(m_member->getPropertyId());
        StorageLocation(receiver).getProperty(*property).emitAssignment(
            *compiler, function, *initializer, m_stagingVariable);
        return;
    }
    if (initializer->getType()->asStructType() != nullptr) {
        const auto* property = compiler->getRuntime()->getProperty(m_member->getPropertyId());
        StorageLocation(property).emitAssignment(*compiler, function, *initializer, m_stagingVariable);
        return;
    }
    initializer->compile(compiler, function, ExpressionTarget::Result);
    compiler->addLoadReceiver(function, ExpressionTarget::Intermediate);
    compiler->addStoreProperty(m_member->getPropertyId());
    initializer->getType()->emitFreeResult(*compiler);
    receiverType->emitFreeIntermediate(*compiler);
}

} // namespace nomad
