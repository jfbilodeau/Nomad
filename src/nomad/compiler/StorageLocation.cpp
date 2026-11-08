// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/StorageLocation.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/SyntaxTree.hpp>

#include <nomad/interpreter/Interpreter.hpp>
#include <nomad/interpreter/InterpreterStorage.hpp>

#include <nomad/script/ClassType.hpp>
#include <nomad/script/CompositeType.hpp>
#include <nomad/script/Property.hpp>
#include <nomad/script/ScriptCallableBase.hpp>
#include <nomad/script/ScriptObject.hpp>
#include <nomad/script/StructType.hpp>
#include <nomad/script/VirtualMachine.hpp>

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

StorageLocation::StorageLocation(Expression* value):
    m_type(value != nullptr ? value->getType() : nullptr),
    m_temporary(value) {
    if (m_type == nullptr || m_type->asCompositeType() == nullptr) {
        throw NomadBug("Temporary receiver location requires a resolved composite value");
    }
    m_root.valueType = m_type;
}

StorageLocation::StorageLocation(Expression* value, const VariableId variable):
    StorageLocation(value) {
    if (m_type == nullptr || !m_type->isStruct() || variable.isInvalid()) {
        throw NomadBug("Temporary storage location requires a resolved struct value and local");
    }
    m_root.identifierType = IdentifierType::FunctionVariable;
    m_root.variableId = variable;
    m_root.valueType = m_type;
}

