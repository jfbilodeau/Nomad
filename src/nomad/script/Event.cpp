// Copyright (c) 2025-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Event.hpp>

namespace nomad {

EventType::EventType(Runtime* runtime, TypeId id, const EventDefinition* declaration):
    ValueType(runtime, id),
    m_event(*declaration)
{}

void EventType::initValue(RuntimeValue& value) const {
    value.setIdValue(0);
}

NomadString EventType::getTypeName() const {
    return EVENT_TYPE_NAME;
}

void EventType::copyValue(const RuntimeValue &sourceValue, RuntimeValue &destinationValue) const {
    destinationValue.setIdValue(sourceValue.getIdValue());
}

void EventType::toString(const RuntimeValue & /*value*/, NomadString &string) const {
    string = m_event.name;

    if (m_event.parameters.empty() == false) {
        string += " ";

        for (const auto& parameterType : m_event.parameters) {
            string += parameterType.name;
            string += ":";
            string += parameterType.typeName;
        }
    }
}

bool EventType::sameType(const Type *other) const {
    return other == this;
}

const NomadString & EventType::getEventName() const {
    return m_event.name;
}

} // namespace nomad
