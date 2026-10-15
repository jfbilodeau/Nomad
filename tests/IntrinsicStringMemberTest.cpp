// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/script/Documentation.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <sstream>

using namespace nomad;

namespace {

NomadId compileFunction(Runtime& runtime, const NomadString& source) {
    auto compiler = runtime.createCompiler();
    const auto functionId = compiler->registerScriptFile("test", "test.nomad", source);
    CompilerContext context(compiler.get());

    const auto compiled = compiler->compileFunctions(&context);
    const auto* error = context.getFirstError();
    const auto message = error == nullptr ? NomadString("Unknown compilation error") : error->message;
    BOOST_REQUIRE_MESSAGE(compiled, message);
    return functionId;
}

} // namespace

BOOST_AUTO_TEST_SUITE(intrinsic_string_members)

BOOST_AUTO_TEST_CASE(registers_immutable_string_members) {
    Runtime runtime;

    const auto lengthId = runtime.getIntrinsicMemberId(runtime.getStringType(), "length");
    const auto toUpperId = runtime.getIntrinsicMemberId(runtime.getStringType(), "toUpper");

    BOOST_REQUIRE(lengthId != NOMAD_INVALID_ID);
    BOOST_REQUIRE(toUpperId != NOMAD_INVALID_ID);

    IntrinsicPropertyDefinition length;
    BOOST_REQUIRE(runtime.getIntrinsicPropertyDefinition(lengthId, length));
    BOOST_TEST(static_cast<bool>(length.getFn));
    BOOST_TEST(!length.setFn);
    BOOST_TEST(length.valueType == runtime.getIntegerType());

    IntrinsicMethodDefinition toUpper;
    BOOST_REQUIRE(runtime.getIntrinsicMethodDefinition(toUpperId, toUpper));
    BOOST_TEST(!toUpper.mutatesReceiver);
    BOOST_TEST(toUpper.returnType == runtime.getStringType());
}

BOOST_AUTO_TEST_CASE(exports_string_members_to_documentation_and_textmate_grammar) {
    Runtime runtime;
    std::ostringstream documentation;
    std::ostringstream keywords;
    std::ostringstream grammar;

    generateDocumentation(&runtime, documentation);
    generateKeywords(&runtime, keywords);
    generateTextMateGrammar(&runtime, grammar);

    BOOST_TEST(documentation.str().find("## Intrinsic Members") != NomadString::npos);
    BOOST_TEST(documentation.str().find("#### `length` (*read-only property*)") != NomadString::npos);
    BOOST_TEST(documentation.str().find("#### `contains` (*method*)") != NomadString::npos);
    BOOST_TEST(
        documentation.str().find(
            "#### `contains` (*method*)\n\n"
            "parameters:\n\n"
            "- `text:string` - The text to find\n"
        ) != NomadString::npos
    );
    BOOST_TEST(keywords.str().find("--- IntrinsicMembers ---") != NomadString::npos);
    BOOST_TEST(keywords.str().find("toUpper") != NomadString::npos);
    BOOST_TEST(grammar.str().find("\"scopeName\":\"source.nomad\"") != NomadString::npos);
    BOOST_TEST(grammar.str().find("\"repository\"") != NomadString::npos);
    BOOST_TEST(grammar.str().find("\"support.function.member.nomad\"") != NomadString::npos);
    BOOST_TEST(grammar.str().find("trimStart") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(compiled_length_counts_unicode_code_points) {
    Runtime runtime;
    const auto functionId = compileFunction(runtime, "return \"J-F \xF0\x9F\x98\x80\".length");

    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);

    BOOST_TEST(result.getIntegerValue() == 5);
}

BOOST_AUTO_TEST_CASE(compiled_to_upper_uses_unicode_casing) {
    Runtime runtime;
    const auto functionId = compileFunction(runtime, "return \"h\xC3\xA9llo\".toUpper");

    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);

    BOOST_TEST(result.getStringValue() == "H\xC3\x89LLO");
    runtime.getStringType()->freeValue(result);
}

BOOST_AUTO_TEST_CASE(to_upper_does_not_modify_receiver) {
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext interpreter(*machine);

    BOOST_REQUIRE(Interpreter{}.execute("name = \"h\xC3\xA9llo\"", interpreter));
    BOOST_REQUIRE(Interpreter{}.execute("upper = name.toUpper", interpreter));
    BOOST_REQUIRE(interpreter.getVariableValue("name") != nullptr);
    BOOST_REQUIRE(interpreter.getVariableValue("upper") != nullptr);

    BOOST_TEST(interpreter.getVariableValue("name")->getStringValue() == "h\xC3\xA9llo");
    BOOST_TEST(interpreter.getVariableValue("upper")->getStringValue() == "H\xC3\x89LLO");
}

