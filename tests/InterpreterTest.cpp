// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/SyntaxTree.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VariableContext.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/SystemFunctions.hpp>

#include <stdexcept>

using namespace nomad;

namespace {

void evaluateFrameExpressions(VirtualMachine* machine) {
    auto* function = machine->getCurrentFunction();
    BOOST_REQUIRE(function != nullptr);
    InterpreterContext context(*machine, *function);
    BOOST_TEST(context.getVirtualMachine() == machine);
    BOOST_TEST(context.getFunction() != function);
    BOOST_TEST(context.getFunction()->getParameterCount() == function->getParameterCount());
    BOOST_TEST(context.isFrameBacked());
    const auto savedResult = machine->getResult();
    const auto savedIntermediate = machine->getIntermediate();
    const auto functionsBefore = [&] {
        std::vector<ScriptFunction*> functions;
        machine->getRuntime()->getFunctions(functions);
        return functions.size();
    }();
    const auto variableCount = function->getVariableCount();
    const auto parameterCount = function->getParameterCount();
    const auto instructionCount = machine->getRuntime()->getInstructions().size();
    const auto captures = function->getCaptureCount();

    const std::vector<std::pair<NomadString, NomadString>> cases{
        {"value + local", "42"},
        {"text.toUpper", "HELLO"},
        {"answer + value", "43"},
        {"global.score + value", "45"},
        {"test.native text", "hello"},
        {"test.parameter", "40"},
        {"false && test.fail", "false"},
        {"$\"{1+2}\"", "3"},
        {"$(\"value {value} local {local} captured {captured}\")", "value 40 local 2 captured 6"},
    };
    for (const auto& [source, expected] : cases) {
        const auto succeeded = Interpreter{}.evaluateExpression(source, context);
        const auto diagnostic = context.getError() ? *context.getError() : source;
        BOOST_REQUIRE_MESSAGE(succeeded, diagnostic);
        BOOST_REQUIRE(context.getResult() != nullptr);
        BOOST_TEST(context.getResult()->getType()->toString(context.getResult()->getValue()) == expected);
    }
    BOOST_REQUIRE(Interpreter{}.evaluateExpression("$\"{value}\"", context));
    BOOST_TEST(context.getResult()->getValue().getStringValue() == "40");
    BOOST_REQUIRE(Interpreter{}.execute("$\"{value + local}\"", context));
    BOOST_TEST(context.getResult()->getValue().getStringValue() == "42");
    for (const auto* source : {"", "value = 1", "missing + 1", "1 +", "1\n2", "test.void", "uncaptured"}) {
        BOOST_TEST_CONTEXT(source) {
            BOOST_TEST(!Interpreter{}.evaluateExpression(source, context));
            BOOST_TEST(context.hasError());
            BOOST_TEST(context.getResult() == nullptr);
        }
    }
    BOOST_CHECK_THROW(static_cast<void>(Interpreter{}.evaluateExpression("test.fail", context)), VirtualMachineException);
    BOOST_REQUIRE(Interpreter{}.execute("local = 7", context));
    BOOST_TEST(context.getVariableValue("local")->getIntegerValue() == 7);
    BOOST_TEST(!Interpreter{}.execute("local = true", context));
    BOOST_TEST(context.getVariableValue("local")->getIntegerValue() == 7);
    BOOST_REQUIRE(Interpreter{}.execute("value = 1", context));
    BOOST_REQUIRE(Interpreter{}.execute("text = \"changed\"", context));
    BOOST_TEST(context.getVariableValue("value")->getIntegerValue() == 1);
    BOOST_TEST(machine->getParameter(0).getIntegerValue() == 1);
    BOOST_REQUIRE(Interpreter{}.execute("created = local + value", context));
    BOOST_REQUIRE(Interpreter{}.evaluateExpression("created", context));
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 8);
    BOOST_REQUIRE(Interpreter{}.execute("created = \"forty-seven\"", context));
    BOOST_REQUIRE(Interpreter{}.evaluateExpression("created", context));
    BOOST_TEST(context.getResult()->getValue().getStringValue() == "forty-seven");
    BOOST_TEST(!Interpreter{}.execute("unknown.name = 1", context));
    BOOST_TEST(context.getVariableValue("unknown.name") == nullptr);
    BOOST_REQUIRE(Interpreter{}.execute("localText = \"first\"", context));
    BOOST_REQUIRE(Interpreter{}.execute("localText = localText.toUpper", context));
    BOOST_TEST(context.getVariableValue("localText")->getStringValue() == "FIRST");
    BOOST_TEST(!Interpreter{}.execute("localText = 1", context));
    context.setVariable("value", machine->getRuntime()->getIntegerType(), RuntimeValue{NomadInteger{1}});
    BOOST_CHECK_THROW(context.setVariable("local", machine->getRuntime()->getBooleanType(),
        RuntimeValue{true}), std::invalid_argument);
    BOOST_TEST(machine->getResult().getIntegerValue() == savedResult.getIntegerValue());
    BOOST_TEST(machine->getIntermediate().getIntegerValue() == savedIntermediate.getIntegerValue());
    BOOST_TEST(function->getVariableCount() == variableCount);
    BOOST_TEST(function->getParameterCount() == parameterCount);
    BOOST_TEST(function->getCaptureCount() == captures);
    BOOST_TEST(machine->getRuntime()->getInstructions().size() == instructionCount);
    std::vector<ScriptFunction*> functions;
    machine->getRuntime()->getFunctions(functions);
    BOOST_TEST(functions.size() == functionsBefore);
    BOOST_TEST(Interpreter{}.evaluateExpression("value + 2", context));
}

} // namespace

