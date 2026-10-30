// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/modules/ScriptBindings.hpp>

namespace nomad {

class ScriptObject;
class VirtualMachine;

// Retains the receiver, not its Runtime or ScriptBindings. Both owners must
// outlive this handle and all generated wrappers derived from it.
class ScriptBindingObject {
public:
    ScriptBindingObject(ScriptBindings& bindings, NomadObject* object, const BindingClass& contract);
    ScriptBindingObject(const ScriptClassBinding& binding, NomadObject* object);
    ScriptBindingObject(const ScriptBindingObject& other);
    ScriptBindingObject(ScriptBindingObject&& other) noexcept;
    ScriptBindingObject& operator=(const ScriptBindingObject& other);
    ScriptBindingObject& operator=(ScriptBindingObject&& other) noexcept;
    virtual ~ScriptBindingObject();

    [[nodiscard]] static ScriptBindingObject construct(
        const ScriptClassBinding& binding,
        VirtualMachine& machine
    );
    [[nodiscard]] static ScriptBindingObject construct(
        ScriptBindings& bindings,
        VirtualMachine& machine,
        const BindingClass& contract
    );
    [[nodiscard]] NomadObject* getObject() const;
    [[nodiscard]] Runtime& getRuntime() const;
    [[nodiscard]] const ScriptClassBinding& getBinding() const;
    [[nodiscard]] NomadBoolean getBoolean(NomadIndex slot) const;
    [[nodiscard]] NomadInteger getInteger(NomadIndex slot) const;
    [[nodiscard]] NomadFloat getFloat(NomadIndex slot) const;
    [[nodiscard]] NomadString getString(NomadIndex slot) const;
    void setBoolean(NomadIndex slot, NomadBoolean value);
    void setInteger(NomadIndex slot, NomadInteger value);
    void setFloat(NomadIndex slot, NomadFloat value);
    void setString(NomadIndex slot, const NomadString& value);

private:
    [[nodiscard]] NomadIndex getFieldIndex(NomadIndex slot, BindingValueKind kind) const;

    ScriptObject* m_object;
    const ScriptClassBinding* m_binding;
};

} // namespace nomad
