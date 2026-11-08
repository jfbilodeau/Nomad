// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/interpreter/InterpreterStorage.hpp>

#include <nomad/script/Type.hpp>

#include <stdexcept>

namespace nomad {

InterpreterStorage::InterpreterStorage(
    const Type* type,
    const std::span<const RuntimeValue> source
):
    m_type(type)
{
    if (m_type == nullptr) {
        throw std::invalid_argument("Interpreter storage type cannot be null");
    }
    if (source.size() != m_type->getSize()) {
        throw std::invalid_argument("Interpreter storage must match its type's physical size");
    }
    m_storage.resize(source.size());
    if (source.empty()) {
        return;
    }
    try {
        m_type->copyValue(source.data(), m_storage.data());
    } catch (...) {
        m_type->freeValue(m_storage.data());
        throw;
    }
}

InterpreterStorage::~InterpreterStorage() {
    if (!m_storage.empty()) {
        m_type->freeValue(m_storage.data());
    }
}

const Type* InterpreterStorage::getType() const {
    return m_type;
}

std::span<const RuntimeValue> InterpreterStorage::getStorage() const {
    return m_storage.empty() ? std::span<const RuntimeValue>{} : m_storage;
}

std::span<RuntimeValue> InterpreterStorage::getMutableStorage() {
    return m_storage.empty() ? std::span<RuntimeValue>{} : m_storage;
}

void InterpreterStorage::assign(const std::span<const RuntimeValue> source) {
    if (source.size() != m_storage.size()) {
        throw std::invalid_argument("Interpreter assignment storage must match its type's physical size");
    }
    std::vector<RuntimeValue> scratch(m_type->getAssignmentScratchSize());
    m_type->assignValue(source.data(), m_storage.data(), scratch.empty() ? nullptr : scratch.data());
}

} // namespace nomad
