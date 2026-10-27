// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/script/FormatString.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/NativeFunction.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <sstream>
#include <string>
#include <vector>

using namespace nomad;
using namespace nomad::test;

namespace {

// Registers `test.add a:int b:int -> int`, `test.echo text:string -> string`, `test.record value:int` and `test.fail`.
void registerTestNativeFunctions(Runtime& runtime, std::vector<NomadInteger>& recorded) {
    runtime.registerNativeFunction(
        "test.add",
        [](VirtualMachine* virtualMachine) {
            virtualMachine->setIntegerResult(
                virtualMachine->getIntegerParameter(0) + virtualMachine->getIntegerParameter(1)
            );
        },
        {
            defParameter("a", runtime.getIntegerType(), NomadParamDoc("First value")),
            defParameter("b", runtime.getIntegerType(), NomadParamDoc("Second value")),
        },
        runtime.getIntegerType(),
        NomadDoc("Add two integers")
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
        "test.record",
        [&recorded](VirtualMachine* virtualMachine) {
            recorded.push_back(virtualMachine->getIntegerParameter(0));
        },
        {
            defParameter("value", runtime.getIntegerType(), NomadParamDoc("Value to record")),
        },
        runtime.getVoidType(),
        NomadDoc("Record an integer")
    );

    runtime.registerNativeFunction(
        "test.fail",
        [](VirtualMachine* virtualMachine) {
            virtualMachine->fault("test.fail called");
        },
        {},
        runtime.getVoidType(),
        NomadDoc("Fault the virtual machine")
    );
}

} // namespace

BOOST_AUTO_TEST_SUITE(language_format_strings)

BOOST_AUTO_TEST_CASE(dynamic_format_operator_uses_live_expression_context)
{
    const std::vector<std::pair<NomadString, NomadString>> cases{
        {"score = 10\nmessage = \"Score: {score}\"\nreturn $message", "Score: 10"},
        {"score = 10\nmessage = \"Score: {score}\"\nscore = 20\nreturn $message", "Score: 20"},
        {"x = 10\ny = 20\nmessage = \"total = {x + y}\"\nreturn $message", "total = 30"},
        {"fun render value:int\n    template = \"Value: {value}\"\n    return $template\nend\nreturn render 42", "Value: 42"},
        {"fun answer\n    return 42\nend\ntemplate = \"{answer}\"\nreturn $template", "42"},
        {"global.score = 9\ntemplate = \"{global.score + 1}\"\nreturn $template", "10"},
        {"name = \"nomad\"\ntemplate = \"{(name.toUpper)}\"\nreturn $template", "NOMAD"},
        {"score = 10\nreturn $(\"{false && missing}\")", "{invalid identifier: 'missing'}"},
        {"score = 10\nreturn $\"compiled {$(\"dynamic {1 + 10}\")}\"", "compiled dynamic 11"},
        {"template = \"unchanged \\\\n }\"\nreturn $template", "unchanged \\n }"},
        {"template = \"\"\nreturn $template", ""},
    };
    for (const auto& [source, expected] : cases) {
        BOOST_TEST_CONTEXT(source) {
            const auto outcome = runSource(source);
            BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
            BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
            BOOST_TEST(outcome.text == expected);
            BOOST_TEST(outcome.leakedStrings == 0);
        }
    }
}

BOOST_AUTO_TEST_CASE(dynamic_format_operator_recovers_from_bad_placeholders)
{
    const std::vector<std::pair<NomadString, NomadString>> cases{
        {"Score: {sore}", "Score: {invalid identifier: 'sore'}"},
        {"before {missing} after {score + 1}", "before {invalid identifier: 'missing'} after 11"},
        {"Score: {score", "Score: {missing `}`}"},
        {"before {} after {score}", "before {empty expression} after 10"},
        {"before {   } after {score}", "before {empty expression} after 10"},
        {"{score} tail {", "10 tail {missing `}`}"},
        {"{missing}{alsoMissing}{score}", "{invalid identifier: 'missing'}{invalid identifier: 'alsoMissing'}10"},
    };
    for (const auto& [format, expected] : cases) {
        BOOST_TEST_CONTEXT(format) {
            LanguageTestFixture fixture;
            fixture.addFunction("main", "params message:string\nscore = 10\nreturn $message");
            BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
            RuntimeValue argument;
            argument.setStringValue(format);
            const auto outcome = fixture.execute("main", {argument});
            argument.freeObjectValue();
            BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
            BOOST_TEST(outcome.text == expected);
            BOOST_TEST(outcome.leakedStrings == 0);
        }
    }
}

