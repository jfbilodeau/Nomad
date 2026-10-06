// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/Type.hpp>

namespace nomad {

// Ids for variables types used in opcodes.
constexpr NomadId VARIABLE_TYPE_CONSTANT = -1;
constexpr NomadId VARIABLE_TYPE_DYNAMIC = -2;
constexpr NomadId VARIABLE_TYPE_FUNCTION = -3;

enum class IdentifierType {
    Unknown = 1,
    Keyword,
    Statement,
    NativeFunction,
    Function,
    Event,
    Constant,
    DynamicVariable,
    ContextVariable,
    FunctionVariable,
    Parameter,
};

struct IdentifierDefinition {
    IdentifierType identifierType = IdentifierType::Unknown;
    const Type* valueType = nullptr;
    union {
        NomadId nativeFunctionId = NOMAD_INVALID_ID;
        NomadId eventId;
        NomadId variableId;
        NomadId functionId;
    };
};

}
