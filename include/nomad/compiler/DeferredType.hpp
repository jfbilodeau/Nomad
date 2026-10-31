// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/modules/TypeReference.hpp>

#include <nomad/script/Type.hpp>

namespace nomad {

class DeferredType final : public Type {
public:
    DeferredType(Runtime* runtime, TypeReference reference);

    [[nodiscard]] const TypeReference& getReference() const;
    [[nodiscard]] NomadString getTypeName() const override;
    [[nodiscard]] TypeStorage getStorage() const override;
    [[nodiscard]] NomadIndex getSize() const override;
    void initValue(RuntimeValue* value) const override;
    void freeValue(RuntimeValue* value) const override;
    void copyValue(const RuntimeValue* source, RuntimeValue* destination) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;

private:
    [[noreturn]] void reportUnresolved() const;

    TypeReference m_reference;
};

} // namespace nomad
