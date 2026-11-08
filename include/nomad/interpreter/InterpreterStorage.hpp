// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/RuntimeValue.hpp>

#include <span>
#include <vector>

namespace nomad {

class Type;

class InterpreterStorage {
public:
    // The type must outlive this storage.
    InterpreterStorage(
        const Type* type,
        std::span<const RuntimeValue> source
    );
    InterpreterStorage(const InterpreterStorage&) = delete;
    InterpreterStorage& operator=(const InterpreterStorage&) = delete;
    ~InterpreterStorage();

    [[nodiscard]] const Type* getType() const;
    [[nodiscard]] std::span<const RuntimeValue> getStorage() const;
    // Borrowed slots stay at the same address until this owner is destroyed.
    [[nodiscard]] std::span<RuntimeValue> getMutableStorage();
    void assign(std::span<const RuntimeValue> source);

private:
    const Type* m_type;
    std::vector<RuntimeValue> m_storage;
};

} // namespace nomad
