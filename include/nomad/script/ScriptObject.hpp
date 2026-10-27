// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/NomadObject.hpp>
#include <nomad/script/RuntimeValue.hpp>

#include <vector>

namespace nomad {

class ObjectType;
class Type;

class ScriptObject final : public NomadObject {
public:
    explicit ScriptObject(const ObjectType* objectType);
    ~ScriptObject() override;

    [[nodiscard]] const ObjectType* getObjectType() const;
    [[nodiscard]] NomadIndex getFieldCount() const;

    [[nodiscard]] const RuntimeValue& getField(NomadIndex index) const;
    void setField(NomadIndex index, const RuntimeValue& value);

private:
    void validateFieldAccess(NomadIndex index) const;

    const ObjectType* m_objectType;
    std::vector<const Type*> m_fieldTypes;
    std::vector<RuntimeValue> m_values;
};

} // namespace nomad
