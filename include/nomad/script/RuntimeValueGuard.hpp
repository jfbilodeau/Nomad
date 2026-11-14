// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

namespace nomad {

class RuntimeValue;
class Type;

class [[nodiscard]] RuntimeValueGuard final {
public:
    // Adopts cleanup of cleared or owned getSize() slots, without copying or retaining.
    // The type and storage must outlive the guard; only zero-sized storage may be null.
    RuntimeValueGuard(const Type& type, RuntimeValue* value) noexcept;
    RuntimeValueGuard(const RuntimeValueGuard&) = delete;
    RuntimeValueGuard& operator=(const RuntimeValueGuard&) = delete;
    RuntimeValueGuard(RuntimeValueGuard&&) = delete;
    RuntimeValueGuard& operator=(RuntimeValueGuard&&) = delete;
    ~RuntimeValueGuard() noexcept;

    // Disarms cleanup after ownership transfers elsewhere; leaves the slots unchanged.
    void release() noexcept;

private:
    const Type* m_type;
    RuntimeValue* m_value;
};

} // namespace nomad
