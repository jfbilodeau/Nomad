// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/TypedValue.hpp>

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

TypedValue::TypedValue(const Type* type, const RuntimeValue& value):
    TypedValue(type, getScalarStorage(type, value))
{}

TypedValue::TypedValue(const Type* type, const std::span<const RuntimeValue> storage):
    m_type(type)
{
    if (m_type == nullptr) {
        throw std::invalid_argument("Typed value type cannot be null");
    }
    if (storage.size() != m_type->getSize()) {
        throw std::invalid_argument("Typed value storage must match its type's physical size");
    }
    m_storage.resize(storage.size());
    if (storage.empty()) {
        return;
    }
    try {
        m_type->copyValue(storage.data(), m_storage.data());
    } catch (...) {
        m_type->freeValue(m_storage.data());
        m_type = nullptr;
        throw;
    }
}

TypedValue::TypedValue(const TypedValue& other):
    TypedValue(other.m_type, other.getStorage())
{}

TypedValue::TypedValue(TypedValue&& other) noexcept:
    m_type(std::exchange(other.m_type, nullptr)),
    m_storage(std::move(other.m_storage))
{
    other.m_storage.clear();
}

TypedValue& TypedValue::operator=(const TypedValue& other) {
    if (this != &other) {
        TypedValue copy(other);
        std::swap(m_type, copy.m_type);
        m_storage.swap(copy.m_storage);
    }
    return *this;
}

TypedValue& TypedValue::operator=(TypedValue&& other) noexcept {
    if (this != &other) {
        release();
        m_type = std::exchange(other.m_type, nullptr);
        m_storage = std::move(other.m_storage);
        other.m_storage.clear();
    }
    return *this;
}

TypedValue::~TypedValue() {
    release();
}

const Type* TypedValue::getType() const {
    return m_type;
}

const RuntimeValue& TypedValue::getValue() const {
    if (m_type != nullptr && m_type->isStruct()) {
        throw std::invalid_argument("Struct values require complete storage views");
    }
    if (m_storage.empty()) {
        throw std::invalid_argument("Empty typed values have no scalar value");
    }
    return m_storage.front();
}

std::span<const RuntimeValue> TypedValue::getStorage() const {
    if (m_storage.empty()) {
        return {};
    }
    return m_storage;
}

void TypedValue::release() noexcept {
    if (m_type != nullptr && !m_storage.empty()) {
        m_type->freeValue(m_storage.data());
    }
    m_type = nullptr;
    m_storage.clear();
}

} // namespace nomad
