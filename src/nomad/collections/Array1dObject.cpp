// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/Array1dObject.hpp>

#include <array>

namespace nomad {

Array1dObject::Array1dObject(const Type* elementType):
    ArrayObject(elementType, 1)
{}

Array1dObject::Array1dObject(const Type* elementType, const NomadInteger size):
    ArrayObject(elementType, std::array{size})
{}

const RuntimeValue& Array1dObject::getValue(const NomadInteger index) const {
    return getElement(std::array{index});
}

void Array1dObject::setValue(const NomadInteger index, const RuntimeValue& value) {
    setElement(std::array{index}, value);
}

} // namespace nomad
