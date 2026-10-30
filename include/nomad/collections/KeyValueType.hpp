// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/ClassType.hpp>

namespace nomad {

class KeyValueType final : public ClassType {
public:
    KeyValueType(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        ClassMode mode,
        const Type* keyType,
        const Type* valueType
    );

    void initValue(RuntimeValue& value) const override;
};

} // namespace nomad