BOOST_AUTO_TEST_CASE(dynamic_format_reports_structured_diagnostics_and_continues)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);
    context.setVariable("score", runtime.getIntegerType(), RuntimeValue{NomadInteger{10}});
    const auto compiler = runtime.createCompiler();
    const auto before = runtime.getInstructions().size();
    const auto result = formatDynamicString("A{missing}B{score + 1}C{", context, "localized.nomad", 4, 7);
    BOOST_TEST(result.text == "A{invalid identifier: 'missing'}B11C{missing `}`}");
    BOOST_REQUIRE_EQUAL(result.diagnostics.size(), 2);
    BOOST_TEST(result.diagnostics[0].message == "invalid identifier: 'missing'");
    BOOST_TEST(result.diagnostics[0].sourceName == "localized.nomad");
    BOOST_TEST(result.diagnostics[0].line == 4);
    BOOST_TEST(result.diagnostics[0].column == 7);
    BOOST_TEST(result.diagnostics[0].offset == 1);
    BOOST_TEST(result.diagnostics[0].length == 9);
    BOOST_TEST(result.diagnostics[1].offset == 23);
    BOOST_TEST(result.diagnostics[1].length == 1);
    BOOST_TEST(!context.hasError());
    BOOST_TEST(runtime.getInstructions().size() == before);
    BOOST_REQUIRE(Interpreter{}.evaluateExpression("$(\"{score}\")", context));
    BOOST_TEST(context.getResult()->getValue().getStringValue() == "10");
}

BOOST_AUTO_TEST_CASE(dynamic_format_operator_validates_string_type_and_retains_strict_literals)
{
    for (const auto* source : {"return $10", "value = true\nreturn $value", "return $"}) {
        const auto outcome = runSource(source);
        BOOST_TEST(!outcome.compiled);
    }
    BOOST_TEST(!runSource("return $\"{missing}\"").compiled);
    BOOST_TEST(!runSource("return $\"{value\"").compiled);
}

BOOST_AUTO_TEST_CASE(dynamic_format_supports_nested_templates_and_quoted_braces)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);
    context.setVariable("score", runtime.getIntegerType(), RuntimeValue{NomadInteger{10}});
    const std::vector<std::pair<NomadString, NomadString>> cases{
        {"nested {$\"inner {score + 1}\"}", "nested inner 11"},
        {"quoted {\"{braces}\"}", "quoted {braces}"},
        {"nested bad {$\"bad {missing} good {score}\"} tail {score}",
            "nested bad bad {invalid identifier: 'missing'} good 10 tail 10"},
        {"{false && (1 == 2)}", "false"},
    };
    for (const auto& [format, expected] : cases) {
        const auto result = formatDynamicString(format, context, "nested.nomad", 1, 1);
        BOOST_TEST(result.text == expected);
        BOOST_TEST(!context.hasError());
    }
}

BOOST_AUTO_TEST_CASE(dynamic_format_recovers_from_syntax_type_and_void_errors_before_side_effects)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);
    NomadInteger calls = 0;
    runtime.registerNativeFunction("test.count", [&](VirtualMachine* calledMachine) {
        ++calls;
        calledMachine->setIntegerResult(1);
    }, {}, runtime.getIntegerType(), NomadDoc("Count calls"));
    runtime.registerNativeFunction("test.void", [&](VirtualMachine*) {
        ++calls;
    }, {}, runtime.getVoidType(), NomadDoc("Void call"));
    context.setVariable("score", runtime.getIntegerType(), RuntimeValue{NomadInteger{10}});
    for (const auto* expression : {"1 +", "score = 1", "(test.count) + true", "test.void", "1\n2"}) {
        BOOST_TEST_CONTEXT(expression) {
            const auto result = formatDynamicString(NomadString{"before {"} + expression + "} after {score}",
                context, "invalid.nomad", 1, 1);
            BOOST_REQUIRE_EQUAL(result.diagnostics.size(), 1);
            BOOST_TEST(result.text.starts_with("before {"));
            BOOST_TEST(result.text.ends_with("} after 10"));
            BOOST_TEST(calls == 0);
            BOOST_TEST(!context.hasError());
        }
    }
}

