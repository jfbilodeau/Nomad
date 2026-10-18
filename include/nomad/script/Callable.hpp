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

ParameterDefinition defParameter(const NomadString& name, const Type* type, NomadDocArg);

enum class CallableType {
    Function = 1,
    Method,
    Constructor,
};

enum class CallableSource {
    Script = 1, // Compiled from Nomad source.
    Native,     // Implemented in C++ and registered by the host.
};

// Describes any callable, whatever its type or source. All callables share one id space.
// Each source adds its own details in a derived class; `Runtime` hands them out through typed getters.
struct FunctionDefinition {
    FunctionDefinition() = default;
    FunctionDefinition(const FunctionDefinition&) = default;
    FunctionDefinition& operator=(const FunctionDefinition&) = default;
    virtual ~FunctionDefinition() = default;

    NomadId id = NOMAD_INVALID_ID;
    NomadString name;
    CallableType type = CallableType::Function;
    CallableSource source = CallableSource::Native;
    std::vector<ParameterDefinition> parameters;
    // The type of the value the callable yields; void when it yields nothing.
    const Type* returnType = nullptr;
    // Method: the receiver's type. Constructor: the class being constructed (e.g. `Array`), which may differ from
    // `returnType` (e.g. `Array1<int>`). Null for functions.
    const Type* objectType = nullptr;
    NomadDocField;
};

// Functions and methods implemented in C++. A method has an `objectType`.
struct NativeFunctionDefinition : FunctionDefinition {
    NativeFunctionDefinition() {
        source = CallableSource::Native;
    }

    NativeFunctionFn fn;
};

struct ConstructorDefinition : FunctionDefinition {
    ConstructorDefinition() {
        type = CallableType::Constructor;
        source = CallableSource::Native;
    }

    ConstructorFactory factory;
};

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
