// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/collections/ArrayObject.hpp>

namespace nomad {

class Array2dObject final : public ArrayObject {
public:
    explicit Array2dObject(const Type* elementType);
    Array2dObject(const Type* elementType, NomadInteger size1, NomadInteger size2);

    [[nodiscard]] const RuntimeValue& getValue(NomadInteger index1, NomadInteger index2) const;
    void setValue(NomadInteger index1, NomadInteger index2, const RuntimeValue& value);

private:
    [[nodiscard]] NomadIndex getOffset(NomadInteger index1, NomadInteger index2) const;
};

} // namespace nomad
