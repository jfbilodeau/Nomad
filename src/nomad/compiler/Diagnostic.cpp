// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Diagnostic.hpp>

#include <ostream>
#include <sstream>

namespace nomad {

void formatDiagnostic(const Diagnostic& diagnostic, std::ostream& stream) {
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
    stream << (diagnostic.severity == DiagnosticSeverity::Error ? "error" : "warning")
        << ": " << diagnostic.message;
}

NomadString formatDiagnostic(const Diagnostic& diagnostic) {
    std::ostringstream stream;
    formatDiagnostic(diagnostic, stream);
    return stream.str();
}

} // namespace nomad
