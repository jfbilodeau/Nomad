// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/collections/ArrayType.hpp>

#include <array>

namespace nomad {

class Array2dObject;

class Array2Type final : public ArrayType {
public:
    Array2Type(const Type* elementType, const Type* dimensionType);

    [[nodiscard]] std::span<const Type* const> getConstructorParameterTypes() const override;
    [[nodiscard]] NomadObject* createObject(std::span<const RuntimeValue> arguments) const override;
    [[nodiscard]] Array2dObject* createObject(NomadInteger size1, NomadInteger size2) const;

protected:
    [[nodiscard]] ArrayObject* createEmptyArray() const override;

private:
    std::array<const Type*, 2> m_constructorParameterTypes;
};

} // namespace nomad
