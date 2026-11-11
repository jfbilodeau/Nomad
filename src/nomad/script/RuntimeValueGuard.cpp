// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/RuntimeValueGuard.hpp>

#include <nomad/script/Type.hpp>

namespace nomad {

RuntimeValueGuard::RuntimeValueGuard(const Type& type, RuntimeValue* value) noexcept:
    m_type(&type),
    m_value(value)
{}

RuntimeValueGuard::~RuntimeValueGuard() noexcept {
    if (m_type != nullptr) {
        m_type->freeValue(m_value);
    }
}

void RuntimeValueGuard::release() noexcept {
    m_type = nullptr;
}

} // namespace nomad
