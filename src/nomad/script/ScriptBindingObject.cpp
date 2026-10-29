// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/ScriptBindingObject.hpp>

#include <nomad/script/NomadStringObject.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptObject.hpp>

#include <utility>

namespace nomad {

ScriptBindingObject::ScriptBindingObject(Runtime& runtime, NomadObject* object, const BindingClass& contract) :
    ScriptBindingObject(runtime.getScriptClassBinding(contract), object) {
}

ScriptBindingObject::ScriptBindingObject(const ScriptClassBinding& binding, NomadObject* object) :
    m_object(dynamic_cast<ScriptObject*>(object)),
    m_binding(&binding) {
    if (m_object == nullptr || m_object->getClassType() != binding.getClassType()) {
        throw NomadException("Script binding contract: wrong runtime or receiver for '" +
            binding.getContract().name + "'");
    }
    m_object->addRef();
}

ScriptBindingObject::ScriptBindingObject(const ScriptBindingObject& other) :
    m_object(other.m_object), m_binding(other.m_binding) {
    addRef(m_object);
}

ScriptBindingObject::ScriptBindingObject(ScriptBindingObject&& other) noexcept :
    m_object(std::exchange(other.m_object, nullptr)), m_binding(other.m_binding) {
}

ScriptBindingObject& ScriptBindingObject::operator=(const ScriptBindingObject& other) {
    if (this != &other) {
        m_object = replace(m_object, other.m_object);
        m_binding = other.m_binding;
    }
    return *this;
}

ScriptBindingObject& ScriptBindingObject::operator=(ScriptBindingObject&& other) noexcept {
    if (this != &other) {
        releaseRef(m_object);
        m_object = std::exchange(other.m_object, nullptr);
        m_binding = other.m_binding;
    }
    return *this;
}

ScriptBindingObject::~ScriptBindingObject() {
    releaseRef(m_object);
}

ScriptBindingObject ScriptBindingObject::construct(
    const ScriptClassBinding& binding,
    VirtualMachine& machine
) {
    auto* object = binding.construct(machine);
    try {
        auto result = ScriptBindingObject(binding, object);
        releaseRef(object);
        return result;
    } catch (...) {
        releaseRef(object);
        throw;
    }
}

ScriptBindingObject ScriptBindingObject::construct(
    Runtime& runtime,
    VirtualMachine& machine,
    const BindingClass& contract
) {
    return construct(runtime.getScriptClassBinding(contract), machine);
}

NomadObject* ScriptBindingObject::getObject() const {
    if (m_object == nullptr) {
        throw NomadException("Script binding contract: access to a moved-from handle");
    }
    return m_object;
}

Runtime& ScriptBindingObject::getRuntime() const {
    return getBinding().getRuntime();
}

const ScriptClassBinding& ScriptBindingObject::getBinding() const {
    (void)getObject();
    return *m_binding;
}

NomadIndex ScriptBindingObject::getFieldIndex(NomadIndex slot, BindingValueKind kind) const {
    return getBinding().getFieldIndex(slot, kind);
}

NomadBoolean ScriptBindingObject::getBoolean(NomadIndex slot) const {
    const auto index = getFieldIndex(slot, BindingValueKind::Boolean);
    return m_object->getField(index).getBooleanValue();
}

NomadInteger ScriptBindingObject::getInteger(NomadIndex slot) const {
    const auto index = getFieldIndex(slot, BindingValueKind::Integer);
    const auto value = m_object->getField(index).getIntegerValue();
    m_binding->validateInteger(slot, value);
    return value;
}

NomadFloat ScriptBindingObject::getFloat(NomadIndex slot) const {
    const auto index = getFieldIndex(slot, BindingValueKind::Float);
    return m_object->getField(index).getFloatValue();
}

NomadString ScriptBindingObject::getString(NomadIndex slot) const {
    const auto index = getFieldIndex(slot, BindingValueKind::String);
    const auto& value = m_object->getField(index);
    const auto* string = dynamic_cast<const NomadStringObject*>(value.getObjectValue());
    if (string == nullptr) {
        throw NomadException("Script binding contract: uninitialized or invalid string field");
    }
    return string->getString();
}

void ScriptBindingObject::setBoolean(NomadIndex slot, NomadBoolean value) {
    const auto index = getFieldIndex(slot, BindingValueKind::Boolean);
    m_object->setField(index, RuntimeValue(value));
}

void ScriptBindingObject::setInteger(NomadIndex slot, NomadInteger value) {
    const auto index = getFieldIndex(slot, BindingValueKind::Integer);
    m_binding->validateInteger(slot, value);
    m_object->setField(index, RuntimeValue(value));
}

void ScriptBindingObject::setFloat(NomadIndex slot, NomadFloat value) {
    const auto index = getFieldIndex(slot, BindingValueKind::Float);
    m_object->setField(index, RuntimeValue(value));
}

void ScriptBindingObject::setString(NomadIndex slot, const NomadString& value) {
    const auto index = getFieldIndex(slot, BindingValueKind::String);
    auto stored = RuntimeValue(value);
    try {
        m_object->setField(index, stored);
    } catch (...) {
        stored.freeObjectValue();
        throw;
    }
    stored.freeObjectValue();
}

} // namespace nomad
