// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/ListType.hpp>

#include <nomad/collections/ListObject.hpp>

namespace nomad {

namespace {

NomadString getListTypeName(const Type* elementType) {
    if (elementType == nullptr || elementType->isVoid() || elementType->isInternal()) {
        throw NomadException("List element type must be a public, non-void type");
    }
    return "List<" + elementType->getTypeName() + ">";
}

} // namespace

ListType::ListType(const Type* elementType):
    ObjectType(
        getListTypeName(elementType),
        {elementType}
    )
{}

void ListType::initValue(RuntimeValue& value) const {
    value.setObjectValue(new ListObject(getTypeArguments()[0]));
}

} // namespace nomad
