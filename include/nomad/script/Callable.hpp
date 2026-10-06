// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <vector>

namespace nomad {

class Type;

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

// How the parser consumes the tokens of a parameter. A call is parsed before its overload is selected, so every
// overload of a name must agree on the shape of each parameter.
enum class ParameterShape {
    Value = 1,      // An ordinary value argument, parsed as an expression independently of the expected type.
    SourceFile,     // Hidden `$file`; supplied by the compiler and consumes no tokens.
    SourceFunction, // Hidden `$function`; supplied by the compiler and consumes no tokens.
    SourceLine,     // Hidden `$line`; supplied by the compiler and consumes no tokens.
    EventDispatch,
    EventCallback,
    // Also covers predicates: the predicate type is a callback type, and the parser treats it as one.
    Callback,
};

// Shapes that produce exactly one argument and whose parsing does not depend on the parameter type. Only these can
// appear in an overloaded callable: the other shapes either consume a variable number of tokens or produce a variable
// number of arguments, which would make the parse ambiguous before an overload is chosen.
[[nodiscard]] constexpr bool isOverloadableShape(const ParameterShape shape) {
    return shape == ParameterShape::Value ||
        shape == ParameterShape::SourceFile ||
        shape == ParameterShape::SourceFunction ||
        shape == ParameterShape::SourceLine;
}

// All callables sharing one name. A set holds more than one entry when the name is overloaded; overloads may differ
// only in the types of their `ParameterShape::Value` parameters.
struct CallableOverloadSet {
    NomadString name;
    std::vector<CallableId> overloads;
};

} // nomad
