// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/Array3dObject.hpp>

#include <nomad/script/Type.hpp>

#include <array>
#include <cstdint>

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
    return m_values[getOffset(index1, index2, index3)];
}

void Array3dObject::setValue(
    const NomadInteger index1,
    const NomadInteger index2,
    const NomadInteger index3,
    const RuntimeValue& value
) {
    m_elementType->assignValue(value, m_values[getOffset(index1, index2, index3)]);
}

NomadIndex Array3dObject::getOffset(
    const NomadInteger index1,
    const NomadInteger index2,
    const NomadInteger index3
) const {
    if (index1 < 0 || static_cast<std::uintmax_t>(index1) >= m_dimensions[0]) {
        reportIndexOutOfBounds(index1, 0, m_dimensions[0]);
    }
    if (index2 < 0 || static_cast<std::uintmax_t>(index2) >= m_dimensions[1]) {
        reportIndexOutOfBounds(index2, 1, m_dimensions[1]);
    }
    if (index3 < 0 || static_cast<std::uintmax_t>(index3) >= m_dimensions[2]) {
        reportIndexOutOfBounds(index3, 2, m_dimensions[2]);
    }
    return (static_cast<NomadIndex>(index1) * m_dimensions[1] + static_cast<NomadIndex>(index2)) *
        m_dimensions[2] + static_cast<NomadIndex>(index3);
}

} // namespace nomad
