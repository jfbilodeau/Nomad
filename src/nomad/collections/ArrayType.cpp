// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/ArrayType.hpp>

#include <nomad/collections/Array1dObject.hpp>
#include <nomad/collections/Array2dObject.hpp>
#include <nomad/collections/Array3dObject.hpp>

#include <nomad/system/String.hpp>

namespace nomad {

namespace {

NomadString getArrayTypeName(const Type* elementType, const NomadIndex dimensionCount) {
    if (elementType == nullptr || elementType->isVoid() || elementType->isInternal()) {
        throw NomadException("Array element type must be a public, non-void type");
    }
    if (dimensionCount < 1 || dimensionCount > 3) {
        throw NomadException("Array dimension count must be between 1 and 3");
    }
    return "Array" + toString(dimensionCount) + "<" + elementType->getTypeName() + ">";
}

} // namespace

ArrayType::ArrayType(
    const Type* elementType,
    const NomadIndex dimensionCount,
    const Type* dimensionType
):
    ObjectType(
        getArrayTypeName(elementType, dimensionCount), {elementType}, {},
        "A fixed-size array. Explicit construction requires one positive int size per dimension."
    ),
    m_dimensionCount(dimensionCount),
    m_constructorParameterTypes(dimensionCount, dimensionType)
{
    if (dynamic_cast<const IntegerType*>(dimensionType) == nullptr) {
        throw NomadException("Array dimension type must be int");
    }
}

ArrayType::~ArrayType() {
    releaseRef(m_emptyArray);
}

bool ArrayType::isConstructible() const {
    return true;
}

std::span<const Type* const> ArrayType::getConstructorParameterTypes() const {
    return m_constructorParameterTypes;
}

NomadObject* ArrayType::createObject(const std::span<const RuntimeValue> arguments) const {
    if (arguments.size() != m_dimensionCount) {
        throw NomadException("Incorrect constructor argument count for '" + getTypeName() + "'");
    }
    switch (m_dimensionCount) {
    case 1:
        return new Array1dObject(getElementType(), arguments[0].getIntegerValue());
    case 2:
        return new Array2dObject(getElementType(), arguments[0].getIntegerValue(), arguments[1].getIntegerValue());
    case 3:
        return new Array3dObject(
            getElementType(), arguments[0].getIntegerValue(), arguments[1].getIntegerValue(), arguments[2].getIntegerValue()
        );
    }
    throw NomadBug("Invalid array dimension count");
}

void ArrayType::initValue(RuntimeValue& value) const {
    if (m_emptyArray == nullptr) {
        switch (m_dimensionCount) {
        case 1:
            m_emptyArray = new Array1dObject(getElementType());
            break;
        case 2:
            m_emptyArray = new Array2dObject(getElementType());
            break;
        case 3:
            m_emptyArray = new Array3dObject(getElementType());
            break;
        }
    }
    addRef(m_emptyArray);
    value.setObjectValue(m_emptyArray);
}

const Type* ArrayType::getElementType() const {
    return getTypeArguments()[0];
}

NomadIndex ArrayType::getDimensionCount() const {
    return m_dimensionCount;
}

} // namespace nomad
