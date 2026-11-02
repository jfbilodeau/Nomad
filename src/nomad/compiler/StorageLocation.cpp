// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/StorageLocation.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/SyntaxTree.hpp>

#include <nomad/script/CompositeType.hpp>
#include <nomad/script/Property.hpp>
#include <nomad/script/ScriptCallableBase.hpp>

namespace nomad {

StorageLocation::StorageLocation(const IdentifierDefinition root):
    m_root(root),
    m_type(root.valueType) {
    if (m_type == nullptr ||
        (root.identifierType != IdentifierType::FunctionVariable &&
         root.identifierType != IdentifierType::Parameter && root.identifierType != IdentifierType::Receiver)) {
        throw NomadBug("Storage location requires a resolved local, parameter, or borrowed receiver");
    }
}

StorageLocation::StorageLocation(Expression* owner, const Property* property):
    m_type(property != nullptr ? property->getValueType() : nullptr),
    m_owner(owner),
    m_objectProperty(property) {
    if (owner == nullptr || property == nullptr || owner->getType() == nullptr ||
        owner->getType()->asClassType() == nullptr || owner->getType() != property->getOwnerType() ||
        property->asNativeProperty() != nullptr || m_type == nullptr || m_type->asStructType() == nullptr) {
        throw NomadBug("Object storage location requires a resolved class owner and stored struct property");
    }
    m_root.valueType = m_type;
}

const Type* StorageLocation::getType() const {
    return m_type;
}

StorageLocation StorageLocation::getProperty(const Property& property) const {
    if (m_type->asStructType() == nullptr || property.getOwnerType() != m_type ||
        property.asNativeProperty() != nullptr) {
        throw NomadBug("Storage property requires matching stored struct metadata");
    }
    auto storage = *this;
    storage.m_propertyOffset += m_type->asCompositeType()->getFieldOffset(property.getId());
    storage.m_type = property.getValueType();
    return storage;
}

NomadIndex StorageLocation::getRootOffset(const ScriptCallableBase* function) const {
    if (m_objectProperty != nullptr) {
        return m_objectProperty->getOwnerType()->getFieldOffset(m_objectProperty->getId());
    }
    const Type* rootType = nullptr;
    auto offset = NomadIndex{0};
    switch (m_root.identifierType) {
    case IdentifierType::FunctionVariable:
        rootType = function->getVariableType(m_root.variableId);
        offset = function->getVariableOffset(m_root.variableId);
        break;
    case IdentifierType::Parameter:
        offset = function->getParameterOffset(m_root.parameterId);
        rootType = function->getParameterType(m_root.parameterId);
        break;
    case IdentifierType::Receiver:
        rootType = function->getClassType();
        if (rootType != nullptr && rootType->getSize() != 0) {
            const auto layout = function->getCallStorageLayout();
            if (!layout.receiverOffset.has_value()) {
                throw NomadBug("Storage location requires an enclosing borrowed receiver");
            }
            offset = *layout.receiverOffset;
        }
        break;
    default:
        throw NomadBug("Invalid storage location root");
    }
    if (rootType != m_root.valueType) {
        throw NomadBug("Storage location root type does not match the callable");
    }
    return offset;
}

NomadIndex StorageLocation::getOffset(const ScriptCallableBase* function) const {
    const auto offset = getRootOffset(function);
    if (m_root.identifierType == IdentifierType::Parameter) {
        return offset + (m_root.valueType->getSize() - m_propertyOffset - m_type->getSize());
    }
    return offset + m_propertyOffset;
}

void StorageLocation::validate(const ScriptCallableBase* function) const {
    (void)getRootOffset(function);
    const auto rootSize = m_root.valueType->getSize();
    if (m_propertyOffset > rootSize || m_type->getSize() > rootSize - m_propertyOffset) {
        throw NomadBug("Storage location exceeds its root storage");
    }
}

void StorageLocation::emitRetainOwner(Compiler& compiler, ScriptCallableBase* function) const {
    if (m_owner != nullptr) {
        m_owner->compile(&compiler, function, ExpressionTarget::Stack);
    }
}

void StorageLocation::emitFreeOwner(Compiler& compiler) const {
    if (m_owner != nullptr) {
        m_owner->getType()->emitFreeStackValue(compiler, 0);
        compiler.addOpCode(OpCodes::op_pop_1);
    }
}

void StorageLocation::emitAddress(
    Compiler& compiler,
    const ScriptCallableBase* function,
    const NomadIndex ownerOffset
) const {
    validate(function);
    if (m_type->getSize() == 0) {
        return;
    }
    if (m_objectProperty != nullptr) {
        const auto firstOffset = getOffset(function);
        compiler.addOpCode(OpCodes::op_stored_property_address_push);
        compiler.addIndex(ownerOffset);
        compiler.addIndex(firstOffset);
        compiler.addIndex(firstOffset + m_type->getSize() - 1);
        return;
    }
    switch (m_root.identifierType) {
    case IdentifierType::FunctionVariable:
        compiler.addOpCode(OpCodes::op_local_address_push);
        compiler.addIndex(getOffset(function));
        break;
    case IdentifierType::Parameter:
        compiler.addOpCode(OpCodes::op_parameter_address_push);
        compiler.addIndex(getOffset(function) + m_type->getSize() - 1);
        break;
    case IdentifierType::Receiver:
        compiler.addOpCode(OpCodes::op_storage_parameter_address_push);
        compiler.addIndex(getRootOffset(function));
        compiler.addIndex(m_propertyOffset);
        break;
    default:
        throw NomadBug("Invalid storage address root");
    }
}

void StorageLocation::emitLoad(
    Compiler& compiler,
    const ScriptCallableBase* function,
    const ExpressionTarget target
) const {
    if (m_owner != nullptr) {
        compiler.reportError("Object-owned struct property reads and writes are not yet supported");
    }
    validate(function);
    switch (m_root.identifierType) {
    case IdentifierType::FunctionVariable:
        m_type->emitLoadFunctionVariable(compiler, getOffset(function), target);
        break;
    case IdentifierType::Parameter:
        m_type->emitLoadParameter(compiler, getOffset(function), target);
        break;
    case IdentifierType::Receiver:
        m_type->emitLoadStorageParameter(compiler, getRootOffset(function), m_propertyOffset, target);
        break;
    default:
        throw NomadBug("Invalid storage load root");
    }
}

void StorageLocation::emitStoreResult(Compiler& compiler, const ScriptCallableBase* function) const {
    if (m_owner != nullptr) {
        compiler.reportError("Object-owned struct property reads and writes are not yet supported");
    }
    validate(function);
    if (m_type->asStructType() != nullptr || m_type->getSize() != 1) {
        throw NomadBug("Result storage requires a single-slot non-struct value");
    }
    switch (m_root.identifierType) {
    case IdentifierType::FunctionVariable:
        m_type->emitFreeFunctionVariable(compiler, getOffset(function));
        m_type->emitStoreFunctionVariable(compiler, getOffset(function));
        break;
    case IdentifierType::Parameter:
        m_type->emitFreeParameter(compiler, getOffset(function));
        m_type->emitStoreParameter(compiler, getOffset(function));
        break;
    case IdentifierType::Receiver:
        m_type->emitFreeStorageParameter(compiler, getRootOffset(function), m_propertyOffset);
        compiler.addOpCode(OpCodes::op_storage_parameter_set);
        compiler.addIndex(getRootOffset(function));
        compiler.addIndex(m_propertyOffset);
        break;
    default:
        throw NomadBug("Invalid storage store root");
    }
}

void StorageLocation::emitCopyToLocal(
    Compiler& compiler,
    const ScriptCallableBase* function,
    const VariableId destination
) const {
    if (m_owner != nullptr) {
        compiler.reportError("Object-owned struct property reads and writes are not yet supported");
    }
    validate(function);
    if (function->getVariableType(destination) != m_type) {
        throw NomadBug("Storage copy requires a matching local type");
    }
    if (m_type->getSize() == 0) {
        return;
    }
    const auto offset = function->getVariableOffset(destination);
    switch (m_root.identifierType) {
    case IdentifierType::FunctionVariable:
        m_type->emitCopyFunctionVariable(compiler, getOffset(function), offset);
        break;
    case IdentifierType::Parameter:
        m_type->emitCopyParameterToFunctionVariable(compiler, getOffset(function), offset);
        break;
    case IdentifierType::Receiver:
        m_type->emitCopyStorageParameterToFunctionVariable(compiler, getRootOffset(function), m_propertyOffset, offset);
        break;
    default:
        throw NomadBug("Invalid storage copy source");
    }
}

void StorageLocation::emitCopyFromLocal(
    Compiler& compiler,
    const ScriptCallableBase* function,
    const VariableId source
) const {
    if (m_owner != nullptr) {
        compiler.reportError("Object-owned struct property reads and writes are not yet supported");
    }
    validate(function);
    if (function->getVariableType(source) != m_type) {
        throw NomadBug("Storage copy requires a matching local type");
    }
    if (m_type->getSize() == 0) {
        return;
    }
    const auto offset = function->getVariableOffset(source);
    switch (m_root.identifierType) {
    case IdentifierType::FunctionVariable:
        m_type->emitCopyFunctionVariable(compiler, offset, getOffset(function));
        break;
    case IdentifierType::Parameter:
        m_type->emitCopyFunctionVariableToParameter(compiler, offset, getOffset(function));
        break;
    case IdentifierType::Receiver:
        m_type->emitCopyFunctionVariableToStorageParameter(compiler, offset, getRootOffset(function), m_propertyOffset);
        break;
    default:
        throw NomadBug("Invalid storage copy destination");
    }
}

} // namespace nomad
