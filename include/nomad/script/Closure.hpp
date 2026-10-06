// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/script/RuntimeValue.hpp>

#include <vector>

namespace nomad {

// Forward declarations
class Function;
class Type;

class Closure {
public:
    explicit Closure(const Function* function, std::vector<RuntimeValue> captures = {});
    Closure(const Closure& other) = delete;
    ~Closure();

    [[nodiscard]]
    const Function* getFunction() const { return m_function; }

    [[nodiscard]]
    NomadIndex getCaptureCount() const;

    [[nodiscard]]
    const RuntimeValue& getCaptureValue(NomadIndex captureIndex) const;

    [[nodiscard]]
    const Type* getCaptureType(NomadIndex captureIndex) const;

private:
    const Function* m_function;
    std::vector<RuntimeValue> m_capturedParameters;
};

} // namespace nomad
