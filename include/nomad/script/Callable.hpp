// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/script/Documentation.hpp>
#include <nomad/script/RuntimeValue.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <functional>
#include <span>
#include <vector>

namespace nomad {

class NomadObject;
class ObjectTypeBase;
class Type;

using NativeFunctionFn = std::function<void(VirtualMachine*)>;

using ConstructorFactory = std::function<NomadObject*(std::span<const RuntimeValue>)>;

struct ParameterDefinition {
    NomadString name;
    const Type* type;
    NomadDocField;
};

struct NativeFunctionDefinition {
    NomadId id = NOMAD_INVALID_ID;
    NomadString name;
    NativeFunctionFn fn;
    std::vector<ParameterDefinition> parameters;
    const Type* returnType = nullptr;
    NomadDocField;
    const Type* objectType = nullptr;
};

struct ConstructorDefinition {
    NomadId id = NOMAD_INVALID_ID;
    NomadString name;
    const ObjectTypeBase* returnType = nullptr;
    std::vector<ParameterDefinition> parameters;
    ConstructorFactory factory;
    NomadDocField;
};

ParameterDefinition defParameter(const NomadString& name, const Type* type, NomadDocArg);

// Where a callable is implemented. Both kinds are called with the same syntax in Nomad code.
enum class CallableKind {
    NativeFunction = 1, // Implemented in C++ and registered by the host runtime.
    Function,           // Compiled from Nomad source, either from a script or from a `fun` declaration.
};

// Identifies a single callable in the unified catalog. `id` indexes the registry of the matching kind, so the
// pair is required to address a callable unambiguously.
struct CallableId {
    CallableKind kind = CallableKind::NativeFunction;
    NomadId id = NOMAD_INVALID_ID;

    [[nodiscard]] bool isValid() const { return id != NOMAD_INVALID_ID; }

    bool operator==(const CallableId& other) const {
        return kind == other.kind && id == other.id;
    }

    bool operator!=(const CallableId& other) const { return !(*this == other); }
};

inline constexpr CallableId NOMAD_INVALID_CALLABLE_ID = CallableId{};

// All callables sharing one name. A set holds more than one entry when the name is overloaded; overloads may differ
// only in the types of their `ParameterShape::Value` parameters.
struct CallableOverloadSet {
    NomadString name;
    std::vector<CallableId> overloads;
};

} // nomad
