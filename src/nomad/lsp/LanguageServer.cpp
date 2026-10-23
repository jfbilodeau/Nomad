// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/lsp/LanguageServer.hpp>

#include <nomad/compiler/CompilerTools.hpp>
#include <nomad/compiler/SourceLoader.hpp>

#include <nomad/project/ProjectConfiguration.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>

#include <nomad/system/Path.hpp>
#include <nomad/system/ProjectSession.hpp>

#include <boost/json.hpp>

#include <algorithm>
#include <charconv>
#include <compare>
#include <istream>
#include <limits>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <stdexcept>
#include <string_view>

namespace nomad {
namespace {

namespace json = boost::json;
constexpr std::size_t maximumMessageSize = 16 * 1024 * 1024;

enum class CompletionItemKind {
    Method = 2,
    Function = 3,
    Variable = 6,
    Class = 7,
    Module = 9,
    Property = 10,
    Keyword = 14,
    Constant = 21,
};

struct ApiSymbol {
    NomadString name;
    CompletionItemKind kind;
    NomadString signature;
    NomadString documentation;
    NomadString valueType = {};
    NomadString receiverType = {};
};

struct ApiCatalogKey {
    NomadString documentUri;
    NomadIndex line;
    bool completion;

    auto operator<=>(const ApiCatalogKey&) const = default;
};

struct AnalysisGroup {
    std::vector<NomadPath> roots;
    std::vector<ScriptSource> overrides;
    std::optional<NomadProject> project;
};

struct ApiAnalysis {
    std::vector<ApiSymbol> symbols;
    std::set<NomadString> localNames;
    bool recover;
};

std::map<NomadPath, AnalysisGroup> collectAnalysisGroups(const LanguageDocuments& documents);
NomadPath documentPath(const NomadString& uri);

std::unique_ptr<ModuleHost> createAnalysisSession(const AnalysisGroup& group, const NomadPath& path) {
    if (group.project) {
        return createProjectSession(*group.project, ModuleMode::Analysis);
    }
    NomadProject project;
    project.root = path.parent_path();
    project.configuration.sdk.modules = {"nomad-system"};
    return ModuleHost::create(
        project.document, project.root, resolveProjectModules(project, getSdkDirectory()),
        std::vector<NomadString>{}, ModuleMode::Analysis
    );
}

ApiAnalysis analyzeApiSymbols(
    const AnalysisGroup& group,
    const NomadPath& path,
    const NomadString& uri,
    const NomadIndex line,
    const bool completion,
    const bool omitCurrentLine
) {
    // Destroy callback metadata before unloading modules, and the complete session before recovery.
    const auto session = createAnalysisSession(group, path);
    auto* runtime = session->getRuntime();
    auto batch = loadSourcePaths(group.roots, group.overrides);
    if (omitCurrentLine) {
        for (auto& script : batch.sources) {
            if (script.source.sourceName != uri) continue;
            auto start = std::size_t{0};
            for (auto index = NomadIndex{1}; index < line; ++index) {
                const auto newline = script.source.text.find('\n', start);
                if (newline == NomadString::npos) break;
                start = newline + 1;
            }
            const auto newline = script.source.text.find('\n', start);
            const auto end = newline == NomadString::npos ? script.source.text.size() : newline;
            std::fill(script.source.text.begin() + static_cast<std::ptrdiff_t>(start),
                script.source.text.begin() + static_cast<std::ptrdiff_t>(end), ' ');
        }
    }
    // Failed compilations still retain declarations collected by the compiler's earlier passes.
    const auto compilation = session->compileSources(batch, CompilationTarget::Analysis);
    const auto currentLineError = std::ranges::any_of(compilation.diagnostics, [&](const auto& diagnostic) {
        return diagnostic.sourceName == uri && diagnostic.line == line &&
            diagnostic.severity == DiagnosticSeverity::Error;
    });
    std::vector<ApiSymbol> symbols;
    std::vector<KeywordDefinition> keywords;
    runtime->getKeywords(keywords);
    for (const auto& keyword : keywords) {
        symbols.push_back({keyword.keyword, CompletionItemKind::Keyword, keyword.keyword, keyword.doc});
    }
    std::vector<const Type*> types;
    runtime->getTypes(types);
    for (const auto* type : types) {
        if (!type->isInternal()) {
            symbols.push_back({type->getTypeName(), CompletionItemKind::Class,
                type->getTypeName(), type->getDocumentation()});
        }
    }
    const auto functions = runtime->getNativeFunctions();
    for (const auto* function : functions) {
        auto signature = function->getName();
        auto documentation = function->getDoc();
        for (const auto& parameter : function->getParameters()) {
            const auto shape = runtime->getParameterShape(parameter.type);
            if (shape == ParameterShape::SourceFile || shape == ParameterShape::SourceFunction ||
                shape == ParameterShape::SourceLine) continue;
            const auto typeName = parameter.type->getTypeName();
            signature += " " + parameter.name + ":" + typeName;
            if (!parameter.doc.empty()) {
                documentation += "\n\n- `" + parameter.name + "`: " + parameter.doc;
            }
        }
        signature += " -> " + function->getReturnType()->getTypeName();
        symbols.push_back({function->getName(), CompletionItemKind::Function,
            std::move(signature), std::move(documentation)});
    }
    for (auto id = runtime->getFirstConstantId(); id.isValid();
        id = runtime->getNextConstantId(id)) {
        const auto& name = runtime->getConstantName(id);
        symbols.push_back({name, CompletionItemKind::Constant,
            name + ":" + runtime->getConstantType(id)->getTypeName(), ""});
    }
    for (auto id = runtime->getFirstDynamicVariableId(); id.isValid();
        id = runtime->getNextDynamicVariableId(id)) {
        const auto name = runtime->getDynamicVariableName(id);
        const auto typeName = runtime->getDynamicVariableType(id)->getTypeName();
        symbols.push_back({name, CompletionItemKind::Variable, name + ":" + typeName,
            "Registered engine variable.", typeName});
    }
    for (auto contextId = runtime->getFirstVariableContextId(); contextId.isValid();
        contextId = runtime->getNextVariableContextId(contextId)) {
        const auto* context = runtime->getVariableContext(contextId);
        for (auto id = context->getFirstVariableId(); id != NOMAD_INVALID_ID;
            id = context->getNextVariableId(id)) {
            const auto name = context->getVariableName(id);
            const auto* type = context->getVariableType(id);
            const auto typeName = type ? type->getTypeName() : "unknown";
            symbols.push_back({name, CompletionItemKind::Variable, name + ":" + typeName,
                "Script context variable.", typeName});
        }
    }
    std::vector<MemberReference> members;
    runtime->getNativeMembers(members);
    for (const auto id : members) {
        if (runtime->getMemberType(id) == MemberType::Property) {
            NativePropertyDefinition definition;
            if (!runtime->getNativePropertyDefinition(id, definition)) {
                throw std::runtime_error("Registered native property is unavailable");
            }
            const auto receiver = definition.objectType->getTypeName();
            const auto value = definition.valueType->getTypeName();
            symbols.push_back({definition.name, CompletionItemKind::Property,
                receiver + "." + definition.name + ":" + value, definition.doc, value, receiver});
        } else if (runtime->getMemberType(id) == MemberType::Method) {
            const auto* method = runtime->getNativeMemberMethod(id);
            if (method == nullptr) {
                throw std::runtime_error("Registered native method is unavailable");
            }
            const auto receiver = method->getObjectType()->getTypeName();
            auto signature = receiver + "." + method->getName();
            auto documentation = method->getDoc();
            for (const auto& parameter : method->getParameters()) {
                const auto typeName = parameter.type->getTypeName();
                signature += " " + parameter.name + ":" + typeName;
                if (!parameter.doc.empty()) documentation += "\n\n- `" + parameter.name + "`: " + parameter.doc;
            }
            signature += " -> " + method->getReturnType()->getTypeName();
            symbols.push_back({method->getName(), CompletionItemKind::Method,
                std::move(signature), std::move(documentation), "", receiver});
        }
    }
    std::vector<ScriptFunction*> scriptFunctions;
    runtime->getFunctions(scriptFunctions);
    for (const auto* function : scriptFunctions) {
        if (function->isNested()) continue;
        auto signature = function->getName();
        for (auto index = NomadIndex{0}; index < function->getParameterCount(); ++index) {
            const auto parameterId = toNomadId(index);
            signature += " " + function->getParameterName(parameterId) + ":" +
                function->getParameterType(parameterId)->getTypeName();
        }
        const auto* returnType = function->getReturnType();
        signature += " -> " + (returnType ? returnType->getTypeName() : "unknown");
        symbols.push_back({function->getName(), CompletionItemKind::Function,
            std::move(signature), "Defined in `" + function->getPath() + "`."});
    }
    std::set<NomadString> localNames;
    for (auto* function = runtime->getFunctionAtLine(uri, line); function;
        function = runtime->getScriptFunction(function->getParentId())) {
        const auto addLocal = [&](const NomadString& name, const Type* type) {
            if (name.empty() || name.find('@') != NomadString::npos) return;
            if (!localNames.insert(name).second) return;
            const auto typeName = type ? type->getTypeName() : "unknown";
            std::erase_if(symbols, [&](const auto& symbol) {
                return symbol.receiverType.empty() && symbol.name == name;
            });
            symbols.push_back({name, CompletionItemKind::Variable, name + ":" + typeName,
                "Local to `" + function->getName() + "`.", type ? typeName : NomadString{}});
        };
        for (auto index = NomadIndex{0}; index < function->getVariableCount(); ++index) {
            addLocal(function->getVisibleVariableName(toNomadId(index)), function->getVariableType(toNomadId(index)));
        }
        for (auto index = NomadIndex{0}; index < function->getParameterCount(); ++index) {
            addLocal(function->getParameterName(toNomadId(index)), function->getParameterType(toNomadId(index)));
        }
    }
    std::set<NomadString> namespaces;
    for (const auto& symbol : symbols) {
        if (!symbol.receiverType.empty()) continue;
        const auto separator = symbol.name.find('.');
        if (separator != NomadString::npos) namespaces.insert(symbol.name.substr(0, separator + 1));
    }
    for (const auto& name : namespaces) {
        symbols.push_back({name, CompletionItemKind::Module, name, "Symbol namespace."});
    }
    return {std::move(symbols), std::move(localNames),
        !compilation.succeeded() && currentLineError && completion && !omitCurrentLine};
}

std::vector<ApiSymbol> createApiSymbols(
    const NomadPath& path,
    const LanguageDocuments& documents,
    const NomadString& uri,
    const NomadIndex line,
    const bool completion,
    const bool omitCurrentLine = false
) {
    const auto root = findProjectRoot(path.parent_path());
    LanguageDocuments relatedDocuments;
    for (const auto& [documentUri, document] : documents) {
        const auto candidate = documentPath(documentUri);
        if (root ? findProjectRoot(candidate.parent_path()) == root : candidate == path) {
            relatedDocuments.emplace(documentUri, document);
        }
    }
    const auto groups = collectAnalysisGroups(relatedDocuments);
    const auto& group = groups.at(root.value_or(path));
    auto analysis = analyzeApiSymbols(group, path, uri, line, completion, omitCurrentLine);
    auto& symbols = analysis.symbols;
    if (analysis.recover) {
        auto recovered = createApiSymbols(path, documents, uri, line, completion, true);
        for (auto& symbol : symbols) {
            if (analysis.localNames.contains(symbol.name)) continue;
            const auto present = std::ranges::any_of(recovered, [&](const auto& candidate) {
                return candidate.name == symbol.name && candidate.receiverType == symbol.receiverType;
            });
            if (!present) recovered.push_back(std::move(symbol));
        }
        symbols = std::move(recovered);
    }
    std::ranges::sort(symbols, [](const auto& left, const auto& right) {
        return left.name == right.name ? left.signature < right.signature : left.name < right.name;
    });
    return std::move(analysis.symbols);
}

bool isNameCharacter(const char character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9') || character == '_' || character == '.';
}

NomadString symbolValueType(
    const std::vector<ApiSymbol>& symbols,
    const NomadString& name
) {
    for (auto iterator = symbols.rbegin(); iterator != symbols.rend(); ++iterator) {
        if (iterator->receiverType.empty() && iterator->name == name && !iterator->valueType.empty()) {
            return iterator->valueType;
        }
    }
    const auto separator = name.rfind('.');
    if (separator == NomadString::npos) return {};
    const auto receiverType = symbolValueType(symbols, name.substr(0, separator));
    if (receiverType.empty()) return {};
    for (const auto& symbol : symbols) {
        if (symbol.receiverType == receiverType && symbol.name == name.substr(separator + 1)) {
            return symbol.valueType;
        }
    }
    return {};
}

struct DocumentWord {
    NomadString prefix;
    NomadString name;
    json::object range;
    bool suppressed;
};

DocumentWord documentWord(const NomadString& text, const json::object& position) {
    const auto requestedLine = position.at("line").as_int64();
    const auto requestedCharacter = position.at("character").as_int64();
    if (requestedLine < 0 || requestedCharacter < 0) throw std::invalid_argument("Negative document position");
    auto lineStart = std::size_t{0};
    for (auto line = std::int64_t{0}; line < requestedLine; ++line) {
        const auto newline = text.find('\n', lineStart);
        if (newline == NomadString::npos) throw std::invalid_argument("Line is outside document");
        lineStart = newline + 1;
    }
    const auto newline = text.find('\n', lineStart);
    auto lineEnd = newline == NomadString::npos ? text.size() : newline;
    if (lineEnd > lineStart && text[lineEnd - 1] == '\r') --lineEnd;
    auto offset = lineStart;
    auto character = std::int64_t{0};
    while (offset < lineEnd && character < requestedCharacter) {
        const auto byte = static_cast<unsigned char>(text[offset]);
        const auto units = byte >= 0xF0 ? 2 : 1;
        if (character + units > requestedCharacter) throw std::invalid_argument("Position splits a UTF-16 character");
        const auto bytes = byte >= 0xF0 ? 4U : byte >= 0xE0 ? 3U : byte >= 0xC0 ? 2U : 1U;
        offset = std::min(offset + bytes, lineEnd);
        character += units;
    }
    if (character != requestedCharacter) throw std::invalid_argument("Character is outside document");
    auto quoted = false;
    auto escaped = false;
    auto comment = false;
    for (auto index = lineStart; index < offset; ++index) {
        if (escaped) { escaped = false; continue; }
        if (quoted && text[index] == '\\') { escaped = true; continue; }
        if (text[index] == '"') quoted = !quoted;
        if (!quoted && text[index] == '#') { comment = true; break; }
    }
    auto start = offset;
    auto end = offset;
    while (start > lineStart && isNameCharacter(text[start - 1])) --start;
    while (end < lineEnd && isNameCharacter(text[end])) ++end;
    const auto startCharacter = requestedCharacter - static_cast<std::int64_t>(offset - start);
    const auto endCharacter = requestedCharacter + static_cast<std::int64_t>(end - offset);
    return {text.substr(start, offset - start), text.substr(start, end - start),
        {{"start", {{"line", requestedLine}, {"character", startCharacter}}},
            {"end", {{"line", requestedLine}, {"character", endCharacter}}}},
        quoted || comment};
}

json::object symbolDocumentation(const ApiSymbol& symbol) {
    return {{"kind", "markdown"},
        {"value", "```nomad\n" + symbol.signature + "\n```\n\n" + symbol.documentation}};
}

NomadString stringField(const json::object& object, const char* name) {
    return NomadString(object.at(name).as_string());
}

int hexDigit(const char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    throw std::invalid_argument("Invalid percent escape in document URI");
}

NomadPath documentPath(const NomadString& uri) {
    if (!uri.starts_with("file://")) {
        throw std::invalid_argument("Only file document URIs are supported");
    }
    auto encoded = uri.substr(7);
    if (encoded.starts_with("localhost/")) encoded.erase(0, 9);
    if (encoded.empty() || encoded.find_first_of("?#") != NomadString::npos) {
        throw std::invalid_argument("Invalid file document URI");
    }
    NomadString path;
    for (std::size_t index = 0; index < encoded.size(); ++index) {
        if (encoded[index] == '%') {
            if (index + 2 >= encoded.size()) throw std::invalid_argument("Incomplete URI escape");
            path.push_back(static_cast<char>(hexDigit(encoded[index + 1]) * 16 + hexDigit(encoded[index + 2])));
            index += 2;
        } else {
            path.push_back(encoded[index]);
        }
    }
    if (path.find('\0') != NomadString::npos) throw std::invalid_argument("NUL in document URI");
#ifdef _WIN32
    if (path.size() >= 3 && path[0] == '/' && path[2] == ':') path.erase(0, 1);
    else if (!path.starts_with('/')) path = "//" + path;
#else
    if (!path.starts_with('/')) throw std::invalid_argument("Remote file authorities are not supported");
#endif
    const auto result = pathFromUtf8(path);
    if (!result.is_absolute()) throw std::invalid_argument("Document path must be absolute");
    return result.lexically_normal();
}

NomadString documentUri(const NomadPath& path) {
    const auto text = pathToGenericUtf8(std::filesystem::absolute(path).lexically_normal());
    NomadString uri = "file://";
    if (!text.starts_with('/')) uri += '/';
    constexpr auto digits = "0123456789ABCDEF";
    for (const auto character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || character == '/' || character == ':' ||
            character == '-' || character == '_' || character == '.' || character == '~') {
            uri += character;
        } else {
            uri += '%';
            uri += digits[byte >> 4];
            uri += digits[byte & 15];
        }
    }
#ifdef _WIN32
    if (text.starts_with("//")) uri.erase(7, 2);
#endif
    return uri;
}

json::object diagnosticRange(const Diagnostic& diagnostic, NomadStringView text) {
    const auto requestedLine = diagnostic.line == NOMAD_INVALID_INDEX || diagnostic.line == 0
        ? 0 : diagnostic.line - 1;
    auto line = std::size_t{0};
    auto start = std::size_t{0};
    while (line < requestedLine) {
        const auto newline = text.find('\n', start);
        if (newline == NomadStringView::npos) break;
        start = newline + 1;
        ++line;
    }
    const auto newline = text.find('\n', start);
    auto end = newline == NomadStringView::npos ? text.size() : newline;
    if (end > start && text[end - 1] == '\r') --end;
    const auto column = diagnostic.column == NOMAD_INVALID_INDEX || diagnostic.column == 0
        ? 0 : diagnostic.column - 1;
    const auto byteEnd = start + std::min(column, end - start);
    auto character = std::size_t{0};
    for (auto index = start; index < byteEnd; ++index) {
        const auto byte = static_cast<unsigned char>(text[index]);
        if ((byte & 0xC0) != 0x80) character += byte >= 0xF0 ? 2 : 1;
    }
    const auto width = byteEnd < end ? (static_cast<unsigned char>(text[byteEnd]) >= 0xF0 ? 2U : 1U) : 0U;
    return {{"start", {{"line", line}, {"character", character}}},
        {"end", {{"line", line}, {"character", character + width}}}};
}

void sendMessage(std::ostream& output, const json::object& message) {
    const auto body = json::serialize(message);
    output << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    output.flush();
    if (!output) throw std::runtime_error("Failed to write LSP message");
}

std::optional<NomadString> readMessage(std::istream& input) {
    NomadString header;
    std::optional<std::size_t> length;
    auto headerBytes = std::size_t{0};
    char character;
    while (input.get(character)) {
        if (++headerBytes > 8192) throw std::runtime_error("LSP headers exceed size limit");
        if (character != '\n') {
            header += character;
            continue;
        }
        if (header == "\r") {
            if (!length) throw std::runtime_error("Missing LSP Content-Length");
            NomadString body(*length, '\0');
            input.read(body.data(), static_cast<std::streamsize>(*length));
            if (input.gcount() != static_cast<std::streamsize>(*length)) {
                throw std::runtime_error("Truncated LSP message");
            }
            return body;
        }
        if (header.starts_with("Content-Length:")) {
            if (length) throw std::runtime_error("Duplicate LSP Content-Length");
            auto value = std::string_view(header).substr(15);
            while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
            if (!value.empty() && value.back() == '\r') value.remove_suffix(1);
            std::size_t parsed = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
                parsed == 0 || parsed > maximumMessageSize) {
                throw std::runtime_error("Invalid LSP Content-Length");
            }
            length = parsed;
        }
        header.clear();
    }
    if (headerBytes != 0) throw std::runtime_error("Truncated LSP headers");
    return std::nullopt;
}

