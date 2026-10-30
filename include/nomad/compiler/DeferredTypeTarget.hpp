// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/modules/TypeReference.hpp>

namespace nomad {

class Type;

class DeferredTypeTarget {
public:
    virtual ~DeferredTypeTarget() = default;

    [[nodiscard]] virtual const TypeReference& getDeferredType() const = 0;
    [[nodiscard]] virtual NomadString getDescription() const = 0;
    virtual void resolve(const Type* type) = 0;
};

} // namespace nomad
