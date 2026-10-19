// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/NomadObject.hpp>
#include <nomad/script/RuntimeValue.hpp>

#include <unordered_map>

namespace nomad {

class Type;

class MapObject final : public NomadObject {
public:
    explicit MapObject(const Type* valueType);
    ~MapObject() override;

    [[nodiscard]] const Type* getValueType() const;
    [[nodiscard]] NomadIndex getCount() const;
    [[nodiscard]] bool contains(NomadStringView key) const;
    [[nodiscard]] bool remove(NomadStringView key);
    void clear();

    // The result must be empty; the caller releases it using the value type, including on failure.
    void getValue(NomadStringView key, RuntimeValue& result) const;
    void setValue(NomadStringView key, const RuntimeValue& value);

private:
    const Type* m_valueType;
    std::unordered_map<NomadString, RuntimeValue, NomadStringHash, NomadStringEqual> m_values;
};

} // namespace nomad
