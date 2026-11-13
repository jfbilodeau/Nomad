// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <iosfwd>

namespace nomad {

enum class CompilerDiagnosticSeverity {
    Warning = 1,
    Error,
};

enum class CompilerDiagnosticCode {
    General = 1,
    UnknownIdentifier,
};

struct CompilerDiagnostic {
    CompilerDiagnosticSeverity severity;
    NomadString message;
    NomadString sourceName;
    NomadIndex line;
    NomadIndex column;
    CompilerDiagnosticCode code = CompilerDiagnosticCode::General;
    // The unresolved name for UnknownIdentifier; empty for general diagnostics.
    NomadString subject = {};
};

// Formats as `source:line:column: error: message`, omitting unknown location parts.
void formatDiagnostic(const CompilerDiagnostic& diagnostic, std::ostream& stream);
[[nodiscard]] NomadString formatDiagnostic(const CompilerDiagnostic& diagnostic);

} // namespace nomad
