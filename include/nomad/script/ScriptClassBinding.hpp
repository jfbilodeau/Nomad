// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/Type.hpp>

namespace nomad {

class VirtualMachine;

struct BindingField {
    NomadString name;
    NomadString type;

    [[nodiscard]] bool operator==(const BindingField&) const = default;
};

struct BindingEnum {
    NomadString name;
    std::vector<EnumValueDefinition> values;

    [[nodiscard]] bool operator==(const BindingEnum& other) const;
};

struct BindingClass {
    NomadString name;
    std::vector<BindingField> fields;
    std::vector<BindingEnum> enums;

    [[nodiscard]] bool operator==(const BindingClass&) const = default;
};

enum class BindingValueKind {
    Boolean = 1,
    Integer,
    Float,
    String,
};

[[nodiscard]] bool isScriptBindingClass(const ClassType& type);

// Runtime-owned, immutable resolution of one contract. Slots follow contract field order,
// independently of the runtime's property IDs or the script's field order.
class ScriptClassBinding final {
public:
    ScriptClassBinding(const ScriptClassBinding&) = delete;
    ScriptClassBinding& operator=(const ScriptClassBinding&) = delete;

    [[nodiscard]] Runtime& getRuntime() const;
    [[nodiscard]] const BindingClass& getContract() const;
    [[nodiscard]] const ClassType* getClassType() const;
    [[nodiscard]] PropertyId getPropertyId(NomadIndex slot) const;
    [[nodiscard]] NomadIndex getFieldIndex(NomadIndex slot, BindingValueKind kind) const;
    void validateInteger(NomadIndex slot, NomadInteger value) const;
    [[nodiscard]] NomadObject* construct(VirtualMachine& machine) const;

private:
    friend class Runtime;
    ScriptClassBinding(Runtime& runtime, const BindingClass& contract);

    struct ResolvedField {
        PropertyId propertyId;
        NomadIndex index;
        BindingValueKind kind;
        std::vector<NomadInteger> enumValues;
    };

    [[nodiscard]] const ResolvedField& getField(NomadIndex slot) const;

    Runtime* m_runtime;
    BindingClass m_contract;
    const ClassType* m_classType;
    CallableId m_constructorId;
    std::vector<ResolvedField> m_fields;
};

} // namespace nomad
