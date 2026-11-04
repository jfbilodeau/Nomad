// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/Identifier.hpp>

#include <nomad/script/OpCode.hpp>

#include <vector>

namespace nomad {

class Compiler;
class Expression;
class Property;
class ScriptCallableBase;

class StorageLocation {
public:
    explicit StorageLocation(IdentifierDefinition root);
    StorageLocation(Expression* value, VariableId variable);
    explicit StorageLocation(const Property* property);
    StorageLocation(Expression* owner, const Property* property);

    [[nodiscard]] const Type* getType() const;
    [[nodiscard]] StorageLocation getProperty(const Property& property) const;
    void validate(const ScriptCallableBase* function) const;
    void emitRetainOwner(Compiler& compiler, ScriptCallableBase* function) const;
    void emitFreeOwner(Compiler& compiler, const ScriptCallableBase* function) const;
    void emitAssignment(
        Compiler& compiler,
        ScriptCallableBase* function,
        Expression& expression,
        VariableId stagingVariable
    ) const;
    void emitAddress(
        Compiler& compiler,
        const ScriptCallableBase* function,
        NomadIndex ownerOffset = 0
    ) const;
    void emitLoad(
        Compiler& compiler,
        ScriptCallableBase* function,
        ExpressionTarget target
    ) const;
    void emitStoreResult(Compiler& compiler, const ScriptCallableBase* function) const;
    void emitCopyToLocal(
        Compiler& compiler,
        ScriptCallableBase* function,
        VariableId destination
    ) const;
    void emitCopyFromLocal(
        Compiler& compiler,
        const ScriptCallableBase* function,
        VariableId source
    ) const;

private:
    [[nodiscard]] NomadIndex getPropertyOffset() const;
    [[nodiscard]] NomadIndex getRootOffset(const ScriptCallableBase* function) const;
    [[nodiscard]] NomadIndex getOffset(const ScriptCallableBase* function) const;

    IdentifierDefinition m_root;
    const Type* m_type;
    std::vector<const Property*> m_properties;
    Expression* m_owner = nullptr;
    Expression* m_temporary = nullptr;
    const Property* m_objectProperty = nullptr;
};

} // namespace nomad
