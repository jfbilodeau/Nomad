// Copyright (c) 2025-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/script/RuntimeValue.hpp>

#include <vector>

namespace nomad {

// Forward declarations
class Type;

struct EventParameter {
    NomadString name;
    NomadId typeId;
    NomadString typeName;
};

struct EventDefinition {
    NomadId id;
    NomadString name;
    std::vector<EventParameter> parameters;
    NomadString doc;
};

struct EventDispatch {
    NomadId eventId;
    NomadId sceneId;
    NomadId layerId;
    NomadId entityId;
    NomadInteger mask;
    std::vector<RuntimeValue> arguments;
};

}
