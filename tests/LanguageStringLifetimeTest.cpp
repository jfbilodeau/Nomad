// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/script/Closure.hpp>
#include <nomad/script/NativeFunction.hpp>
#include <nomad/script/RuntimeValue.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <memory>
#include <string>
#include <vector>

using namespace nomad;
using namespace nomad::test;

namespace {

struct LifetimeCase {
    const char* source;
    const char* expectedText;
};

void registerLifetimeNativeFunctions(Runtime& runtime, std::vector<NomadString>& consumed) {
    runtime.registerNativeFunction(
        "test.name",
        [](VirtualMachine* virtualMachine) {
            virtualMachine->setStringResult("name");
        },
        {},
        runtime.getStringType(),
        NomadDoc("Return an owned string")
    );

    runtime.registerNativeFunction(
        "test.echo",
        [](VirtualMachine* virtualMachine) {
            virtualMachine->setStringResult(virtualMachine->getStringParameter(0)->getString());
        },
        {
            defParameter("text", runtime.getStringType(), NomadParamDoc("Text to return")),
        },
        runtime.getStringType(),
        NomadDoc("Return the provided string")
    );

    runtime.registerNativeFunction(
        "test.consume",
        [&consumed](VirtualMachine* virtualMachine) {
            consumed.emplace_back(virtualMachine->getStringParameter(0)->getString());
        },
        {
            defParameter("text", runtime.getStringType(), NomadParamDoc("Text to record")),
        },
        runtime.getVoidType(),
        NomadDoc("Record a string")
    );
}

FunctionOutcome runWithNativeFunctions(const NomadString& source, std::vector<NomadString>& consumed) {
    LanguageTestFixture fixture;
    registerLifetimeNativeFunctions(fixture.getRuntime(), consumed);
    fixture.addFunction("main", source);

    if (!fixture.compile()) {
        FunctionOutcome outcome;
        outcome.diagnostics = fixture.getDiagnostics();

        return outcome;
    }

    return fixture.execute("main");
}

FunctionOutcome runWithNativeFunctions(const NomadString& source) {
    std::vector<NomadString> consumed;

    return runWithNativeFunctions(source, consumed);
}

void checkCases(const std::vector<LifetimeCase>& cases) {
    for (const auto& [source, expectedText]: cases) {
        BOOST_TEST_CONTEXT(source) {
            const auto outcome = runWithNativeFunctions(source);

            BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
            BOOST_TEST(!outcome.fault.has_value());
            BOOST_TEST(outcome.text == expectedText);
            BOOST_TEST(outcome.leakedStrings == 0);
        }
    }
}

} // namespace

BOOST_AUTO_TEST_SUITE(language_string_lifetime)

#ifdef NOMAD_DEBUG
BOOST_AUTO_TEST_CASE(live_string_counter_tracks_allocations)
{
    const auto before = RuntimeValue::getLiveStringCount();

    RuntimeValue value;
    value.setStringValue("abc");
    BOOST_TEST(RuntimeValue::getLiveStringCount() == before + 1);

    value.freeObjectValue();
    BOOST_TEST(RuntimeValue::getLiveStringCount() == before);
}
#endif

BOOST_AUTO_TEST_CASE(moving_string_values_transfers_ownership)
{
    RuntimeValue source;
    source.setStringValue("abc");

    RuntimeValue destination;
    destination.moveObjectValue(source);

    BOOST_TEST(source.getObjectValue() == nullptr);
    BOOST_TEST(destination.getStringValue() == "abc");

    destination.freeObjectValue();
    BOOST_TEST(destination.getObjectValue() == nullptr);
}

BOOST_AUTO_TEST_CASE(discarded_string_results_are_freed)
{
    checkCases({
        {"test.name\nreturn 1", "1"},
        {"test.echo \"a\"\nreturn 1", "1"},
        {"fun f\n    return \"x\"\nend\nf\nreturn 1", "1"},
        {"fun f\n    return test.name\nend\nf\nf\nreturn 1", "1"},
    });
}

BOOST_AUTO_TEST_CASE(string_variables_release_previous_values)
{
    checkCases({
        {"s = \"a\"\ns = \"b\"\nreturn s", "b"},
        {"s = test.name\ns = test.name\nreturn s", "name"},
        {"s = \"a\"\nt = s\ns = \"b\"\nreturn $\"{s}{t}\"", "ba"},
        {"fun f\n    s = \"a\"\n    s = \"b\"\n    return s\nend\nreturn f", "b"},
    });
}

BOOST_AUTO_TEST_CASE(string_arguments_are_released_after_calls)
{
    checkCases({
        {"return test.echo test.echo \"a\"", "a"},
        {"s = \"a\"\nreturn test.echo s", "a"},
        {"test.consume \"a\"\ntest.consume test.name\ns = \"b\"\ntest.consume s\ntest.consume $\"x{s}\"\nreturn 1", "1"},
        {"fun id a:string\n    return a\nend\nreturn id test.name", "name"},
        {"fun id a:string\n    return a\nend\nid \"x\"\nreturn 1", "1"},
    });
}

