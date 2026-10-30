// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <iosfwd>

namespace nomad {

enum class DiagnosticSeverity {
    Warning = 1,
    Error,
};

struct Diagnostic {
    DiagnosticSeverity severity;
    NomadString message;
    NomadString sourceName;
    NomadIndex line;
    NomadIndex column;
};

// Formats as `source:line:column: error: message`, omitting unknown location parts.
void formatDiagnostic(const Diagnostic& diagnostic, std::ostream& stream);
[[nodiscard]] NomadString formatDiagnostic(const Diagnostic& diagnostic);

} // namespace nomad