std::map<NomadPath, AnalysisGroup> collectAnalysisGroups(const LanguageDocuments& documents) {
    std::map<NomadPath, AnalysisGroup> groups;
    for (const auto& [uri, document] : documents) {
        const auto path = documentPath(uri);
        const auto projectRoot = findProjectRoot(path.parent_path());
        const auto key = projectRoot.value_or(path);
        auto [iterator, inserted] = groups.try_emplace(key);
        auto& group = iterator->second;
        if (inserted) {
            if (projectRoot) {
                group.project = loadNomadProject(*projectRoot / NOMAD_PROJECT_FILE_NAME);
                group.roots = resolveScriptPaths(group.project->document, group.project->root);
            } else {
                group.roots.push_back(path);
            }
        }
        auto scriptName = pathToUtf8(path.stem());
        if (group.project) {
            auto matched = false;
            for (const auto& root : group.roots) {
                const auto relative = path.lexically_relative(root.lexically_normal());
                if (!relative.empty() && *relative.begin() != "..") {
                    scriptName = pathToGenericUtf8(relative.parent_path() / relative.stem());
                    std::replace(scriptName.begin(), scriptName.end(), '/', '.');
                    matched = true;
                    break;
                }
            }
            if (!matched) throw std::runtime_error("Document is outside configured script roots: " + uri);
        }
        group.overrides.push_back({scriptName, {uri, document.text, path}});
    }
    return groups;
}

} // namespace

