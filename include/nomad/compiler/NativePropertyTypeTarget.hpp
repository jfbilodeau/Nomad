// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/DeferredTypeTarget.hpp>

#include <nomad/script/Property.hpp>

namespace nomad {

class NativePropertyTypeTarget final : public DeferredTypeTarget {
public:
    NativePropertyTypeTarget(Runtime& runtime, PropertyId id, TypeReference type);

    [[nodiscard]] const TypeReference& getDeferredType() const override;
    [[nodiscard]] NomadString getDescription() const override;
    void resolve(const Type* type) override;

private:
    Runtime* m_runtime;
    PropertyId m_id;
    TypeReference m_type;
};

} // namespace nomad
