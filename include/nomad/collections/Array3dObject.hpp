// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/collections/ArrayObject.hpp>

namespace nomad {

class Array3dObject final : public ArrayObject {
public:
    explicit Array3dObject(const Type* elementType);
    Array3dObject(const Type* elementType, NomadInteger size1, NomadInteger size2, NomadInteger size3);

    [[nodiscard]] const RuntimeValue& getValue(NomadInteger index1, NomadInteger index2, NomadInteger index3) const;
    void setValue(NomadInteger index1, NomadInteger index2, NomadInteger index3, const RuntimeValue& value);
};

} // namespace nomad
