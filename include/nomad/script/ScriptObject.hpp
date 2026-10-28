// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/NomadObject.hpp>
#include <nomad/script/RuntimeValue.hpp>

#include <vector>

namespace nomad {

class ClassType;
class Type;

class ScriptObject final : public NomadObject {
public:
    explicit ScriptObject(const ClassType* classType);
    ~ScriptObject() override;

    [[nodiscard]] const ClassType* getClassType() const;
    [[nodiscard]] NomadIndex getFieldCount() const;

    [[nodiscard]] const RuntimeValue& getField(NomadIndex index) const;
    void setField(NomadIndex index, const RuntimeValue& value);

private:
    void validateFieldAccess(NomadIndex index) const;

    const ClassType* m_classType;
    std::vector<const Type*> m_fieldTypes;
    std::vector<RuntimeValue> m_values;
};

} // namespace nomad