BOOST_AUTO_TEST_CASE(dynamic_format_propagates_called_function_faults_and_preserves_vm_usability)
{
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    runtime.registerNativeFunction("test.failValue", [](VirtualMachine* machine) {
        machine->fault("dynamic formatting call failed");
    }, {}, runtime.getIntegerType(), NomadDoc("Fault"));
    fixture.addFunction("main", "params message:string\nreturn $message");
    fixture.addFunction("failingScript", "return test.failValue");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto machine = runtime.createVirtualMachine();
    for (const auto* format : {"{test.failValue}", "{failingScript}"}) {
        RuntimeValue argument;
        argument.setStringValue(format);
#ifdef NOMAD_DEBUG
        const auto liveBefore = RuntimeValue::getLiveStringCount();
#endif
        const auto* function = runtime.getScriptFunction(runtime.getFunctionId("main"));
        BOOST_CHECK_THROW(machine->run(function, {argument}), VirtualMachineException);
#ifdef NOMAD_DEBUG
        BOOST_TEST(RuntimeValue::getLiveStringCount() == liveBefore);
#endif
        argument.freeObjectValue();
        InterpreterContext context(*machine);
        BOOST_CHECK_THROW(static_cast<void>(formatDynamicString(format, context, "fault.nomad", 1, 1)),
            VirtualMachineException);
        BOOST_REQUIRE(Interpreter{}.evaluateExpression("2 + 3", context));
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 5);
    }
}

BOOST_AUTO_TEST_CASE(dynamic_format_reads_current_values_in_order_and_restores_operands)
{
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    NomadInteger count = 0;
    runtime.registerDynamicVariable("test.next", {}, [&](VirtualMachine*, RuntimeValue& value) {
        value.setIntegerValue(++count);
    }, runtime.getIntegerType(), NomadDoc("Next value"));
    fixture.addFunction("main", "template = \"{test.next}:{test.next}\"\n"
        "first = $template\nsecond = $template\nreturn $\"{first}|{second}\"");
    fixture.addFunction("comparison", "template = \"value {test.next}\"\nreturn $template == $template");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto outcome = fixture.execute("main");
    BOOST_REQUIRE(!outcome.fault.has_value());
    BOOST_TEST(outcome.text == "1:2|3:4");
    BOOST_TEST(outcome.leakedStrings == 0);
    const auto comparison = fixture.execute("comparison");
    BOOST_REQUIRE(!comparison.fault.has_value());
    BOOST_TEST(!comparison.booleanValue);
    BOOST_TEST(count == 6);
    BOOST_TEST(comparison.leakedStrings == 0);
}

BOOST_AUTO_TEST_CASE(dynamic_format_evaluates_operand_once_and_does_not_execute_invalid_placeholder)
{
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    NomadInteger templateCalls = 0;
    NomadInteger expressionCalls = 0;
    runtime.registerNativeFunction("test.template", [&](VirtualMachine* machine) {
        ++templateCalls;
        machine->setStringResult("{(test.count) + missing}|{test.count}");
    }, {}, runtime.getStringType(), NomadDoc("Return template"));
    runtime.registerNativeFunction("test.count", [&](VirtualMachine* machine) {
        machine->setIntegerResult(++expressionCalls);
    }, {}, runtime.getIntegerType(), NomadDoc("Count calls"));
    fixture.addFunction("main", "return $(test.template)");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto outcome = fixture.execute("main");
    BOOST_REQUIRE(!outcome.fault.has_value());
    BOOST_TEST(outcome.text == "{invalid identifier: 'missing'}|1");
    BOOST_TEST(templateCalls == 1);
    BOOST_TEST(expressionCalls == 1);
    BOOST_TEST(outcome.leakedStrings == 0);
}

