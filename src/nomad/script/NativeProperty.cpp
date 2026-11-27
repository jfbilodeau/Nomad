// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/NativeProperty.hpp>

#include <nomad/compiler/DeferredType.hpp>

#include <nomad/script/ClassType.hpp>
#include <nomad/script/Runtime.hpp>

#include <utility>

namespace nomad {

NativeProperty::NativeProperty(
    PropertyId id,
    const ClassType* classType,
    NomadString name,
    const Type* valueType,
    NativePropertyGetFn getFn,
    NativePropertySetFn setFn,
    NomadString doc,
    const CallableId getterMethodId,
    const CallableId setterMethodId
):
    Property(id, classType, std::move(name), valueType, std::move(doc)),
    m_getFn(std::move(getFn)),
    m_setFn(std::move(setFn)),
    m_getterMethodId(getterMethodId),
    m_setterMethodId(setterMethodId),
    m_typeResolved(dynamic_cast<const DeferredType*>(valueType) == nullptr)
{}

const NativeProperty* NativeProperty::asNativeProperty() const {
    return this;
}

NativePropertyInfo NativeProperty::getInfo() const {
    if (m_typeResolved) {
        return {
            getId(),
            getClassType(), 
            getName(), 
            getValueType(), 
            m_getFn, 
            m_setFn,
            getDoc(),
            m_getterMethodId,
            m_setterMethodId
        };
    }
    NativePropertyGetFn getter;
    NativePropertySetFn setter;
    if (m_getFn) {
        getter = [this](VirtualMachine* machine, RuntimeValue& result) {
            getValue(machine, result);
        };
    }
    if (m_setFn) {
        setter = [this](VirtualMachine* machine, const RuntimeValue& value) {
            setValue(machine, value);
        };
    }
    return {
        getId(), 
        getClassType(), 
        getName(), 
        getValueType(), 
        std::move(getter), 
        std::move(setter), 
        getDoc(),
        m_getterMethodId, 
        m_setterMethodId
    };
}

CallableId NativeProperty::getGetterMethodId() const noexcept {

    return m_getterMethodId;
}

CallableId NativeProperty::getSetterMethodId() const noexcept {

    return m_setterMethodId;
}

bool NativeProperty::hasGetterCallback() const noexcept {

    return static_cast<bool>(m_getFn);
}

bool NativeProperty::hasSetterCallback() const noexcept {

    return static_cast<bool>(m_setFn);
}

void NativeProperty::getValue(VirtualMachine* machine, RuntimeValue& result) const {
    requireResolvedType();
    m_getFn(machine, result);
}

void NativeProperty::setValue(VirtualMachine* machine, const RuntimeValue& value) const {
    requireResolvedType();
    m_setFn(machine, value);
}

bool NativeProperty::isTypeResolved() const {
    return m_typeResolved;
}

bool NativeProperty::isRegisteredType(const Type* type) const {
    const auto* runtime = getClassType()->getRuntime();
    return 
        type != nullptr &&
        type->getRuntime() == runtime &&
        runtime->getType(type->getId()) == type &&
        runtime->getNativeProperty(getId()) == this;
}

void NativeProperty::resolveValueType(const Type* type) {
    const auto* placeholder = dynamic_cast<const DeferredType*>(getValueType());
    if (
        m_typeResolved || 
        !isRegisteredType(type) || 
        type->isVoid() || 
        type->isInternal() ||
        placeholder == nullptr || 
        placeholder->getTypeName() != type->getTypeName()
    ) {
        throw NomadException("Cannot resolve native property '" + getClassType()->getTypeName() + "." +
            getName() + "': invalid type or target '" + getValueType()->getTypeName() + "'");
    }
    setValueType(type);
}

void NativeProperty::completeTypeResolution() {
    if (
        !isRegisteredType(getValueType()) || 
        getValueType()->isVoid() || 
        getValueType()->isInternal()
    ) {
        throw NomadException("Cannot complete native property '" + getClassType()->getTypeName() + "." +
            getName() + "': unresolved or invalid value type");
    }
    
    m_typeResolved = true;
}

void NativeProperty::requireResolvedType() const {
    if (!m_typeResolved) {
        throw NomadException("Native property '" + getClassType()->getTypeName() + "." +
            getName() + "' has an unresolved value type");
    }
}

} // namespace nomad
