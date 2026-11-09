// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/ValueType.hpp>

#include <vector>

namespace nomad {

struct EnumValueDefinition {
    NomadString name;
    NomadInteger value;
};

class EnumType final : public ValueType {
public:
    void initValue(RuntimeValue* value) const override;
    EnumType(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        std::vector<EnumValueDefinition> values,
        NomadString documentation = {}
    );

    using Type::toString;

    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue* sourceValue, RuntimeValue* destinationValue) const override;
    void toString(const RuntimeValue* value, NomadString& string) const override;
    [[nodiscard]] bool sameType(const Type* other) const override;
    [[nodiscard]] const EnumType* asEnum() const override;

    [[nodiscard]] NomadIndex getValueCount() const;
    [[nodiscard]] const EnumValueDefinition& getValue(NomadIndex index) const;
    [[nodiscard]] bool getValueByName(const NomadString& name, NomadInteger& value) const;
    [[nodiscard]] bool getNameByValue(NomadInteger value, NomadString& name) const;

private:
    NomadString m_name;
    std::vector<EnumValueDefinition> m_values;
};

} // namespace nomad
