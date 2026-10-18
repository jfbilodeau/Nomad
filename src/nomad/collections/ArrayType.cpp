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

ArrayType::ArrayType(const Type* elementType, const NomadIndex dimensionCount):
    ObjectType(getArrayTypeName(elementType, dimensionCount), {elementType}),
    m_dimensionCount(dimensionCount)
{}

ArrayType::~ArrayType() {
    releaseRef(m_emptyArray);
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
