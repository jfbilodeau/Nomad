// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/NomadObject.hpp>
#include <nomad/script/RuntimeValue.hpp>

#include <vector>

namespace nomad {

class Type;

class ListObject final : public NomadObject {
public:
    explicit ListObject(const Type* elementType);
    ~ListObject() override;

    [[nodiscard]] const Type* getElementType() const;
    [[nodiscard]] NomadIndex getCount() const;

    void push(const RuntimeValue& value);
    [[nodiscard]] const RuntimeValue& getValue(NomadInteger index) const;
    void setValue(NomadInteger index, const RuntimeValue& value);

private:
    [[nodiscard]] NomadIndex validateIndex(NomadInteger index) const;
    const Type* m_elementType;
    std::vector<RuntimeValue> m_values;
};

} // namespace nomad
