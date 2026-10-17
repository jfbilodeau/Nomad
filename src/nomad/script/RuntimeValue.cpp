// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/RuntimeValue.hpp>

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
    m_stringValue = nullptr;
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
    setStringValue(value.c_str());
}

void RuntimeValue::setIdValue(NomadId value) {
    m_idValue = value;
}

void RuntimeValue::setStringValue(const NomadChar* value) {
    if (value == nullptr) {
        m_stringValue = nullptr;
        return;
    }

    m_stringValue = NomadStringObject::create(value)->getText();
}

void RuntimeValue::setNullStringValue() {
    m_stringValue = nullptr;
}

void RuntimeValue::setStringRefValue(const NomadChar* value) {
    m_stringValue = value;
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
    setStringValue(value.c_str());
}

void RuntimeValue::copyStringValue(const RuntimeValue &other){
    if (other.m_stringValue == nullptr) {
        m_stringValue = nullptr;
        return;
    }

    NomadStringObject::fromText(other.m_stringValue)->addRef();
    m_stringValue = other.m_stringValue;
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

const NomadChar* RuntimeValue::getStringValue() const {
    return m_stringValue;
}

void RuntimeValue::initStringValue() {
    m_stringValue = nullptr;
}

void RuntimeValue::initStringValue(const NomadString& value) {
    setStringValue(value);
}

void RuntimeValue::moveStringValue(RuntimeValue& other) {
    m_stringValue = other.m_stringValue;
    other.m_stringValue = nullptr;
}

void RuntimeValue::freeStringValue() {
    if (m_stringValue != nullptr) {
        NomadStringObject::fromText(m_stringValue)->releaseRef();
    }

    m_stringValue = nullptr;
}

NomadInteger RuntimeValue::getLiveStringCount() {
    return NomadStringObject::getLiveCount();
}

} // namespace nomad