BOOST_AUTO_TEST_CASE(string_comparisons_do_not_leak)
{
    checkCases({
        {"fun check a:string\n    return a == \"x\"\nend\nreturn check \"x\"", "true"},
        {"s = \"name\"\nreturn s == test.name", "true"},
        {"s = \"name\"\nreturn test.name != s", "false"},
        {"return test.name == test.echo \"name\"", "true"},
        {"s = \"a\"\nreturn $\"{s}\" != \"a\"", "false"},
        {"fun f\n    return \"v\"\nend\nreturn f == f", "true"},
        {"return (test.name == \"name\") == ((test.echo \"x\") != \"y\")", "true"},
        {"s = \"a\"\nif s == \"a\"\n    return 1\nend\nreturn 2", "1"},
        {"s = \"a\"\nt = \"b\"\nreturn s == t", "false"},
    });
}

BOOST_AUTO_TEST_CASE(format_string_function_segments_do_not_leak)
{
    checkCases({
        {"fun f\n    return \"v\"\nend\nreturn $\"[{f}]\"", "[v]"},
        {"fun f\n    return \"v\"\nend\ns = test.name\nreturn $\"{s}{f}{s}\"", "namevname"},
    });
}

BOOST_AUTO_TEST_CASE(format_string_expression_captures_and_results_do_not_leak)
{
    checkCases({
        {"name = \"n\"\nreturn $\"{test.echo name}\"", "n"},
        {"fun describe text:string\n    return $\"{test.echo text}\"\nend\nreturn describe \"p\"", "p"},
        {"name = \"n\"\nreturn $\"{$\"<{name}>\"}\"", "<n>"},
        {"name = \"n\"\nreturn $\"{$\"{$\"{test.echo name}\"}\"}\"", "n"},
        {"name = \"n\"\nfirst = $\"{test.echo name}\"\nname = \"m\"\nsecond = $\"{test.echo name}\"\nreturn $\"{first}:{second}\"", "n:m"},
    });
}

BOOST_AUTO_TEST_CASE(function_string_parameters_do_not_leak)
{
    checkCases({
        {"fun id a:string\n    return a\nend\nreturn id \"x\"", "x"},
        {"fun id a:string\n    return a\nend\ns = \"x\"\nreturn id s", "x"},
        {"fun inner a:string\n    return a\nend\nfun outer a:string\n    return inner a\nend\nreturn outer \"x\"", "x"},
        {"fun id a:string\n    return a\nend\nreturn id test.name", "name"},
        {"fun id a:string\n    return a\nend\ns = \"x\"\nreturn id $\"[{s}]\"", "[x]"},
        {"fun v\n    return \"v\"\nend\nfun id a:string\n    return a\nend\nreturn id v", "v"},
        {"fun f a:string\n    b = a\n    return b\nend\nreturn f \"x\"", "x"},
        {"fun join a:string b:string c:string\n    return $\"{a}{b}{c}\"\nend\ns = \"s\"\nreturn join s test.name \"l\"", "snamel"},
        {"fun id a:string\n    return a\nend\nreturn (id \"x\") == \"x\"", "true"},
    });
}

BOOST_AUTO_TEST_CASE(null_string_converts_to_empty_text)
{
    LanguageTestFixture fixture;
    RuntimeValue value;
    fixture.getRuntime().getStringType()->initValue(&value);

    BOOST_TEST(fixture.getRuntime().getStringType()->toString(value) == "");

    fixture.getRuntime().getStringType()->freeValue(&value);
}

BOOST_AUTO_TEST_CASE(discarded_closure_string_result_is_freed)
{
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    std::unique_ptr<Closure> closure;

    runtime.registerNativeFunction(
        "test.storeText",
        [&runtime, &closure](VirtualMachine* virtualMachine) {
            closure = virtualMachine->createClosure(&runtime, CallableId{virtualMachine->getIdParameter(0)});
        },
        {
            defParameter("callback", runtime.getCallbackType({}, runtime.getStringType()), NomadParamDoc("Callback to store")),
        },
        runtime.getVoidType(),
        NomadDoc("Store a callback returning a string")
    );

    fixture.addFunction("main", "test.storeText fun\n    return \"text\"\nend");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_REQUIRE(!fixture.execute("main").fault.has_value());
    BOOST_REQUIRE(closure != nullptr);

    const auto before = RuntimeValue::getLiveStringCount();
    runtime.executeFunction(closure.get(), {});
    BOOST_TEST(RuntimeValue::getLiveStringCount() == before);
}

