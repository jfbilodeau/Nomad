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

StorageLocation::StorageLocation(const Property* property):
    m_type(property != nullptr ? property->getValueType() : nullptr),
    m_objectProperty(property) {
    if (property == nullptr || property->getOwnerType()->asClassType() == nullptr ||
        property->asNativeProperty() != nullptr || m_type == nullptr || m_type->asStructType() == nullptr) {
        throw NomadBug("Object storage location requires a stored class-owned struct property");
    }
    m_root.valueType = m_type;
}

StorageLocation::StorageLocation(Expression* owner, const Property* property):
    StorageLocation(property) {
    if (owner == nullptr || owner->getType() != property->getOwnerType()) {
        throw NomadBug("Object storage location requires a resolved matching class owner");
    }
    m_owner = owner;
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
    storage.m_properties.push_back(&property);
    storage.m_type = property.getValueType();
    return storage;
}

NomadIndex StorageLocation::getPropertyOffset() const {
    auto offset = NomadIndex{0};
    for (const auto* property : m_properties) {
        offset += property->getOwnerType()->getFieldOffset(property->getId());
    }
    return offset;
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
    const auto propertyOffset = getPropertyOffset();
    if (m_root.identifierType == IdentifierType::Parameter) {
        return offset + (m_root.valueType->getSize() - propertyOffset - m_type->getSize());
    }
    return offset + propertyOffset;
}

void StorageLocation::validate(const ScriptCallableBase* function) const {
    (void)getRootOffset(function);
    const auto rootSize = m_root.valueType->getSize();
    const auto propertyOffset = getPropertyOffset();
    if (propertyOffset > rootSize || m_type->getSize() > rootSize - propertyOffset) {
        throw NomadBug("Storage location exceeds its root storage");
    }
}

void StorageLocation::emitRetainOwner(Compiler& compiler, ScriptCallableBase* function) const {
    if (m_objectProperty != nullptr) {
        if (m_owner != nullptr) {
            m_owner->compile(&compiler, function, ExpressionTarget::Stack);
        } else {
            if (compiler.getReceiverType(function) != m_objectProperty->getOwnerType()) {
                compiler.reportInternalError("Stored property requires a matching enclosing class receiver");
            }
            compiler.addLoadReceiver(function, ExpressionTarget::Stack);
        }
    }
}

void StorageLocation::emitFreeOwner(Compiler& compiler) const {
    if (m_objectProperty != nullptr) {
        m_objectProperty->getOwnerType()->emitFreeStackValue(compiler, 0);
        compiler.addOpCode(OpCodes::op_pop_1);
    }
}

void StorageLocation::emitAssignment(
    Compiler& compiler,
    ScriptCallableBase* function,
    Expression& expression,
    const VariableId stagingVariable
) const {
    validate(function);
    const auto aggregate = m_type->asStructType() != nullptr;
    if (expression.getType() != m_type ||
        (aggregate && (stagingVariable.isInvalid() || function->getVariableType(stagingVariable) != m_type))) {
        compiler.reportInternalError("Storage assignment requires a same-type expression and aggregate staging local");
    }
    emitRetainOwner(compiler, function);
    if (m_objectProperty != nullptr) {
        emitAddress(compiler, function);
    }
    if (aggregate) {
        expression.compileIntoLocal(&compiler, function, stagingVariable);
    } else {
        expression.compile(&compiler, function, ExpressionTarget::Result);
    }
    if (m_objectProperty != nullptr && m_type->getSize() != 0) {
        compiler.addOpCode(OpCodes::op_pop_i);
    }
    if (aggregate) {
        emitCopyFromLocal(compiler, function, stagingVariable);
    } else {
        emitStoreResult(compiler, function);
    }
    emitFreeOwner(compiler);
    if (aggregate) {
        m_type->emitFreeFunctionVariable(compiler, function->getVariableOffset(stagingVariable));
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
        compiler.addIndex(getPropertyOffset());
        break;
    default:
        throw NomadBug("Invalid storage address root");
    }
}

