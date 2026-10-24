// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/Type.hpp>

namespace nomad {

class ListType final : public ObjectType {
public:
    ListType(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        ObjectTypeMode mode,
        const Type* elementType
    );

    void initValue(RuntimeValue& value) const override;
};

} // namespace nomad