LanguageDiagnostics analyzeLanguageDocuments(const LanguageDocuments& documents) {
    const auto groups = collectAnalysisGroups(documents);
    LanguageDiagnostics published;
    for (const auto& [key, group] : groups) {
        const auto batch = loadSourcePaths(group.roots, group.overrides);
        const auto session = createAnalysisSession(group, key);
        const auto result = session->compileSources(batch, CompilationTarget::Analysis);
        std::map<NomadString, NomadString> sourceUris;
        std::map<NomadString, NomadStringView> sourceTexts;
        for (const auto& script : group.overrides) {
            sourceUris[script.source.sourceName] = script.source.sourceName;
            sourceTexts[script.source.sourceName] = script.source.text;
        }
        for (const auto& script : batch.sources) {
            const auto uri = documents.contains(script.source.sourceName) ? script.source.sourceName
                : documentUri(*script.source.filePath);
            sourceUris[script.source.sourceName] = uri;
            sourceTexts[script.source.sourceName] = script.source.text;
        }
        for (const auto& diagnostic : result.diagnostics) {
            const auto found = sourceUris.find(diagnostic.sourceName);
            const auto uri = found != sourceUris.end() ? found->second
                : documentUri(pathFromUtf8(diagnostic.sourceName));
            published[uri].push_back(json::object{
                {"range", diagnosticRange(diagnostic, sourceTexts[diagnostic.sourceName])},
                {"severity", diagnostic.severity == DiagnosticSeverity::Error ? 1 : 2},
                {"source", "nomad"}, {"message", diagnostic.message}
            });
        }
    }
    return published;
}

