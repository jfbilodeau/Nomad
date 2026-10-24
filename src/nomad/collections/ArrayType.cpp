// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/ArrayType.hpp>

#include <nomad/collections/ArrayObject.hpp>

namespace nomad {

ArrayType::ArrayType(
    Runtime* runtime,
    NomadString name,
    const ObjectTypeMode mode,
    const Type* elementType,
    const NomadIndex dimensionCount,
    const bool readOnly
):
    ObjectType(
        runtime, std::move(name), mode,
        {elementType},
        readOnly ? "A read-only fixed-size array." :
            "A fixed-size array. Explicit construction requires one positive int size per dimension."
    ),
    m_dimensionCount(dimensionCount)
{
    if (elementType == nullptr || elementType->isVoid() || elementType->isInternal()) {
        throw NomadException("Array element type must be a public, non-void type");
    }
    if (dimensionCount < 1 || dimensionCount > 3) {
        throw NomadException("Array dimension count must be between 1 and 3");
    }
}

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
