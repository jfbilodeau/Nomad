// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/NomadObject.hpp>
#include <nomad/script/RuntimeValue.hpp>

namespace nomad {

class Type;

class KeyValueObject final : public NomadObject {
public:
    KeyValueObject(const Type* keyType, const Type* valueType);
    KeyValueObject(
        const Type* keyType,
        const Type* valueType,
        const RuntimeValue& key,
        const RuntimeValue& value
    );
    ~KeyValueObject() override;

    [[nodiscard]] const RuntimeValue& getKey() const;
    [[nodiscard]] const RuntimeValue& getValue() const;

private:
    const Type* m_keyType;
    const Type* m_valueType;
    RuntimeValue m_key;
    RuntimeValue m_value;
};

} // namespace nomad
