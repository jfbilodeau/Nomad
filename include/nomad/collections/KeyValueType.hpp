// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/Type.hpp>

namespace nomad {

class KeyValueType final : public ObjectType {
public:
    KeyValueType(
        Runtime* runtime,
        NomadString name,
        ObjectTypeMode mode,
        const Type* keyType,
        const Type* valueType
    );

    void initValue(RuntimeValue& value) const override;
};

} // namespace nomad
