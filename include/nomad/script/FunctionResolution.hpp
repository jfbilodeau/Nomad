// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

namespace nomad {

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

} // nomad
