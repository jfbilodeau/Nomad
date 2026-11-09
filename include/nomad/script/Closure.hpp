// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/script/RuntimeValue.hpp>

#include <span>
#include <vector>

namespace nomad {

// Forward declarations
class ScriptFunction;
class Type;

// A closure references function and type metadata owned by its Runtime and must not outlive that Runtime.
class Closure {
public:
    // Adopts complete, owned capture slots only after validating their layout.
    explicit Closure(
        const ScriptFunction* function,
        std::vector<RuntimeValue>&& captures = {}
    );
    Closure(const Closure& other) = delete;
    Closure& operator=(const Closure&) = delete;
    ~Closure();

    [[nodiscard]]
    const ScriptFunction* getFunction() const { return m_function; }

    [[nodiscard]]
    NomadIndex getCaptureCount() const;

    [[nodiscard]]
    const RuntimeValue& getCaptureValue(NomadIndex captureIndex) const;

    // Borrows complete owned slots; empty struct captures have no storage pointer.
    [[nodiscard]] std::span<const RuntimeValue> getCaptureStorage(
        NomadIndex captureIndex
    ) const;

    [[nodiscard]]
    const Type* getCaptureType(NomadIndex captureIndex) const;

private:
    const ScriptFunction* m_function;
    NomadIndex m_captureCount = 0;
    std::vector<RuntimeValue> m_captureStorage;
};

} // namespace nomad
