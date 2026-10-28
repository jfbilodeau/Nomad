// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/collections/ArrayType.hpp>

namespace nomad {

class Array2dObject;

class Array2Type final : public ArrayType {
public:
    Array2Type(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        ClassMode mode,
        const Type* elementType
    );

    [[nodiscard]] Array2dObject* createObject(NomadInteger size1, NomadInteger size2) const;

protected:
    [[nodiscard]] ArrayObject* createEmptyArray() const override;
};

} // namespace nomad