BOOST_AUTO_TEST_CASE(interpreter_executes_conditional_loops)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);
    const auto succeeded = Interpreter{}.execute(
        "value = 0\n"
        "total = 0\n"
        "while value < 6\n"
        "    value = value + 1\n"
        "    if value == 2 continue\n"
        "    if value == 5 break\n"
        "    total = total + value\n"
        "end\n"
        "do\n"
        "    total = total + 1\n"
        "    continue\n"
        "end until true", context);
    const auto diagnostic = context.getError() ? *context.getError() : "Loop execution failed";
    BOOST_REQUIRE_MESSAGE(succeeded, diagnostic);
    BOOST_REQUIRE(Interpreter{}.evaluateExpression("total", context));
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 9);
    BOOST_TEST(!Interpreter{}.execute("break", context));
}

BOOST_AUTO_TEST_CASE(interpreter_detects_type_changes_across_loop_iterations)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);
    BOOST_TEST(!Interpreter{}.execute(
        "value = 1\n"
        "while value > 0\n"
        "    value = \"changed\"\n"
        "end", context));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(context.getError()->find("changed type") != NomadString::npos);
    BOOST_REQUIRE(Interpreter{}.execute("value = 2", context));
    BOOST_REQUIRE(Interpreter{}.evaluateExpression("value + 1", context));
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 3);
}

BOOST_AUTO_TEST_CASE(frame_interpreter_requires_an_active_script_function)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    ScriptFunction function(CallableId::INVALID, "inactive", "inactive.nomad", "");
    BOOST_CHECK_THROW((InterpreterContext{*machine, function}), NomadBug);
}

BOOST_AUTO_TEST_CASE(interpreter_uses_named_source_text_and_compiled_source_batches)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    const SourceBatch batch{{{"answer", {"untitled:answer", "return 42"}}}, {}};
    CompilerContext compilation(compiler.get());
    compiler->registerSources(&compilation, batch);
    BOOST_REQUIRE(compiler->compileFunctions(&compilation));

    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);
    const Interpreter interpreter;
    BOOST_REQUIRE(interpreter.execute(SourceText{"console:input", "value = answer"}, context));
    BOOST_REQUIRE(interpreter.evaluateExpression(SourceText{"console:input", "value + 1"}, context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 43);

    for (const auto* text : {"missing", "\"unterminated", "1 +", "1\n2"}) {
        const SourceText source{"console:input", text};
        BOOST_TEST(!interpreter.execute(source, context));
        BOOST_REQUIRE(!context.getDiagnostics().empty());
        for (const auto& diagnostic : context.getDiagnostics()) {
            BOOST_TEST(diagnostic.sourceName == source.sourceName);
        }
        BOOST_TEST(!interpreter.evaluateExpression(source, context));
        BOOST_REQUIRE(!context.getDiagnostics().empty());
        for (const auto& diagnostic : context.getDiagnostics()) {
            BOOST_TEST(diagnostic.sourceName == source.sourceName);
        }
    }
}

