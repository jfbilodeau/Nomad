// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/Array2Type.hpp>

#include <nomad/collections/Array2dObject.hpp>

namespace nomad {

Array2Type::Array2Type(
    Runtime* runtime,
    NomadString name,
    const ObjectTypeMode mode,
    const Type* elementType
):
    ArrayType(runtime, std::move(name), mode, elementType, 2)
{}

Array2dObject* Array2Type::createObject(const NomadInteger size1, const NomadInteger size2) const {
    return new Array2dObject(getElementType(), size1, size2);
}

ArrayObject* Array2Type::createEmptyArray() const {
    return new Array2dObject(getElementType());
}

} // namespace nomad
