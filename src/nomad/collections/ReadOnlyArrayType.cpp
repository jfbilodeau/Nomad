#include <nomad/collections/ReadOnlyArrayType.hpp>

#include <nomad/collections/Array1dObject.hpp>
#include <nomad/collections/Array2dObject.hpp>
#include <nomad/collections/Array3dObject.hpp>

namespace nomad {

ReadOnlyArrayType::ReadOnlyArrayType(
    Runtime* runtime,
    TypeId id,
    NomadString name,
    const ClassMode mode,
    const Type* elementType,
    const NomadIndex dimensionCount
):
    ArrayType(runtime, id, std::move(name), mode, elementType, dimensionCount, true)
{}

ArrayObject* ReadOnlyArrayType::createEmptyArray() const {
    switch (getDimensionCount()) {
    case 1: return new Array1dObject(getElementType());
    case 2: return new Array2dObject(getElementType());
    case 3: return new Array3dObject(getElementType());
    default: throw NomadException("Invalid read-only array rank");
    }
}

} // namespace nomad