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

// Live strings at the start of `execute`, so nativeFunctions can report allocations made by the function itself.
NomadInteger s_liveStringBaseline = 0;

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
            virtualMachine->setStringResult(virtualMachine->getStringParameter(0));
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
            consumed.emplace_back(virtualMachine->getStringParameter(0));
        },
        {
            defParameter("text", runtime.getStringType(), NomadParamDoc("Text to record")),
        },
        runtime.getVoidType(),
        NomadDoc("Record a string")
    );

    runtime.registerNativeFunction(
        "test.ref.consume",
        [&consumed](VirtualMachine* virtualMachine) {
            // A `$stringref` is only valid during the call: keep a copy.
            consumed.emplace_back(virtualMachine->getStringParameter(0));
        },
        {
            defParameter("text", runtime.getStringRefType(), NomadParamDoc("Text to record")),
        },
        runtime.getVoidType(),
        NomadDoc("Record a borrowed string")
    );

    runtime.registerNativeFunction(
        "test.ref.echo",
        [](VirtualMachine* virtualMachine) {
            virtualMachine->setStringResult(virtualMachine->getStringParameter(0));
        },
        {
            defParameter("text", runtime.getStringRefType(), NomadParamDoc("Text to return")),
        },
        runtime.getStringType(),
        NomadDoc("Return a copy of the borrowed string")
    );

    runtime.registerNativeFunction(
        "test.ref.allocations",
        [](VirtualMachine* virtualMachine) {
            virtualMachine->setIntegerResult(RuntimeValue::getLiveStringCount() - s_liveStringBaseline);
        },
        {
            defParameter("text", runtime.getStringRefType(), NomadParamDoc("Ignored")),
        },
        runtime.getIntegerType(),
        NomadDoc("Return the number of strings allocated by the function so far")
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

    s_liveStringBaseline = RuntimeValue::getLiveStringCount();

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

    RuntimeValue reference;
    reference.setStringRefValue(value.getStringValue());
    BOOST_TEST(RuntimeValue::getLiveStringCount() == before + 1);

    value.freeStringValue();
    BOOST_TEST(RuntimeValue::getLiveStringCount() == before);
}
#endif

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

BOOST_AUTO_TEST_CASE(stringref_parameters_accept_any_string_expression)
{
    std::vector<NomadString> consumed;
    const auto outcome = runWithNativeFunctions(
        "fun f a:string\n"
        "    test.ref.consume a\n"
        "    return a\n"
        "end\n"
        "s = \"variable\"\n"
        "test.ref.consume \"literal\"\n"
        "test.ref.consume s\n"
        "test.ref.consume test.name\n"
        "test.ref.consume $\"format {s}\"\n"
        "test.ref.consume f \"function\"\n"
        "return test.ref.echo s",
        consumed
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.text == "variable");
    BOOST_TEST(outcome.leakedStrings == 0);

    const std::vector<NomadString> expected{"literal", "variable", "name", "format variable", "function", "function"};
    BOOST_TEST(consumed == expected, boost::test_tools::per_element());
}

#ifdef NOMAD_DEBUG
BOOST_AUTO_TEST_CASE(stringref_arguments_borrow_without_copying)
{
    // A literal is borrowed from the string table: nothing is allocated.
    BOOST_TEST(runWithNativeFunctions("return test.ref.allocations \"literal\"").integerValue == 0);

    // A variable is borrowed: only the variable's own string is live.
    BOOST_TEST(runWithNativeFunctions("s = \"a\"\nreturn test.ref.allocations s").integerValue == 1);

    // A parameter is borrowed, and the literal was lent to the function: nothing is allocated.
    BOOST_TEST(
        runWithNativeFunctions("fun f a:string\n    return test.ref.allocations a\nend\nreturn f \"a\"").integerValue == 0
    );

    // An owned argument (a temporary) is still copied into the function: only it is live.
    BOOST_TEST(
        runWithNativeFunctions("fun f a:string\n    return test.ref.allocations a\nend\nreturn f test.name").integerValue == 1
    );

    // A temporary is owned for the duration of the call.
    BOOST_TEST(runWithNativeFunctions("return test.ref.allocations test.name").integerValue == 1);
}

