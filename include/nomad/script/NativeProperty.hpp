// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/NativeMember.hpp>
#include <nomad/script/Property.hpp>

namespace nomad {

class NativeProperty final : public Property {
public:
    NativeProperty(
        PropertyId id,
        const ClassType* classType,
        NomadString name,
        const Type* valueType,
        NativePropertyGetFn getFn,
        NativePropertySetFn setFn,
        NomadString doc,
        CallableId getterMethodId = CallableId::INVALID,
        CallableId setterMethodId = CallableId::INVALID
    );

    [[nodiscard]] const NativeProperty* asNativeProperty() const override;
    [[nodiscard]] NativePropertyInfo getInfo() const;
    [[nodiscard]] CallableId getGetterMethodId() const noexcept;
    [[nodiscard]] CallableId getSetterMethodId() const noexcept;
    [[nodiscard]] bool hasGetterCallback() const noexcept;
    [[nodiscard]] bool hasSetterCallback() const noexcept;
    // Requires the raw callback; caller handles receiver binding and value ownership.
    // Incomplete value types throw before invocation.
    void getValue(VirtualMachine* machine, RuntimeValue& result) const;
    void setValue(VirtualMachine* machine, const RuntimeValue& value) const;
    [[nodiscard]] bool isTypeResolved() const;
    void resolveValueType(const Type* type);
    void completeTypeResolution();

private:
    [[nodiscard]] bool isRegisteredType(const Type* type) const;
    void requireResolvedType() const;

    NativePropertyGetFn m_getFn;
    NativePropertySetFn m_setFn;
    CallableId m_getterMethodId;
    CallableId m_setterMethodId;
    bool m_typeResolved;
};

} // namespace nomad