BOOST_AUTO_TEST_CASE(frame_interpreter_reads_live_parameters_locals_and_calls_on_active_vm)
{
    Runtime runtime;
    runtime.registerNativeFunction("test.native",
        [](VirtualMachine* machine) { machine->setStringResult(machine->getStringParameter(0)->getString()); },
        {defParameter("text", runtime.getStringType(), NomadParamDoc("Text"))},
        runtime.getStringType(), NomadDoc("Echo"));
    runtime.registerNativeFunction("test.fail",
        [](VirtualMachine* machine) { machine->fault("frame failure"); },
        {}, runtime.getBooleanType(), NomadDoc("Fail"));
    runtime.registerNativeFunction("test.void",
        [](VirtualMachine*) {}, {}, runtime.getVoidType(), NomadDoc("No value"));
    runtime.registerDynamicVariable("test.parameter",
        [](VirtualMachine*, const RuntimeValue&) {},
        [](VirtualMachine* machine, RuntimeValue& value) { value = machine->getParameter(0); },
        runtime.getIntegerType(), NomadDoc("Current frame parameter"));

    auto compiler = runtime.createCompiler();
    CompilerContext compilation(compiler.get());
    compiler->registerSources(&compilation, {{{"answer", {"answer.nomad", "return 3"}}}, {}});
    BOOST_REQUIRE(compiler->compileFunctions(&compilation));
    const auto parentId = runtime.registerScriptFunction("parent", "frame.nomad", "");
    auto* parent = runtime.getScriptFunction(parentId);
    parent->registerVariable("uncaptured", runtime.getIntegerType());
    const auto id = runtime.registerScriptFunction("frame", "frame.nomad", "");
    auto* function = runtime.getScriptFunction(id);
    function->setParentId(parentId);
    function->addParameter("value", runtime.getIntegerType(), "");
    function->addParameter("text", runtime.getStringType(), "");
    function->createCapture(VariableId{0}, "captured", runtime.getIntegerType());
    function->registerVariable("local", runtime.getIntegerType());
    function->registerVariable("localText", runtime.getStringType());
    function->setReturnType(runtime.getVoidType());
    function->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(function);
    const auto initializeFrame = runtime.registerInstruction("test.initializeFrame", [](VirtualMachine* machine) {
        machine->setFunctionVariableValue(
            machine->getCurrentFunction()->getVariableId("local"), RuntimeValue{NomadInteger{2}});
        machine->setIntegerResult(77);
        machine->setIntegerIntermediate(88);
    }, NomadDoc("Initialize the test frame"), {});
    const auto evaluateFrame = runtime.registerInstruction("test.evaluateFrame", evaluateFrameExpressions,
        NomadDoc("Evaluate expressions in the test frame"), {});
    compiler->addOpCode(initializeFrame);
    compiler->addOpCode(evaluateFrame);
    compiler->addFunctionReturn(function);
    function->setFunctionEnd(runtime.getInstructions().size());
    const auto contextId = runtime.registerOrUpdateContextVariable("global.score", runtime.getIntegerType());
    runtime.setContextVariableValue(contextId, RuntimeValue{NomadInteger{5}});

    auto machine = runtime.createVirtualMachine();
    RuntimeValue text;
    text.setStringValue("hello");
#ifdef NOMAD_DEBUG
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
#endif
    machine->run(function, {RuntimeValue{NomadInteger{40}}, text}, {RuntimeValue{NomadInteger{6}}});
#ifdef NOMAD_DEBUG
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
#endif
    text.freeObjectValue();
}

BOOST_AUTO_TEST_CASE(interpreter_context_stores_persistent_typed_variables)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);

    const RuntimeValue integerValue{NomadInteger{42}};
    context.setVariable("score", runtime.getIntegerType(), integerValue);

    const auto* variableValue = context.getVariableValue("score");
    BOOST_REQUIRE(variableValue != nullptr);
    BOOST_TEST(context.getVariableType("score") == runtime.getIntegerType());
    BOOST_TEST(variableValue->getIntegerValue() == 42);

    const RuntimeValue updatedValue{NomadInteger{99}};
    context.setVariable("score", runtime.getIntegerType(), updatedValue);
    BOOST_TEST(context.getVariableValue("score")->getIntegerValue() == 99);
}

