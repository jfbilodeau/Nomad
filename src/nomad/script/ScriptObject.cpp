// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/ScriptObject.hpp>

#include <nomad/script/ClassType.hpp>
#include <nomad/script/Property.hpp>

namespace nomad {

ScriptObject::ScriptObject(const ClassType* classType):
    m_classType(classType)
{
    if (classType == nullptr) {
        throw NomadException("Script object requires a class type");
    }
    m_fields.reserve(classType->getPropertyCount());
    for (auto index = NomadIndex{0}; index < classType->getPropertyCount(); ++index) {
        const auto* property = classType->getPropertyAt(index);
        if (property->asNativeProperty() == nullptr) {
            const auto* type = property->getValueType();
            m_fields.push_back({type, classType->getFieldOffset(property->getId()), type->getSize()});
        }
    }
    m_values.resize(classType->getFieldStorageSize());
}

ScriptObject::~ScriptObject() {
    for (const auto& field : m_fields) {
        if (field.size != 0 && field.type->needsFree()) {
            field.type->freeValue(m_values.data() + field.offset);
        }
    }
}

const ClassType* ScriptObject::getClassType() const {
    return m_classType;
}

NomadIndex ScriptObject::getFieldCount() const {
    return m_fields.size();
}

const RuntimeValue& ScriptObject::getField(const NomadIndex index) const {
    validateSingleSlotFieldAccess(index);
    return m_values[m_fields[index].offset];
}

void ScriptObject::setField(const NomadIndex index, const RuntimeValue& value) {
    validateSingleSlotFieldAccess(index);
    const auto& field = m_fields[index];
    field.type->assignValue(&value, m_values.data() + field.offset);
}

std::span<RuntimeValue> ScriptObject::getFieldStorage(const NomadIndex index) {
    validateFieldAccess(index);
    const auto& field = m_fields[index];
    if (field.size == 0) {
        return {};
    }
    return std::span<RuntimeValue>(m_values).subspan(field.offset, field.size);
}

std::span<const RuntimeValue> ScriptObject::getFieldStorage(const NomadIndex index) const {
    validateFieldAccess(index);
    const auto& field = m_fields[index];
    if (field.size == 0) {
        return {};
    }
    return std::span<const RuntimeValue>(m_values).subspan(field.offset, field.size);
}

void ScriptObject::setFieldStorage(
    const NomadIndex index,
    const std::span<const RuntimeValue> value,
    const std::span<RuntimeValue> scratch
) {
    validateFieldAccess(index);
    const auto& field = m_fields[index];
    if (value.size() != field.size) {
        throw NomadException("Script object field source storage has an incorrect slot count");
    }
    const auto scratchSize = field.type->getAssignmentScratchSize();
    if (scratch.size() < scratchSize) {
        throw NomadException("Script object field assignment has insufficient scratch storage");
    }
    const auto destination = getFieldStorage(index);
    field.type->assignValue(value.data(), destination.data(), scratch.empty() ? nullptr : scratch.data());
}

void ScriptObject::validateFieldAccess(const NomadIndex index) const {
    if (index >= m_fields.size()) {
        throw NomadException("Script object field index is out of bounds");
    }
}

void ScriptObject::validateSingleSlotFieldAccess(const NomadIndex index) const {
    validateFieldAccess(index);
    const auto& field = m_fields[index];
    if (field.size != 1 || field.type->getStorage() == TypeStorage::Struct) {
        throw NomadException("Script object field requires the field storage API");
    }
}

} // namespace nomad