int runLanguageServer(
    std::istream& input,
    std::ostream& output,
    std::ostream& errors,
    const LanguageAnalyzer& analyze
) {
    LanguageDocuments documents;
    std::map<ApiCatalogKey, std::vector<ApiSymbol>> apiCatalogs;
    std::set<NomadString> previousUris;
    auto initialized = false;
    auto ready = false;
    auto shutdown = false;
    const auto publish = [&] {
        LanguageDiagnostics diagnostics;
        try {
            diagnostics = analyze(documents);
        } catch (const std::exception& exception) {
            errors << "Nomad analysis failed: " << exception.what() << '\n';
            sendMessage(output, {{"jsonrpc", "2.0"}, {"method", "window/showMessage"},
                {"params", {{"type", 1}, {"message", NomadString("Nomad analysis failed: ") + exception.what()}}}});
            for (const auto& [uri, document] : documents) {
                (void)document;
                diagnostics[uri].push_back(json::object{
                    {"range", diagnosticRange({DiagnosticSeverity::Error, "", "", 1, 1}, "")},
                    {"severity", 1}, {"source", "nomad"}, {"message", exception.what()}
                });
            }
        }
        auto uris = previousUris;
        for (const auto& [uri, document] : documents) { (void)document; uris.insert(uri); }
        for (const auto& [uri, values] : diagnostics) { (void)values; uris.insert(uri); }
        previousUris.clear();
        for (const auto& uri : uris) {
            json::object params{{"uri", uri}, {"diagnostics", diagnostics[uri]}};
            if (const auto found = documents.find(uri); found != documents.end()) {
                params["version"] = found->second.version;
            }
            if (!diagnostics[uri].empty()) previousUris.insert(uri);
            sendMessage(output, {{"jsonrpc", "2.0"}, {"method", "textDocument/publishDiagnostics"},
                {"params", std::move(params)}});
        }
    };
    try {
        while (const auto body = readMessage(input)) {
            boost::system::error_code parseError;
            auto message = json::parse(*body, parseError);
            if (parseError) {
                sendMessage(output, {{"jsonrpc", "2.0"}, {"id", nullptr},
                    {"error", {{"code", -32700}, {"message", "Invalid JSON"}}}});
                continue;
            }
            json::value id;
            auto request = false;
            try {
                const auto& object = message.as_object();
                if (const auto* value = object.if_contains("id")) { id = *value; request = true; }
                if (stringField(object, "jsonrpc") != "2.0") throw std::invalid_argument("Expected JSON-RPC 2.0");
                if (!object.contains("method")) continue; // Client responses are not requests.
                const auto method = stringField(object, "method");
                if (method == "exit") return shutdown ? 0 : 1;
                const auto fail = [&](const int code, const char* text) {
                    if (request) sendMessage(output, {{"jsonrpc", "2.0"}, {"id", id},
                        {"error", {{"code", code}, {"message", text}}}});
                };
                if (method == "initialize" && !initialized && request) {
                    initialized = true;
                    sendMessage(output, {{"jsonrpc", "2.0"}, {"id", id},
                        {"result", {{"capabilities", {
                            {"positionEncoding", "utf-16"},
                            {"completionProvider", {{"triggerCharacters", json::array{"."}}}},
                            {"hoverProvider", true},
                            {"textDocumentSync", {{"openClose", true}, {"change", 1}, {"save", true}}}
                        }}, {"serverInfo", {{"name", "Nomad"}}}}}});
                } else if (!initialized) {
                    fail(-32002, "Server not initialized");
                } else if (method == "shutdown" && request && !shutdown) {
                    shutdown = true;
                    sendMessage(output, {{"jsonrpc", "2.0"}, {"id", id}, {"result", nullptr}});
                } else if (shutdown) {
                    fail(-32600, "Server is shutting down");
                } else if (method == "initialized") {
                    ready = true;
                } else if (!ready) {
                    fail(-32002, "Waiting for initialized notification");
                } else if ((method == "textDocument/completion" || method == "textDocument/hover") && request) {
                    const auto& params = object.at("params").as_object();
                    const auto uri = stringField(params.at("textDocument").as_object(), "uri");
                    const auto& document = documents.at(uri);
                    const auto word = documentWord(document.text, params.at("position").as_object());
                    const auto path = documentPath(uri);
                    const auto line = static_cast<NomadIndex>(params.at("position").as_object().at("line").as_int64()) + 1;
                    const auto completion = method == "textDocument/completion";
                    const auto key = ApiCatalogKey{uri, line, completion};
                    auto found = apiCatalogs.find(key);
                    if (found == apiCatalogs.end() && !word.suppressed) {
                        found = apiCatalogs.emplace(key, createApiSymbols(path, documents, uri, line, completion)).first;
                    }
                    json::value result;
                    if (method == "textDocument/completion") {
                        json::array items;
                        if (!word.suppressed) {
                            const auto separator = word.prefix.rfind('.');
                            const auto receiver = separator == NomadString::npos
                                ? NomadString{} : word.prefix.substr(0, separator);
                            const auto receiverType = symbolValueType(found->second, receiver);
                            auto namespacePrefix = false;
                            for (const auto& symbol : found->second) {
                                if (!receiver.empty() && symbol.receiverType.empty() &&
                                    symbol.name.starts_with(receiver + ".")) namespacePrefix = true;
                            }
                            for (const auto& symbol : found->second) {
                                auto replacement = symbol.name;
                                if (!symbol.receiverType.empty()) {
                                    if (separator == NomadString::npos || namespacePrefix) continue;
                                    if (!receiverType.empty() && symbol.receiverType != receiverType) continue;
                                    if (!symbol.name.starts_with(word.prefix.substr(separator + 1))) continue;
                                    replacement = receiver + "." + symbol.name;
                                } else {
                                    if (!symbol.name.starts_with(word.prefix)) continue;
                                    if (word.prefix.empty() && symbol.kind != CompletionItemKind::Function &&
                                        symbol.kind != CompletionItemKind::Keyword &&
                                        symbol.kind != CompletionItemKind::Variable &&
                                        symbol.kind != CompletionItemKind::Module) continue;
                                }
                                items.push_back(json::object{
                                    {"label", symbol.name}, {"kind", static_cast<int>(symbol.kind)},
                                    {"filterText", replacement},
                                    {"detail", symbol.signature}, {"documentation", symbolDocumentation(symbol)},
                                    {"textEdit", {{"range", word.range}, {"newText", replacement}}}
                                });
                            }
                        }
                        result = json::object{{"isIncomplete", false}, {"items", std::move(items)}};
                    } else if (!word.suppressed && !word.name.empty()) {
                        NomadString markdown;
                        const auto separator = word.name.rfind('.');
                        const auto receiverType = separator == NomadString::npos ? NomadString{}
                            : symbolValueType(found->second, word.name.substr(0, separator));
                        for (const auto& symbol : found->second) {
                            if (!symbol.receiverType.empty()) {
                                if (receiverType != symbol.receiverType ||
                                    symbol.name != word.name.substr(separator + 1)) continue;
                            } else if (symbol.name != word.name) continue;
                            if (!markdown.empty()) markdown += "\n\n---\n\n";
                            markdown += stringField(symbolDocumentation(symbol), "value");
                        }
                        if (!markdown.empty()) {
                            result = json::object{{"contents", {{"kind", "markdown"}, {"value", markdown}}},
                                {"range", word.range}};
                        }
                    }
                    sendMessage(output, {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}});
                } else if (method == "textDocument/didOpen") {
                    const auto& document = object.at("params").as_object().at("textDocument").as_object();
                    const auto uri = stringField(document, "uri");
                    (void)documentPath(uri);
                    if (documents.contains(uri)) throw std::invalid_argument("Document is already open");
                    documents.emplace(uri, LanguageDocument{stringField(document, "text"), document.at("version").as_int64()});
                    apiCatalogs.clear();
                    publish();
                } else if (method == "textDocument/didChange") {
                    const auto& params = object.at("params").as_object();
                    const auto& document = params.at("textDocument").as_object();
                    auto& current = documents.at(stringField(document, "uri"));
                    const auto version = document.at("version").as_int64();
                    if (version <= current.version) throw std::invalid_argument("Document version must increase");
                    const auto& changes = params.at("contentChanges").as_array();
                    auto text = current.text;
                    for (const auto& change : changes) {
                        if (change.as_object().contains("range")) throw std::invalid_argument("Full text changes required");
                        text = stringField(change.as_object(), "text");
                    }
                    current = {std::move(text), version};
                    apiCatalogs.clear();
                    publish();
                } else if (method == "textDocument/didClose") {
                    const auto& document = object.at("params").as_object().at("textDocument").as_object();
                    const auto uri = stringField(document, "uri");
                    if (documents.erase(uri) == 0) throw std::invalid_argument("Document is not open");
                    apiCatalogs.clear();
                    previousUris.insert(uri);
                    publish();
                } else if (method == "textDocument/didSave" || method == "workspace/didChangeWatchedFiles") {
                    apiCatalogs.clear();
                    publish();
                } else if (request) {
                    fail(-32601, "Method not supported");
                }
            } catch (const std::exception& exception) {
                errors << "Invalid LSP message: " << exception.what() << '\n';
                if (request) {
                    sendMessage(output, {{"jsonrpc", "2.0"}, {"id", id},
                        {"error", {{"code", -32602}, {"message", exception.what()}}}});
                } else {
                    sendMessage(output, {{"jsonrpc", "2.0"}, {"method", "window/logMessage"},
                        {"params", {{"type", 1}, {"message", exception.what()}}}});
                }
            }
        }
    } catch (const std::exception& exception) {
        errors << "Nomad language server stopped: " << exception.what() << '\n';
        return 1;
    }
    return shutdown ? 0 : 1;
}

} // namespace nomad