BOOST_AUTO_TEST_CASE(format_string_substitutes_variables)
{
    const auto outcome = runSource(
        "name = \"J-F\"\n"
        "score = 10\n"
        "return $\"Player {name} got {score} points\""
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.returnTypeName == "string");
    BOOST_TEST(outcome.text == "Player J-F got 10 points");
}

BOOST_AUTO_TEST_CASE(format_string_forms)
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"return $\"\"", ""},
        {"return $\"no placeholders\"", "no placeholders"},
        {"a = 1\nreturn $\"{a}\"", "1"},
        {"a = 1\nb = 2\nreturn $\"{a}{b}\"", "12"},
        {"a = -3\nreturn $\"[{a}]\"", "[-3]"},
        {"a = 1\nreturn $\"{a} and {a}\"", "1 and 1"},
        {"flag = true\nreturn $\"{flag}\"", "true"},
        {"text = \"inner\"\nreturn $\"<{text}>\"", "<inner>"},
        {"return $\"line\\nbreak\"", "line\nbreak"},
        {"global.formatValue = 5\nreturn $\"{global.formatValue}\"", "5"},
        {"const LIMIT = 9\nreturn $\"{LIMIT}\"", "9"},
    };

    for (const auto& [source, expected]: cases) {
        BOOST_TEST_CONTEXT(source) {
            const auto outcome = runSource(source);

            BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
            BOOST_TEST(outcome.text == expected);
        }
    }
}

BOOST_AUTO_TEST_CASE(format_string_uses_current_values)
{
    const auto outcome = runSource(
        "fun describe value:int\n"
        "    return $\"value={value}\"\n"
        "end\n"
        "first = describe 1\n"
        "second = describe 2\n"
        "return $\"{first},{second}\""
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.text == "value=1,value=2");
}

BOOST_AUTO_TEST_CASE(format_string_calls_parameterless_functions)
{
    const auto outcome = runSource(
        "fun answer\n"
        "    return 42\n"
        "end\n"
        "fun label\n"
        "    return \"x\"\n"
        "end\n"
        "return $\"{label}={answer}\""
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.text == "x=42");
}

BOOST_AUTO_TEST_CASE(format_string_function_preserves_caller_state)
{
    const auto outcome = runSource(
        "fun answer\n"
        "    value = 40 + 2\n"
        "    return value\n"
        "end\n"
        "before = 7\n"
        "text = $\"{answer}\"\n"
        "after = before + 1\n"
        "return $\"{before}:{text}:{after}\""
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_TEST(outcome.text == "7:42:8");
}

BOOST_AUTO_TEST_CASE(format_string_rejects_unknown_identifiers)
{
    const auto errors = compileErrors("return $\"{missing}\"");

    BOOST_TEST(errors.find("missing") != std::string::npos, errors);
}

BOOST_AUTO_TEST_CASE(format_string_rejects_functions_with_parameters)
{
    const auto errors = compileErrors("fun double value:int\n    return value * 2\nend\nreturn $\"{double}\"");

    BOOST_TEST(errors.find("double") != std::string::npos, errors);
}

BOOST_AUTO_TEST_CASE(format_string_rejects_malformed_placeholders)
{
    for (const auto* source: {"a = 1\nreturn $\"{a\"", "return $\"{}\"", "return $\"unterminated"}) {
        BOOST_TEST_CONTEXT(source) {
            BOOST_TEST(!compileErrors(source).empty());
        }
    }
}

BOOST_AUTO_TEST_CASE(format_string_executes_compiled_expressions)
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"x = 10\ny = 20\nreturn $\"total = {x + y}\"", "total = 30"},
        {"fun add a:int b:int\n    return a + b\nend\nreturn $\"{add 10 20}\"", "30"},
        {"fun describe value:int\n    local = 2\n    return $\"{value + local}\"\nend\nreturn describe 10", "12"},
        {R"(score = 10
return $"outer {$"inner {score + 1}"}")", "outer inner 11"},
        {"x = 3\nreturn $\"{x}:{x + 2}:{x}:{(x + 2) * 4}\"", "3:5:3:20"},
        {"flag = true\nx = 3\nreturn $\"{!flag}:{x > 2 && flag}:{x & 1}:{-x}\"", "false:true:1:-3"},
        {"text = \" hi \"\nreturn $\"{(text.trim)}:{(text.length)}\"", "hi:4"},
        {"x = 1.5\nreturn $\"{x * 2.0}\"", "3"},
        {"return $\"{1 + 2}:{true}:{\"}\"}\"", "3:true:}"},
        {"fun describe value:int\n    return $\"{value + 1}\"\nend\nfirst = describe 1\nsecond = describe 9\nreturn $\"{first}:{second}\"", "2:10"},
        {"x = 1\nfirst = $\"{x + 1}\"\nx = 5\nsecond = $\"{x + 1}\"\nreturn $\"{first}:{second}\"", "2:6"},
    };
    for (const auto& [source, expected] : cases) {
        BOOST_TEST_CONTEXT(source) {
            const auto outcome = runSource(source);
            BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
            BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
            BOOST_TEST(outcome.text == expected);
            BOOST_TEST(outcome.leakedStrings == 0);
        }
    }
}

