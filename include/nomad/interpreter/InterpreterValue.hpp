// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/RuntimeValue.hpp>

#include <span>
#include <vector>

namespace nomad {

class Type;

class InterpreterValue {
public:
    InterpreterValue(const Type* type, const RuntimeValue& value);
    InterpreterValue(const Type* type, std::span<const RuntimeValue> storage);
    InterpreterValue(const InterpreterValue& other);
    InterpreterValue(InterpreterValue&& other) noexcept;
    InterpreterValue& operator=(const InterpreterValue& other);
    InterpreterValue& operator=(InterpreterValue&& other) noexcept;
    ~InterpreterValue();

    [[nodiscard]] const Type* getType() const;
    // Scalar access only; empty values and structs require getStorage().
    [[nodiscard]] const RuntimeValue& getValue() const;
    // Borrowed slots remain valid until this value is replaced or destroyed; moves preserve slot addresses.
    [[nodiscard]] std::span<const RuntimeValue> getStorage() const;

private:
    void release() noexcept;

    const Type* m_type;
    std::vector<RuntimeValue> m_storage;
};

} // namespace nomad
