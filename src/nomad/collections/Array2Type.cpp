// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/Array2Type.hpp>

#include <nomad/collections/Array2dObject.hpp>

namespace nomad {

Array2Type::Array2Type(const Type* elementType, const Type* dimensionType):
    ArrayType(elementType, 2, dimensionType),
    m_constructorParameterTypes{dimensionType, dimensionType}
{}

std::span<const Type* const> Array2Type::getConstructorParameterTypes() const {
    return m_constructorParameterTypes;
}

NomadObject* Array2Type::createObject(const std::span<const RuntimeValue> arguments) const {
    if (arguments.size() != m_constructorParameterTypes.size()) {
        throw NomadException("Incorrect constructor argument count for '" + getTypeName() + "'");
    }
    return createObject(arguments[0].getIntegerValue(), arguments[1].getIntegerValue());
}

Array2dObject* Array2Type::createObject(const NomadInteger size1, const NomadInteger size2) const {
    return new Array2dObject(getElementType(), size1, size2);
}

ArrayObject* Array2Type::createEmptyArray() const {
    return new Array2dObject(getElementType());
}

} // namespace nomad
