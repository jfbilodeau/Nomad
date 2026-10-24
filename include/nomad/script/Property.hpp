// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

namespace nomad {

class NativeProperty;
class ObjectType;
class Runtime;
class Type;

DeclareId(PropertyId);

class Property {
public:
    Property(
        const ObjectType* objectType,
        NomadString name,
        const Type* valueType,
        NomadString doc = {}
    );
    Property(const Property&) = delete;
    Property& operator=(const Property&) = delete;
    virtual ~Property();

    [[nodiscard]] PropertyId getId() const;
    [[nodiscard]] const ObjectType* getObjectType() const;
    [[nodiscard]] const NomadString& getName() const;
    [[nodiscard]] const Type* getValueType() const;
    [[nodiscard]] const NomadString& getDoc() const;
    [[nodiscard]] virtual const NativeProperty* asNativeProperty() const;
    [[nodiscard]] PropertyId registerWithRuntime(Runtime& runtime);

private:
    PropertyId m_id;
    const ObjectType* m_objectType;
    NomadString m_name;
    const Type* m_valueType;
    NomadString m_doc;
};

} // namespace nomad
