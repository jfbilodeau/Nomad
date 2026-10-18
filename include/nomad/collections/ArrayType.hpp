// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/Type.hpp>

namespace nomad {

class ArrayObject;

class ArrayType : public ObjectType {
public:
    ArrayType(const ArrayType&) = delete;
    ArrayType& operator=(const ArrayType&) = delete;
    ~ArrayType() override;

    void initValue(RuntimeValue& value) const override;
    [[nodiscard]] bool isConstructible() const override;
    [[nodiscard]] std::span<const Type* const> getConstructorParameterTypes() const override = 0;
    [[nodiscard]] NomadObject* createObject(std::span<const RuntimeValue> arguments) const override = 0;
    [[nodiscard]] const Type* getElementType() const;
    [[nodiscard]] NomadIndex getDimensionCount() const;

protected:
    ArrayType(const Type* elementType, NomadIndex dimensionCount, const Type* dimensionType);
    [[nodiscard]] virtual ArrayObject* createEmptyArray() const = 0;

private:
    NomadIndex m_dimensionCount;
    mutable ArrayObject* m_emptyArray = nullptr;
};

} // namespace nomad
