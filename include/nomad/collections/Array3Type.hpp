// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/collections/ArrayType.hpp>

namespace nomad {

class Array3dObject;

class Array3Type final : public ArrayType {
public:
    Array3Type(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        ClassMode mode,
        const Type* elementType
    );

    [[nodiscard]] Array3dObject* createObject(NomadInteger size1, NomadInteger size2, NomadInteger size3) const;

protected:
    [[nodiscard]] ArrayObject* createEmptyArray() const override;
};

} // namespace nomad
