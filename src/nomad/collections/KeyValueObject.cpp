// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/KeyValueObject.hpp>

#include <nomad/script/Type.hpp>

namespace nomad {

KeyValueObject::KeyValueObject(const Type* keyType, const Type* valueType):
    m_keyType(keyType),
    m_valueType(valueType)
{
    if (keyType == nullptr || keyType->isVoid() || keyType->isInternal() ||
        valueType == nullptr || valueType->isVoid() || valueType->isInternal()) {
        throw NomadException("KeyValue types must be public, non-void types");
    }
    try {
        m_keyType->initValue(&m_key);
        m_valueType->initValue(&m_value);
    } catch (...) {
        m_keyType->freeValue(&m_key);
        m_valueType->freeValue(&m_value);
        throw;
    }
}

KeyValueObject::KeyValueObject(
    const Type* keyType,
    const Type* valueType,
    const RuntimeValue& key,
    const RuntimeValue& value
):
    m_keyType(keyType),
    m_valueType(valueType)
{
    if (keyType == nullptr || keyType->isVoid() || keyType->isInternal() ||
        valueType == nullptr || valueType->isVoid() || valueType->isInternal()) {
        throw NomadException("KeyValue types must be public, non-void types");
    }
    try {
        m_keyType->copyValue(&key, &m_key);
        m_valueType->copyValue(&value, &m_value);
    } catch (...) {
        m_keyType->freeValue(&m_key);
        m_valueType->freeValue(&m_value);
        throw;
    }
}

KeyValueObject::~KeyValueObject() {
    if (m_keyType->needsFree()) {
        m_keyType->freeValue(&m_key);
    }
    if (m_valueType->needsFree()) {
        m_valueType->freeValue(&m_value);
    }
}

const RuntimeValue& KeyValueObject::getKey() const {
    return m_key;
}

const RuntimeValue& KeyValueObject::getValue() const {
    return m_value;
}

} // namespace nomad
