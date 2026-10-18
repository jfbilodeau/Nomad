// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/Array2dObject.hpp>

#include <array>

namespace nomad {

Array2dObject::Array2dObject(const Type* elementType):
    ArrayObject(elementType, 2)
{}

Array2dObject::Array2dObject(
    const Type* elementType,
    const NomadInteger size1,
    const NomadInteger size2
):
    ArrayObject(elementType, std::array{size1, size2})
{}

const RuntimeValue& Array2dObject::getValue(const NomadInteger index1, const NomadInteger index2) const {
    return getElement(std::array{index1, index2});
}

void Array2dObject::setValue(
    const NomadInteger index1,
    const NomadInteger index2,
    const RuntimeValue& value
) {
    setElement(std::array{index1, index2}, value);
}

} // namespace nomad
