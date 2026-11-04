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
    if (m_type->isStruct()) {
        m_storage.emplace<std::unique_ptr<RuntimeValue[]>>();
        if (!storage.empty()) {
            std::get<std::unique_ptr<RuntimeValue[]>>(m_storage) =
                std::make_unique<RuntimeValue[]>(storage.size());
        }
    }
    if (storage.empty()) {
        return;
    }
    auto* destination = getStorageAddress();
    try {
        m_type->copyValue(storage.data(), destination);
    } catch (...) {
        m_type->freeValue(destination);
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
    other.m_storage.emplace<RuntimeValue>();
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
        other.m_storage.emplace<RuntimeValue>();
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
    return std::get<RuntimeValue>(m_storage);
}

std::span<const RuntimeValue> TypedValue::getStorage() const {
    if (m_type == nullptr || m_type->getSize() == 0) {
        return {};
    }
    return {getStorageAddress(), m_type->getSize()};
}

RuntimeValue* TypedValue::getStorageAddress() {
    if (auto* scalar = std::get_if<RuntimeValue>(&m_storage)) {
        return scalar;
    }
    return std::get<std::unique_ptr<RuntimeValue[]>>(m_storage).get();
}

const RuntimeValue* TypedValue::getStorageAddress() const {
    if (const auto* scalar = std::get_if<RuntimeValue>(&m_storage)) {
        return scalar;
    }
    return std::get<std::unique_ptr<RuntimeValue[]>>(m_storage).get();
}

void TypedValue::release() noexcept {
    if (m_type != nullptr && !m_type->isVoid()) {
        m_type->freeValue(getStorageAddress());
    }
    m_type = nullptr;
}

} // namespace nomad
