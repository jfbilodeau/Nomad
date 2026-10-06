// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <optional>

#include <optional>

namespace nomad {

// Forward declarations
class Compiler;
class FormatString;
class Function;

// Compiles format into formatString. With a null formatString, only validates format. Returns an error message if
// format is invalid.
[[nodiscard]] std::optional<NomadString> compileFormatString(
    const Compiler* compiler,
    Function* function,
    const NomadString& format,
    FormatString* formatString
);

} // nomad
