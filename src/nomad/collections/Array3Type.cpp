// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/Array3Type.hpp>

#include <nomad/collections/Array3dObject.hpp>

namespace nomad {

Array3Type::Array3Type(
    Runtime* runtime,
    TypeId id,
    NomadString name,
    const ClassMode mode,
    const Type* elementType
):
    ArrayType(runtime, id, std::move(name), mode, elementType, 3)
{}

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
