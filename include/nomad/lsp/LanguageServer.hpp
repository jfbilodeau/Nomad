// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <boost/json.hpp>

#include <functional>
#include <iosfwd>
#include <map>

namespace nomad {

struct LanguageDocument {
    NomadString text;
    std::int64_t version;
};

using LanguageDocuments = std::map<NomadString, LanguageDocument>;
using LanguageDiagnostics = std::map<NomadString, boost::json::array>;
using LanguageAnalyzer = std::function<LanguageDiagnostics(const LanguageDocuments&)>;

// Full-document synchronization and push diagnostics. Analysis is synchronous and never executes scripts.
[[nodiscard]] int runLanguageServer(
    std::istream& input,
    std::ostream& output,
    std::ostream& errors,
    const LanguageAnalyzer& analyze
);
[[nodiscard]] LanguageDiagnostics analyzeLanguageDocuments(const LanguageDocuments& documents);

} // namespace nomad