BOOST_AUTO_TEST_CASE(format_string_expression_reports_type_and_syntax_errors)
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"return $\"{1 +}\"", "Unexpected end of line"},
        {"return $\"{(1 + 2}\"", "Expected ')'"},
        {"return $\"{1 2}\"", "Expected end of line"},
        {"return $\"{missing + 1}\"", "Unknown identifier"},
        {"return $\"{1 + true}\"", "operator"},
        {"fun noValue\nend\nreturn $\"{(noValue)}\"", "does not return a value"},
    };

    for (const auto& [source, expected] : cases) {
        BOOST_TEST_CONTEXT(source) {
            const auto errors = compileErrors(source);
            BOOST_TEST(errors.find(expected) != std::string::npos, errors);
        }
    }
}

BOOST_AUTO_TEST_CASE(format_string_resolves_bare_native_members_in_lexical_scope)
{
    const std::vector<std::pair<NomadString, NomadString>> cases{
        {"name = \"nomad\"\nreturn $\"{name.toUpper}:{name.length}\"", "NOMAD:5"},
        {"name = \" nomad \"\nreturn $\"{name.trim.toUpper}:{name.trim.length}\"", "NOMAD:5"},
        {"fun render name:string\n    return $\"{name.toUpper}\"\nend\nreturn render \"nomad\"", "NOMAD"},
        {"name = \"nomad\"\nreturn $\"outer {$\"inner {$\"{name.toUpper}\"}\"}\"", "outer inner NOMAD"},
        {"name.toUpper = 7\nreturn $\"{$\"{name.toUpper}:{name.toUpper + 1}\"}\"", "7:8"},
        {"name.toUpper = 7\nreturn $\"{name.toUpper}\"", "7"},
        {"text = $\"{label.toUpper}:{add 10 20}\"\n"
            "fun label\n    return \"nomad\"\nend\n"
            "fun add left:int right:int\n    return left + right\nend\nreturn text", "NOMAD:30"},
    };
    for (const auto& [source, expected] : cases) {
        BOOST_TEST_CONTEXT(source) {
            const auto outcome = runSource(source);
            BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
            BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
            BOOST_TEST(outcome.text == expected);
            BOOST_TEST(outcome.leakedStrings == 0);
        }
    }
}

BOOST_AUTO_TEST_CASE(format_string_bare_members_use_forward_inferred_function_types)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main", "name = zLabel\nreturn $\"{name.toUpper}:{zLabel.length}\"");
    fixture.addFunction("zLabel", "return \"nomad\"");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto outcome = fixture.execute("main");
    BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
    BOOST_TEST(outcome.text == "NOMAD:5");
    BOOST_TEST(outcome.leakedStrings == 0);
}

BOOST_AUTO_TEST_CASE(format_string_bare_members_reject_invalid_receivers_and_preserve_native_rules)
{
    for (const auto* source : {
             "name = 10\nreturn $\"{name.toUpper}\"",
             "return $\"{missing.toUpper}\"",
             "name = \"nomad\"\nreturn $\"{name.unknownMember}\"",
             "name = \"nomad\"\nreturn $\"{name.contains}\"",
             "return $\"{math.floor}\"",
         }) {
        BOOST_TEST_CONTEXT(source) {
            BOOST_TEST(!compileErrors(source).empty());
        }
    }
}

