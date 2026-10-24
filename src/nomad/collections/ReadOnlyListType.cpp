#include <nomad/collections/ReadOnlyListType.hpp>

#include <nomad/collections/ListObject.hpp>
#include <nomad/collections/ListType.hpp>

namespace nomad {

ReadOnlyListType::ReadOnlyListType(const Type* elementType):
    ObjectType("ReadOnly" + ListType(elementType).getTypeName(), ObjectTypeMode::Closed, {elementType})
{}

void ReadOnlyListType::initValue(RuntimeValue& value) const {
    value.setObjectValue(new ListObject(getTypeArguments()[0]));
}

} // namespace nomad