BOOST_AUTO_TEST_CASE(hidden_source_arguments_are_passed_as_strings)
{
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    NomadString fileName;
    NomadString functionName;
    NomadInteger lineNumber = 0;

    runtime.registerNativeFunction(
        "test.source",
        [&](VirtualMachine* virtualMachine) {
            fileName = virtualMachine->getStringParameter(0)->getString();
            functionName = virtualMachine->getStringParameter(1)->getString();
            lineNumber = virtualMachine->getIntegerParameter(2);
        },
        {
            defParameter("$file", runtime.getFileNameType(), NomadParamDoc("Calling file")),
            defParameter("$function", runtime.getFunctionNameType(), NomadParamDoc("Calling function")),
            defParameter("$line", runtime.getLineNumberType(), NomadParamDoc("Calling line")),
        },
        runtime.getVoidType(),
        NomadDoc("Record the hidden source arguments")
    );

    fixture.addFunction("main", "x = 1\ntest.source\nreturn 1");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

    const auto outcome = fixture.execute("main");

    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(fileName == "main.nomad");
    BOOST_TEST(functionName == "main");
    BOOST_TEST(lineNumber == 2);
    BOOST_TEST(outcome.leakedStrings == 0);
}

BOOST_AUTO_TEST_CASE(function_parameters_keep_their_value_when_context_variables_change)
{
    std::vector<NomadString> consumed;
    const auto outcome = runWithNativeFunctions(
        "global.x = \"A\"\n"
        "fun boom p:string\n"
        "    test.consume p\n"
        "    global.x = \"B\"\n"
        "    test.consume p\n"
        "end\n"
        "boom global.x\n"
        "return global.x",
        consumed
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.text == "B");
#ifdef NOMAD_DEBUG
    // `global.x` holds the literal "B", which is owned by the string table; nothing else is live.
    BOOST_TEST(outcome.leakedStrings == 0);
#endif

    const std::vector<NomadString> expected{"A", "A"};
    BOOST_TEST(consumed == expected, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(context_variables_passed_to_functions_are_shared)
{
    const std::vector<LifetimeCase> cases = {
        {"global.text = \"g\"\nfun id a:string\n    return a\nend\nreturn id global.text", "g"},
        {"fun f a:string\n    global.text = a\nend\nf \"x\"\nreturn global.text", "x"},
        {"fun f a:string\n    global.text = a\nend\ns = test.name\nf s\nreturn global.text", "name"},
    };

    for (const auto& [source, expectedText]: cases) {
        BOOST_TEST_CONTEXT(source) {
            const auto outcome = runWithNativeFunctions(source);

            BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
            BOOST_TEST(!outcome.fault.has_value());
            BOOST_TEST(outcome.text == expectedText);
#ifdef NOMAD_DEBUG
            // `global.text` may still own a string created at run time; nothing else is live.
            BOOST_TEST(outcome.leakedStrings <= 1);
#endif
        }
    }
}

BOOST_AUTO_TEST_CASE(closures_keep_captured_parameters_alive)
{
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    std::unique_ptr<Closure> closure;
    std::vector<NomadString> consumed;

    registerLifetimeNativeFunctions(runtime, consumed);
    runtime.registerNativeFunction(
        "test.storeText",
        [&runtime, &closure](VirtualMachine* virtualMachine) {
            closure = virtualMachine->createClosure(&runtime, CallableId{virtualMachine->getIdParameter(0)});
        },
        {
            defParameter("callback", runtime.getCallbackType({}, runtime.getStringType()), NomadParamDoc("Callback to store")),
        },
        runtime.getVoidType(),
        NomadDoc("Store a callback returning a string")
    );

    fixture.addFunction(
        "main",
        "fun keep a:string\n"
        "    test.storeText fun\n"
        "        return a\n"
        "    end\n"
        "end\n"
        "s = test.name\n"
        "keep s\n"
        "return 1"
    );
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

#ifdef NOMAD_DEBUG
    const auto before = RuntimeValue::getLiveStringCount();
#endif
    BOOST_REQUIRE(!fixture.execute("main").fault.has_value());
    BOOST_REQUIRE(closure != nullptr);
#ifdef NOMAD_DEBUG
    // Only the captured string is live.
    BOOST_TEST(RuntimeValue::getLiveStringCount() - before == 1);
#endif

    RuntimeValue result;
    runtime.executeFunction(closure.get(), {}, result);
    BOOST_TEST(result.getStringValue() == "name");
    runtime.getStringType()->freeValue(&result);

    closure.reset();
#ifdef NOMAD_DEBUG
    BOOST_TEST(RuntimeValue::getLiveStringCount() == before);
#endif
}

BOOST_AUTO_TEST_CASE(function_parameters_are_mutable_owned_local_bindings)
{
    const auto outcome = runSource(
        "fun change text:string\ntext = text.toUpper\ntext = $\"{text}!\"\nreturn text\nend\n"
        "original = \"hello\"\nchanged = change original\nreturn $\"{original}{changed}\"");
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.text == "helloHELLO!");
    BOOST_TEST(outcome.leakedStrings == 0);
    const auto integer = runSource("fun change value:int\nvalue = 2\nreturn value\nend\nreturn change 1");
    BOOST_REQUIRE_MESSAGE(integer.compiled, integer.diagnostics);
    BOOST_TEST(integer.integerValue == 2);
    BOOST_TEST(!compileErrors("fun change value:int\nvalue = \"wrong\"\nend\nchange 1").empty());
}

BOOST_AUTO_TEST_SUITE_END()
