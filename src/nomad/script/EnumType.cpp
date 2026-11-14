// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/EnumType.hpp>

#include <algorithm>
#include <utility>

namespace nomad {

EnumType::EnumType(
    Runtime* runtime,
    TypeId id,
    NomadString name,
    std::vector<EnumValueDefinition> values,
    NomadString documentation
):
    ValueType(runtime, id, std::move(documentation)),
    m_name(std::move(name)),
    m_values(std::move(values))
{
}

void EnumType::initValue(RuntimeValue* value) const {
    value->setIntegerValue(0);
}

NomadString EnumType::getTypeName() const {
    return m_name;
}

void EnumType::copyValue(const RuntimeValue* sourceValue, RuntimeValue* destinationValue) const {
    destinationValue->setIntegerValue(sourceValue->getIntegerValue());
}

void EnumType::toString(const RuntimeValue* value, NomadString& string) const {
    if (!getNameByValue(value->getIntegerValue(), string)) {
        string = "<invalid " + m_name + " value>";
    }
}

bool EnumType::sameType(const Type* other) const {
    return other == this;
}

const EnumType* EnumType::asEnum() const {
    return this;
}

NomadIndex EnumType::getValueCount() const {
    return m_values.size();
}

const EnumValueDefinition& EnumType::getValue(const NomadIndex index) const {
    return m_values.at(index);
}

bool EnumType::getValueByName(const NomadString& name, NomadInteger& value) const {
    const auto definition = std::ranges::find(m_values, name, &EnumValueDefinition::name);

    if (definition == m_values.end()) {
        return false;
    }

    value = definition->value;
    return true;
}

bool EnumType::getNameByValue(const NomadInteger value, NomadString& name) const {
    const auto definition = std::ranges::find(m_values, value, &EnumValueDefinition::value);

    if (definition == m_values.end()) {
        return false;
    }

    name = definition->name;
    return true;
}

} // namespace nomad
