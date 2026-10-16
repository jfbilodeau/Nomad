// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/lsp/LanguageServer.hpp>

#include <nomad/system/Path.hpp>

#include <TestDirectory.hpp>

#include <boost/json.hpp>

#include <boost/test/unit_test.hpp>

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
entry = "init"
[nomad]
version = "0.1.0"
[resources]
directory = "res"
[scripts]
directories = ["scripts", "mods"]
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

BOOST_AUTO_TEST_SUITE_END()
