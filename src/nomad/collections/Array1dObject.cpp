// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/Array1dObject.hpp>

#include <nomad/script/Type.hpp>

#include <array>
#include <cstdint>

namespace nomad {

Array1dObject::Array1dObject(const Type* elementType):
    ArrayObject(elementType, 1)
{}

Array1dObject::Array1dObject(const Type* elementType, const NomadInteger size):
    ArrayObject(elementType, std::array{size})
{}

const RuntimeValue& Array1dObject::getValue(const NomadInteger index) const {
    return m_values[getOffset(index)];
}

void Array1dObject::setValue(const NomadInteger index, const RuntimeValue& value) {
    m_elementType->assignValue(value, m_values[getOffset(index)]);
}

NomadIndex Array1dObject::getOffset(const NomadInteger index) const {
    if (index < 0 || static_cast<std::uintmax_t>(index) >= m_dimensions[0]) {
        reportIndexOutOfBounds(index, 0, m_dimensions[0]);
    }
    return static_cast<NomadIndex>(index);
}

} // namespace nomad
