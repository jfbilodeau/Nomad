// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/collections/ArrayObject.hpp>

namespace nomad {

class Array1dObject final : public ArrayObject {
public:
    explicit Array1dObject(const Type* elementType);
    Array1dObject(const Type* elementType, NomadInteger size);

    [[nodiscard]] const RuntimeValue& getValue(NomadInteger index) const;
    void setValue(NomadInteger index, const RuntimeValue& value);
};

} // namespace nomad
