// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/Array1Type.hpp>

#include <nomad/collections/Array1dObject.hpp>

namespace nomad {

Array1Type::Array1Type(
    Runtime* runtime,
    TypeId id,
    NomadString name,
    const ObjectTypeMode mode,
    const Type* elementType
):
    ArrayType(runtime, id, std::move(name), mode, elementType, 1)
{}

Array1dObject* Array1Type::createObject(const NomadInteger size) const {
    return new Array1dObject(getElementType(), size);
}

ArrayObject* Array1Type::createEmptyArray() const {
    return new Array1dObject(getElementType());
}

} // namespace nomad
