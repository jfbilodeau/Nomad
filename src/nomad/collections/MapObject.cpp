// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/MapObject.hpp>

#include <nomad/collections/Array1dObject.hpp>
#include <nomad/collections/KeyValueObject.hpp>
#include <nomad/collections/KeyValueType.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/Type.hpp>

#include <utility>

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

std::unique_ptr<MapObject> MapObject::createCopy() const {
    auto snapshot = std::make_unique<MapObject>(m_valueType);
    for (const auto& [key, value] : m_values) {
        snapshot->setValue(key, value);
    }
    return snapshot;
}

std::unique_ptr<Array1dObject> MapObject::getKeys(const Type* keyType) const {
    if (keyType == nullptr || !keyType->isString() || keyType->isInternal()) {
        throw NomadException("Map key type must be string");
    }
    if (m_values.empty()) {
        return std::make_unique<Array1dObject>(keyType);
    }
    if (!std::in_range<NomadInteger>(m_values.size())) {
        throw NomadException("Map key count exceeds the maximum array size");
    }
    const auto count = static_cast<NomadInteger>(m_values.size());
    auto keys = std::make_unique<Array1dObject>(keyType, count);
    auto index = NomadInteger{0};
    for (const auto& [key, value] : m_values) {
        RuntimeValue storedKey;
        storedKey.setStringValue(key);
        try {
            keys->setValue(index, storedKey);
        } catch (...) {
            keyType->freeValue(storedKey);
            throw;
        }
        keyType->freeValue(storedKey);
        ++index;
    }
    return keys;
}

std::unique_ptr<Array1dObject> MapObject::getValues() const {
    if (m_values.empty()) {
        return std::make_unique<Array1dObject>(m_valueType);
    }
    if (!std::in_range<NomadInteger>(m_values.size())) {
        throw NomadException("Map value count exceeds the maximum array size");
    }
    auto values = std::make_unique<Array1dObject>(m_valueType, static_cast<NomadInteger>(m_values.size()));
    auto index = NomadInteger{0};
    for (const auto& [key, value] : m_values) {
        values->setValue(index, value);
        ++index;
    }
    return values;
}

std::unique_ptr<Array1dObject> MapObject::getEntries(const KeyValueType* entryType) const {
    if (entryType == nullptr || !entryType->getTypeArguments()[0]->isString() ||
        !entryType->getTypeArguments()[1]->sameType(m_valueType)) {
        throw NomadException("Map entry type must be KeyValue<string V> with the map's value type");
    }
    const auto* keyType = entryType->getTypeArguments()[0];
    if (m_values.empty()) {
        return std::make_unique<Array1dObject>(entryType);
    }
    if (!std::in_range<NomadInteger>(m_values.size())) {
        throw NomadException("Map entry count exceeds the maximum array size");
    }
    auto entries = std::make_unique<Array1dObject>(entryType, static_cast<NomadInteger>(m_values.size()));
    auto index = NomadInteger{0};
    for (const auto& [key, value] : m_values) {
        RuntimeValue storedKey;
        storedKey.setStringValue(key);
        RuntimeValue entry;
        try {
            entry.setObjectValue(new KeyValueObject(keyType, m_valueType, storedKey, value));
            entries->setValue(index, entry);
        } catch (...) {
            entryType->freeValue(entry);
            keyType->freeValue(storedKey);
            throw;
        }
        entryType->freeValue(entry);
        keyType->freeValue(storedKey);
        ++index;
    }
    return entries;
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
    clear();
}

void MapObject::clear() {
    if (m_valueType->needsFree()) {
        for (auto& [key, value] : m_values) {
            m_valueType->freeValue(value);
        }
    }
    m_values.clear();
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
