// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/ArrayType.hpp>

#include <nomad/collections/ArrayObject.hpp>

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
    const bool readOnly
):
    ObjectType(
        (readOnly ? "ReadOnly" : "") + getArrayTypeName(elementType, dimensionCount), {elementType},
        readOnly ? "A read-only fixed-size array." :
            "A fixed-size array. Explicit construction requires one positive int size per dimension."
    ),
    m_dimensionCount(dimensionCount)
{}

ArrayType::~ArrayType() {
    releaseRef(m_emptyArray);
}

void ArrayType::initValue(RuntimeValue& value) const {
    if (m_emptyArray == nullptr) {
        m_emptyArray = createEmptyArray();
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
