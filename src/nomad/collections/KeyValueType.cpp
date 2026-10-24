// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/KeyValueType.hpp>

#include <nomad/collections/KeyValueObject.hpp>

#include <nomad/script/Documentation.hpp>

namespace nomad {

namespace {

NomadString getKeyValueTypeName(const Type* keyType, const Type* valueType) {
    if (keyType == nullptr || keyType->isVoid() || keyType->isInternal() ||
        valueType == nullptr || valueType->isVoid() || valueType->isInternal()) {
        throw NomadException("KeyValue types must be public, non-void types");
    }
    return "KeyValue<" + keyType->getTypeName() + " " + valueType->getTypeName() + ">";
}

} // namespace

KeyValueType::KeyValueType(const Type* keyType, const Type* valueType):
    ObjectType(
        getKeyValueTypeName(keyType, valueType),
        ObjectTypeMode::Closed,
        {keyType, valueType},
        NomadDoc("A pair with read-only key and value properties. Object fields share their referenced objects.")
    )
{}

void KeyValueType::initValue(RuntimeValue& value) const {
    value.setObjectValue(new KeyValueObject(getTypeArguments()[0], getTypeArguments()[1]));
}

} // namespace nomad
