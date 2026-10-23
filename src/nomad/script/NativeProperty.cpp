// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/NativeProperty.hpp>

#include <nomad/script/Type.hpp>

#include <utility>

namespace nomad {

NativeProperty::NativeProperty(
    const ObjectType* objectType,
    NomadString name,
    const Type* valueType,
    NativePropertyGetFn getFn,
    NativePropertySetFn setFn,
    NomadString doc
):
    Property(objectType, std::move(name), valueType, std::move(doc)),
    m_getFn(std::move(getFn)),
    m_setFn(std::move(setFn))
{}

const NativeProperty* NativeProperty::asNativeProperty() const {
    return this;
}

NativePropertyDefinition NativeProperty::getDefinition() const {
    return {getId(), getObjectType(), getName(), getValueType(), m_getFn, m_setFn, getDoc()};
}

} // namespace nomad
