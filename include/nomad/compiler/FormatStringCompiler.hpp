// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <optional>
#include <vector>

namespace nomad {

// Forward declarations
class Compiler;
class FormatString;
class ScriptCallableBase;

enum class FormatTemplateSegmentType {
    Literal = 1,
    Placeholder,
    Error,
};

struct FormatTemplateDiagnostic {
    NomadString message;
    NomadIndex offset;
    NomadIndex length;
};

struct FormatTemplateSegment {
    FormatTemplateSegmentType type;
    NomadString text;
    NomadIndex offset;
    NomadIndex length;
    std::optional<NomadIndex> diagnosticIndex;
};

struct FormatTemplate {
    std::vector<FormatTemplateSegment> segments;
    std::vector<FormatTemplateDiagnostic> diagnostics;
};

// Scans template values without decoding escapes or resolving names. Offsets are zero-based byte offsets in format.
// Closed invalid placeholders retain a recovery boundary; an unclosed placeholder consumes the remaining template.
[[nodiscard]] FormatTemplate parseFormatTemplate(NomadStringView format);

// Decodes literal-text escapes in source content without the surrounding $"...".
// Placeholder text is preserved for its own expression tokenizer.
[[nodiscard]] NomadString decodeFormatStringLiteral(NomadStringView content);

// Finds the closing quote of a $"..." token, respecting quoted expressions and nested format strings.
// offset points at '$'. Returns nullopt when the literal or one of its nested delimiters is unclosed.
[[nodiscard]] std::optional<NomadIndex> findFormatStringLiteralEnd(NomadStringView source, NomadIndex offset);

// Compiles format into formatString. With a null formatString, only validates format. Returns an error message if
// format is invalid.
[[nodiscard]] std::optional<NomadString> compileFormatString(
    const Compiler* compiler,
    ScriptCallableBase* function,
    const NomadString& format,
    FormatString* formatString
);

} // nomad
