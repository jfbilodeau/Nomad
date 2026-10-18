// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/Type.hpp>

namespace nomad {

class ArrayObject;

class ArrayType final : public ObjectType {
public:
    ArrayType(const Type* elementType, NomadIndex dimensionCount);
    ArrayType(const ArrayType&) = delete;
    ArrayType& operator=(const ArrayType&) = delete;
    ~ArrayType() override;

    void initValue(RuntimeValue& value) const override;
    [[nodiscard]] const Type* getElementType() const;
    [[nodiscard]] NomadIndex getDimensionCount() const;

private:
    NomadIndex m_dimensionCount;
    mutable ArrayObject* m_emptyArray = nullptr;
};

} // namespace nomad
