// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/Array3dObject.hpp>

#include <array>

namespace nomad {

Array3dObject::Array3dObject(const Type* elementType):
    ArrayObject(elementType, 3)
{}

Array3dObject::Array3dObject(
    const Type* elementType,
    const NomadInteger size1,
    const NomadInteger size2,
    const NomadInteger size3
):
    ArrayObject(elementType, std::array{size1, size2, size3})
{}

const RuntimeValue& Array3dObject::getValue(
    const NomadInteger index1,
    const NomadInteger index2,
    const NomadInteger index3
) const {
    return getElement(std::array{index1, index2, index3});
}

void Array3dObject::setValue(
    const NomadInteger index1,
    const NomadInteger index2,
    const NomadInteger index3,
    const RuntimeValue& value
) {
    setElement(std::array{index1, index2, index3}, value);
}

} // namespace nomad
