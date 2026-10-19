// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/MapType.hpp>

#include <nomad/collections/MapObject.hpp>

namespace nomad {

namespace {

NomadString getMapTypeName(const Type* keyType, const Type* valueType) {
    if (keyType == nullptr || !keyType->isString() || keyType->isInternal()) {
        throw NomadException("Map key type must be string");
    }
    if (valueType == nullptr || valueType->isVoid() || valueType->isInternal()) {
        throw NomadException("Map value type must be a public, non-void type");
    }
    return "Map<" + keyType->getTypeName() + " " + valueType->getTypeName() + ">";
}

} // namespace

MapType::MapType(const Type* keyType, const Type* valueType):
    ObjectType(getMapTypeName(keyType, valueType), {keyType, valueType})
{}

void MapType::initValue(RuntimeValue& value) const {
    value.setObjectValue(new MapObject(getTypeArguments()[1]));
}

} // namespace nomad
