// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/NomadObject.hpp>
#include <nomad/script/RuntimeValue.hpp>

#include <span>
#include <vector>

namespace nomad {

class Type;

class ArrayObject : public NomadObject {
public:
    ~ArrayObject() override;

    [[nodiscard]] const Type* getElementType() const;
    [[nodiscard]] NomadIndex getCount() const;
    [[nodiscard]] std::unique_ptr<ArrayObject> createCopy() const;
    [[nodiscard]] virtual NomadIndex getDimensionCount() const = 0;
    [[nodiscard]] virtual NomadIndex getDimensionSize(NomadIndex dimension) const = 0;

protected:
    explicit ArrayObject(const Type* elementType);
    ArrayObject(const Type* elementType, std::span<const NomadInteger> dimensions);

    static void reportIndexOutOfBounds(NomadInteger index, NomadIndex dimension, NomadIndex size);

    const Type* m_elementType;
    std::vector<RuntimeValue> m_values;

private:
    void freeElements() noexcept;
};

} // namespace nomad