BOOST_AUTO_TEST_CASE(string_equality_on_borrowable_operands_allocates_nothing)
{
    // `test.ref.allocations` receives the borrowed literal, so only strings created by the comparison would count.
    const auto outcome = runWithNativeFunctions(
        "fun f a:string\n"
        "    if a == \"x\"\n"
        "        return test.ref.allocations \"\"\n"
        "    end\n"
        "    return -1\n"
        "end\n"
        "return f \"x\""
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    // The literal `"x"` is lent to `f`, so nothing is live.
    BOOST_TEST(outcome.integerValue == 0);
    BOOST_TEST(outcome.leakedStrings == 0);
}
#endif

BOOST_AUTO_TEST_CASE(stringref_type_cannot_be_declared)
{
    BOOST_TEST(!compileErrors("fun f a:$stringref\n    return 1\nend\nreturn f \"x\"").empty());
    BOOST_TEST(!compileErrors("params a:$stringref\nreturn 1").empty());
    BOOST_TEST(!compileErrors("fun f a:$file\n    return 1\nend\nreturn 1").empty());
}

BOOST_AUTO_TEST_CASE(stringref_parameters_reject_non_string_arguments)
{
    std::vector<NomadString> consumed;
    const auto outcome = runWithNativeFunctions("test.ref.consume 1\nreturn 1", consumed);

    BOOST_TEST(!outcome.compiled);
    BOOST_TEST(outcome.diagnostics.find("Argument type mismatch") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(null_string_converts_to_empty_text)
{
    LanguageTestFixture fixture;
    RuntimeValue value;
    fixture.getRuntime().getStringType()->initValue(value);

    BOOST_TEST(fixture.getRuntime().getStringType()->toString(value) == "");
}

BOOST_AUTO_TEST_CASE(discarded_closure_string_result_is_freed)
{
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    std::unique_ptr<Closure> closure;

    runtime.registerNativeFunction(
        "test.storeText",
        [&runtime, &closure](VirtualMachine* virtualMachine) {
            closure = virtualMachine->createClosure(&runtime, virtualMachine->getIdParameter(0));
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

BOOST_AUTO_TEST_CASE(hidden_source_arguments_are_borrowed)
{
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    NomadString fileName;
    NomadString functionName;
    NomadInteger lineNumber = 0;
    NomadInteger allocations = -1;

    runtime.registerNativeFunction(
        "test.source",
        [&](VirtualMachine* virtualMachine) {
            allocations = RuntimeValue::getLiveStringCount() - s_liveStringBaseline;
            fileName = virtualMachine->getStringParameter(0);
            functionName = virtualMachine->getStringParameter(1);
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

    s_liveStringBaseline = RuntimeValue::getLiveStringCount();
    const auto outcome = fixture.execute("main");

    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(fileName == "main.nomad");
    BOOST_TEST(functionName == "main");
    BOOST_TEST(lineNumber == 2);
    BOOST_TEST(allocations == 0);
    BOOST_TEST(outcome.leakedStrings == 0);
}

BOOST_AUTO_TEST_CASE(function_string_parameters_do_not_leak)
{
    checkCases({
        // Borrowable arguments: literal, local variable, and the caller's own parameter.
        {"fun id a:string\n    return a\nend\nreturn id \"x\"", "x"},
        {"fun id a:string\n    return a\nend\ns = \"x\"\nreturn id s", "x"},
        {"fun inner a:string\n    return a\nend\nfun outer a:string\n    return inner a\nend\nreturn outer \"x\"", "x"},
        // Owned arguments: nativeFunction result, format string, function result, context variable.
        {"fun id a:string\n    return a\nend\nreturn id test.name", "name"},
        {"fun id a:string\n    return a\nend\ns = \"x\"\nreturn id $\"[{s}]\"", "[x]"},
        {"fun v\n    return \"v\"\nend\nfun id a:string\n    return a\nend\nreturn id v", "v"},
        // A borrowed parameter copied into a local.
        {"fun f a:string\n    b = a\n    return b\nend\nreturn f \"x\"", "x"},
        // Multiple string parameters mixing borrowed and owned arguments.
        {"fun join a:string b:string c:string\n    return $\"{a}{b}{c}\"\nend\ns = \"s\"\nreturn join s test.name \"l\"", "snamel"},
        // Function call as an expression inside a comparison.
        {"fun id a:string\n    return a\nend\nreturn (id \"x\") == \"x\"", "true"},
    });
}

BOOST_AUTO_TEST_CASE(function_parameters_borrowed_from_context_variables_are_copies)
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
    // `global.x` still owns "B"; nothing else is live.
    BOOST_TEST(outcome.leakedStrings == 1);
#endif

    const std::vector<NomadString> expected{"A", "A"};
    BOOST_TEST(consumed == expected, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(context_variables_passed_to_functions_are_copied)
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
            // `global.text` still owns its string; nothing else is live.
            BOOST_TEST(outcome.leakedStrings == 1);
#endif
        }
    }
}

BOOST_AUTO_TEST_CASE(closures_capture_copies_of_borrowed_parameters)
{
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    std::unique_ptr<Closure> closure;
    std::vector<NomadString> consumed;

    registerLifetimeNativeFunctions(runtime, consumed);
    runtime.registerNativeFunction(
        "test.storeText",
        [&runtime, &closure](VirtualMachine* virtualMachine) {
            closure = virtualMachine->createClosure(&runtime, virtualMachine->getIdParameter(0));
        },
        {
            defParameter("callback", runtime.getCallbackType({}, runtime.getStringType()), NomadParamDoc("Callback to store")),
        },
        runtime.getVoidType(),
        NomadDoc("Store a callback returning a string")
    );

    // `s` is lent to `keep` and freed when `main` returns; the closure must hold its own copy.
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
    // Only the captured copy is live.
    BOOST_TEST(RuntimeValue::getLiveStringCount() - before == 1);
#endif

    RuntimeValue result;
    runtime.executeFunction(closure.get(), {}, result);
    BOOST_TEST(result.getStringValue() == "name");
    runtime.getStringType()->freeValue(result);

    closure.reset();
#ifdef NOMAD_DEBUG
    BOOST_TEST(RuntimeValue::getLiveStringCount() == before);
#endif
}

#ifdef NOMAD_DEBUG
BOOST_AUTO_TEST_CASE(function_string_arguments_are_lent)
{
    // A local variable is lent: only the variable's own string is live inside the callee.
    BOOST_TEST(
        runWithNativeFunctions("fun f a:string\n    return test.ref.allocations a\nend\ns = \"a\"\nreturn f s").integerValue
        == 1
    );

    // A parameter is lent onward: still nothing allocated.
    BOOST_TEST(
        runWithNativeFunctions(
            "fun inner a:string\n    return test.ref.allocations a\nend\n"
            "fun outer a:string\n    return inner a\nend\n"
            "return outer \"a\""
        )
            .integerValue
        == 0
    );

    // A context variable is copied.
    BOOST_TEST(
        runWithNativeFunctions(
            "fun f a:string\n    return test.ref.allocations a\nend\nglobal.lent = \"a\"\nreturn f global.lent"
        )
            .integerValue
        == 2
    );
}
#endif

BOOST_AUTO_TEST_CASE(function_parameters_are_read_only)
{
    const auto errors = compileErrors("fun f a:string\n    a = \"b\"\n    return a\nend\nreturn f \"x\"");
    BOOST_TEST(errors.find("parameters are read-only") != std::string::npos, errors);
    BOOST_TEST(errors.find("'a'") != std::string::npos, errors);

    const auto integerErrors = compileErrors("fun f a:int\n    a = 2\n    return a\nend\nreturn f 1");
    BOOST_TEST(integerErrors.find("parameters are read-only") != std::string::npos, integerErrors);
}

BOOST_AUTO_TEST_SUITE_END()
