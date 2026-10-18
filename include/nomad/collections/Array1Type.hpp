// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/collections/ArrayType.hpp>

#include <array>

namespace nomad {

class Array1dObject;

class Array1Type final : public ArrayType {
public:
    Array1Type(const Type* elementType, const Type* dimensionType);

    [[nodiscard]] std::span<const Type* const> getConstructorParameterTypes() const override;
    [[nodiscard]] NomadObject* createObject(std::span<const RuntimeValue> arguments) const override;
    [[nodiscard]] Array1dObject* createObject(NomadInteger size) const;

protected:
    [[nodiscard]] ArrayObject* createEmptyArray() const override;

private:
    std::array<const Type*, 1> m_constructorParameterTypes;
};

} // namespace nomad
