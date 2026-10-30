// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/ClassType.hpp>

namespace nomad {

class ListType final : public ClassType {
public:
    ListType(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        ClassMode mode,
        const Type* elementType
    );

    void initValue(RuntimeValue& value) const override;
};

} // namespace nomad