BOOST_AUTO_TEST_CASE(exposes_byte_length_and_empty_properties) {
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext interpreter(*machine);

    BOOST_REQUIRE(Interpreter{}.execute("\"h\xC3\xA9\".byteLength", interpreter));
    BOOST_REQUIRE(interpreter.getResult() != nullptr);
    BOOST_TEST(interpreter.getResult()->getValue().getIntegerValue() == 3);

    BOOST_REQUIRE(Interpreter{}.execute("\"\".isEmpty", interpreter));
    BOOST_REQUIRE(interpreter.getResult() != nullptr);
    BOOST_TEST(interpreter.getResult()->getValue().getBooleanValue());

    BOOST_REQUIRE(Interpreter{}.execute("\"Nomad\".isEmpty", interpreter));
    BOOST_REQUIRE(interpreter.getResult() != nullptr);
    BOOST_TEST(!interpreter.getResult()->getValue().getBooleanValue());
}

BOOST_AUTO_TEST_CASE(to_lower_uses_unicode_casing) {
    Runtime runtime;
    const auto functionId = compileFunction(runtime, "return \"H\xC3\x89LLO\".toLower");

    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);

    BOOST_TEST(result.getStringValue() == "h\xC3\xA9llo");
    runtime.getStringType()->freeValue(result);
}

BOOST_AUTO_TEST_CASE(trim_methods_remove_locale_whitespace) {
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext interpreter(*machine);

    BOOST_REQUIRE(Interpreter{}.execute("\" \\tNomad\\n \".trim", interpreter));
    BOOST_TEST(interpreter.getResult()->getValue().getStringValue() == "Nomad");

    BOOST_REQUIRE(Interpreter{}.execute("\" \\tNomad \".trimStart", interpreter));
    BOOST_TEST(interpreter.getResult()->getValue().getStringValue() == "Nomad ");

    BOOST_REQUIRE(Interpreter{}.execute("\" Nomad\\n \".trimEnd", interpreter));
    BOOST_TEST(interpreter.getResult()->getValue().getStringValue() == " Nomad");

    BOOST_REQUIRE(Interpreter{}.execute("\"\xC2\xA0Nomad\xE2\x80\x83\".trim", interpreter));
    BOOST_TEST(interpreter.getResult()->getValue().getStringValue() == "Nomad");
}

BOOST_AUTO_TEST_CASE(search_methods_are_case_sensitive) {
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext interpreter(*machine);

    BOOST_REQUIRE(Interpreter{}.execute("\"Nomad Engine\".startsWith \"Nomad\"", interpreter));
    BOOST_TEST(interpreter.getResult()->getValue().getBooleanValue());

    BOOST_REQUIRE(Interpreter{}.execute("\"Nomad Engine\".endsWith \"Engine\"", interpreter));
    BOOST_TEST(interpreter.getResult()->getValue().getBooleanValue());

    BOOST_REQUIRE(Interpreter{}.execute("\"Nomad Engine\".contains \"ad En\"", interpreter));
    BOOST_TEST(interpreter.getResult()->getValue().getBooleanValue());

    BOOST_REQUIRE(Interpreter{}.execute("\"Nomad Engine\".contains \"nomad\"", interpreter));
    BOOST_TEST(!interpreter.getResult()->getValue().getBooleanValue());
}

BOOST_AUTO_TEST_CASE(string_members_reject_malformed_utf8) {
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext interpreter(*machine);
    RuntimeValue invalidValue;
    invalidValue.setStringValue(NomadString("\xC3\x28", 2));
    interpreter.setVariable("text", runtime.getStringType(), invalidValue);
    runtime.getStringType()->freeValue(invalidValue);

    BOOST_REQUIRE(Interpreter{}.execute("text.byteLength", interpreter));
    BOOST_TEST(interpreter.getResult()->getValue().getIntegerValue() == 2);

    BOOST_TEST(!Interpreter{}.execute("text.length", interpreter));
    BOOST_REQUIRE(interpreter.getError() != nullptr);
    BOOST_TEST(interpreter.getError()->find("valid UTF-8") != NomadString::npos);

    BOOST_TEST(!Interpreter{}.execute("text.toUpper", interpreter));
    BOOST_REQUIRE(interpreter.getError() != nullptr);
    BOOST_TEST(interpreter.getError()->find("valid UTF-8") != NomadString::npos);

    BOOST_TEST(!Interpreter{}.execute("text.trim", interpreter));
    BOOST_REQUIRE(interpreter.getError() != nullptr);
    BOOST_TEST(interpreter.getError()->find("valid UTF-8") != NomadString::npos);

    BOOST_TEST(!Interpreter{}.execute("\"valid\".contains text", interpreter));
    BOOST_REQUIRE(interpreter.getError() != nullptr);
    BOOST_TEST(interpreter.getError()->find("valid UTF-8") != NomadString::npos);
}

BOOST_AUTO_TEST_SUITE_END()