BOOST_AUTO_TEST_CASE(interpreter_contexts_are_independent_and_borrow_the_callers_vm)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter;
    {
        InterpreterContext first(*machine);
        InterpreterContext second(*machine);
        BOOST_TEST(first.getVirtualMachine() == machine.get());
        runtime.registerNativeFunction("test.sameVm", [&](VirtualMachine* calledMachine) {
            calledMachine->setBooleanResult(calledMachine == machine.get());
        }, {}, runtime.getBooleanType(), NomadDoc("Check VM identity"));
        machine->setIntegerResult(71);
        machine->setIntegerIntermediate(72);
        BOOST_REQUIRE(interpreter.execute("test.sameVm", first));
        BOOST_TEST(first.getResult()->getValue().getBooleanValue());
        BOOST_TEST(machine->getResult().getIntegerValue() == 71);
        BOOST_TEST(machine->getIntermediate().getIntegerValue() == 72);
        BOOST_REQUIRE(interpreter.execute("score = 10", first));
        BOOST_TEST(!interpreter.evaluateExpression("score", second));
        BOOST_REQUIRE(interpreter.execute("score = \"second\"", second));
        BOOST_REQUIRE(interpreter.evaluateExpression("score + 5", first));
        BOOST_TEST(first.getResult()->getValue().getIntegerValue() == 15);
        BOOST_TEST(second.getVariableType("score") == runtime.getStringType());
        BOOST_TEST(!interpreter.execute("unknown.name = 1", first));
        BOOST_TEST(first.getVariableValue("unknown.name") == nullptr);
    }
    InterpreterContext next(*machine);
    BOOST_REQUIRE(interpreter.evaluateExpression("2 + 3", next));
    BOOST_TEST(next.getResult()->getValue().getIntegerValue() == 5);
}

BOOST_AUTO_TEST_CASE(interpreter_context_stores_expression_and_void_results)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);

    BOOST_TEST(context.getResult() == nullptr);

    context.setResult(runtime.getIntegerType(), RuntimeValue{NomadInteger{10}});
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getIntegerType());
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 10);

    context.setVoidResult();
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType()->isVoid());

    context.clearResult();
    BOOST_TEST(context.getResult() == nullptr);
}

BOOST_AUTO_TEST_CASE(interpreter_context_stores_and_clears_errors)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);

    BOOST_TEST(!context.hasError());
    BOOST_TEST(context.getError() == nullptr);

    context.setError("Unknown variable 'score'");
    BOOST_TEST(context.hasError());
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "Unknown variable 'score'");

    context.setError("A later nested error");
    BOOST_TEST(*context.getError() == "Unknown variable 'score'");
    BOOST_REQUIRE_EQUAL(context.getDiagnostics().size(), 2);
    BOOST_TEST(context.getDiagnostics()[1].message == "A later nested error");

    context.clearError();
    BOOST_TEST(!context.hasError());
    BOOST_TEST(!context.hasWarning());
    BOOST_TEST(context.getError() == nullptr);

    context.setError("A later input error");
    BOOST_TEST(context.hasError());
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "A later input error");
}

BOOST_AUTO_TEST_CASE(interpreter_context_retains_multiple_diagnostics_and_first_error)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);
    context.addDiagnostic({DiagnosticSeverity::Warning, "warning", "test.nomad", 1, 2});
    BOOST_TEST(!context.hasError());
    BOOST_TEST(context.hasWarning());
    BOOST_TEST(context.getError() == nullptr);
    context.addDiagnostic({DiagnosticSeverity::Error, "first error", "test.nomad", 2, 3});
    context.addDiagnostic({DiagnosticSeverity::Error, "second error", "test.nomad", 3, 4});
    BOOST_REQUIRE_EQUAL(context.getDiagnostics().size(), 3);
    BOOST_TEST(context.getDiagnostics()[0].message == "warning");
    BOOST_TEST(context.getDiagnostics()[1].message == "first error");
    BOOST_TEST(context.getDiagnostics()[2].message == "second error");
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "first error");
    BOOST_TEST(context.hasError());
    BOOST_TEST(context.hasWarning());
    context.clearError();
    BOOST_TEST(context.getDiagnostics().empty());
    BOOST_TEST(!context.hasError());
    BOOST_TEST(!context.hasWarning());
    for (const auto expressionOnly : {false, true}) {
        const auto succeeded = expressionOnly
            ? Interpreter{}.evaluateExpression("missingLeft + missingRight", context)
            : Interpreter{}.execute("missingLeft + missingRight", context);
        BOOST_TEST(!succeeded);
        BOOST_REQUIRE_EQUAL(context.getDiagnostics().size(), 2);
        BOOST_TEST(context.getDiagnostics()[0].message == "Unknown identifier: missingLeft");
        BOOST_TEST(context.getDiagnostics()[1].message == "Unknown identifier: missingRight");
        BOOST_REQUIRE(Interpreter{}.evaluateExpression("1 + 2", context));
        BOOST_TEST(context.getDiagnostics().empty());
        BOOST_TEST(!context.hasError());
    }
}

