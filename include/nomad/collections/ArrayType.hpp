// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/Type.hpp>

namespace nomad {

class ArrayObject;

class ArrayType final : public ObjectType {
public:
    ArrayType(const Type* elementType, NomadIndex dimensionCount, const Type* dimensionType);
    ArrayType(const ArrayType&) = delete;
    ArrayType& operator=(const ArrayType&) = delete;
    ~ArrayType() override;

    void initValue(RuntimeValue& value) const override;
    [[nodiscard]] bool isConstructible() const override;
    [[nodiscard]] std::span<const Type* const> getConstructorParameterTypes() const override;
    [[nodiscard]] NomadObject* createObject(std::span<const RuntimeValue> arguments) const override;
    [[nodiscard]] const Type* getElementType() const;
    [[nodiscard]] NomadIndex getDimensionCount() const;

private:
    NomadIndex m_dimensionCount;
    std::vector<const Type*> m_constructorParameterTypes;
    mutable ArrayObject* m_emptyArray = nullptr;
};

} // namespace nomad
