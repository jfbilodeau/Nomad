// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/lsp/LanguageServer.hpp>

#include <nomad/system/Path.hpp>

#include <TestDirectory.hpp>

#include <boost/json.hpp>

#include <boost/test/unit_test.hpp>

#include <set>
#include <sstream>

using namespace nomad;
using namespace nomad::test;
namespace json = boost::json;

namespace {

NomadString frame(const json::object& message) {
    const auto body = json::serialize(message);
    return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

std::vector<json::object> responses(const NomadString& text) {
    std::vector<json::object> result;
    auto offset = std::size_t{0};
    while (offset < text.size()) {
        const auto end = text.find("\r\n\r\n", offset);
        BOOST_REQUIRE(end != NomadString::npos);
        BOOST_REQUIRE(text.substr(offset).starts_with("Content-Length: "));
        const auto length = std::stoull(text.substr(offset + 16, end - offset - 16));
        offset = end + 4;
        BOOST_REQUIRE(length <= text.size() - offset);
        result.push_back(json::parse(text.substr(offset, length)).as_object());
        offset += length;
    }
    return result;
}

NomadString uriFor(const NomadPath& path) {
    auto text = pathToGenericUtf8(path);
    if (!text.starts_with('/')) text.insert(0, "/");
    NomadString result = "file://";
    for (const auto character : text) {
        if (character == ' ') result += "%20";
        else result += character;
    }
    return result;
}

NomadString start() {
    return frame({{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}, {"params", json::object{}}}) +
        frame({{"jsonrpc", "2.0"}, {"method", "initialized"}, {"params", json::object{}}});
}

NomadString finish() {
    return frame({{"jsonrpc", "2.0"}, {"id", 2}, {"method", "shutdown"}}) +
        frame({{"jsonrpc", "2.0"}, {"method", "exit"}});
}

json::object openDocument(
    const NomadString& uri,
    const NomadString& text,
    const int version = 1
) {
    return {{"jsonrpc", "2.0"}, {"method", "textDocument/didOpen"},
        {"params", {{"textDocument", {
            {"uri", uri}, {"languageId", "nomad"}, {"version", version}, {"text", text}
        }}}}};
}

} // namespace

BOOST_AUTO_TEST_SUITE(language_server)

BOOST_AUTO_TEST_CASE(init_completion_and_hover_are_limited_to_class_member_headers)
{
    TestDirectory directory("nomad_lsp_contextual_init");
    const auto uri = uriFor(directory.getPath() / "point.nomad");
    const auto query = [&](const int id, const char* method, const int line, const int character) {
        return frame({{"jsonrpc", "2.0"}, {"id", id}, {"method", method},
            {"params", {{"textDocument", {{"uri", uri}}},
                {"position", {{"line", line}, {"character", character}}}}}});
    };
    const auto text =
        "hub model\nopen class Point\n"
        "ini\n"
        "init value:float\n"
        "ini\n"
        "if true\nini\nend\n"
        "end\n"
        "fun move\nini\nend\n"
        "ini\n"
        "end\nend\n"
        "ini\n";
    std::istringstream input(start() + frame(openDocument(uri, text)) +
        query(10, "textDocument/completion", 2, 3) +
        query(11, "textDocument/hover", 3, 2) +
        query(12, "textDocument/completion", 4, 3) +
        query(13, "textDocument/completion", 6, 3) +
        query(14, "textDocument/completion", 10, 3) +
        query(15, "textDocument/completion", 12, 3) +
        query(16, "textDocument/completion", 15, 3) +
        query(17, "textDocument/completion", 3, 5) + finish());
    std::ostringstream output;
    std::ostringstream errors;
    BOOST_TEST(runLanguageServer(input, output, errors, [](const auto&) { return LanguageDiagnostics{}; }) == 0);
    std::map<std::int64_t, json::object> replies;
    for (const auto& message : responses(output.str())) {
        if (message.contains("id")) replies.emplace(message.at("id").as_int64(), message);
    }
    for (const auto id : {10, 15}) {
        const auto& items = replies.at(id).at("result").as_object().at("items").as_array();
        BOOST_REQUIRE(items.size() == 1u);
        BOOST_TEST(items.front().as_object().at("label").as_string() == "init");
        BOOST_TEST(items.front().as_object().at("kind").as_int64() == 14);
    }
    BOOST_TEST(!replies.at(11).at("result").is_null());
    for (const auto id : {12, 13, 14, 16}) {
        BOOST_TEST(replies.at(id).at("result").as_object().at("items").as_array().empty());
    }
    for (const auto& item : replies.at(17).at("result").as_object().at("items").as_array()) {
        BOOST_TEST(item.as_object().at("label").as_string() != "init");
    }
    BOOST_TEST(errors.str().empty());
}

BOOST_AUTO_TEST_CASE(initializes_full_sync_utf16_and_shutdown)
{
    std::istringstream input(start() + finish());
    std::ostringstream output;
    std::ostringstream errors;
    BOOST_TEST(runLanguageServer(input, output, errors, [](const auto&) { return LanguageDiagnostics{}; }) == 0);
    const auto messages = responses(output.str());
    BOOST_REQUIRE(messages.size() == 2U);
    const auto& capabilities = messages[0].at("result").as_object().at("capabilities").as_object();
    BOOST_TEST(capabilities.at("positionEncoding").as_string() == "utf-16");
    BOOST_TEST(capabilities.at("textDocumentSync").as_object().at("change").as_int64() == 1);
    BOOST_TEST(messages[1].at("result").is_null());
    BOOST_TEST(errors.str().empty());
}

BOOST_AUTO_TEST_CASE(synchronizes_unsaved_changes_and_clears_closed_documents)
{
    TestDirectory directory("nomad_lsp_protocol");
    const auto uri = uriFor(directory.getPath() / "answer.nomad");
    const auto change = json::object{{"jsonrpc", "2.0"}, {"method", "textDocument/didChange"},
        {"params", {{"textDocument", {{"uri", uri}, {"version", 2}}},
            {"contentChanges", json::array{json::object{{"text", "return 42"}}}}}}};
    const auto close = json::object{{"jsonrpc", "2.0"}, {"method", "textDocument/didClose"},
        {"params", {{"textDocument", {{"uri", uri}}}}}};
    std::istringstream input(start() + frame(openDocument(uri, "return missing")) +
        frame(change) + frame(close) + finish());
    std::ostringstream output;
    std::ostringstream errors;
    BOOST_TEST(runLanguageServer(input, output, errors, analyzeLanguageDocuments) == 0);
    const auto messages = responses(output.str());
    BOOST_REQUIRE(messages.size() == 5U);
    const auto& first = messages[1].at("params").as_object();
    BOOST_TEST(first.at("uri").as_string() == uri);
    BOOST_TEST(first.at("version").as_int64() == 1);
    BOOST_REQUIRE(first.at("diagnostics").as_array().size() == 1U);
    BOOST_TEST(messages[2].at("params").as_object().at("version").as_int64() == 2);
    BOOST_TEST(messages[2].at("params").as_object().at("diagnostics").as_array().empty());
    BOOST_TEST(messages[3].at("params").as_object().at("diagnostics").as_array().empty());
    BOOST_TEST(errors.str().empty());
}

BOOST_AUTO_TEST_CASE(rejects_stale_versions_and_incremental_changes_without_mutating_text)
{
    TestDirectory directory("nomad_lsp_versions");
    const auto uri = uriFor(directory.getPath() / "answer.nomad");
    const auto stale = json::object{{"jsonrpc", "2.0"}, {"method", "textDocument/didChange"},
        {"params", {{"textDocument", {{"uri", uri}, {"version", 1}}},
            {"contentChanges", json::array{json::object{{"text", "return 2"}}}}}}};
    const auto incremental = json::object{{"jsonrpc", "2.0"}, {"method", "textDocument/didChange"},
        {"params", {{"textDocument", {{"uri", uri}, {"version", 2}}},
            {"contentChanges", json::array{json::object{{"range", json::object{}}, {"text", "return 3"}}}}}}};
    auto calls = 0;
    const LanguageAnalyzer analyzer = [&](const LanguageDocuments& documents) {
        ++calls;
        BOOST_TEST(documents.at(uri).text == "return 1");
        return LanguageDiagnostics{};
    };
    std::istringstream input(start() + frame(openDocument(uri, "return 1")) +
        frame(stale) + frame(incremental) + finish());
    std::ostringstream output;
    std::ostringstream errors;
    BOOST_TEST(runLanguageServer(input, output, errors, analyzer) == 0);
    BOOST_TEST(calls == 1);
    BOOST_TEST(errors.str().find("version must increase") != NomadString::npos);
    BOOST_TEST(errors.str().find("Full text changes required") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(reports_protocol_errors_and_unknown_requests)
{
    std::istringstream input(
        "Content-Length: 1\r\n\r\n{" +
        frame({{"jsonrpc", "2.0"}, {"id", 10}, {"method", "unknown"}}) +
        start() + frame({{"jsonrpc", "2.0"}, {"id", 11}, {"method", "unknown"}}) + finish());
    std::ostringstream output;
    std::ostringstream errors;
    BOOST_TEST(runLanguageServer(input, output, errors, [](const auto&) { return LanguageDiagnostics{}; }) == 0);
    const auto messages = responses(output.str());
    BOOST_REQUIRE(messages.size() == 5U);
    BOOST_TEST(messages[0].at("error").as_object().at("code").as_int64() == -32700);
    BOOST_TEST(messages[1].at("error").as_object().at("code").as_int64() == -32002);
    BOOST_TEST(messages[3].at("error").as_object().at("code").as_int64() == -32601);
}

BOOST_AUTO_TEST_CASE(transport_rejects_bad_lengths_truncation_and_exit_without_shutdown)
{
    for (const auto* message : {
        "Content-Length: -1\r\n\r\n",
        "Content-Length: 16777217\r\n\r\n",
        "Content-Length: 2\r\nContent-Length: 2\r\n\r\n{}",
        "Content-Length: 20\r\n\r\n{}",
        "Content-Type: application/json\r\n\r\n{}"
    }) {
        std::istringstream input(message);
        std::ostringstream output;
        std::ostringstream errors;
        BOOST_TEST(runLanguageServer(input, output, errors, [](const auto&) { return LanguageDiagnostics{}; }) == 1);
        BOOST_TEST(!errors.str().empty());
    }
    std::istringstream input(frame({{"jsonrpc", "2.0"}, {"method", "exit"}}));
    std::ostringstream output;
    std::ostringstream errors;
    BOOST_TEST(runLanguageServer(input, output, errors, [](const auto&) { return LanguageDiagnostics{}; }) == 1);
}

BOOST_AUTO_TEST_CASE(analysis_failures_are_visible_and_do_not_look_successful)
{
    TestDirectory directory("nomad_lsp_failure");
    const auto uri = uriFor(directory.getPath() / "answer.nomad");
    std::istringstream input(start() + frame(openDocument(uri, "return 1")) + finish());
    std::ostringstream output;
    std::ostringstream errors;
    const LanguageAnalyzer analyzer = [](const auto&) -> LanguageDiagnostics {
        throw std::runtime_error("Analysis failed deliberately");
    };
    BOOST_TEST(runLanguageServer(input, output, errors, analyzer) == 0);
    const auto messages = responses(output.str());
    BOOST_REQUIRE(messages.size() == 4U);
    BOOST_TEST(messages[1].at("method").as_string() == "window/showMessage");
    BOOST_TEST(messages[2].at("params").as_object().at("diagnostics").as_array().size() == 1U);
    BOOST_TEST(errors.str().find("Analysis failed deliberately") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(converts_utf8_columns_to_utf16_with_crlf)
{
    TestDirectory directory("nomad_lsp_utf16");
    const auto uri = uriFor(directory.getPath() / "unicode name.nomad");
    const auto text = NomadString("value = 1\r\ntext = \"") + "\xF0\x9F\x98\x80" + "\" + @";
    const auto diagnostics = analyzeLanguageDocuments({{uri, {text, 1}}});
    BOOST_REQUIRE(diagnostics.contains(uri));
    const auto& values = diagnostics.at(uri);
    BOOST_REQUIRE(values.size() == 1U);
    const auto& position = values[0].as_object().at("range").as_object().at("start").as_object();
    BOOST_TEST(position.at("line").as_uint64() == 1U);
    BOOST_TEST(position.at("character").as_uint64() == 14U);
}

BOOST_AUTO_TEST_CASE(diagnostics_without_columns_start_at_the_beginning_of_the_line)
{
    TestDirectory directory("nomad_lsp_line_only");
    const auto uri = uriFor(directory.getPath() / "answer.nomad");
    const auto diagnostics = analyzeLanguageDocuments({{uri, {"value = 1\nreturn missing", 1}}});
    BOOST_REQUIRE(diagnostics.contains(uri));
    BOOST_REQUIRE(diagnostics.at(uri).size() == 1U);
    const auto& position = diagnostics.at(uri)[0].as_object().at("range").as_object().at("start").as_object();
    BOOST_TEST(position.at("line").as_uint64() == 1U);
    BOOST_TEST(position.at("character").as_uint64() == 0U);
}

BOOST_AUTO_TEST_CASE(project_analysis_uses_disk_and_multiple_unsaved_buffers)
{
    TestDirectory directory("nomad_lsp_project");
    directory.write("nomad.toml", R"(schema = 1
[project]
name = "LSP test"
identifier = "org.nomad.lsp"
version = "0.1.0"
executable = "lsp-test"
[game]
entry = "init"
[nomad]
version = "0.1.0"
modules = ["nomad-game"]
[resources]
directory = "res"
[scripts]
directories = ["res/scripts", "res/mods"]
)");
    const auto init = directory.write("res/scripts/init.nomad", "value = helpers.answer\nlog.info \"ready\"");
    const auto answer = directory.write("res/mods/helpers/answer.nomad", "return missing");
    const auto initUri = uriFor(init);
    const auto answerUri = uriFor(answer);
    LanguageDocuments documents{{initUri, {"value = helpers.answer\nlog.info \"ready\"", 1}}};
    const auto diskErrors = analyzeLanguageDocuments(documents);
    BOOST_TEST(!diskErrors.empty());
    documents[answerUri] = {"return 42", 2};
    BOOST_TEST(analyzeLanguageDocuments(documents).empty());
    documents[answerUri] = {"return unknown", 3};
    const auto editorErrors = analyzeLanguageDocuments(documents);
    BOOST_REQUIRE(editorErrors.contains(answerUri));
    BOOST_REQUIRE(!editorErrors.at(answerUri).empty());
}

BOOST_AUTO_TEST_CASE(completion_and_hover_use_api_metadata_on_incomplete_unsaved_text)
{
    TestDirectory directory("nomad_lsp_completion");
    const auto uri = uriFor(directory.getPath() / "answer.nomad");
    const auto text = NomadString("math.fl\r\nlog.info \"") + "\xF0\x9F\x98\x80" +
        "\"\r\nvalue = \"" + "\xF0\x9F\x98\x80" +
        "\" + toFloat 1\r\n# math.fl\r\ntext = \"math.fl\"\r\nunknown";
    const auto query = [&](const int id, const char* method, const int line, const int character) {
        return frame({{"jsonrpc", "2.0"}, {"id", id}, {"method", method},
            {"params", {{"textDocument", {{"uri", uri}}},
                {"position", {{"line", line}, {"character", character}}}}}});
    };
    const auto change = json::object{{"jsonrpc", "2.0"}, {"method", "textDocument/didChange"},
        {"params", {{"textDocument", {{"uri", uri}, {"version", 2}}},
            {"contentChanges", json::array{json::object{{"text", "math.ce"}}}}}}};
    std::istringstream input(start() + frame(openDocument(uri, text)) +
        query(10, "textDocument/completion", 0, 7) +
        query(11, "textDocument/hover", 2, 19) +
        query(12, "textDocument/completion", 3, 9) +
        query(13, "textDocument/hover", 4, 12) +
        query(14, "textDocument/hover", 5, 3) +
        query(15, "textDocument/hover", 1, 4) +
        query(16, "textDocument/completion", 0, 100) +
        query(18, "textDocument/completion", 0, 0) +
        frame(change) + query(17, "textDocument/completion", 0, 7) + finish());
    std::ostringstream output;
    std::ostringstream errors;
    BOOST_TEST(runLanguageServer(input, output, errors, [](const auto&) { return LanguageDiagnostics{}; }) == 0);
    const auto messages = responses(output.str());
    std::map<std::int64_t, json::object> replies;
    for (const auto& message : messages) {
        if (message.contains("id")) replies.emplace(message.at("id").as_int64(), message);
    }
    const auto& capabilities = replies.at(1).at("result").as_object().at("capabilities").as_object();
    BOOST_TEST(capabilities.at("hoverProvider").as_bool());
    BOOST_TEST(capabilities.contains("completionProvider"));
    const auto& items = replies.at(10).at("result").as_object().at("items").as_array();
    BOOST_REQUIRE(items.size() == 1U);
    const auto& item = items[0].as_object();
    BOOST_TEST(item.at("label").as_string() == "math.floor");
    BOOST_TEST(!item.at("documentation").as_object().at("value").as_string().empty());
    const auto& edit = item.at("textEdit").as_object();
    BOOST_TEST(edit.at("newText").as_string() == "math.floor");
    BOOST_TEST(edit.at("range").as_object().at("start").as_object().at("character").as_int64() == 0);
    BOOST_TEST(edit.at("range").as_object().at("end").as_object().at("character").as_int64() == 7);
    const auto& hover = replies.at(11).at("result").as_object();
    const auto markdown = NomadString(hover.at("contents").as_object().at("value").as_string());
    BOOST_TEST(markdown.find("toFloat") != NomadString::npos);
    BOOST_TEST(markdown.find("---") != NomadString::npos);
    BOOST_TEST(hover.at("range").as_object().at("start").as_object().at("character").as_int64() == 15);
    BOOST_TEST(hover.at("range").as_object().at("end").as_object().at("character").as_int64() == 22);
    std::set<NomadString> labels;
    for (const auto& value : replies.at(18).at("result").as_object().at("items").as_array()) {
        labels.insert(NomadString(value.as_object().at("label").as_string()));
    }
    BOOST_TEST(labels.contains("if"));
    BOOST_TEST(!labels.contains("int"));
    BOOST_TEST(!labels.contains("game.createScene"));
    BOOST_TEST(replies.at(12).at("result").as_object().at("items").as_array().empty());
    BOOST_TEST(replies.at(13).at("result").is_null());
    BOOST_TEST(replies.at(14).at("result").is_null());
    const auto logDocumentation = json::serialize(replies.at(15).at("result"));
    BOOST_TEST(logDocumentation.find("$file") == NomadString::npos);
    BOOST_TEST(logDocumentation.find("$line") == NomadString::npos);
    BOOST_TEST(replies.at(16).at("error").as_object().at("code").as_int64() == -32602);
    const auto& updated = replies.at(17).at("result").as_object().at("items").as_array();
    BOOST_REQUIRE(updated.size() == 1U);
    BOOST_TEST(updated[0].as_object().at("label").as_string() == "math.ceiling");
}

BOOST_AUTO_TEST_CASE(project_completion_includes_engine_api_without_compiling_the_script)
{
    TestDirectory directory("nomad_lsp_engine_completion");
    directory.write("nomad.toml", R"(schema = 1
[project]
name = "LSP completion"
identifier = "org.nomad.completion"
version = "0.1.0"
executable = "completion"
[game]
entry = "init"
[nomad]
version = "0.1.0"
modules = ["nomad-game"]
[scripts]
directories = ["res/scripts"]
[resources]
directory = "res"
)");
    const auto uri = uriFor(directory.write("res/scripts/init.nomad", "game."));
    const auto request = [&](const int id, const char* method, const int character) {
        return frame({{"jsonrpc", "2.0"}, {"id", id}, {"method", method},
            {"params", {{"textDocument", {{"uri", uri}}},
                {"position", {{"line", 0}, {"character", character}}}}}});
    };
    std::istringstream input(start() + frame(openDocument(uri, "game.createScene")) +
        request(10, "textDocument/completion", 5) + request(11, "textDocument/hover", 8) + finish());
    std::ostringstream output;
    std::ostringstream errors;
    BOOST_TEST(runLanguageServer(input, output, errors, [](const auto&) { return LanguageDiagnostics{}; }) == 0);
    const auto messages = responses(output.str());
    BOOST_REQUIRE(messages.size() == 5U);
    BOOST_REQUIRE_MESSAGE(errors.str().empty(), errors.str());
    const auto& items = messages[2].at("result").as_object().at("items").as_array();
    BOOST_REQUIRE(!items.empty());
    for (const auto& item : items) {
        BOOST_TEST(NomadString(item.as_object().at("label").as_string()).starts_with("game."));
        BOOST_TEST(item.as_object().at("textEdit").as_object().at("range").as_object()
            .at("end").as_object().at("character").as_int64() == 16);
    }
    const auto hoverText = json::serialize(messages[3].at("result"));
    BOOST_TEST(hoverText.find("game.createScene") != NomadString::npos);
    BOOST_TEST(errors.str().empty());
}

BOOST_AUTO_TEST_CASE(project_analysis_and_completion_respect_configured_modules)
{
    TestDirectory directory("nomad_lsp_system_project");
    directory.write("nomad.toml", R"(schema = 1
[project]
name = "System project"
identifier = "org.nomad.system-project"
version = "0.1.0"
executable = "system-project"
[nomad]
version = "0.1.0"
modules = ["nomad-system"]
[scripts]
directories = ["res/scripts"]
[resources]
directory = "res"
)");
    const auto valid = NomadString("value = math.floor 1.5\nlog.info \"ready\"\nreturn value");
    const auto uri = uriFor(directory.write("res/scripts/init.nomad", valid));
    BOOST_TEST(analyzeLanguageDocuments({{uri, {valid, 1}}}).empty());
    const auto diagnostics = analyzeLanguageDocuments({{uri, {"game.createScene", 2}}});
    BOOST_REQUIRE(diagnostics.contains(uri));
    BOOST_TEST(!diagnostics.at(uri).empty());
    const auto query = [&](const int id, const int line, const int character) {
        return frame({{"jsonrpc", "2.0"}, {"id", id}, {"method", "textDocument/completion"},
            {"params", {{"textDocument", {{"uri", uri}}},
                {"position", {{"line", line}, {"character", character}}}}}});
    };
    std::istringstream input(start() + frame(openDocument(uri, "game.\nmath.fl")) +
        query(10, 0, 5) + query(11, 1, 7) + finish());
    std::ostringstream output;
    std::ostringstream errors;
    BOOST_TEST(runLanguageServer(input, output, errors, [](const auto&) { return LanguageDiagnostics{}; }) == 0);
    BOOST_REQUIRE_MESSAGE(errors.str().empty(), errors.str());
    std::map<std::int64_t, json::object> replies;
    for (const auto& message : responses(output.str())) {
        if (message.contains("id")) replies.emplace(message.at("id").as_int64(), message);
    }
    // Unknown receivers retain native candidates, but must not expose unconfigured game APIs.
    for (const auto& item : replies.at(10).at("result").as_object().at("items").as_array()) {
        const auto& candidate = item.as_object();
        BOOST_TEST(!NomadString(candidate.at("label").as_string()).starts_with("game."));
        const auto kind = candidate.at("kind").as_int64();
        BOOST_TEST((kind == 2 || kind == 10));
    }
    const auto& items = replies.at(11).at("result").as_object().at("items").as_array();
    BOOST_REQUIRE(items.size() == 1U);
    BOOST_TEST(items[0].as_object().at("label").as_string() == "math.floor");
}

BOOST_AUTO_TEST_CASE(project_symbols_follow_disk_editor_changes_and_close)
{
    TestDirectory directory("nomad_lsp_project_symbols");
    directory.write("nomad.toml", R"(schema = 1
[project]
name = "Project symbols"
identifier = "org.nomad.symbols"
version = "0.1.0"
executable = "symbols"
[game]
entry = "init"
[nomad]
version = "0.1.0"
modules = ["nomad-game"]
[scripts]
directories = ["res/scripts"]
[resources]
directory = "res"
)");
    const auto initUri = uriFor(directory.write("res/scripts/init.nomad", "log.info \"ready\""));
    const auto libraryUri = uriFor(directory.write("res/scripts/helpers/library.nomad",
        "fun doubleValue value:int\nreturn value * 2\nend"));
    const auto request = [&](const int id, const char* method, const int character) {
        return frame({{"jsonrpc", "2.0"}, {"id", id}, {"method", method},
            {"params", {{"textDocument", {{"uri", initUri}}},
                {"position", {{"line", 0}, {"character", character}}}}}});
    };
    const auto change = [&](const NomadString& uri, const int version, const NomadString& text) {
        return frame({{"jsonrpc", "2.0"}, {"method", "textDocument/didChange"},
            {"params", {{"textDocument", {{"uri", uri}, {"version", version}}},
                {"contentChanges", json::array{json::object{{"text", text}}}}}}});
    };
    std::istringstream input(start() + frame(openDocument(initUri, "doubleValue")) +
        request(10, "textDocument/completion", 0) + request(11, "textDocument/hover", 4) +
        frame(openDocument(libraryUri, "fun tripleValue value:int\nreturn value * 3\nend")) +
        request(12, "textDocument/completion", 0) +
        change(libraryUri, 2, "fun quadrupleValue value:int\nreturn value * 4\nend") +
        request(13, "textDocument/completion", 0) +
        frame({{"jsonrpc", "2.0"}, {"method", "textDocument/didClose"},
            {"params", {{"textDocument", {{"uri", libraryUri}}}}}}) +
        request(14, "textDocument/completion", 0) +
        change(initUri, 2, "helpers.lib") + request(15, "textDocument/completion", 11) + finish());
    std::ostringstream output;
    std::ostringstream errors;
    BOOST_TEST(runLanguageServer(input, output, errors, [](const auto&) { return LanguageDiagnostics{}; }) == 0);
    BOOST_REQUIRE_MESSAGE(errors.str().empty(), errors.str());
    std::map<std::int64_t, json::object> replies;
    for (const auto& message : responses(output.str())) {
        if (message.contains("id")) replies.emplace(message.at("id").as_int64(), message);
    }
    const auto labels = [&](const int id) {
        std::set<NomadString> names;
        for (const auto& item : replies.at(id).at("result").as_object().at("items").as_array()) {
            names.emplace(item.as_object().at("label").as_string());
        }
        return names;
    };
    BOOST_TEST(labels(10).contains("doubleValue"));
    BOOST_TEST(labels(10).contains("helpers.library"));
    const auto hover = json::serialize(replies.at(11).at("result"));
    BOOST_TEST(hover.find("value:int") != NomadString::npos);
    BOOST_TEST(hover.find("Defined in") != NomadString::npos);
    BOOST_TEST(labels(12).contains("tripleValue"));
    BOOST_TEST(!labels(12).contains("doubleValue"));
    BOOST_TEST(labels(13).contains("quadrupleValue"));
    BOOST_TEST(!labels(13).contains("tripleValue"));
    BOOST_TEST(labels(14).contains("doubleValue"));
    BOOST_TEST(!labels(14).contains("quadrupleValue"));
    BOOST_TEST(labels(15).contains("helpers.library"));
}

BOOST_AUTO_TEST_CASE(class_member_scopes_expose_parameters_locals_and_receiver)
{
    TestDirectory directory("nomad_lsp_member_scopes");
    const auto uri = uriFor(directory.getPath() / "counter.nomad");
    const auto text =
        "class Counter\nvalue = 0\n"
        "init amount:int\nlocal = amount + 1\nthis.value = local\nreceiver = this\nend\n"
        "fun read delta:int\nresult = this.value + delta\nreturn result\nend\nend\n"
        "counter = new Counter 4\nreturn counter.read 3";
    const auto query = [&](const int id, const char* method, const int line, const int character) {
        return frame({{"jsonrpc", "2.0"}, {"id", id}, {"method", method},
            {"params", {{"textDocument", {{"uri", uri}}},
                {"position", {{"line", line}, {"character", character}}}}}});
    };
    std::istringstream input(start() + frame(openDocument(uri, text)) +
        query(10, "textDocument/completion", 4, 0) +
        query(11, "textDocument/completion", 9, 0) +
        query(12, "textDocument/hover", 3, 10) +
        query(13, "textDocument/hover", 8, 24) +
        query(14, "textDocument/hover", 5, 12) +
        query(15, "textDocument/completion", 13, 0) + finish());
    std::ostringstream output;
    std::ostringstream errors;
    BOOST_TEST(runLanguageServer(input, output, errors, [](const auto&) { return LanguageDiagnostics{}; }) == 0);
    BOOST_REQUIRE_MESSAGE(errors.str().empty(), errors.str());
    std::map<std::int64_t, json::object> replies;
    for (const auto& message : responses(output.str())) {
        if (message.contains("id")) replies.emplace(message.at("id").as_int64(), message);
    }
    const auto labels = [&](const int id) {
        std::set<NomadString> names;
        for (const auto& item : replies.at(id).at("result").as_object().at("items").as_array()) {
            names.emplace(item.as_object().at("label").as_string());
        }
        return names;
    };
    BOOST_TEST(labels(10).contains("amount"));
    BOOST_TEST(labels(10).contains("local"));
    BOOST_TEST(labels(10).contains("this"));
    BOOST_TEST(!labels(10).contains("delta"));
    BOOST_TEST(labels(11).contains("delta"));
    BOOST_TEST(labels(11).contains("result"));
    BOOST_TEST(labels(11).contains("this"));
    BOOST_TEST(!labels(11).contains("amount"));
    BOOST_TEST(!labels(15).contains("this"));
    BOOST_TEST(!labels(15).contains("delta"));
    BOOST_TEST(json::serialize(replies.at(12).at("result")).find("amount:int") != NomadString::npos);
    BOOST_TEST(json::serialize(replies.at(13).at("result")).find("delta:int") != NomadString::npos);
    BOOST_TEST(json::serialize(replies.at(14).at("result")).find("this:Counter") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(standalone_symbols_remain_isolated_between_documents)
{
    TestDirectory directory("nomad_lsp_standalone_symbols");
    const auto firstUri = uriFor(directory.getPath() / "first.nomad");
    const auto secondUri = uriFor(directory.getPath() / "second.nomad");
    const auto request = [&](const int id, const NomadString& uri) {
        return frame({{"jsonrpc", "2.0"}, {"id", id}, {"method", "textDocument/completion"},
            {"params", {{"textDocument", {{"uri", uri}}},
                {"position", {{"line", 0}, {"character", 0}}}}}});
    };
    std::istringstream input(start() +
        frame(openDocument(firstUri, "fun firstFunction count:int\nreturn count\nend\nunknown")) +
        frame(openDocument(secondUri, "fun secondFunction\nend")) +
        request(10, firstUri) + request(11, secondUri) + finish());
    std::ostringstream output;
    std::ostringstream errors;
    BOOST_TEST(runLanguageServer(input, output, errors, [](const auto&) { return LanguageDiagnostics{}; }) == 0);
    BOOST_REQUIRE_MESSAGE(errors.str().empty(), errors.str());
    for (const auto& message : responses(output.str())) {
        if (!message.contains("id") || message.at("id").as_int64() < 10) continue;
        const auto first = message.at("id").as_int64() == 10;
        const auto serialized = json::serialize(message.at("result"));
        BOOST_TEST(serialized.find(first ? "firstFunction" : "secondFunction") != NomadString::npos);
        BOOST_TEST(serialized.find(first ? "secondFunction" : "firstFunction") == NomadString::npos);
    }
}

BOOST_AUTO_TEST_CASE(completion_includes_variables_namespaces_and_native_candidates)
{
    TestDirectory directory("nomad_lsp_member_suggestions");
    directory.write("nomad.toml", R"(schema = 1
[project]
name = "Member suggestions"
identifier = "org.nomad.members"
version = "0.1.0"
executable = "members"
[game]
entry = "init"
[nomad]
version = "0.1.0"
modules = ["nomad-game"]
[scripts]
directories = ["res/scripts"]
[resources]
directory = "res"
)");
    const auto uri = uriFor(directory.write("res/scripts/init.nomad", ""));
    const auto text = NomadString("\nthis.\nvalue.tr\nali\nthis.name.\nthis.name.tr");
    NomadString requests;
    const std::vector<int> characters{0, 5, 8, 3, 10, 12};
    for (auto line = 0; line < 6; ++line) {
        const auto character = characters[static_cast<std::size_t>(line)];
        requests += frame({{"jsonrpc", "2.0"}, {"id", 10 + line}, {"method", "textDocument/completion"},
            {"params", {{"textDocument", {{"uri", uri}}},
                {"position", {{"line", line}, {"character", character}}}}}});
    }
    std::istringstream input(start() + frame(openDocument(uri, text)) + requests + finish());
    std::ostringstream output;
    std::ostringstream errors;
    BOOST_TEST(runLanguageServer(input, output, errors, [](const auto&) { return LanguageDiagnostics{}; }) == 0);
    BOOST_REQUIRE_MESSAGE(errors.str().empty(), errors.str());
    std::map<std::int64_t, json::array> results;
    for (const auto& message : responses(output.str())) {
        if (message.contains("id") && message.at("id").as_int64() >= 10) {
            results.emplace(message.at("id").as_int64(),
                message.at("result").as_object().at("items").as_array());
        }
    }
    std::set<NomadString> blankLabels;
    for (const auto& item : results.at(10)) blankLabels.emplace(item.as_object().at("label").as_string());
    BOOST_TEST(blankLabels.contains("game."));
    BOOST_TEST(blankLabels.contains("this."));
    BOOST_TEST(!blankLabels.contains("alignment"));
    auto variableFound = false;
    for (const auto& item : results.at(11)) {
        const auto& value = item.as_object();
        if (value.at("kind").as_int64() == 6) {
            variableFound = true;
            BOOST_TEST(NomadString(value.at("label").as_string()).starts_with("this."));
            BOOST_TEST(blankLabels.contains(NomadString(value.at("label").as_string())));
        }
    }
    BOOST_TEST(variableFound);
    auto trimFound = false;
    for (const auto& item : results.at(12)) {
        const auto& value = item.as_object();
        if (value.at("label").as_string() == "trim") {
            trimFound = true;
            BOOST_TEST(value.at("textEdit").as_object().at("newText").as_string() == "value.trim");
            BOOST_TEST(value.at("filterText").as_string() == "value.trim");
            BOOST_TEST(NomadString(value.at("detail").as_string()).starts_with("string.trim"));
        }
    }
    BOOST_TEST(trimFound);
    BOOST_REQUIRE(!results.at(13).empty());
    BOOST_TEST(results.at(13)[0].as_object().at("label").as_string() == "alignment");
    for (const auto id : {14, 15}) {
        BOOST_REQUIRE(!results.at(id).empty());
        auto nameTrimFound = false;
        for (const auto& item : results.at(id)) {
            const auto& value = item.as_object();
            const auto replacement = NomadString(value.at("textEdit").as_object().at("newText").as_string());
            BOOST_TEST(replacement.starts_with("this.name."));
            BOOST_TEST(value.at("filterText").as_string() == replacement);
            if (value.at("label").as_string() == "trim") nameTrimFound = true;
        }
        BOOST_TEST(nameTrimFound);
    }
}

BOOST_AUTO_TEST_CASE(loop_completion_shows_ordinary_bindings_but_not_hidden_iterators)
{
    TestDirectory directory("nomad_lsp_loop_scope");
    const auto uri = uriFor(directory.getPath() / "loops.nomad");
    const NomadString source =
        "total = 0\n"
        "for value in range 1 4\n"
        "    total = total + value\n"
        "end\n"
        "total = total + 1";
    const auto query = [&](const int id, const int line) {
        return frame({{"jsonrpc", "2.0"}, {"id", id}, {"method", "textDocument/completion"},
            {"params", {{"textDocument", {{"uri", uri}}},
                {"position", {{"line", line}, {"character", 0}}}}}});
    };
    std::istringstream input(start() + frame(openDocument(uri, source)) +
        query(10, 2) + query(11, 4) + finish());
    std::ostringstream output;
    std::ostringstream errors;
    BOOST_TEST(runLanguageServer(input, output, errors, [](const auto&) { return LanguageDiagnostics{}; }) == 0);
    BOOST_REQUIRE_MESSAGE(errors.str().empty(), errors.str());
    for (const auto& message : responses(output.str())) {
        if (!message.contains("id")) continue;
        const auto id = message.at("id").as_int64();
        if (id != 10 && id != 11) continue;
        auto found = false;
        for (const auto& item : message.at("result").as_object().at("items").as_array()) {
            const NomadString name{item.as_object().at("label").as_string()};
            BOOST_TEST(name.find('@') == NomadString::npos);
            if (name == "value") found = true;
        }
        BOOST_TEST(found);
    }
}

BOOST_AUTO_TEST_CASE(local_completion_uses_compiler_scope_and_types_on_an_incomplete_line)
{
    TestDirectory directory("nomad_lsp_local_symbols");
    const auto uri = uriFor(directory.getPath() / "locals.nomad");
    const auto text = NomadString(
        "fun first title:string\n"
        "name = title\n"
        "count = 42\n"
        "name.\n"
        "end\n"
        "fun second other:int\n"
        "value = other + 1\n"
        "end");
    const auto query = [&](const int id, const char* method, const int line, const int character) {
        return frame({{"jsonrpc", "2.0"}, {"id", id}, {"method", method},
            {"params", {{"textDocument", {{"uri", uri}}},
                {"position", {{"line", line}, {"character", character}}}}}});
    };
    const auto change = [&](const int version, const NomadString& source) {
        return frame({{"jsonrpc", "2.0"}, {"method", "textDocument/didChange"},
            {"params", {{"textDocument", {{"uri", uri}, {"version", version}}},
                {"contentChanges", json::array{json::object{{"text", source}}}}}}});
    };
    const auto valid = NomadString(
        "fun first title:string\n"
        "name = title\n"
        "count = 42\n"
        "name = name.trim\n"
        "end\n"
        "fun second other:int\n"
        "value = other + 1\n"
        "end");
    std::istringstream input(start() + frame(openDocument(uri, text)) +
        query(10, "textDocument/completion", 3, 5) +
        change(2, valid) + query(11, "textDocument/completion", 3, 0) +
        query(12, "textDocument/hover", 3, 2) + query(13, "textDocument/hover", 3, 14) +
        query(14, "textDocument/completion", 6, 0) +
        change(3, "fun first title:string\ncount = 42\ncount.\nend") +
        query(16, "textDocument/completion", 2, 6) +
        change(4, "fun first title:string\ntitle.\nend") +
        query(17, "textDocument/completion", 1, 6) +
        change(5, "name = \"Nomad\"\nname.length.\n") +
        query(18, "textDocument/completion", 1, 12) +
        change(6, "name = \"Nomad\"\ncount = name.length\n") +
        query(19, "textDocument/hover", 1, 16) +
        change(7, "name = 42\nname.\n") +
        query(20, "textDocument/completion", 1, 5) +
        change(8, valid) + query(21, "textDocument/completion", 0, 12) + finish());
    std::ostringstream output;
    std::ostringstream errors;
    BOOST_TEST(runLanguageServer(input, output, errors, [](const auto&) { return LanguageDiagnostics{}; }) == 0);
    BOOST_REQUIRE_MESSAGE(errors.str().empty(), errors.str());
    std::map<std::int64_t, json::object> replies;
    for (const auto& message : responses(output.str())) {
        if (message.contains("id")) replies.emplace(message.at("id").as_int64(), message);
    }
    const auto labels = [&](const int id) {
        std::set<NomadString> names;
        for (const auto& item : replies.at(id).at("result").as_object().at("items").as_array()) {
            names.emplace(item.as_object().at("label").as_string());
        }
        return names;
    };
    BOOST_TEST(labels(10).contains("trim"));
    BOOST_TEST(labels(10).contains("length"));
    BOOST_TEST(!labels(10).contains("play"));
    BOOST_TEST(labels(11).contains("name"));
    BOOST_TEST(labels(11).contains("count"));
    BOOST_TEST(labels(11).contains("title"));
    BOOST_TEST(!labels(11).contains("other"));
    BOOST_TEST(labels(14).contains("other"));
    BOOST_TEST(!labels(14).contains("name"));
    BOOST_TEST(!labels(14).contains("title"));
    BOOST_TEST(json::serialize(replies.at(12).at("result")).find("name:string") != NomadString::npos);
    BOOST_TEST(json::serialize(replies.at(13).at("result")).find("string.trim") != NomadString::npos);
    BOOST_TEST(!labels(16).contains("trim"));
    BOOST_TEST(labels(17).contains("trim"));
    BOOST_TEST(!labels(17).contains("play"));
    BOOST_TEST(!labels(18).contains("trim"));
    BOOST_TEST(json::serialize(replies.at(19).at("result")).find("string.length:int") != NomadString::npos);
    BOOST_TEST(!labels(20).contains("trim"));
    BOOST_TEST(labels(21).contains("title"));
}

BOOST_AUTO_TEST_SUITE_END()