BOOST_AUTO_TEST_CASE(interpreter_context_executes_single_line_assignments_and_expressions)
{
    Runtime runtime;
    registerSystemFunctions(&runtime);
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);

    BOOST_TEST(Interpreter{}.execute("score = 10", context));
    BOOST_TEST(!context.hasError());
    BOOST_REQUIRE(context.getVariableValue("score") != nullptr);
    BOOST_TEST(context.getVariableValue("score")->getIntegerValue() == 10);
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 10);

    BOOST_TEST(Interpreter{}.execute("score = score + 5", context));
    BOOST_TEST(Interpreter{}.execute("score", context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getIntegerType());
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 15);

    BOOST_TEST(Interpreter{}.execute("math.floor 4.8", context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getFloatType());
    BOOST_TEST(context.getResult()->getValue().getFloatValue() == 4.0f);
}

BOOST_AUTO_TEST_CASE(interpreter_evaluates_format_literal_arithmetic)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);

    BOOST_REQUIRE(Interpreter{}.execute("number = 4", context));
    const std::vector<std::pair<NomadString, NomadString>> cases{
        {R"($"{1+2}")", "3"},
        {R"(    $"{1+2}")", "3"},
        {R"($"literal")", "literal"},
        {R"($"value {number + 2}")", "value 6"},
        {R"($"nested {$"sum {1+2}"}")", "nested sum 3"},
        {R"($"{"hello".toUpper}")", "HELLO"},
        {R"($"line\n{1+2}")", "line\n3"},
    };
    for (const auto& [source, expected] : cases) {
        BOOST_TEST_CONTEXT(source) {
            for (const auto expressionOnly : {false, true}) {
                const auto succeeded = expressionOnly
                    ? Interpreter{}.evaluateExpression(source, context)
                    : Interpreter{}.execute(source, context);
                BOOST_REQUIRE_MESSAGE(succeeded, (context.getError() ? *context.getError() : source));
                BOOST_REQUIRE(context.getResult() != nullptr);
                BOOST_TEST(context.getResult()->getType() == runtime.getStringType());
                BOOST_TEST(context.getResult()->getValue().getStringValue() == expected);
            }
        }
    }
    BOOST_REQUIRE(Interpreter{}.execute(R"(text = $"{1+2}")", context));
    BOOST_REQUIRE(Interpreter{}.evaluateExpression("text", context));
    BOOST_TEST(context.getResult()->getValue().getStringValue() == "3");

    for (const auto* source : {"", "   ", "# comment"}) {
        BOOST_REQUIRE(Interpreter{}.execute(source, context));
        BOOST_REQUIRE(context.getResult() != nullptr);
        BOOST_TEST(context.getResult()->getType()->isVoid());
    }
}

BOOST_AUTO_TEST_CASE(interpreter_reports_unknown_function_calls)
{
    Runtime runtime;
    registerSystemFunctions(&runtime);
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);

    BOOST_TEST(!Interpreter{}.execute("toInteger 1.2", context));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "Unknown function 'toInteger'");

    BOOST_TEST(!Interpreter{}.execute("toInteger \"12\"", context));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "Unknown function 'toInteger'");

    BOOST_TEST(!Interpreter{}.execute("result = toInteger 1.2", context));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "Unknown function 'toInteger'");

    BOOST_TEST(!Interpreter{}.execute("missing + 1", context));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "Unknown identifier: missing");

    BOOST_TEST(Interpreter{}.execute("toInt \"12\"", context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 12);
}

BOOST_AUTO_TEST_CASE(interpreter_reads_and_writes_context_variables)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);

    BOOST_TEST(Interpreter{}.execute("global.score = 10", context));
    BOOST_TEST(!context.hasError());

    // The value must land in the runtime's global context, not in a console variable.
    const auto contextId = runtime.getContextId("global");
    BOOST_REQUIRE(contextId.isValid());
    auto* variableContext = runtime.getVariableContext(contextId);
    BOOST_REQUIRE(variableContext != nullptr);
    const auto variableId = variableContext->getVariableId("global.score");
    BOOST_REQUIRE(variableId.isValid());

    RuntimeValue stored;
    variableContext->getValue(variableId, stored);
    BOOST_TEST(stored.getIntegerValue() == 10);

    BOOST_TEST(Interpreter{}.execute("global.score", context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getIntegerType());
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 10);

    BOOST_TEST(Interpreter{}.execute("global.score = global.score + 5", context));
    BOOST_TEST(Interpreter{}.execute("global.score", context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 15);
}

