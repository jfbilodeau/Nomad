// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/DeferredTypeTarget.hpp>

#include <nomad/script/Callable.hpp>

namespace nomad {

class FunctionParameterTypeTarget final : public DeferredTypeTarget {
public:
    FunctionParameterTypeTarget(Runtime& runtime, CallableId id, NomadIndex index, TypeReference type);

    [[nodiscard]] const TypeReference& getDeferredType() const override;
    [[nodiscard]] NomadString getDescription() const override;
    void resolve(const Type* type) override;

private:
    Runtime* m_runtime;
    CallableId m_id;
    NomadIndex m_index;
    TypeReference m_type;
};

} // namespace nomad
