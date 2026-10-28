// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/NativeProperty.hpp>

#include <nomad/script/Type.hpp>

#include <utility>

namespace nomad {

NativeProperty::NativeProperty(
    const ClassType* classType,
    NomadString name,
    const Type* valueType,
    NativePropertyGetFn getFn,
    NativePropertySetFn setFn,
    NomadString doc
):
    Property(classType, std::move(name), valueType, std::move(doc)),
    m_getFn(std::move(getFn)),
    m_setFn(std::move(setFn))
{}

const NativeProperty* NativeProperty::asNativeProperty() const {
    return this;
}

NativePropertyInfo NativeProperty::getInfo() const {
    return {getId(), getClassType(), getName(), getValueType(), m_getFn, m_setFn, getDoc()};
}

} // namespace nomad