BOOST_AUTO_TEST_CASE(interpreter_reassigns_string_context_variables)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);

    // Reassignment must release the replaced buffer and keep the new one alive.
    BOOST_TEST(Interpreter{}.execute("global.name = \"first\"", context));
    BOOST_TEST(Interpreter{}.execute("global.name = \"second\"", context));

    BOOST_TEST(Interpreter{}.execute("global.name", context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getStringType());
    BOOST_REQUIRE(context.getResult()->getValue().getStringValue() != nullptr);
    BOOST_TEST(NomadString(context.getResult()->getValue().getStringValue()) == "second");
}

BOOST_AUTO_TEST_CASE(interpreter_reads_and_writes_dynamic_variables)
{
    Runtime runtime;
    NomadInteger backingValue = 7;

    runtime.registerDynamicVariable(
        "test.value",
        [&backingValue](VirtualMachine*, const RuntimeValue& value) {
            backingValue = value.getIntegerValue();
        },
        [&backingValue](VirtualMachine*, RuntimeValue& value) {
            value.setIntegerValue(backingValue);
        },
        runtime.getIntegerType(),
        NomadDoc("Test dynamic variable.")
    );

    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);

    BOOST_TEST(Interpreter{}.execute("test.value", context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getIntegerType());
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 7);

    BOOST_TEST(Interpreter{}.execute("test.value = 42", context));
    BOOST_TEST(!context.hasError());
    BOOST_TEST(backingValue == 42);

    BOOST_TEST(Interpreter{}.execute("test.value + 1", context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 43);
}

BOOST_AUTO_TEST_CASE(interpreter_reads_and_writes_string_dynamic_variables)
{
    Runtime runtime;
    NomadString backingValue = "initial";

    runtime.registerDynamicVariable(
        "test.title",
        [&backingValue](VirtualMachine*, const RuntimeValue& value) {
            backingValue = value.getStringValue();
        },
        [&backingValue](VirtualMachine*, RuntimeValue& value) {
            value.setStringValue(backingValue);
        },
        runtime.getStringType(),
        NomadDoc("Test string dynamic variable.")
    );

    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);

    BOOST_TEST(Interpreter{}.execute("test.title", context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_REQUIRE(context.getResult()->getValue().getStringValue() != nullptr);
    BOOST_TEST(NomadString(context.getResult()->getValue().getStringValue()) == "initial");

    BOOST_TEST(Interpreter{}.execute("test.title = \"updated\"", context));
    BOOST_TEST(!context.hasError());
    BOOST_TEST(backingValue == "updated");
}

BOOST_AUTO_TEST_CASE(interpreter_rejects_writing_read_only_dynamic_variables)
{
    Runtime runtime;

    runtime.registerDynamicVariable(
        "test.readOnly",
        nullptr,
        [](VirtualMachine*, RuntimeValue& value) {
            value.setIntegerValue(3);
        },
        runtime.getIntegerType(),
        NomadDoc("Read-only test dynamic variable.")
    );

    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);

    BOOST_TEST(Interpreter{}.execute("test.readOnly", context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 3);

    BOOST_TEST(!Interpreter{}.execute("test.readOnly = 9", context));
    BOOST_TEST(context.hasError());
}

BOOST_AUTO_TEST_CASE(interpreter_rejects_type_mismatched_dynamic_assignment)
{
    Runtime runtime;
    NomadInteger backingValue = 1;

    runtime.registerDynamicVariable(
        "test.count",
        [&backingValue](VirtualMachine*, const RuntimeValue& value) {
            backingValue = value.getIntegerValue();
        },
        [&backingValue](VirtualMachine*, RuntimeValue& value) {
            value.setIntegerValue(backingValue);
        },
        runtime.getIntegerType(),
        NomadDoc("Test dynamic variable.")
    );

    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);

    BOOST_TEST(!Interpreter{}.execute("test.count = \"text\"", context));
    BOOST_TEST(context.hasError());
    BOOST_TEST(backingValue == 1);
}

