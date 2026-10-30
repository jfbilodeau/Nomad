// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/modules/ScriptClassBinding.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <algorithm>

namespace nomad {

namespace {

[[noreturn]] void bindingError(const NomadString& message) {
    throw NomadException("Script binding contract: " + message);
}

} // namespace

bool BindingEnum::operator==(const BindingEnum& other) const {
    return name == other.name && std::ranges::equal(values, other.values, [](const auto& first, const auto& second) {
        return first.name == second.name && first.value == second.value;
    });
}

bool isScriptBindingClass(const ClassType& type) {
    if (!type.getTypeArguments().empty() || !type.getNativeIndexers().empty() || type.getIterableInfo()) {
        return false;
    }
    const auto constructors = type.getConstructors();
    if (constructors.size() != 1 || constructors.front()->asScriptConstructor() == nullptr ||
        constructors.front()->getParameterCount() != 0 || constructors.front()->getConstructedType() != &type) {
        return false;
    }
    // Every ordinary script class has one compiler-owned field initializer method.
    const auto methods = type.getMethods();
    return methods.size() == 1 && methods.front()->getName() == type.getInitializerName() &&
        methods.front()->asScriptMethod() != nullptr && methods.front()->getParameterCount() == 0 &&
        methods.front()->getReturnType() != nullptr && methods.front()->getReturnType()->isVoid() &&
        methods.front()->getClassType() == &type;
}

ScriptClassBinding::ScriptClassBinding(Runtime& runtime, const BindingClass& contract) :
    m_runtime(&runtime),
    m_contract(contract),
    m_classType(nullptr) {
    const auto* type = runtime.getTypeByName(contract.name);
    m_classType = type == nullptr ? nullptr : type->asClassType();
    if (m_classType == nullptr || m_classType->getRuntime() != &runtime ||
        m_classType->getProperties().size() != contract.fields.size() || !isScriptBindingClass(*m_classType)) {
        bindingError("class shape mismatch for '" + contract.name + "'");
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
        const auto* field = m_classType->getProperty(expected.name);
        if (field == nullptr || field->asNativeProperty() != nullptr || field->getValueType() == nullptr ||
            field->getValueType() != runtime.getTypeByName(expected.type) ||
            m_classType->getFieldIndex(field->getId()) == NOMAD_INVALID_INDEX) {
            bindingError("stored field mismatch for '" + contract.name + "." + expected.name + "'");
        }
        const auto* fieldType = field->getValueType();
        const auto* enumeration = fieldType->asEnum();
        auto kind = BindingValueKind::Integer;
        if (fieldType == runtime.getBooleanType()) {
            kind = BindingValueKind::Boolean;
        } else if (fieldType == runtime.getFloatType()) {
            kind = BindingValueKind::Float;
        } else if (fieldType == runtime.getStringType()) {
            kind = BindingValueKind::String;
        } else if (enumeration != nullptr) {
            if (std::ranges::none_of(contract.enums, [&](const auto& expectedEnum) {
                return expectedEnum.name == enumeration->getTypeName();
            })) {
                bindingError("unsupported field type for '" + expected.name + "'");
            }
        } else if (fieldType != runtime.getIntegerType()) {
            bindingError("unsupported field type for '" + expected.name + "'");
        }
        m_fields.push_back({field->getId(), m_classType->getFieldIndex(field->getId()), kind});
    }
    const auto constructor = runtime.resolveConstructor(m_classType, {});
    if (!constructor.isResolved()) {
        bindingError("cannot resolve constructor for '" + contract.name + "'");
    }
    m_constructorId = constructor.id;
}

Runtime& ScriptClassBinding::getRuntime() const {
    return *m_runtime;
}

const BindingClass& ScriptClassBinding::getContract() const {
    return m_contract;
}

const ClassType* ScriptClassBinding::getClassType() const {
    return m_classType;
}

const ScriptClassBinding::ResolvedField& ScriptClassBinding::getField(NomadIndex slot) const {
    if (slot >= m_fields.size()) {
        bindingError("field slot out of range for '" + m_contract.name + "'");
    }
    return m_fields[slot];
}

PropertyId ScriptClassBinding::getPropertyId(NomadIndex slot) const {
    return getField(slot).propertyId;
}

NomadIndex ScriptClassBinding::getFieldIndex(NomadIndex slot, BindingValueKind kind) const {
    const auto& field = getField(slot);
    if (field.kind != kind) {
        bindingError("wrong access type for '" + m_contract.name + "." + m_contract.fields[slot].name + "'");
    }
    return field.index;
}

NomadObject* ScriptClassBinding::construct(VirtualMachine& machine) const {
    if (machine.getRuntime() != m_runtime) {
        bindingError("virtual machine belongs to another runtime");
    }
    return machine.constructObject(m_constructorId, {});
}

} // namespace nomad
