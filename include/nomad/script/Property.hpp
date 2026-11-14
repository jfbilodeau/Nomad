// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

namespace nomad {

class NativeProperty;
class ClassType;
class CompositeType;
class Runtime;
class Type;

DeclareId(PropertyId);

class Property {
public:
    Property(
        const CompositeType* ownerType,
        NomadString name,
        const Type* valueType,
        NomadString doc = {}
    );
    Property(const Property&) = delete;
    Property& operator=(const Property&) = delete;
    virtual ~Property();

    [[nodiscard]] PropertyId getId() const;
    [[nodiscard]] const ClassType* getClassType() const;
    [[nodiscard]] const CompositeType* getOwnerType() const;
    [[nodiscard]] const NomadString& getName() const;
    [[nodiscard]] const Type* getValueType() const;
    [[nodiscard]] const NomadString& getDoc() const;
    [[nodiscard]] bool hasExplicitInitializer() const;
    void setHasExplicitInitializer(bool hasExplicitInitializer);
    [[nodiscard]] virtual const NativeProperty* asNativeProperty() const;
    [[nodiscard]] PropertyId registerWithRuntime(Runtime& runtime);

protected:
    void setValueType(const Type* valueType);

private:
    PropertyId m_id;
    const CompositeType* m_ownerType;
    NomadString m_name;
    const Type* m_valueType;
    NomadString m_doc;
    bool m_hasExplicitInitializer = false;
};

} // namespace nomad