BOOST_AUTO_TEST_CASE(interpreter_context_executes_compiled_value_returning_functions)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext compileContext(compiler.get());
    compiler->registerSources(&compileContext, {{{"increment", {"increment.nomad",
        "params value:int\nreturn value + 1"}}}, {}});
    const auto functionId = runtime.getFunctionId("increment");
    BOOST_REQUIRE(functionId.isValid());
    BOOST_REQUIRE(compiler->compileFunctions(&compileContext));

    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);
    BOOST_TEST(Interpreter{}.execute("increment 41", context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getIntegerType());
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 42);
}

BOOST_AUTO_TEST_CASE(interpreter_context_executes_nativeFunctions_with_hidden_source_arguments)
{
    Runtime runtime;
    registerSystemFunctions(&runtime);
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);
    NomadString fileName;
    NomadString functionName;
    NomadInteger lineNumber = 0;
    NomadString message;

    runtime.registerNativeFunction(
        "test.hiddenSource",
        [&](const VirtualMachine* virtualMachine) {
            fileName = virtualMachine->getStringParameter(0)->getString();
            functionName = virtualMachine->getStringParameter(1)->getString();
            lineNumber = virtualMachine->getIntegerParameter(2);
            message = virtualMachine->getStringParameter(3)->getString();
        },
        {
            defParameter("$file", runtime.getFileNameType(), ""),
            defParameter("$function", runtime.getFunctionNameType(), ""),
            defParameter("$line", runtime.getLineNumberType(), ""),
            defParameter("message", runtime.getStringType(), "")
        },
        runtime.getVoidType(),
        ""
    );

    BOOST_TEST(Interpreter{}.execute("test.hiddenSource(\"test\")", context));
    BOOST_TEST(!context.hasError());
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType()->isVoid());

    BOOST_TEST(Interpreter{}.execute("test.hiddenSource(\"payload\")", context));
    BOOST_TEST(fileName == "<console>");
    BOOST_TEST(functionName == "<console>");
    BOOST_TEST(lineNumber == 1);
    BOOST_TEST(message == "payload");
    BOOST_REQUIRE(Interpreter{}.execute(R"(test.hiddenSource $"{1+2}")", context));
    BOOST_TEST(lineNumber == 1);
    BOOST_TEST(message == "3");
}

BOOST_AUTO_TEST_CASE(interpreter_context_reports_line_errors_and_clears_them_on_next_execute)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);

    BOOST_TEST(!Interpreter{}.execute("if", context));
    BOOST_TEST(context.hasError());
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(!context.getError()->empty());
    BOOST_TEST(context.getResult() == nullptr);

    BOOST_TEST(Interpreter{}.execute("40 + 2", context));
    BOOST_TEST(!context.hasError());
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 42);
}

BOOST_AUTO_TEST_CASE(interpreter_context_executes_multiline_assignments)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);

    BOOST_REQUIRE(Interpreter{}.execute("score = 10\nscore = score + 1", context));
    BOOST_REQUIRE(context.getVariableValue("score") != nullptr);
    BOOST_TEST(context.getVariableValue("score")->getIntegerValue() == 11);
}

BOOST_AUTO_TEST_CASE(interpreter_context_lists_variables_in_name_order)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);

    context.setVariable("zeta", runtime.getBooleanType(), RuntimeValue{true});
    RuntimeValue stringValue{NomadString{"ready"}};
    context.setVariable("alpha", runtime.getStringType(), stringValue);
    runtime.getStringType()->freeValue(stringValue);

    const auto variables = context.listVariables();
    BOOST_REQUIRE_EQUAL(variables.size(), 2U);
    BOOST_TEST(variables[0].name == "alpha");
    BOOST_TEST(variables[0].type == runtime.getStringType());
    BOOST_TEST(variables[0].value == "ready");
    BOOST_TEST(variables[1].name == "zeta");
    BOOST_TEST(variables[1].value == "true");
}

BOOST_AUTO_TEST_CASE(interpreter_context_rejects_invalid_types)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);

    BOOST_CHECK_THROW(context.setVariable("", runtime.getIntegerType(), RuntimeValue{NomadInteger{1}}),
                      std::invalid_argument);
    BOOST_CHECK_THROW(context.setVariable("missing-type", nullptr, RuntimeValue{NomadInteger{1}}),
                      std::invalid_argument);
    BOOST_CHECK_THROW(context.setVariable("void-value", runtime.getVoidType(), RuntimeValue{}),
                      std::invalid_argument);

    BOOST_TEST(context.getVariableValue("unknown") == nullptr);
    BOOST_TEST(context.getVariableType("unknown") == nullptr);
    BOOST_TEST(context.getFunction()->getVariableCount() == 0U);
}

