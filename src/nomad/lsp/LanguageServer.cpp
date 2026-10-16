// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/lsp/LanguageServer.hpp>

#include <nomad/compiler/CompilerTools.hpp>
#include <nomad/compiler/SourceLoader.hpp>

#include <nomad/game/Game.hpp>

#include <nomad/project/ProjectConfiguration.hpp>

#include <nomad/system/Path.hpp>

#include <algorithm>
#include <charconv>
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

} // namespace

LanguageDiagnostics analyzeLanguageDocuments(const LanguageDocuments& documents) {
    struct AnalysisGroup {
        std::vector<NomadPath> roots;
        std::vector<ScriptSource> overrides;
        std::optional<NomadProject> project;
    };
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
                const auto resources = resolveProjectResourcePath(*group.project);
                for (const auto& directory : group.project->gameOptions.scripts.directories.value_or(
                    std::vector<NomadPath>{"scripts"})) {
                    group.roots.push_back(resources / directory);
                }
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

    LanguageDiagnostics published;
    for (const auto& [key, group] : groups) {
        (void)key;
        const auto batch = loadSourcePaths(group.roots, group.overrides);
        std::unique_ptr<Game> game;
        if (group.project) {
            GameOptions options;
            options.resourcePath = pathToUtf8(resolveProjectResourcePath(*group.project));
            game = Game::createHeadless(std::move(options));
        }
        auto result = checkSources(batch, game ? game->getRuntime() : nullptr);
        if (group.project) {
            validateEntryFunction(result, game->getRuntime(), group.project->gameOptions.entryFunction,
                group.project->root / NOMAD_PROJECT_FILE_NAME);
        }
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

int runLanguageServer(std::istream& input, std::ostream& output, std::ostream& errors,
    const LanguageAnalyzer& analyze) {
    LanguageDocuments documents;
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
                } else if (method == "textDocument/didOpen") {
                    const auto& document = object.at("params").as_object().at("textDocument").as_object();
                    const auto uri = stringField(document, "uri");
                    (void)documentPath(uri);
                    if (documents.contains(uri)) throw std::invalid_argument("Document is already open");
                    documents.emplace(uri, LanguageDocument{stringField(document, "text"), document.at("version").as_int64()});
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
                    publish();
                } else if (method == "textDocument/didClose") {
                    const auto& document = object.at("params").as_object().at("textDocument").as_object();
                    const auto uri = stringField(document, "uri");
                    if (documents.erase(uri) == 0) throw std::invalid_argument("Document is not open");
                    previousUris.insert(uri);
                    publish();
                } else if (method == "textDocument/didSave" || method == "workspace/didChangeWatchedFiles") {
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
