// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/interpreter/InterpreterValue.hpp>

#include <nomad/script/Type.hpp>

#include <stdexcept>
#include <utility>

namespace nomad {

namespace {

std::span<const RuntimeValue> getScalarStorage(const Type* type, const RuntimeValue& value) {
    if (type != nullptr && type->isStruct()) {
        throw std::invalid_argument("Struct values require complete storage views");
    }
    return type != nullptr && type->isVoid() ? std::span<const RuntimeValue>{} :
        std::span<const RuntimeValue>{&value, 1};
}

} // namespace

InterpreterValue::InterpreterValue(const Type* type, const RuntimeValue& value):
    InterpreterValue(type, getScalarStorage(type, value))
{}

InterpreterValue::InterpreterValue(const Type* type, const std::span<const RuntimeValue> storage):
    m_type(type)
{
    if (m_type == nullptr) {
        throw std::invalid_argument("Interpreter value type cannot be null");
    }
    if (storage.size() != m_type->getSize()) {
        throw std::invalid_argument("Interpreter value storage must match its type's physical size");
    }
    m_storage.resize(storage.size());
    if (storage.empty()) {
        return;
    }
    m_type->copyValue(storage.data(), m_storage.data());
}

InterpreterValue::InterpreterValue(const InterpreterValue& other):
    InterpreterValue(other.m_type, other.getStorage())
{}

InterpreterValue::InterpreterValue(InterpreterValue&& other) noexcept:
    m_type(std::exchange(other.m_type, nullptr)),
    m_storage(std::move(other.m_storage))
{
    other.m_storage.clear();
}

InterpreterValue& InterpreterValue::operator=(const InterpreterValue& other) {
    if (this != &other) {
        InterpreterValue copy(other);
        std::swap(m_type, copy.m_type);
        m_storage.swap(copy.m_storage);
    }
    return *this;
}

InterpreterValue& InterpreterValue::operator=(InterpreterValue&& other) noexcept {
    if (this != &other) {
        release();
        m_type = std::exchange(other.m_type, nullptr);
        m_storage = std::move(other.m_storage);
        other.m_storage.clear();
    }
    return *this;
}

InterpreterValue::~InterpreterValue() {
    release();
}

const Type* InterpreterValue::getType() const {
    return m_type;
}

const RuntimeValue& InterpreterValue::getValue() const {
    if (m_type != nullptr && m_type->isStruct()) {
        throw std::invalid_argument("Struct values require complete storage views");
    }
    if (m_storage.empty()) {
        throw std::invalid_argument("Empty interpreter values have no scalar value");
    }
    return m_storage.front();
}

std::span<const RuntimeValue> InterpreterValue::getStorage() const {
    if (m_storage.empty()) {
        return {};
    }
    return m_storage;
}

void InterpreterValue::release() noexcept {
    if (m_type != nullptr && !m_storage.empty()) {
        m_type->freeValue(m_storage.data());
    }
    m_type = nullptr;
    m_storage.clear();
}

} // namespace nomad
