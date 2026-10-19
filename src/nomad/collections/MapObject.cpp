// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/MapObject.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/Type.hpp>

namespace nomad {

MapObject::MapObject(const Type* valueType):
    m_valueType(valueType)
{
    if (valueType == nullptr || valueType->isVoid() || valueType->isInternal()) {
        throw NomadException("Map value type must be a public, non-void type");
    }
}

const Type* MapObject::getValueType() const {
    return m_valueType;
}

NomadIndex MapObject::getCount() const {
    return m_values.size();
}

bool MapObject::contains(const NomadStringView key) const {
    return m_values.contains(key);
}

bool MapObject::remove(const NomadStringView key) {
    const auto entry = m_values.find(key);
    if (entry == m_values.end()) {
        return false;
    }
    m_valueType->freeValue(entry->second);
    m_values.erase(entry);
    return true;
}

MapObject::~MapObject() {
    if (m_valueType->needsFree()) {
        for (auto& [key, value] : m_values) {
            m_valueType->freeValue(value);
        }
    }
}

void MapObject::getValue(const NomadStringView key, RuntimeValue& result) const {
    const auto entry = m_values.find(key);
    if (entry == m_values.end()) {
        log::error("Map key '" + NomadString(key) + "' was not found");
        m_valueType->initValue(result);
    } else {
        m_valueType->copyValue(entry->second, result);
    }
}

void MapObject::setValue(const NomadStringView key, const RuntimeValue& value) {
    const auto entry = m_values.find(key);
    if (entry != m_values.end()) {
        m_valueType->assignValue(value, entry->second);
        return;
    }
    RuntimeValue storedValue;
    try {
        m_valueType->copyValue(value, storedValue);
        m_values.emplace(NomadString(key), storedValue);
    } catch (...) {
        m_valueType->freeValue(storedValue);
        throw;
    }
}

} // namespace nomad