BOOST_AUTO_TEST_CASE(format_string_direct_lookup_does_not_create_expression_functions)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main",
        "if false\n    message = $\"{name.toUpper}\"\nend\n"
        "name.toUpper = 7\nscore = 10\nreturn $\"{name.toUpper}:{score}\"");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    std::vector<ScriptFunction*> functions;
    fixture.getRuntime().getFunctions(functions);
    BOOST_TEST(functions.size() == 1);
    const auto outcome = fixture.execute("main");
    BOOST_TEST(outcome.text == "7:10");
}

BOOST_AUTO_TEST_CASE(format_string_deferred_nested_errors_preserve_source_locations)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main", "name = \"nomad\"\nreturn $\"outer {$\"{missing + 1}\"}\"");
    BOOST_TEST(!fixture.compile());
    const auto* error = fixture.findError("Unknown identifier: missing");
    BOOST_REQUIRE_MESSAGE(error != nullptr, fixture.getDiagnostics());
    BOOST_TEST(error->line == 2);
    BOOST_TEST(error->column == 28);
}

BOOST_AUTO_TEST_CASE(format_string_expression_calls_native_functions)
{
    std::vector<NomadInteger> recorded;
    LanguageTestFixture fixture;
    registerTestNativeFunctions(fixture.getRuntime(), recorded);
    fixture.addFunction("main",
        "name = \"hello\"\nvalue = 4\nreturn $\"{test.add value 3}:{test.echo name}\"");

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto outcome = fixture.execute("main");
    BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
    BOOST_TEST(outcome.text == "7:hello");
    BOOST_TEST(outcome.leakedStrings == 0);
}

BOOST_AUTO_TEST_CASE(format_string_expression_uses_forward_inferred_local_type)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main", "value = zAnswer\nreturn $\"{value + 1}\"");
    fixture.addFunction("zAnswer", "return 41");

    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    const auto outcome = fixture.execute("main");
    BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
    BOOST_TEST(outcome.text == "42");
}

BOOST_AUTO_TEST_CASE(format_string_expression_reads_dynamic_values_at_execution)
{
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    auto current = NomadInteger{3};
    runtime.registerDynamicVariable(
        "test.current",
        [&current](VirtualMachine*, const RuntimeValue& value) { current = value.getIntegerValue(); },
        [&current](VirtualMachine*, RuntimeValue& value) { value.setIntegerValue(current); },
        runtime.getIntegerType(),
        NomadDoc("Current test value")
    );
    fixture.addFunction("main", "return $\"{test.current + 1}\"");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(fixture.execute("main").text == "4");
    current = 10;
    BOOST_TEST(fixture.execute("main").text == "11");
}

