// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/CompilerDiagnostic.hpp>

#include <ostream>
#include <sstream>

namespace nomad {

void formatDiagnostic(const CompilerDiagnostic& diagnostic, std::ostream& stream) {
    stream << diagnostic.sourceName;
    if (diagnostic.line != NOMAD_INVALID_INDEX) {
        stream << ':' << std::to_string(diagnostic.line);
        if (diagnostic.column != NOMAD_INVALID_INDEX) {
            stream << ':' << std::to_string(diagnostic.column);
        }
    }
    if (!diagnostic.sourceName.empty() || diagnostic.line != NOMAD_INVALID_INDEX) {
        stream << ": ";
    }
    stream << (diagnostic.severity == CompilerDiagnosticSeverity::Error ? "error" : "warning")
        << ": " << diagnostic.message;
}

NomadString formatDiagnostic(const CompilerDiagnostic& diagnostic) {
    std::ostringstream stream;
    formatDiagnostic(diagnostic, stream);
    return stream.str();
}

} // namespace nomad
