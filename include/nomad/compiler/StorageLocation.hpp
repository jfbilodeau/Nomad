// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/Identifier.hpp>

#include <nomad/script/OpCode.hpp>

namespace nomad {

class Compiler;
class Property;
class ScriptCallableBase;

class StorageLocation {
public:
    explicit StorageLocation(IdentifierDefinition root);

    [[nodiscard]] const Type* getType() const;
    [[nodiscard]] StorageLocation getProperty(const Property& property) const;
    void validate(const ScriptCallableBase* function) const;
    void emitAddress(Compiler& compiler, const ScriptCallableBase* function) const;
    void emitLoad(
        Compiler& compiler,
        const ScriptCallableBase* function,
        ExpressionTarget target
    ) const;
    void emitStoreResult(Compiler& compiler, const ScriptCallableBase* function) const;
    void emitCopyToLocal(
        Compiler& compiler,
        const ScriptCallableBase* function,
        VariableId destination
    ) const;
    void emitCopyFromLocal(
        Compiler& compiler,
        const ScriptCallableBase* function,
        VariableId source
    ) const;

private:
    [[nodiscard]] NomadIndex getRootOffset(const ScriptCallableBase* function) const;
    [[nodiscard]] NomadIndex getOffset(const ScriptCallableBase* function) const;

    IdentifierDefinition m_root;
    const Type* m_type;
    NomadIndex m_propertyOffset = 0;
};

} // namespace nomad