BOOST_AUTO_TEST_CASE(format_string_expression_placeholders_execute_in_source_order)
{
    const auto outcome = runSource(
        "fun advance\n    global.score = global.score + 1\n    return global.score\nend\n"
        "global.score = 0\n"
        "return $\"{global.score}:{advance + 0}:{global.score}:{advance}:{global.score + 0}\""
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
    BOOST_TEST(outcome.text == "0:1:1:2:2");
}

BOOST_AUTO_TEST_CASE(format_string_expression_preserves_enclosing_operands)
{
    const auto outcome = runSource(
        "x = 5\n"
        "same = $\"{x + 1}\" == $\"{x * 2 - 4}\"\n"
        "different = $\"{$\"{x}\"}\" != \"other\"\n"
        "return $\"{same && different}:{x}\""
    );

    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
    BOOST_TEST(outcome.text == "true:5");
    BOOST_TEST(outcome.leakedStrings == 0);
}

BOOST_AUTO_TEST_CASE(format_string_expression_short_circuits_without_calling_faulting_function)
{
    std::vector<NomadInteger> recorded;
    LanguageTestFixture fixture;
    registerTestNativeFunctions(fixture.getRuntime(), recorded);
    fixture.addFunction("main",
        "fun fail\n    test.fail\n    return true\nend\n"
        "flag = false\nreturn $\"{flag && fail}:{!flag || fail}\"");

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto outcome = fixture.execute("main");
    BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
    BOOST_TEST(outcome.text == "false:true");
}

BOOST_AUTO_TEST_CASE(format_string_expression_fault_releases_captures_and_restores_registers)
{
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    runtime.registerNativeFunction(
        "test.explode",
        [](VirtualMachine* machine) { machine->fault("expression failed"); },
        {defParameter("text", runtime.getStringType(), NomadParamDoc("Captured text"))},
        runtime.getIntegerType(),
        NomadDoc("Fail expression execution")
    );
    fixture.addFunction("main", "params text:string\nreturn $\"{test.explode text}\"");
    fixture.addFunction("healthy", "return $\"{1 + 2}\"");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

    auto machine = runtime.createVirtualMachine();
    RuntimeValue argument;
    argument.setStringValue("capture");
#ifdef NOMAD_DEBUG
    const auto liveBefore = RuntimeValue::getLiveStringCount();
#endif
    machine->setIntegerResult(77);
    machine->setIntegerIntermediate(88);
    BOOST_CHECK_EXCEPTION(
        machine->run(runtime.getScriptFunction(runtime.getFunctionId("main")), {argument}),
        VirtualMachineException,
        [](const VirtualMachineException& exception) { return std::string(exception.what()) == "expression failed"; }
    );
    BOOST_TEST(machine->getIntegerResult() == 77);
    BOOST_TEST(machine->getIntegerIntermediate() == 88);
#ifdef NOMAD_DEBUG
    BOOST_TEST(RuntimeValue::getLiveStringCount() == liveBefore);
#endif
    argument.freeObjectValue();

    machine->run(runtime.getScriptFunction(runtime.getFunctionId("healthy")), {});
    BOOST_TEST(machine->getStringResult()->getString() == "3");
    runtime.getStringType()->freeValue(machine->getResult());
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(language_nativeFunctions)

BOOST_AUTO_TEST_CASE(nativeFunction_returns_value)
{
    std::vector<NomadInteger> recorded;
    LanguageTestFixture fixture;
    registerTestNativeFunctions(fixture.getRuntime(), recorded);
    fixture.addFunction("main", "return test.add 40 2");

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

    const auto outcome = fixture.execute("main");
    BOOST_TEST(outcome.returnTypeName == "int");
    BOOST_TEST(outcome.integerValue == 42);
}

BOOST_AUTO_TEST_CASE(nativeFunction_arguments_are_expressions)
{
    std::vector<NomadInteger> recorded;
    LanguageTestFixture fixture;
    registerTestNativeFunctions(fixture.getRuntime(), recorded);
    fixture.addFunction(
        "main",
        "a = 3\n"
        "test.record (a * 2)\n"
        "test.record a\n"
        "test.record (test.add a 4)\n"
        "return test.echo $\"a={a}\""
    );

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

    const auto outcome = fixture.execute("main");
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.text == "a=3");
    BOOST_TEST(recorded == (std::vector<NomadInteger>{6, 3, 7}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(nativeFunction_string_results_are_owned_values)
{
    std::vector<NomadInteger> recorded;
    LanguageTestFixture fixture;
    registerTestNativeFunctions(fixture.getRuntime(), recorded);
    fixture.addFunction(
        "main",
        "fun wrap text:string\n"
        "    return $\"[{text}]\"\n"
        "end\n"
        "name = \"n\"\n"
        "nested = test.echo (test.echo (test.echo \"deep\"))\n"
        "wrapped = wrap (test.echo \"w\")\n"
        "formatted = test.echo $\"f={name}\"\n"
        "same = (test.echo \"x\") == (test.echo \"x\")\n"
        "different = (test.echo \"x\") != $\"{name}\"\n"
        "if same && different\n"
        "    return $\"{nested} {wrapped} {formatted}\"\n"
        "end\n"
        "return \"comparison failed\""
    );

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

    const auto outcome = fixture.execute("main");
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.text == "deep [w] f=n");
}

BOOST_AUTO_TEST_CASE(nativeFunctions_execute_in_order)
{
    std::vector<NomadInteger> recorded;
    LanguageTestFixture fixture;
    registerTestNativeFunctions(fixture.getRuntime(), recorded);
    fixture.addFunction("main", "test.record 1\ntest.record 2\ntest.record 3");

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(!fixture.execute("main").fault.has_value());
    BOOST_TEST(recorded == (std::vector<NomadInteger>{1, 2, 3}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(calls_with_more_than_three_arguments_pop_the_stack)
{
    std::vector<NomadInteger> recorded;
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    registerTestNativeFunctions(runtime, recorded);
    runtime.registerNativeFunction(
        "test.digits",
        [](VirtualMachine* virtualMachine) {
            virtualMachine->setIntegerResult(
                virtualMachine->getIntegerParameter(0) * 1000 + virtualMachine->getIntegerParameter(1) * 100
                + virtualMachine->getIntegerParameter(2) * 10 + virtualMachine->getIntegerParameter(3)
            );
        },
        {
            defParameter("a", runtime.getIntegerType(), NomadParamDoc("Thousands")),
            defParameter("b", runtime.getIntegerType(), NomadParamDoc("Hundreds")),
            defParameter("c", runtime.getIntegerType(), NomadParamDoc("Tens")),
            defParameter("d", runtime.getIntegerType(), NomadParamDoc("Units")),
        },
        runtime.getIntegerType(),
        NomadDoc("Combine four digits")
    );

    // Both calls pop four arguments with `op_pop_n`; a wrong count would corrupt `local`.
    fixture.addFunction(
        "main",
        "fun digits a:int b:int c:int d:int\n"
        "    return a * 1000 + b * 100 + c * 10 + d\n"
        "end\n"
        "local = 5\n"
        "first = test.digits 1 2 3 4\n"
        "second = digits 4 3 2 1\n"
        "return first + second + local"
    );

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

    const auto outcome = fixture.execute("main");
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 1234 + 4321 + 5);

    std::ostringstream disassembly;
    runtime.dumpInstructions(disassembly);
    const auto text = disassembly.str();

    std::size_t popCount = 0;
    for (auto position = text.find("op_pop_n\n"); position != std::string::npos;
         position = text.find("op_pop_n\n", position + 1)) {
        const auto operandStart = text.find(": ", position) + 2;
        BOOST_TEST(text.substr(operandStart, text.find('\n', operandStart) - operandStart) == "4");
        ++popCount;
    }
    BOOST_TEST(popCount == 2u);
}

BOOST_AUTO_TEST_CASE(nativeFunction_fault_stops_function)
{
    std::vector<NomadInteger> recorded;
    LanguageTestFixture fixture;
    registerTestNativeFunctions(fixture.getRuntime(), recorded);
    fixture.addFunction("main", "test.record 1\ntest.fail\ntest.record 2");

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

    const auto outcome = fixture.execute("main");
    BOOST_REQUIRE(outcome.fault.has_value());
    BOOST_TEST(*outcome.fault == "test.fail called");
    BOOST_TEST(recorded == (std::vector<NomadInteger>{1}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(invalid_nativeFunction_calls_are_rejected)
{
    const std::vector<std::string> sources = {
        "test.add 1",
        "test.add 1 2 3",
        "test.add 1.0 2",
        "test.add \"1\" 2",
        "test.echo 1",
        "value = test.record 1",
        "test.unknown 1",
        "test.add = 1",
    };

    for (const auto& source: sources) {
        BOOST_TEST_CONTEXT(source) {
            std::vector<NomadInteger> recorded;
            LanguageTestFixture fixture;
            registerTestNativeFunctions(fixture.getRuntime(), recorded);
            fixture.addFunction("main", source);

            BOOST_TEST(!fixture.compile());
            BOOST_TEST(fixture.getErrorCount() > 0u);
        }
    }
}

BOOST_AUTO_TEST_CASE(predefined_nativeFunctions_are_available)
{
    const auto floor = runExpression("math.floor 2.75");
    BOOST_REQUIRE_MESSAGE(floor.compiled, floor.diagnostics);
    BOOST_TEST(floor.floatValue == 2.0f);

    const auto printed = runSource("print \"language test\"");
    BOOST_REQUIRE_MESSAGE(printed.compiled, printed.diagnostics);
    BOOST_TEST(!printed.fault.has_value());
}

BOOST_AUTO_TEST_SUITE_END()
