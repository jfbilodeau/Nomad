// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <ostream>

namespace nomad {

// Forward declarations
class Runtime;

#define NomadDocField \
    NomadString doc;

#define NomadDocParamField \
    NomadString doc;

#define NomadDocArg \
    const NomadString& doc

#define NomadDoc(doc_text) \
    NomadString(doc_text)

#define NomadParamDoc(param_doc) \
    param_doc

// Generates registered script API reference and format-string usage notes.
void generateDocumentation(const Runtime* runtime, std::ostream& out);
void generateTypeDocumentation(const Runtime* runtime, std::ostream& out);
void generateIntrinsicMemberDocumentation(const Runtime* runtime, std::ostream& out);
void generateKeywords(Runtime* runtime, std::ostream& out);
// Includes expression interpolation, nested format literals, and the format string operator.
void generateTextMateGrammar(Runtime* runtime, std::ostream& out);

} // namespace nomad
