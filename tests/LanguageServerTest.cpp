// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/lsp/LanguageServer.hpp>

#include <nomad/system/Path.hpp>

#include <TestDirectory.hpp>

#include <boost/json.hpp>

#include <boost/test/unit_test.hpp>

#include <algorithm>
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

NomadString symbolRequest(
    const int id,
    const char* method,
    const NomadString& uri,
    const NomadString& source,
    const NomadString& word,
    const std::size_t offset
) {
    auto index = source.find(word);
    BOOST_REQUIRE(index != NomadString::npos);
    index += offset;
    BOOST_REQUIRE(index <= source.size());
    const auto line = std::count(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(index), '\n');
    const auto newline = index == 0 ? NomadString::npos : source.rfind('\n', index - 1);
    auto character = std::size_t{0};
    for (auto byte = newline == NomadString::npos ? std::size_t{0} : newline + 1; byte < index; ++byte) {
        const auto value = static_cast<unsigned char>(source[byte]);
        if (value >= 0xF0) character += 2;
        else if (value < 0x80 || value >= 0xC0) ++character;
    }
    return frame({{"jsonrpc", "2.0"}, {"id", id}, {"method", method},
        {"params", {{"textDocument", {{"uri", uri}}},
            {"position", {{"line", line}, {"character", character}}}}}});
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
    const auto& signatureHelp = capabilities.at("signatureHelpProvider").as_object();
    BOOST_TEST(signatureHelp.at("triggerCharacters").as_array().front().as_string() == " ");
    BOOST_TEST(capabilities.at("textDocumentSync").as_object().at("change").as_int64() == 1);
    BOOST_TEST(messages[1].at("result").is_null());
    BOOST_TEST(errors.str().empty());
}