StorageLocation::StorageLocation(const Property* property):
    m_type(property != nullptr ? property->getValueType() : nullptr),
    m_objectProperty(property) {
    if (property == nullptr || !property->getOwnerType()->isClass() ||
        property->asNativeProperty() != nullptr || m_type == nullptr || !m_type->isStruct()) {
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
    if (!m_type->isStruct() || property.getOwnerType() != m_type ||
        property.asNativeProperty() != nullptr) {
        throw NomadBug("Storage property requires matching stored struct metadata");
    }
    auto storage = *this;
    storage.m_properties.push_back(&property);
    storage.m_type = property.getValueType();
    return storage;
}

InterpreterValue StorageLocation::withReceiver(
    InterpreterContext& context,
    const std::function<InterpreterValue(InterpreterReceiver&)>& operation,
    const std::function<void()>& prepare
) const {
    if (!operation || m_type->asCompositeType() == nullptr) {
        throw NomadBug("Receiver operation requires a composite storage location");
    }
    const auto offset = getPropertyOffset();
    const auto size = m_type->getSize();
    const auto select = [&](const std::span<RuntimeValue> storage) {
        if (offset > storage.size() || size > storage.size() - offset) {
            throw NomadBug("Receiver location exceeds its root storage");
        }
        return size == 0 ? std::span<RuntimeValue>{} : storage.subspan(offset, size);
    };
    const auto invokeObject = [&](const auto& getValue) -> InterpreterValue {
        auto receiver = [&]() {
            const auto value = getValue();
            return context.getInterpreter().createObjectReceiver(
                *m_type->asClassType(), value.getValue().getObjectValue());
        }();
        if (prepare) {
            prepare();
        }
        return operation(*receiver);
    };
    const auto invoke = [&](const auto& getStorage) -> InterpreterValue {
        auto storage = select(getStorage());
        if (m_type->isClass()) {
            return invokeObject([&]() { return InterpreterValue(m_type, storage); });
        }
        if (prepare) {
            prepare();
            storage = select(getStorage());
        }
        auto receiver = context.getInterpreter().createStructReceiver(*m_type->asStructType(), storage.data());
        return operation(*receiver);
    };

    if (m_objectProperty != nullptr) {
        const auto* enclosing = m_owner == nullptr ? context.getReceiver() : nullptr;
        if (m_owner == nullptr && enclosing == nullptr) {
            throw NomadBug("Stored receiver location requires an enclosing object");
        }
        const auto owner = m_owner != nullptr ? m_owner->evaluate(context) : enclosing->getValue();
        auto* object = dynamic_cast<ScriptObject*>(owner.getValue().getObjectValue());
        if (object == nullptr || object->getClassType() != m_objectProperty->getOwnerType()) {
            context.getVirtualMachine()->fault("Stored property receiver has no matching field storage");
        }
        return invoke([&]() {
            return object->getFieldStorage(m_objectProperty->getOwnerType()->getFieldIndex(m_objectProperty->getId()));
        });
    }
    if (m_temporary != nullptr) {
        if (m_root.valueType->isStruct()) {
            auto storage = [&]() {
                const auto value = m_temporary->evaluate(context);
                return InterpreterStorage(m_root.valueType, value.getStorage());
            }();
            return invoke([&]() { return storage.getMutableStorage(); });
        }
        return invokeObject([&]() { return m_temporary->evaluate(context); });
    }
    switch (m_root.identifierType) {
    case IdentifierType::FunctionVariable:
        return invoke([&]() {
            if (!isIdInRange(m_root.variableId.id, context.getCallable()->getVariableCount())) {
                throw NomadBug("Receiver location variable is not defined in this context");
            }
            const auto* type = context.getVariableTypeById(m_root.variableId);
            const auto& name = context.getCallable()->getVariableName(m_root.variableId);
            if (type == nullptr) {
                throw NomadException("Variable '" + name + "' has not been assigned a value");
            }
            if (type != m_root.valueType) {
                throw NomadException("Variable '" + name + "' changed type after this expression was resolved");
            }
            return context.getMutableVariableStorageById(m_root.variableId);
        });
    case IdentifierType::Parameter:
        return invoke([&]() {
            const auto storage = context.getMutableParameterStorage(m_root.parameterId);
            if (context.getCallable()->getParameterType(m_root.parameterId) != m_root.valueType) {
                throw NomadBug("Receiver parameter type does not match its location");
            }
            return storage;
        });
    case IdentifierType::Receiver:
        if (context.getCallable()->getClassType() != m_root.valueType) {
            throw NomadBug("Receiver type does not match its location");
        }
        if (m_root.valueType->isStruct()) {
            return invoke([&]() { return context.getMutableReceiverStorage(); });
        } else {
            const auto* receiver = context.getReceiver();
            if (receiver == nullptr) {
                throw NomadBug("Receiver location requires an active callable frame");
            }
            return invokeObject([&]() { return receiver->getValue(); });
        }
    default:
        throw NomadBug("Invalid interpreter receiver location root");
    }
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
    if (m_temporary != nullptr) {
        m_temporary->compileIntoLocal(&compiler, function, m_root.variableId);
    }
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

void StorageLocation::emitFreeOwner(Compiler& compiler, const ScriptCallableBase* function) const {
    if (m_objectProperty != nullptr) {
        m_objectProperty->getOwnerType()->emitFreeStackValue(compiler, 0);
        compiler.addOpCode(OpCodes::op_pop_1);
    }
    if (m_temporary != nullptr) {
        m_root.valueType->emitFreeFunctionVariable(compiler, function->getVariableOffset(m_root.variableId));
    }
}

void StorageLocation::emitAssignment(
    Compiler& compiler,
    ScriptCallableBase* function,
    Expression& expression,
    const VariableId stagingVariable
) const {
    validate(function);
    const auto aggregate = m_type->isStruct();
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
    emitFreeOwner(compiler, function);
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
        if (m_type->isStruct() && target != ExpressionTarget::Stack) {
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
        emitFreeOwner(compiler, function);
        return;
    }
    emitRetainOwner(compiler, function);
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
    emitFreeOwner(compiler, function);
}

void StorageLocation::emitStoreResult(Compiler& compiler, const ScriptCallableBase* function) const {
    validate(function);
    if (m_type->isStruct() || m_type->getSize() != 1) {
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
        emitFreeOwner(compiler, function);
        return;
    }
    emitRetainOwner(compiler, function);
    if (m_type->getSize() == 0) {
        emitFreeOwner(compiler, function);
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
    emitFreeOwner(compiler, function);
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
