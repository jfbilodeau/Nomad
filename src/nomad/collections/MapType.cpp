// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/MapType.hpp>

#include <nomad/collections/MapObject.hpp>

namespace nomad {

MapType::MapType(
    Runtime* runtime,
    TypeId id,
    NomadString name,
    const ClassMode mode,
    const Type* keyType,
    const Type* valueType
):
    ClassType(runtime, id, std::move(name), mode, {keyType, valueType})
{
    if (keyType == nullptr || !keyType->isString() || keyType->isInternal()) {
        throw NomadException("Map key type must be string");
    }
    if (valueType == nullptr || valueType->isVoid() || valueType->isInternal()) {
        throw NomadException("Map value type must be a public, non-void type");
    }
}

void MapType::initValue(RuntimeValue* value) const {
    value->setObjectValue(new MapObject(getTypeArguments()[1]));
}

} // namespace nomad