BOOST_AUTO_TEST_CASE(signature_help_tracks_expression_arguments_and_nested_calls)
{
    TestDirectory directory("nomad_lsp_signature_expressions");
    const auto uri = uriFor(directory.getPath() / "calls.nomad");
    const NomadString source =
        "fun combine first:int second:int third:int\nreturn first + second + third\nend\n"
        "fun increment value:int\nreturn value + 1\nend\n"
        "fun join first:string second:string\nreturn $\"{first}{second}\"\nend\n"
        "total = combine 1 + 2 (increment 3) 4\n"
        "nested = combine increment 5 6 7\n"
        "words = join \"one two\" \"three\"\n"
        "unicode = join \"\xF0\x9F\x8C\x8D\" \"world\"\n"
        "unfinished = combine (1 + 2) 3 4\n"
        "# combine 1 \n"
        "text = \"combine 1 \"\n"
        "# unknown.read 1\n"
        "closed = (increment 1)\n";
    const auto diagnostics = analyzeLanguageDocuments({{uri, {source, 1}}});
    BOOST_REQUIRE_MESSAGE(diagnostics.empty(), (diagnostics.empty() ? "" : json::serialize(diagnostics.begin()->second)));
    const auto inlineSource = source + "inline = combine 1 fun\n";
    const auto change = json::object{{"jsonrpc", "2.0"}, {"method", "textDocument/didChange"},
        {"params", {{"textDocument", {{"uri", uri}, {"version", 2}}},
            {"contentChanges", json::array{json::object{{"text", inlineSource}}}}}}};
    const auto query = [&](const int id, const NomadString& word, const std::size_t offset) {
        return symbolRequest(id, "textDocument/signatureHelp", uri, source, word, offset);
    };
    std::istringstream input(start() + frame(openDocument(uri, source)) +
        query(10, "combine 1 + 2", 8) +
        query(11, "combine 1 + 2", 12) +
        query(12, "combine 1 + 2 ", 14) +
        query(13, "combine 1 + 2 (increment ", 25) +
        query(14, "combine 1 + 2 (increment 3) ", 28) +
        query(15, "increment 5 6", 12) +
        query(16, "increment 5 6", 11) +
        query(17, "join \"one two\" ", 15) +
        query(18, "join \"\xF0\x9F\x8C\x8D\" ", 12) +
        query(19, "unfinished = combine (1 +", 25) +
        query(20, "# combine 1 ", 12) +
        query(21, "\"combine 1 \"", 10) +
        query(22, "unknown.read 1", 14) +
        query(23, "fun combine", 11) +
        query(24, "closed = (increment 1)", 22) +
        frame(change) + symbolRequest(25, "textDocument/signatureHelp", uri, inlineSource, "inline = combine 1 fun", 22) +
        frame({{"jsonrpc", "2.0"}, {"id", 26}, {"method", "textDocument/signatureHelp"},
            {"params", {{"textDocument", {{"uri", uri}}},
                {"position", {{"line", 12}, {"character", 17}}}}}}) + finish());
    std::ostringstream output;
    std::ostringstream errors;
    BOOST_TEST(runLanguageServer(input, output, errors, [](const auto&) { return LanguageDiagnostics{}; }) == 0);
    std::map<std::int64_t, json::object> replies;
    for (const auto& message : responses(output.str())) {
        if (message.contains("id")) replies.emplace(message.at("id").as_int64(), message);
    }
    for (const auto id : {10, 11, 12, 14, 15, 19}) {
        const auto& result = replies.at(id).at("result").as_object();
        const auto& signatures = result.at("signatures").as_array();
        BOOST_REQUIRE(signatures.size() == 1u);
        BOOST_TEST(signatures.front().as_object().at("label").as_string() ==
            "combine first:int second:int third:int -> int");
        const auto expected = id == 12 || id == 15 ? 1 : id == 14 ? 2 : 0;
        BOOST_TEST(result.at("activeParameter").as_int64() == expected);
    }
    for (const auto id : {13, 16}) {
        const auto& result = replies.at(id).at("result").as_object();
        BOOST_TEST(result.at("signatures").as_array().front().as_object().at("label").as_string() ==
            "increment value:int -> int");
        BOOST_TEST(result.at("activeParameter").as_int64() == 0);
    }
    for (const auto id : {17, 18}) {
        BOOST_TEST(replies.at(id).at("result").as_object().at("activeParameter").as_int64() == 1);
    }
    for (const auto id : {20, 21, 22, 23, 24, 25}) BOOST_TEST(replies.at(id).at("result").is_null());
    BOOST_TEST(replies.at(26).at("error").as_object().at("code").as_int64() == -32602);
    BOOST_TEST(errors.str().find("Position splits a UTF-16 character") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(signature_help_lists_overloads_and_filters_member_receivers)
{
    TestDirectory directory("nomad_lsp_signature_overloads");
    const auto uri = uriFor(directory.getPath() / "members.nomad");
    const NomadString source =
        "fun chooseTyped value:string\nreturn value\nend\n"
        "hub model\nclass Widget\n"
        "init number:int label:string\nthis.value = number\nthis.label = label\nend\n"
        "init label:string\nthis.label = label\nend\n"
        "fun read amount:int suffix:string\nreturn this.value + amount\nend\n"
        "fun read amount:string suffix:string\nreturn $\"{amount}{suffix}\"\nend\n"
        "fun inspect\nreturn this.read 1 \"x\"\nend\nend\nend\n"
        "class Other\nfun read amount:float\nreturn amount\nend\nend\n"
        "class Owner\nchild = new model.Widget 1 \"first\"\nend\n"
        "widget = new model.Widget 2 \"second\"\nowner = new Owner\n"
        "readValue = owner.child.read 3 \"third\"\n"
        "text = \" hello \"\ntrimmed = text.trim\n"
        "converted = toFloat \"2\"\n"
        "sequence = range 1 4 2\n"
        "log.info \"message\"\n"
        "selected = chooseTyped \"text\"\n";
    const auto diagnostics = analyzeLanguageDocuments({{uri, {source, 1}}});
    BOOST_REQUIRE_MESSAGE(diagnostics.empty(), (diagnostics.empty() ? "" : json::serialize(diagnostics.begin()->second)));
    const auto unknownSource = source + "unknown.read 1\n";
    const auto change = json::object{{"jsonrpc", "2.0"}, {"method", "textDocument/didChange"},
        {"params", {{"textDocument", {{"uri", uri}, {"version", 2}}},
            {"contentChanges", json::array{json::object{{"text", unknownSource}}}}}}};
    const auto query = [&](const int id, const NomadString& word, const std::size_t offset) {
        return symbolRequest(id, "textDocument/signatureHelp", uri, source, word, offset);
    };
    std::istringstream input(start() + frame(openDocument(uri, source)) +
        query(10, "owner.child.read 3 ", 19) +
        query(11, "this.read 1 ", 12) +
        query(12, "widget = new model.Widget 2 ", 28) +
        query(13, "text.trim", 9) +
        query(14, "toFloat \"2\"", 11) +
        query(15, "range 1 4 ", 10) +
        query(16, "log.info \"message\"", 18) +
        query(18, "chooseTyped \"text\"", 18) + frame(change) +
        symbolRequest(17, "textDocument/signatureHelp", uri, unknownSource, "unknown.read 1", 14) + finish());
    std::ostringstream output;
    std::ostringstream errors;
    BOOST_TEST(runLanguageServer(input, output, errors, [](const auto&) { return LanguageDiagnostics{}; }) == 0);
    std::map<std::int64_t, json::object> replies;
    for (const auto& message : responses(output.str())) {
        if (message.contains("id")) replies.emplace(message.at("id").as_int64(), message);
    }
    for (const auto id : {10, 11, 12, 13, 14, 15, 16, 18}) {
        BOOST_REQUIRE_MESSAGE(replies.at(id).at("result").is_object(), json::serialize(replies.at(id)));
    }
    for (const auto id : {10, 11}) {
        const auto& result = replies.at(id).at("result").as_object();
        const auto& signatures = result.at("signatures").as_array();
        BOOST_REQUIRE(signatures.size() == 2u);
        BOOST_TEST(result.at("activeParameter").as_int64() == 1);
        for (const auto& signature : signatures) {
            const auto label = NomadString(signature.as_object().at("label").as_string());
            BOOST_TEST(label.starts_with("model.Widget.read "));
            BOOST_TEST(label.find("$init") == NomadString::npos);
            BOOST_TEST(label.find("Other") == NomadString::npos);
        }
        const auto selected = result.at("activeSignature").as_int64();
        BOOST_TEST(NomadString(signatures[static_cast<std::size_t>(selected)].as_object().at("label").as_string())
            .find("amount:int") != NomadString::npos);
    }
    const auto& constructors = replies.at(12).at("result").as_object();
    BOOST_REQUIRE(constructors.at("signatures").as_array().size() == 2u);
    BOOST_TEST(constructors.at("activeParameter").as_int64() == 1);
    for (const auto& signature : constructors.at("signatures").as_array()) {
        const auto label = NomadString(signature.as_object().at("label").as_string());
        BOOST_TEST(label.starts_with("new model.Widget "));
        BOOST_TEST(label.ends_with(" -> model.Widget"));
    }
    const auto& trim = replies.at(13).at("result").as_object();
    BOOST_TEST(trim.at("signatures").as_array().front().as_object().at("label").as_string() == "string.trim -> string");
    BOOST_TEST(!trim.contains("activeParameter"));
    const auto& convert = replies.at(14).at("result").as_object();
    BOOST_REQUIRE(convert.at("signatures").as_array().size() == 2u);
    BOOST_TEST(NomadString(convert.at("signatures").as_array()[
        static_cast<std::size_t>(convert.at("activeSignature").as_int64())].as_object().at("label").as_string())
        .find("value:string") != NomadString::npos);
    const auto& range = replies.at(15).at("result").as_object();
    BOOST_REQUIRE(range.at("signatures").as_array().size() == 3u);
    BOOST_TEST(range.at("activeParameter").as_int64() == 2);
    const auto log = json::serialize(replies.at(16).at("result"));
    BOOST_TEST(log.find("message:string") != NomadString::npos);
    BOOST_TEST(log.find("$file") == NomadString::npos);
    BOOST_TEST(log.find("$line") == NomadString::npos);
    BOOST_TEST(log.find("$function") == NomadString::npos);
    const auto& message = replies.at(16).at("result").as_object().at("signatures").as_array().front().as_object()
        .at("parameters").as_array().front().as_object();
    BOOST_TEST(!message.at("documentation").as_object().at("value").as_string().empty());
    BOOST_TEST(replies.at(17).at("result").is_null());
    const auto& scriptOverloads = replies.at(18).at("result").as_object();
    BOOST_REQUIRE(scriptOverloads.at("signatures").as_array().size() == 1u);
    BOOST_TEST(NomadString(scriptOverloads.at("signatures").as_array()[
        static_cast<std::size_t>(scriptOverloads.at("activeSignature").as_int64())].as_object().at("label").as_string())
        .find("value:string") != NomadString::npos);
    BOOST_TEST(errors.str().empty());
}

BOOST_AUTO_TEST_CASE(signature_help_refreshes_project_buffers_and_supports_native_constructors)
{
    TestDirectory directory("nomad_lsp_signature_project");
    directory.write("nomad.toml", R"(schema = 1
[project]
name = "LSP signature help"
identifier = "org.nomad.signatures"
version = "0.1.0"
executable = "signatures"
[game]
entry = "main"
[nomad]
version = "0.1.0"
modules = ["nomad-system", "nomad-collections"]
[scripts]
directories = ["scripts"]
[resources]
directory = "res"
)");
    const auto libraryUri = uriFor(directory.write("scripts/library.nomad",
        "fun choose first:int second:int\nreturn first + second\nend"));
    const auto uri = uriFor(directory.write("scripts/main.nomad", ""));
    const NomadString source =
        "array = new Array<int> 3\n"
        "list = new List<string>\n"
        "choice = choose \n"
        "# unknown.member 1\n";
    const auto request = [&](const int id, const NomadString& word, const std::size_t offset) {
        return symbolRequest(id, "textDocument/signatureHelp", uri, source, word, offset);
    };
    const auto change = json::object{{"jsonrpc", "2.0"}, {"method", "textDocument/didChange"},
        {"params", {{"textDocument", {{"uri", libraryUri}, {"version", 2}}},
            {"contentChanges", json::array{json::object{
                {"text", "fun choose title:string\nreturn title\nend"}}}}}}};
    auto updatedSource = source;
    updatedSource.replace(updatedSource.find("choice = choose "), 16, "choice = choose 1 ");
    const auto changeMain = json::object{{"jsonrpc", "2.0"}, {"method", "textDocument/didChange"},
        {"params", {{"textDocument", {{"uri", uri}, {"version", 2}}},
            {"contentChanges", json::array{json::object{{"text", updatedSource}}}}}}};
    const auto position = [&](const int id, const int line, const int character) {
        return frame({{"jsonrpc", "2.0"}, {"id", id}, {"method", "textDocument/signatureHelp"},
            {"params", {{"textDocument", {{"uri", uri}}},
                {"position", {{"line", line}, {"character", character}}}}}});
    };
    std::istringstream input(start() + frame(openDocument(uri, source)) +
        frame(openDocument(libraryUri, "fun choose first:int second:int\nreturn first + second\nend")) +
        request(10, "new Array<int> ", 15) +
        request(11, "new List<string>", 16) +
        request(12, "choose ", 7) + frame(change) +
        request(13, "choose ", 7) +
        frame({{"jsonrpc", "2.0"}, {"method", "textDocument/didClose"},
            {"params", {{"textDocument", {{"uri", libraryUri}}}}}}) +
        request(14, "choose ", 7) + frame(changeMain) +
        symbolRequest(19, "textDocument/signatureHelp", uri, updatedSource, "choose 1 ", 9) +
        request(15, "unknown.member 1", 16) +
        position(16, 20, 0) + position(17, 0, 100) + position(18, 0, -1) + finish());
    std::ostringstream output;
    std::ostringstream errors;
    BOOST_TEST(runLanguageServer(input, output, errors, [](const auto&) { return LanguageDiagnostics{}; }) == 0);
    std::map<std::int64_t, json::object> replies;
    for (const auto& message : responses(output.str())) {
        if (message.contains("id")) replies.emplace(message.at("id").as_int64(), message);
    }
    const auto& array = replies.at(10).at("result").as_object().at("signatures").as_array();
    BOOST_REQUIRE(array.size() == 3u);
    for (const auto& signature : array) {
        const auto label = NomadString(signature.as_object().at("label").as_string());
        BOOST_TEST(label.starts_with("new Array<int> "));
        const auto rank = signature.as_object().at("parameters").as_array().size();
        BOOST_TEST(label.ends_with(" -> Array" + std::to_string(rank) + "<int>"));
    }
    const auto& list = replies.at(11).at("result").as_object();
    BOOST_REQUIRE(list.at("signatures").as_array().size() == 1u);
    BOOST_TEST(list.at("signatures").as_array().front().as_object().at("label").as_string() ==
        "new List<string> -> List<string>");
    BOOST_TEST(!list.contains("activeParameter"));
    for (const auto id : {12, 14}) {
        BOOST_TEST(replies.at(id).at("result").as_object().at("signatures").as_array().front().as_object()
            .at("parameters").as_array().size() == 2u);
    }
    BOOST_TEST(replies.at(13).at("result").as_object().at("signatures").as_array().front().as_object()
        .at("parameters").as_array().size() == 1u);
    BOOST_TEST(json::serialize(replies.at(13).at("result")).find("title:string") != NomadString::npos);
    BOOST_TEST(replies.at(15).at("result").is_null());
    BOOST_TEST(replies.at(19).at("result").as_object().at("activeParameter").as_int64() == 1);
    for (const auto id : {16, 17, 18}) {
        BOOST_TEST(replies.at(id).at("error").as_object().at("code").as_int64() == -32602);
    }
    BOOST_TEST(errors.str().find("Line is outside document") != NomadString::npos);
    BOOST_TEST(errors.str().find("Character is outside document") != NomadString::npos);
    BOOST_TEST(errors.str().find("Negative document position") != NomadString::npos);
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

BOOST_AUTO_TEST_CASE(script_member_completion_filters_owners_and_hover_lists_all_overloads)
{
    TestDirectory directory("nomad_lsp_script_members");
    const auto uri = uriFor(directory.getPath() / "members.nomad");
    const NomadString source =
        "class Counter\nlabel = \"counter\"\n"
        "init number:int\nthis.value = number\nend\n"
        "fun read\nreturn this.value\nend\n"
        "fun read delta:int\nreturn this.value + delta\nend\n"
        "fun read suffix:string\nreturn $\"{this.label}{suffix}\"\nend\nend\n"
        "class Other\notherOnly = true\n"
        "fun read factor:float\nreturn factor + 0.5\nend\nend\n"
        "class Owner\nchild = new Counter 2\nend\n"
        "counter = new Counter 4\nother = new Other\nowner = new Owner\n"
        "text = counter.label\ntrimmed = counter.label.trim\n"
        "nested = owner.child.read\nreadValue = counter.read\notherValue = other.read 1.0\n"
        "return counter.value";
    const auto diagnostics = analyzeLanguageDocuments({{uri, {source, 1}}});
    BOOST_REQUIRE_MESSAGE(diagnostics.empty(), (diagnostics.empty() ? "" : json::serialize(diagnostics.begin()->second)));
    const auto query = [&](const int id, const char* method, const NomadString& word, const std::size_t offset) {
        return symbolRequest(id, method, uri, source, word, offset);
    };
    std::istringstream input(start() + frame(openDocument(uri, source)) +
        query(10, "textDocument/completion", "counter.read", 8) +
        query(11, "textDocument/hover", "counter.read", 10) +
        query(12, "textDocument/hover", "counter.label\n", 10) +
        query(13, "textDocument/completion", "owner.child.read", 12) +
        query(14, "textDocument/completion", "counter.label.trim", 14) +
        query(15, "textDocument/completion", "this.value = number", 5) +
        query(16, "textDocument/hover", "this.value = number", 7) +
        query(17, "textDocument/completion", "counter = new Counter", 0) +
        query(18, "textDocument/hover", "other.read 1.0", 8) +
        query(19, "textDocument/completion", "other.read 1.0", 6) +
        query(20, "textDocument/hover", "counter.value", 10) + finish());
    std::ostringstream output;
    std::ostringstream errors;
    BOOST_TEST(runLanguageServer(input, output, errors, [](const auto&) { return LanguageDiagnostics{}; }) == 0);
    BOOST_REQUIRE_MESSAGE(errors.str().empty(), errors.str());
    std::map<std::int64_t, json::object> replies;
    for (const auto& message : responses(output.str())) {
        if (message.contains("id")) replies.emplace(message.at("id").as_int64(), message);
    }
    const std::set<NomadString> signatures{
        "Counter.read -> int", "Counter.read delta:int -> int", "Counter.read suffix:string -> string"
    };
    for (const auto id : {10, 13, 15}) {
        std::set<NomadString> methodDetails;
        std::set<NomadString> names;
        const auto receiver = id == 10 ? NomadString{"counter"} : id == 13 ? NomadString{"owner.child"} : NomadString{"this"};
        for (const auto& item : replies.at(id).at("result").as_object().at("items").as_array()) {
            const auto& candidate = item.as_object();
            const NomadString name{candidate.at("label").as_string()};
            names.insert(name);
            BOOST_TEST(name.find('$') == NomadString::npos);
            BOOST_TEST(candidate.at("textEdit").as_object().at("newText").as_string() == receiver + "." + name);
            BOOST_TEST(candidate.at("filterText").as_string() == receiver + "." + name);
            if (name == "read") {
                BOOST_TEST(candidate.at("kind").as_int64() == 2);
                methodDetails.emplace(candidate.at("detail").as_string());
            }
            if (name == "value") {
                BOOST_TEST(candidate.at("kind").as_int64() == 10);
                BOOST_TEST(candidate.at("detail").as_string() == "Counter.value:int");
            }
        }
        BOOST_TEST(methodDetails == signatures, boost::test_tools::per_element());
        BOOST_TEST(names.contains("value"));
        BOOST_TEST(names.contains("label"));
        BOOST_TEST(!names.contains("otherOnly"));
        BOOST_TEST(!names.contains("trim"));
        BOOST_TEST(!names.contains("init"));
    }
    const auto hover = json::serialize(replies.at(11).at("result"));
    for (const auto& signature : signatures) BOOST_TEST(hover.find(signature) != NomadString::npos);
    BOOST_TEST(hover.find("Other.read") == NomadString::npos);
    BOOST_TEST(hover.find("Defined in") != NomadString::npos);
    BOOST_TEST(json::serialize(replies.at(12).at("result")).find("Counter.label:string") != NomadString::npos);
    for (const auto id : {16, 20}) {
        BOOST_TEST(json::serialize(replies.at(id).at("result")).find("Counter.value:int") != NomadString::npos);
    }
    auto trimFound = false;
    for (const auto& item : replies.at(14).at("result").as_object().at("items").as_array()) {
        const auto& candidate = item.as_object();
        BOOST_TEST(NomadString(candidate.at("detail").as_string()).starts_with("string."));
        if (candidate.at("label").as_string() == "trim") trimFound = true;
    }
    BOOST_TEST(trimFound);
    for (const auto& item : replies.at(17).at("result").as_object().at("items").as_array()) {
        const auto& name = item.as_object().at("label").as_string();
        BOOST_TEST(name != "value");
        BOOST_TEST(name != "label");
        BOOST_TEST(name != "read");
    }
    const auto otherHover = json::serialize(replies.at(18).at("result"));
    BOOST_TEST(otherHover.find("Other.read factor:float -> float") != NomadString::npos);
    BOOST_TEST(otherHover.find("Counter.read") == NomadString::npos);
    for (const auto& item : replies.at(19).at("result").as_object().at("items").as_array()) {
        BOOST_TEST(NomadString(item.as_object().at("detail").as_string()).starts_with("Other."));
    }
}

BOOST_AUTO_TEST_CASE(script_member_recovery_keeps_overloads_and_refreshes_unsaved_definitions)
{
    TestDirectory directory("nomad_lsp_script_member_recovery");
    const auto uri = uriFor(directory.getPath() / "members.nomad");
    const NomadString original =
        "class Counter\nvalue = 2\n"
        "fun read\nreturn this.value\nend\n"
        "fun read amount:int\nreturn this.value + amount\nend\nend\n"
        "counter = new Counter\ncounter.";
    const NomadString edited =
        "class Counter\nlabel = \"updated\"\n"
        "fun read text:string\nreturn $\"{this.label}{text}\"\nend\n"
        "fun read first:string second:string\nreturn $\"{this.label}{first}{second}\"\nend\nend\n"
        "counter = new Counter\ncounter.";
    const auto change = [&](const NomadString& source, const int version) {
        return frame({{"jsonrpc", "2.0"}, {"method", "textDocument/didChange"},
            {"params", {{"textDocument", {{"uri", uri}, {"version", version}}},
                {"contentChanges", json::array{json::object{{"text", source}}}}}}});
    };
    const auto valid = edited.substr(0, edited.size() - 8) + "result = counter.read \"x\"";
    const NomadString unresolved = "class Broken\nfun read amount:int\nreturn this.read amount\nend\nend";
    const auto unknown = original.substr(0, original.size() - 8) + "missing.read 1";
    std::istringstream input(start() + frame(openDocument(uri, original)) +
        symbolRequest(10, "textDocument/completion", uri, original, "counter.", 8) +
        change(edited, 2) + symbolRequest(11, "textDocument/completion", uri, edited, "counter.", 8) +
        change(valid, 3) + symbolRequest(12, "textDocument/hover", uri, valid, "counter.read", 10) +
        change(unresolved, 4) + symbolRequest(13, "textDocument/hover", uri, unresolved, "this.read", 7) +
        change(unknown, 5) + symbolRequest(14, "textDocument/completion", uri, unknown, "missing.read", 8) +
        symbolRequest(15, "textDocument/hover", uri, unknown, "missing.read", 10) + finish());
    std::ostringstream output;
    std::ostringstream errors;
    BOOST_TEST(runLanguageServer(input, output, errors, [](const auto&) { return LanguageDiagnostics{}; }) == 0);
    BOOST_REQUIRE_MESSAGE(errors.str().empty(), errors.str());
    std::map<std::int64_t, json::object> replies;
    for (const auto& message : responses(output.str())) {
        if (message.contains("id")) replies.emplace(message.at("id").as_int64(), message);
    }
    for (const auto id : {10, 11}) {
        auto methods = 0;
        auto fieldFound = false;
        for (const auto& item : replies.at(id).at("result").as_object().at("items").as_array()) {
            const auto& candidate = item.as_object();
            const NomadString name{candidate.at("label").as_string()};
            const NomadString detail{candidate.at("detail").as_string()};
            BOOST_TEST(name.find('$') == NomadString::npos);
            if (name == "read") {
                ++methods;
                BOOST_TEST(detail.ends_with(id == 10 ? " -> int" : " -> string"));
            } else {
                BOOST_TEST(name == (id == 10 ? "value" : "label"));
                BOOST_TEST(detail == (id == 10 ? "Counter.value:int" : "Counter.label:string"));
                fieldFound = true;
            }
        }
        BOOST_TEST(methods == 2);
        BOOST_TEST(fieldFound);
    }
    const auto hover = json::serialize(replies.at(12).at("result"));
    BOOST_TEST(hover.find("Counter.read text:string -> string") != NomadString::npos);
    BOOST_TEST(hover.find("Counter.read first:string second:string -> string") != NomadString::npos);
    BOOST_TEST(hover.find("amount:int") == NomadString::npos);
    BOOST_TEST(json::serialize(replies.at(13).at("result")).find("Broken.read amount:int -> unknown") != NomadString::npos);
    auto unknownMethods = 0;
    for (const auto& item : replies.at(14).at("result").as_object().at("items").as_array()) {
        if (NomadString(item.as_object().at("detail").as_string()).starts_with("Counter.read")) ++unknownMethods;
    }
    BOOST_TEST(unknownMethods == 2);
    BOOST_TEST(replies.at(15).at("result").is_null());
}

BOOST_AUTO_TEST_CASE(project_script_member_catalog_tracks_qualified_types_and_other_open_buffers)
{
    TestDirectory directory("nomad_lsp_project_class_members");
    directory.write("nomad.toml", R"(schema = 1
[project]
name = "Class members"
identifier = "org.nomad.class-members"
version = "0.1.0"
executable = "members"
[nomad]
version = "0.1.0"
modules = ["nomad-system"]
[scripts]
directories = ["res/scripts"]
[resources]
directory = "res"
)");
    const auto libraryUri = uriFor(directory.write("res/scripts/types.nomad",
        "hub model\nclass Record\nname = \"disk\"\nfun describe\nreturn this.name\nend\nend\nend"));
    const NomadString source = "record = new model.Record\nrecord.";
    const auto uri = uriFor(directory.write("res/scripts/main.nomad", source));
    const NomadString edited =
        "hub model\nclass Record\nscore = 5\nfun describe amount:int\nreturn this.score + amount\nend\nend\nend";
    const auto query = [&](const int id) {
        return symbolRequest(id, "textDocument/completion", uri, source, "record.", 7);
    };
    std::istringstream input(start() + frame(openDocument(uri, source)) + query(10) +
        frame(openDocument(libraryUri, edited)) + query(11) +
        frame({{"jsonrpc", "2.0"}, {"method", "textDocument/didClose"},
            {"params", {{"textDocument", {{"uri", libraryUri}}}}}}) + query(12) + finish());
    std::ostringstream output;
    std::ostringstream errors;
    BOOST_TEST(runLanguageServer(input, output, errors, [](const auto&) { return LanguageDiagnostics{}; }) == 0);
    BOOST_REQUIRE_MESSAGE(errors.str().empty(), errors.str());
    auto replies = 0;
    for (const auto& message : responses(output.str())) {
        if (!message.contains("id") || message.at("id").as_int64() < 10) continue;
        const auto editedBuffer = message.at("id").as_int64() == 11;
        std::set<NomadString> details;
        for (const auto& item : message.at("result").as_object().at("items").as_array()) {
            details.emplace(item.as_object().at("detail").as_string());
        }
        BOOST_TEST(details.size() == 2U);
        BOOST_TEST(details.contains(editedBuffer ? "model.Record.score:int" : "model.Record.name:string"));
        BOOST_TEST(details.contains(editedBuffer ? "model.Record.describe amount:int -> int" : "model.Record.describe -> string"));
        ++replies;
    }
    BOOST_TEST(replies == 3);
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
