#include <nomad/collections/ReadOnlyMapType.hpp>

#include <nomad/collections/MapObject.hpp>
#include <nomad/collections/MapType.hpp>

namespace nomad {

ReadOnlyMapType::ReadOnlyMapType(
    Runtime* runtime,
    NomadString name,
    const ObjectTypeMode mode,
    const Type* keyType,
    const Type* valueType
):
    ObjectType(runtime, std::move(name), mode, {keyType, valueType})
{
    if (keyType == nullptr || !keyType->isString() || keyType->isInternal()) {
        throw NomadException("Map key type must be string");
    }
    if (valueType == nullptr || valueType->isVoid() || valueType->isInternal()) {
        throw NomadException("Map value type must be a public, non-void type");
    }
}

void ReadOnlyMapType::initValue(RuntimeValue& value) const {
    value.setObjectValue(new MapObject(getTypeArguments()[1]));
}

} // namespace nomad