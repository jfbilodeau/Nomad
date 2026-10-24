// Copyright (c) 2025-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/script/RuntimeValue.hpp>
#include <nomad/script/Type.hpp>

#include <vector>

namespace nomad {

// Forward declarations
class Type;

DeclareId(EventId);

struct EventParameter {
    NomadString name;
    TypeId typeId;
    NomadString typeName;
};

struct EventDefinition {
    EventId id;
    NomadString name;
    std::vector<EventParameter> parameters;
    NomadString doc;
};

const NomadString EVENT_TYPE_NAME = "event";
class EventType final : public ValueType {
public:
    void initValue(RuntimeValue& value) const override;
    EventType(Runtime* runtime, TypeId id, const EventDefinition* declaration);

    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
    [[nodiscard]] bool sameType(const Type* other) const override;

    [[nodiscard]] const NomadString& getEventName() const;

private:
    const EventDefinition m_event;
};

struct EventDispatch {
    EventId eventId;
    NomadId sceneId;
    NomadId layerId;
    NomadId entityId;
    NomadInteger mask;
    std::vector<RuntimeValue> arguments;
};

}
