// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/collections/ArrayType.hpp>

namespace nomad {

class Array1dObject;

class Array1Type final : public ArrayType {
public:
    Array1Type(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        ClassMode mode,
        const Type* elementType
    );

    [[nodiscard]] Array1dObject* createObject(NomadInteger size) const;

protected:
    [[nodiscard]] ArrayObject* createEmptyArray() const override;
};

} // namespace nomad
