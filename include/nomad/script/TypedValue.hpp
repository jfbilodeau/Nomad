// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/RuntimeValue.hpp>

#include <memory>
#include <span>
#include <variant>

namespace nomad {

class Type;

class TypedValue {
public:
    TypedValue(const Type* type, const RuntimeValue& value);
    TypedValue(const Type* type, std::span<const RuntimeValue> storage);
    TypedValue(const TypedValue& other);
    TypedValue(TypedValue&& other) noexcept;
    TypedValue& operator=(const TypedValue& other);
    TypedValue& operator=(TypedValue&& other) noexcept;
    ~TypedValue();

    [[nodiscard]] const Type* getType() const;
    // Scalar access only; structs, including empty structs, require getStorage().
    [[nodiscard]] const RuntimeValue& getValue() const;
    // Borrowed slots remain valid until this value is replaced or destroyed; moves preserve struct addresses.
    [[nodiscard]] std::span<const RuntimeValue> getStorage() const;

private:
    [[nodiscard]] RuntimeValue* getStorageAddress();
    [[nodiscard]] const RuntimeValue* getStorageAddress() const;
    void release() noexcept;

    const Type* m_type;
    std::variant<RuntimeValue, std::unique_ptr<RuntimeValue[]>> m_storage;
};

} // namespace nomad
