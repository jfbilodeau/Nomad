// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/Type.hpp>

namespace nomad {

class ValueType : public Type {
public:
    explicit ValueType(Runtime* runtime, TypeId id, NomadString documentation = {});
    [[nodiscard]] TypeStorage getStorage() const override;
    [[nodiscard]] NomadIndex getSize() const override;
    void emitFreeParameter(OpCodeEmitter& emitter, NomadId parameterId) const override;
};

} // namespace nomad
