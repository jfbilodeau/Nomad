// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/Type.hpp>

namespace nomad {

class ScriptObject;
class VirtualMachine;

struct BindingField {
    NomadString name;
    NomadString type;
};

struct BindingEnum {
    NomadString name;
    std::vector<EnumValueDefinition> values;
};

struct BindingClass {
    NomadString name;
    std::vector<BindingField> fields;
    std::vector<BindingEnum> enums;
};

// Retains the receiver, not its Runtime. The runtime and contract must outlive this handle.
class ScriptBindingObject final {
public:
    ScriptBindingObject(Runtime& runtime, NomadObject* object, const BindingClass& contract);
    ScriptBindingObject(const ScriptBindingObject& other);
    ScriptBindingObject(ScriptBindingObject&& other) noexcept;
    ScriptBindingObject& operator=(const ScriptBindingObject& other);
    ScriptBindingObject& operator=(ScriptBindingObject&& other) noexcept;
    ~ScriptBindingObject();

    [[nodiscard]] static ScriptBindingObject construct(
        Runtime& runtime,
        VirtualMachine& machine,
        const BindingClass& contract
    );
    [[nodiscard]] NomadObject* getObject() const;
    [[nodiscard]] Runtime& getRuntime() const;
    [[nodiscard]] NomadBoolean getBoolean(const NomadString& name) const;
    [[nodiscard]] NomadInteger getInteger(const NomadString& name) const;
    [[nodiscard]] NomadFloat getFloat(const NomadString& name) const;
    [[nodiscard]] NomadString getString(const NomadString& name) const;
    void setBoolean(const NomadString& name, NomadBoolean value);
    void setInteger(const NomadString& name, NomadInteger value);
    void setFloat(const NomadString& name, NomadFloat value);
    void setString(const NomadString& name, const NomadString& value);

private:
    [[nodiscard]] const Property* getField(const NomadString& name, const NomadString& type) const;
    void validateInteger(const Property* field, NomadInteger value) const;

    Runtime* m_runtime;
    ScriptObject* m_object;
    const BindingClass* m_contract;
};

} // namespace nomad
