#include <nomad/collections/ReadOnlyMapType.hpp>

#include <nomad/collections/MapObject.hpp>
#include <nomad/collections/MapType.hpp>

namespace nomad {

ReadOnlyMapType::ReadOnlyMapType(const Type* keyType, const Type* valueType):
    ObjectType("ReadOnly" + MapType(keyType, valueType).getTypeName(), {keyType, valueType})
{}

void ReadOnlyMapType::initValue(RuntimeValue& value) const {
    value.setObjectValue(new MapObject(getTypeArguments()[1]));
}

} // namespace nomad