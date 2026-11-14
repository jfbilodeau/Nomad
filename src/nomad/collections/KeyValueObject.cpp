// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/KeyValueObject.hpp>

#include <nomad/script/RuntimeValueGuard.hpp>
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
    m_keyType->initValue(&m_key);
    RuntimeValueGuard keyGuard(*m_keyType, &m_key);
    m_valueType->initValue(&m_value);
    keyGuard.release();
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
    m_keyType->copyValue(&key, &m_key);
    RuntimeValueGuard keyGuard(*m_keyType, &m_key);
    m_valueType->copyValue(&value, &m_value);
    keyGuard.release();
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
