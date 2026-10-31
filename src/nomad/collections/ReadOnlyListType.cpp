#include <nomad/collections/ReadOnlyListType.hpp>

#include <nomad/collections/ListObject.hpp>
#include <nomad/collections/ListType.hpp>

namespace nomad {

ReadOnlyListType::ReadOnlyListType(
    Runtime* runtime,
    TypeId id,
    NomadString name,
    const ClassMode mode,
    const Type* elementType
):
    ClassType(runtime, id, std::move(name), mode, {elementType})
{
    if (elementType == nullptr || elementType->isVoid() || elementType->isInternal()) {
        throw NomadException("List element type must be a public, non-void type");
    }
}

void ReadOnlyListType::initValue(RuntimeValue* value) const {
    value->setObjectValue(new ListObject(getTypeArguments()[0]));
}

} // namespace nomad