BOOST_AUTO_TEST_CASE(interpreter_context_variables_may_change_type)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);

    context.setVariable("value", runtime.getIntegerType(), RuntimeValue{NomadInteger{1}});
    context.setVariable("value", runtime.getFloatType(), RuntimeValue{NomadFloat{2.5f}});

    const auto* value = context.getVariableValue("value");
    BOOST_REQUIRE(value != nullptr);
    BOOST_TEST(context.getVariableType("value") == runtime.getFloatType());
    BOOST_TEST(value->getFloatValue() == 2.5f);

    const auto* function = context.getFunction();
    const auto variableId = function->getVariableId("value");
    BOOST_REQUIRE(variableId.isValid());
    BOOST_TEST(function->getVariableType(variableId) == runtime.getFloatType());
    BOOST_TEST(function->getVariableCount() == 1U);
}

BOOST_AUTO_TEST_CASE(interpreter_context_changes_variable_type_through_execute)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);

    BOOST_REQUIRE(Interpreter{}.execute("score = 10", context));
    BOOST_REQUIRE(Interpreter{}.execute("score = \"ten\"", context));

    const auto* score = context.getVariableValue("score");
    BOOST_REQUIRE(score != nullptr);
    BOOST_TEST(context.getVariableType("score") == runtime.getStringType());
    BOOST_TEST(score->getStringValue() == "ten");
}

BOOST_AUTO_TEST_CASE(interpreter_context_defines_variables_in_console_function)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);

    BOOST_REQUIRE(Interpreter{}.execute("first = 1", context));
    BOOST_REQUIRE(Interpreter{}.execute("second = true", context));

    const auto* function = context.getFunction();
    BOOST_REQUIRE(function != nullptr);
    BOOST_TEST(function->getId().isInvalid());
    BOOST_TEST(runtime.getFunctionId(function->getName()).isInvalid());
    BOOST_TEST(function->getVariableCount() == 2U);
    BOOST_TEST(function->getVariableId("first") == VariableId{0});
    BOOST_TEST(function->getVariableId("second") == VariableId{1});
    BOOST_TEST(function->getVariableType(VariableId{1}) == runtime.getBooleanType());
}

BOOST_AUTO_TEST_CASE(interpreter_context_failed_line_does_not_assign_variables)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);

    BOOST_REQUIRE(Interpreter{}.execute("kept = 1", context));
    BOOST_TEST(!Interpreter{}.execute("broken = missing + 1", context));

    BOOST_TEST(context.getVariableValue("broken") == nullptr);
    BOOST_TEST(context.getVariableValue("kept")->getIntegerValue() == 1);

    const auto variables = context.listVariables();
    BOOST_REQUIRE_EQUAL(variables.size(), 1U);
    BOOST_TEST(variables[0].name == "kept");
}

BOOST_AUTO_TEST_CASE(interpreter_context_reports_resolve_errors_before_evaluation)
{
    Runtime runtime;
    NomadInteger calledValue = 0;
    runtime.registerNativeFunction(
        "remember",
        [&calledValue](VirtualMachine* interpreter) {
            calledValue = interpreter->getIntegerParameter(0);
        },
        {
            defParameter("value", runtime.getIntegerType(), NomadParamDoc("Value to remember"))
        },
        runtime.getVoidType(),
        NomadDoc("Remember an integer")
    );
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);

    BOOST_TEST(!Interpreter{}.execute("missing + 1", context));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "Unknown identifier: missing");

    BOOST_TEST(!Interpreter{}.execute("1 + true", context));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(context.getError()->starts_with("Invalid binary operator '+'"));

    BOOST_TEST(!Interpreter{}.execute("pi = 3.0", context));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "Cannot assign value to 'pi' in the interpreter");

    BOOST_TEST(!Interpreter{}.execute("remember true", context));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(context.getError()->starts_with("Argument type mismatch."));
    BOOST_TEST(calledValue == 0);

    BOOST_TEST(!Interpreter{}.execute("value = remember 1", context));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(context.getError()->find("does not return a value") != NomadString::npos);
    BOOST_TEST(calledValue == 0);
    BOOST_TEST(context.getVariableValue("value") == nullptr);
}