void StorageLocation::emitLoad(
    Compiler& compiler,
    ScriptCallableBase* function,
    const ExpressionTarget target
) const {
    validate(function);
    if (m_objectProperty != nullptr) {
        if (m_type->asStructType() != nullptr && target != ExpressionTarget::Stack) {
            compiler.reportError("Struct storage loads require stack copies or a local destination");
        }
        emitRetainOwner(compiler, function);
        if (m_type->getSize() != 0) {
            emitAddress(compiler, function);
            compiler.addOpCode(OpCodes::op_pop_i);
            if (target == ExpressionTarget::Stack) {
                compiler.addOpCode(OpCodes::op_pop_r);
                m_type->emitLoadStorage(compiler, 0, target);
                m_objectProperty->getOwnerType()->emitFreeResult(compiler);
                return;
            }
            m_type->emitLoadStorage(compiler, 0, target);
        }
        emitFreeOwner(compiler);
        return;
    }
    switch (m_root.identifierType) {
    case IdentifierType::FunctionVariable:
        m_type->emitLoadFunctionVariable(compiler, getOffset(function), target);
        break;
    case IdentifierType::Parameter:
        m_type->emitLoadParameter(compiler, getOffset(function), target);
        break;
    case IdentifierType::Receiver:
        m_type->emitLoadStorageParameter(compiler, getRootOffset(function), getPropertyOffset(), target);
        break;
    default:
        throw NomadBug("Invalid storage load root");
    }
}

void StorageLocation::emitStoreResult(Compiler& compiler, const ScriptCallableBase* function) const {
    validate(function);
    if (m_type->asStructType() != nullptr || m_type->getSize() != 1) {
        throw NomadBug("Result storage requires a single-slot non-struct value");
    }
    if (m_objectProperty != nullptr) {
        m_type->emitFreeStorage(compiler, 0);
        compiler.addOpCode(OpCodes::op_storage_set);
        compiler.addIndex(0);
        return;
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
        m_type->emitFreeStorageParameter(compiler, getRootOffset(function), getPropertyOffset());
        compiler.addOpCode(OpCodes::op_storage_parameter_set);
        compiler.addIndex(getRootOffset(function));
        compiler.addIndex(getPropertyOffset());
        break;
    default:
        throw NomadBug("Invalid storage store root");
    }
}

void StorageLocation::emitCopyToLocal(
    Compiler& compiler,
    ScriptCallableBase* function,
    const VariableId destination
) const {
    validate(function);
    if (function->getVariableType(destination) != m_type) {
        throw NomadBug("Storage copy requires a matching local type");
    }
    if (m_objectProperty != nullptr) {
        emitRetainOwner(compiler, function);
        if (m_type->getSize() != 0) {
            emitAddress(compiler, function);
            compiler.addOpCode(OpCodes::op_pop_i);
            m_type->emitCopyStorageToFunctionVariable(compiler, 0, function->getVariableOffset(destination));
        }
        emitFreeOwner(compiler);
        return;
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
        m_type->emitCopyStorageParameterToFunctionVariable(compiler, getRootOffset(function), getPropertyOffset(), offset);
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
    validate(function);
    if (function->getVariableType(source) != m_type) {
        throw NomadBug("Storage copy requires a matching local type");
    }
    if (m_type->getSize() == 0) {
        return;
    }
    const auto offset = function->getVariableOffset(source);
    if (m_objectProperty != nullptr) {
        m_type->emitCopyFunctionVariableToStorage(compiler, offset, 0);
        return;
    }
    switch (m_root.identifierType) {
    case IdentifierType::FunctionVariable:
        m_type->emitCopyFunctionVariable(compiler, offset, getOffset(function));
        break;
    case IdentifierType::Parameter:
        m_type->emitCopyFunctionVariableToParameter(compiler, offset, getOffset(function));
        break;
    case IdentifierType::Receiver:
        m_type->emitCopyFunctionVariableToStorageParameter(compiler, offset, getRootOffset(function), getPropertyOffset());
        break;
    default:
        throw NomadBug("Invalid storage copy destination");
    }
}

} // namespace nomad
