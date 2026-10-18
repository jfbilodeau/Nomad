// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/Array3Type.hpp>

#include <nomad/collections/Array3dObject.hpp>

namespace nomad {

Array3Type::Array3Type(const Type* elementType, const Type* dimensionType):
    ArrayType(elementType, 3, dimensionType),
    m_constructorParameterTypes{dimensionType, dimensionType, dimensionType}
{}

std::span<const Type* const> Array3Type::getConstructorParameterTypes() const {
    return m_constructorParameterTypes;
}

NomadObject* Array3Type::createObject(const std::span<const RuntimeValue> arguments) const {
    if (arguments.size() != m_constructorParameterTypes.size()) {
        throw NomadException("Incorrect constructor argument count for '" + getTypeName() + "'");
    }
    return createObject(
        arguments[0].getIntegerValue(), arguments[1].getIntegerValue(), arguments[2].getIntegerValue()
    );
}

Array3dObject* Array3Type::createObject(
    const NomadInteger size1,
    const NomadInteger size2,
    const NomadInteger size3
) const {
    return new Array3dObject(getElementType(), size1, size2, size3);
}

ArrayObject* Array3Type::createEmptyArray() const {
    return new Array3dObject(getElementType());
}

} // namespace nomad
