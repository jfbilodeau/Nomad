// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/Array1Type.hpp>

#include <nomad/collections/Array1dObject.hpp>

namespace nomad {

Array1Type::Array1Type(const Type* elementType, const Type* dimensionType):
    ArrayType(elementType, 1, dimensionType),
    m_constructorParameterTypes{dimensionType}
{}

std::span<const Type* const> Array1Type::getConstructorParameterTypes() const {
    return m_constructorParameterTypes;
}

NomadObject* Array1Type::createObject(const std::span<const RuntimeValue> arguments) const {
    if (arguments.size() != m_constructorParameterTypes.size()) {
        throw NomadException("Incorrect constructor argument count for '" + getTypeName() + "'");
    }
    return createObject(arguments[0].getIntegerValue());
}

Array1dObject* Array1Type::createObject(const NomadInteger size) const {
    return new Array1dObject(getElementType(), size);
}

ArrayObject* Array1Type::createEmptyArray() const {
    return new Array1dObject(getElementType());
}

} // namespace nomad
