// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/RuntimeValue.hpp>

#include <nomad/script/NomadObject.hpp>
#include <nomad/script/NomadStringObject.hpp>

namespace nomad {

RuntimeValue::RuntimeValue() {
    m_integerValue = 0;
}

RuntimeValue::RuntimeValue(NomadIndex value) {
    m_indexValue = value;
}

RuntimeValue::RuntimeValue(NomadFloat value) {
    m_floatValue = value;
}

RuntimeValue::RuntimeValue(NomadInteger value) {
    m_integerValue = value;
}

RuntimeValue::RuntimeValue(NomadBoolean value) {
    m_booleanValue = value;
}

RuntimeValue::RuntimeValue(NomadId value) {
    m_idValue = value;
}

RuntimeValue::RuntimeValue(const NomadString& value) {
    setStringValue(value);
}

void RuntimeValue::setFloatValue(NomadFloat value) {
    m_floatValue = value;
}

void RuntimeValue::setIntegerValue(NomadInteger value) {
    m_integerValue = value;
}

void RuntimeValue::setIndexValue(NomadIndex value) {
    m_indexValue = value;
}

void RuntimeValue::setBooleanValue(NomadBoolean value) {
    m_booleanValue = value;
}

void RuntimeValue::setStringValue(const NomadString& value) {
    m_objectValue = NomadStringObject::create(value);
}

void RuntimeValue::setIdValue(NomadId value) {
    m_idValue = value;
}

void RuntimeValue::setStringValue(const NomadChar* value) {
    m_objectValue = NomadStringObject::create(value);
}

void RuntimeValue::setObjectValue(NomadObject* value) {
    m_objectValue = value;
}

void RuntimeValue::set(NomadFloat value) {
    setFloatValue(value);
}

void RuntimeValue::set(NomadInteger value) {
    setIntegerValue(value);
}

void RuntimeValue::set(NomadBoolean value) {
    setBooleanValue(value);
}

void RuntimeValue::set(const NomadChar* value) {
    setStringValue(value);
}

void RuntimeValue::set(const NomadString& value) {
    setStringValue(value);
}

void RuntimeValue::copyObjectValue(const RuntimeValue& other) {
    addRef(other.m_objectValue);
    m_objectValue = other.m_objectValue;
}

NomadFloat RuntimeValue::getFloatValue() const {
    return m_floatValue;
}

NomadInteger RuntimeValue::getIntegerValue() const {
    return m_integerValue;
}

NomadIndex RuntimeValue::getIndexValue() const {
    return m_indexValue;
}

NomadBoolean RuntimeValue::getBooleanValue() const {
    return m_booleanValue;
}

NomadId RuntimeValue::getIdValue() const {
    return m_idValue;
}

NomadObject* RuntimeValue::getObjectValue() const {
    return m_objectValue;
}

NomadStringObject* RuntimeValue::getStringObject() const {
    return asObject<NomadStringObject>();
}

const NomadChar* RuntimeValue::getStringValue() const {
    return m_objectValue == nullptr ? "" : getStringObject()->getText();
}

void RuntimeValue::initStringValue() {
    m_objectValue = NomadStringObject::createEmpty();
}

void RuntimeValue::initStringValue(const NomadString& value) {
    setStringValue(value);
}

void RuntimeValue::moveObjectValue(RuntimeValue& other) {
    m_objectValue = other.m_objectValue;
    other.m_objectValue = nullptr;
}

void RuntimeValue::freeObjectValue() {
    releaseRef(m_objectValue);
    m_objectValue = nullptr;
}

NomadInteger RuntimeValue::getLiveStringCount() {
    return NomadStringObject::getLiveCount();
}

} // namespace nomad
