// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/InterpreterContext.hpp>
#include <nomad/script/NomadObject.hpp>

#include <vector>

namespace nomad {

class ListObject final : public NomadObject {
public:
    explicit ListObject(const Type* elementType);
    ~ListObject() override;

    [[nodiscard]] const Type* getElementType() const;
    [[nodiscard]] NomadIndex getCount() const;

    void push(const RuntimeValue& value);

private:
    const Type* m_elementType;
    std::vector<TypedValue> m_values;
};

} // namespace nomad
