// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/NativeMember.hpp>
#include <nomad/script/Property.hpp>

namespace nomad {

class NativeProperty final : public Property {
public:
    NativeProperty(
        const ClassType* classType,
        NomadString name,
        const Type* valueType,
        NativePropertyGetFn getFn,
        NativePropertySetFn setFn,
        NomadString doc
    );

    [[nodiscard]] const NativeProperty* asNativeProperty() const override;
    [[nodiscard]] NativePropertyInfo getInfo() const;
    [[nodiscard]] bool isTypeResolved() const;
    void resolveValueType(const Type* type);
    void completeTypeResolution();

private:
    [[nodiscard]] bool isRegisteredType(const Type* type) const;
    void requireResolvedType() const;

    NativePropertyGetFn m_getFn;
    NativePropertySetFn m_setFn;
    bool m_typeResolved;
};

} // namespace nomad
