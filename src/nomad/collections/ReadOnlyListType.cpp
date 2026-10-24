#include <nomad/collections/ReadOnlyListType.hpp>

#include <nomad/collections/ListObject.hpp>
#include <nomad/collections/ListType.hpp>

namespace nomad {

ReadOnlyListType::ReadOnlyListType(
    Runtime* runtime,
    NomadString name,
    const ObjectTypeMode mode,
    const Type* elementType
):
    ObjectType(runtime, std::move(name), mode, {elementType})
{
    if (elementType == nullptr || elementType->isVoid() || elementType->isInternal()) {
        throw NomadException("List element type must be a public, non-void type");
    }
}

void ReadOnlyListType::initValue(RuntimeValue& value) const {
    value.setObjectValue(new ListObject(getTypeArguments()[0]));
}

} // namespace nomad