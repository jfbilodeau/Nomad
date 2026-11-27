// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/KeyValueType.hpp>

#include <nomad/collections/KeyValueObject.hpp>

#include <nomad/script/Documentation.hpp>

namespace nomad {

KeyValueType::KeyValueType(
    Runtime* runtime,
    TypeId id,
    NomadString name,
    const ClassMode mode,
    const Type* keyType,
    const Type* valueType
):
    ClassType(
        runtime, id, std::move(name), mode,
        {keyType, valueType},
        NomadDoc("A pair with read-only key and value properties. Object fields share their referenced objects.")
    )
{
    if (keyType == nullptr || keyType->isVoid() || keyType->isInternal() ||
        valueType == nullptr || valueType->isVoid() || valueType->isInternal()) {
        throw NomadException("KeyValue types must be public, non-void types");
    }
}

void KeyValueType::initValue(RuntimeValue* value) const {
    value->setObjectValue(new KeyValueObject(getTypeArguments()[0], getTypeArguments()[1]));
}

} // namespace nomad
