// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/collections/ArrayType.hpp>

#include <array>

namespace nomad {

class Array3dObject;

class Array3Type final : public ArrayType {
public:
    Array3Type(const Type* elementType, const Type* dimensionType);

    [[nodiscard]] std::span<const Type* const> getConstructorParameterTypes() const override;
    [[nodiscard]] NomadObject* createObject(std::span<const RuntimeValue> arguments) const override;
    [[nodiscard]] Array3dObject* createObject(NomadInteger size1, NomadInteger size2, NomadInteger size3) const;

protected:
    [[nodiscard]] ArrayObject* createEmptyArray() const override;

private:
    std::array<const Type*, 3> m_constructorParameterTypes;
};

} // namespace nomad
