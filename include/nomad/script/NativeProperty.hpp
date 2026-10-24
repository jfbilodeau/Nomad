// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/NativeMember.hpp>
#include <nomad/script/Property.hpp>

namespace nomad {

class NativeProperty final : public Property {
public:
    NativeProperty(
        const ObjectType* objectType,
        NomadString name,
        const Type* valueType,
        NativePropertyGetFn getFn,
        NativePropertySetFn setFn,
        NomadString doc
    );

    [[nodiscard]] const NativeProperty* asNativeProperty() const override;
    [[nodiscard]] NativePropertyInfo getInfo() const;

private:
    NativePropertyGetFn m_getFn;
    NativePropertySetFn m_setFn;
};

} // namespace nomad
