// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/ScriptBindingObject.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptObject.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <algorithm>
#include <utility>

namespace nomad {

namespace {

[[noreturn]] void bindingError(const NomadString& message) {
    throw NomadException("Script binding contract: " + message);
}

const ClassType* validateContract(Runtime& runtime, const BindingClass& contract) {
    const auto* type = runtime.getTypeByName(contract.name);
    const auto* classType = type == nullptr ? nullptr : type->asClassType();
    if (classType == nullptr || classType->getRuntime() != &runtime ||
        classType->getProperties().size() != contract.fields.size() ||
        !classType->getTypeArguments().empty() || !classType->getMethods().empty() ||
        !classType->getNativeIndexers().empty() || classType->getIterableInfo()) {
        bindingError("class shape mismatch for '" + contract.name + "'");
    }
    const auto constructors = classType->getConstructors();
    if (constructors.size() != 1 || constructors.front()->asScriptConstructor() == nullptr ||
        constructors.front()->getParameterCount() != 0) {
        bindingError("expected one ordinary zero-argument script constructor for '" + contract.name + "'");
    }
    for (const auto& expected : contract.enums) {
        const auto* enumType = runtime.getTypeByName(expected.name);
        const auto* enumeration = enumType == nullptr ? nullptr : enumType->asEnum();
        if (enumeration == nullptr || enumeration->getValueCount() != expected.values.size()) {
            bindingError("enum mismatch for '" + expected.name + "'");
        }
        for (const auto& value : expected.values) {
            NomadInteger actual{};
            if (!enumeration->getValueByName(value.name, actual) || actual != value.value) {
                bindingError("enum mapping mismatch for '" + expected.name + "." + value.name + "'");
            }
        }
    }
    for (const auto& expected : contract.fields) {
        const auto* field = classType->getProperty(expected.name);
        if (field == nullptr || field->asNativeProperty() != nullptr || field->getValueType() == nullptr ||
            field->getValueType() != runtime.getTypeByName(expected.type) ||
            classType->getFieldIndex(field->getId()) == NOMAD_INVALID_INDEX) {
            bindingError("stored field mismatch for '" + contract.name + "." + expected.name + "'");
        }
        const auto* fieldType = field->getValueType();
        const auto* enumeration = fieldType->asEnum();
        if (fieldType != runtime.getBooleanType() && fieldType != runtime.getIntegerType() &&
            fieldType != runtime.getFloatType() && fieldType != runtime.getStringType() &&
            (enumeration == nullptr || std::ranges::none_of(contract.enums, [&](const auto& expectedEnum) {
                return expectedEnum.name == enumeration->getTypeName();
            }))) {
            bindingError("unsupported field type for '" + expected.name + "'");
        }
    }
    return classType;
}

} // namespace

ScriptBindingObject::ScriptBindingObject(Runtime& runtime, NomadObject* object, const BindingClass& contract) :
    m_runtime(&runtime),
    m_object(dynamic_cast<ScriptObject*>(object)),
    m_contract(&contract) {
    const auto* expected = validateContract(runtime, contract);
    if (m_object == nullptr || m_object->getClassType() != expected) {
        bindingError("wrong runtime or receiver for '" + contract.name + "'");
    }
    m_object->addRef();
}

ScriptBindingObject::ScriptBindingObject(const ScriptBindingObject& other) :
    m_runtime(other.m_runtime), m_object(other.m_object), m_contract(other.m_contract) {
    addRef(m_object);
}

ScriptBindingObject::ScriptBindingObject(ScriptBindingObject&& other) noexcept :
    m_runtime(other.m_runtime), m_object(std::exchange(other.m_object, nullptr)), m_contract(other.m_contract) {
}

ScriptBindingObject& ScriptBindingObject::operator=(const ScriptBindingObject& other) {
    if (this != &other) {
        m_object = replace(m_object, other.m_object);
        m_runtime = other.m_runtime;
        m_contract = other.m_contract;
    }
    return *this;
}

ScriptBindingObject& ScriptBindingObject::operator=(ScriptBindingObject&& other) noexcept {
    if (this != &other) {
        releaseRef(m_object);
        m_object = std::exchange(other.m_object, nullptr);
        m_runtime = other.m_runtime;
        m_contract = other.m_contract;
    }
    return *this;
}

ScriptBindingObject::~ScriptBindingObject() {
    releaseRef(m_object);
}

ScriptBindingObject ScriptBindingObject::construct(
    Runtime& runtime,
    VirtualMachine& machine,
    const BindingClass& contract
) {
    if (machine.getRuntime() != &runtime) {
        bindingError("virtual machine belongs to another runtime");
    }
    const auto* type = validateContract(runtime, contract);
    const auto constructor = runtime.resolveConstructor(type, {});
    if (!constructor.isResolved()) {
        bindingError("cannot resolve constructor for '" + contract.name + "'");
    }
    auto* object = machine.constructObject(constructor.id, {});
    try {
        auto result = ScriptBindingObject(runtime, object, contract);
        releaseRef(object);
        return result;
    } catch (...) {
        releaseRef(object);
        throw;
    }
}

NomadObject* ScriptBindingObject::getObject() const {
    if (m_object == nullptr) {
        bindingError("access to a moved-from handle");
    }
    return m_object;
}

Runtime& ScriptBindingObject::getRuntime() const {
    (void)getObject();
    return *m_runtime;
}

const Property* ScriptBindingObject::getField(const NomadString& name, const NomadString& type) const {
    (void)getObject();
    const auto* classType = validateContract(*m_runtime, *m_contract);
    const auto* field = classType->getProperty(name);
    if (field == nullptr || (field->getValueType()->getTypeName() != type &&
        !(type == INTEGER_TYPE_NAME && field->getValueType()->asEnum() != nullptr))) {
        bindingError("wrong field or access type for '" + name + "'");
    }
    return field;
}

void ScriptBindingObject::validateInteger(const Property* field, NomadInteger value) const {
    const auto* enumeration = field->getValueType()->asEnum();
    NomadString name;
    if (enumeration != nullptr && !enumeration->getNameByValue(value, name)) {
        bindingError("invalid enum value for '" + field->getName() + "'");
    }
}

NomadBoolean ScriptBindingObject::getBoolean(const NomadString& name) const {
    const auto* field = getField(name, BOOLEAN_TYPE_NAME);
    return m_object->getField(m_object->getClassType()->getFieldIndex(field->getId())).getBooleanValue();
}

NomadInteger ScriptBindingObject::getInteger(const NomadString& name) const {
    const auto* field = getField(name, INTEGER_TYPE_NAME);
    const auto value = m_object->getField(m_object->getClassType()->getFieldIndex(field->getId())).getIntegerValue();
    validateInteger(field, value);
    return value;
}

NomadFloat ScriptBindingObject::getFloat(const NomadString& name) const {
    const auto* field = getField(name, FLOAT_TYPE_NAME);
    return m_object->getField(m_object->getClassType()->getFieldIndex(field->getId())).getFloatValue();
}

NomadString ScriptBindingObject::getString(const NomadString& name) const {
    const auto* field = getField(name, STRING_TYPE_NAME);
    const auto& value = m_object->getField(m_object->getClassType()->getFieldIndex(field->getId()));
    const auto* string = dynamic_cast<const NomadStringObject*>(value.getObjectValue());
    if (string == nullptr) {
        bindingError("uninitialized or invalid string field '" + name + "'");
    }
    return string->getString();
}

void ScriptBindingObject::setBoolean(const NomadString& name, NomadBoolean value) {
    const auto* field = getField(name, BOOLEAN_TYPE_NAME);
    m_object->setField(m_object->getClassType()->getFieldIndex(field->getId()), RuntimeValue(value));
}

void ScriptBindingObject::setInteger(const NomadString& name, NomadInteger value) {
    const auto* field = getField(name, INTEGER_TYPE_NAME);
    validateInteger(field, value);
    m_object->setField(m_object->getClassType()->getFieldIndex(field->getId()), RuntimeValue(value));
}

void ScriptBindingObject::setFloat(const NomadString& name, NomadFloat value) {
    const auto* field = getField(name, FLOAT_TYPE_NAME);
    m_object->setField(m_object->getClassType()->getFieldIndex(field->getId()), RuntimeValue(value));
}

void ScriptBindingObject::setString(const NomadString& name, const NomadString& value) {
    const auto* field = getField(name, STRING_TYPE_NAME);
    auto stored = RuntimeValue(value);
    try {
        m_object->setField(m_object->getClassType()->getFieldIndex(field->getId()), stored);
    } catch (...) {
        stored.freeObjectValue();
        throw;
    }
    stored.freeObjectValue();
}

} // namespace nomad
