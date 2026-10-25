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

    void initializeFields();
    [[nodiscard]] const RuntimeValue& getField(NomadIndex index) const;
    void setField(NomadIndex index, const RuntimeValue& value);

private:
    enum class InitializationState {
        Allocated = 1,
        Initializing,
        Initialized
    };

    void validateFieldAccess(NomadIndex index) const;

    const ObjectType* m_objectType;
    std::vector<const Type*> m_fieldTypes;
    std::vector<RuntimeValue> m_values;
    NomadIndex m_initializedFieldCount = 0;
    InitializationState m_initializationState = InitializationState::Allocated;
};

} // namespace nomad
