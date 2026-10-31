// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/NomadObject.hpp>
#include <nomad/script/RuntimeValue.hpp>

#include <span>
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
    [[nodiscard]] std::span<RuntimeValue> getFieldStorage(NomadIndex index);
    [[nodiscard]] std::span<const RuntimeValue> getFieldStorage(NomadIndex index) const;
    void setFieldStorage(
        NomadIndex index,
        std::span<const RuntimeValue> value,
        std::span<RuntimeValue> scratch = {}
    );

private:
    struct StoredField {
        const Type* type;
        NomadIndex offset;
        NomadIndex size;
    };

    void validateFieldAccess(NomadIndex index) const;
    void validateSingleSlotFieldAccess(NomadIndex index) const;

    const ClassType* m_classType;
    std::vector<StoredField> m_fields;
    std::vector<RuntimeValue> m_values;
};

} // namespace nomad
