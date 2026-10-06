// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/script/VariableContext.hpp>
#include <nomad/script/VirtualMachine.hpp>
#include <nomad/script/VirtualMachine.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/Function.hpp>
#include <nomad/script/Type.hpp>

#include <stdexcept>

using namespace nomad;

BOOST_AUTO_TEST_CASE(interpreter_context_stores_persistent_typed_variables)
{
    Runtime runtime;
    Interpreter context;

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

BOOST_AUTO_TEST_CASE(interpreter_context_stores_expression_and_void_results)
{
    Runtime runtime;
    Interpreter context(&runtime);

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
    Interpreter context;

    BOOST_TEST(!context.hasError());
    BOOST_TEST(context.getError() == nullptr);

    context.setError("Unknown variable 'score'");
    BOOST_TEST(context.hasError());
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "Unknown variable 'score'");

    context.setError("A later nested error");
    BOOST_TEST(*context.getError() == "Unknown variable 'score'");

    context.clearError();
    BOOST_TEST(!context.hasError());
    BOOST_TEST(context.getError() == nullptr);

    context.setError("A later input error");
    BOOST_TEST(context.hasError());
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "A later input error");
}

BOOST_AUTO_TEST_CASE(interpreter_context_executes_single_line_assignments_and_expressions)
{
    Runtime runtime;
    Interpreter context(&runtime);

    BOOST_TEST(context.execute("score = 10"));
    BOOST_TEST(!context.hasError());
    BOOST_REQUIRE(context.getVariableValue("score") != nullptr);
    BOOST_TEST(context.getVariableValue("score")->getIntegerValue() == 10);
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 10);

    BOOST_TEST(context.execute("score = score + 5"));
    BOOST_TEST(context.execute("score"));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getIntegerType());
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 15);

    BOOST_TEST(context.execute("math.floor 4.8"));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getFloatType());
    BOOST_TEST(context.getResult()->getValue().getFloatValue() == 4.0f);
}

BOOST_AUTO_TEST_CASE(interpreter_reports_unknown_function_calls)
{
    Runtime runtime;
    Interpreter context(&runtime);

    BOOST_TEST(!context.execute("toInteger 1.2"));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "Unknown function 'toInteger'");

    BOOST_TEST(!context.execute("toInteger \"12\""));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "Unknown function 'toInteger'");

    BOOST_TEST(!context.execute("result = toInteger 1.2"));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "Unknown function 'toInteger'");

    BOOST_TEST(!context.execute("missing + 1"));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "Unknown identifier: missing");

    BOOST_TEST(context.execute("toInt \"12\""));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 12);
}

BOOST_AUTO_TEST_CASE(interpreter_reads_and_writes_context_variables)
{
    Runtime runtime;
    Interpreter context(&runtime);

    BOOST_TEST(context.execute("global.score = 10"));
    BOOST_TEST(!context.hasError());

    // The value must land in the runtime's global context, not in a console variable.
    const auto contextId = runtime.getContextId("global");
    BOOST_REQUIRE(contextId != NOMAD_INVALID_ID);
    auto* variableContext = runtime.getVariableContext(contextId);
    BOOST_REQUIRE(variableContext != nullptr);
    const auto variableId = variableContext->getVariableId("global.score");
    BOOST_REQUIRE(variableId != NOMAD_INVALID_ID);

    RuntimeValue stored;
    variableContext->getValue(variableId, stored);
    BOOST_TEST(stored.getIntegerValue() == 10);

    BOOST_TEST(context.execute("global.score"));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getIntegerType());
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 10);

    BOOST_TEST(context.execute("global.score = global.score + 5"));
    BOOST_TEST(context.execute("global.score"));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 15);
}

BOOST_AUTO_TEST_CASE(interpreter_reassigns_string_context_variables)
{
    Runtime runtime;
    Interpreter context(&runtime);

    // Reassignment must release the replaced buffer and keep the new one alive.
    BOOST_TEST(context.execute("global.name = \"first\""));
    BOOST_TEST(context.execute("global.name = \"second\""));

    BOOST_TEST(context.execute("global.name"));
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

    Interpreter context(&runtime);

    BOOST_TEST(context.execute("test.value"));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getIntegerType());
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 7);

    BOOST_TEST(context.execute("test.value = 42"));
    BOOST_TEST(!context.hasError());
    BOOST_TEST(backingValue == 42);

    BOOST_TEST(context.execute("test.value + 1"));
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

    Interpreter context(&runtime);

    BOOST_TEST(context.execute("test.title"));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_REQUIRE(context.getResult()->getValue().getStringValue() != nullptr);
    BOOST_TEST(NomadString(context.getResult()->getValue().getStringValue()) == "initial");

    BOOST_TEST(context.execute("test.title = \"updated\""));
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

    Interpreter context(&runtime);

    BOOST_TEST(context.execute("test.readOnly"));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 3);

    BOOST_TEST(!context.execute("test.readOnly = 9"));
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

    Interpreter context(&runtime);

    BOOST_TEST(!context.execute("test.count = \"text\""));
    BOOST_TEST(context.hasError());
    BOOST_TEST(backingValue == 1);
}

BOOST_AUTO_TEST_CASE(interpreter_context_executes_compiled_value_returning_functions)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    const auto functionId = compiler->registerScriptFile(
        "increment",
        "increment.nomad",
        "params value:int\nreturn value + 1"
    );
    BOOST_REQUIRE(functionId != NOMAD_INVALID_ID);
    CompilerContext compileContext(compiler.get());
    BOOST_REQUIRE(compiler->compileFunctions(&compileContext));

    Interpreter context(&runtime);
    BOOST_TEST(context.execute("increment 41"));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == runtime.getIntegerType());
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 42);
}

BOOST_AUTO_TEST_CASE(interpreter_context_executes_nativeFunctions_with_hidden_source_arguments)
{
    Runtime runtime;
    Interpreter context(&runtime);
    NomadString fileName;
    NomadString functionName;
    NomadInteger lineNumber = 0;
    NomadString message;

    runtime.registerNativeFunction(
        "test.hiddenSource",
        [&](const VirtualMachine* virtualMachine) {
            fileName = virtualMachine->getStringParameter(0);
            functionName = virtualMachine->getStringParameter(1);
            lineNumber = virtualMachine->getIntegerParameter(2);
            message = virtualMachine->getStringParameter(3);
        },
        {
            defParameter("$file", runtime.getFileNameType(), ""),
            defParameter("$function", runtime.getFunctionNameType(), ""),
            defParameter("$line", runtime.getLineNumberType(), ""),
            defParameter("message", runtime.getStringRefType(), "")
        },
        runtime.getVoidType(),
        ""
    );

    BOOST_TEST(context.execute("log.info(\"test\")"));
    BOOST_TEST(!context.hasError());
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType()->isVoid());

    BOOST_TEST(context.execute("test.hiddenSource(\"payload\")"));
    BOOST_TEST(fileName == "<console>");
    BOOST_TEST(functionName == "<console>");
    BOOST_TEST(lineNumber == 0);
    BOOST_TEST(message == "payload");
}

BOOST_AUTO_TEST_CASE(interpreter_context_reports_line_errors_and_clears_them_on_next_execute)
{
    Runtime runtime;
    Interpreter context(&runtime);

    BOOST_TEST(!context.execute("if"));
    BOOST_TEST(context.hasError());
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(!context.getError()->empty());
    BOOST_TEST(context.getResult() == nullptr);

    BOOST_TEST(context.execute("40 + 2"));
    BOOST_TEST(!context.hasError());
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 42);
}

BOOST_AUTO_TEST_CASE(interpreter_context_rejects_multiline_input)
{
    Runtime runtime;
    Interpreter context(&runtime);

    BOOST_TEST(!context.execute("score = 10\nscore"));
    BOOST_TEST(context.hasError());
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "Interpreter accepts a single line of input");
    BOOST_TEST(context.getVariableValue("score") == nullptr);
}

BOOST_AUTO_TEST_CASE(interpreter_context_lists_variables_in_name_order)
{
    Runtime runtime;
    Interpreter context;

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
    Interpreter context;

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
    Interpreter context;

    context.setVariable("value", runtime.getIntegerType(), RuntimeValue{NomadInteger{1}});
    context.setVariable("value", runtime.getFloatType(), RuntimeValue{NomadFloat{2.5f}});

    const auto* value = context.getVariableValue("value");
    BOOST_REQUIRE(value != nullptr);
    BOOST_TEST(context.getVariableType("value") == runtime.getFloatType());
    BOOST_TEST(value->getFloatValue() == 2.5f);

    const auto* function = context.getFunction();
    const auto variableId = function->getVariableId("value");
    BOOST_REQUIRE(variableId != NOMAD_INVALID_ID);
    BOOST_TEST(function->getVariableType(variableId) == runtime.getFloatType());
    BOOST_TEST(function->getVariableCount() == 1U);
}

BOOST_AUTO_TEST_CASE(interpreter_context_changes_variable_type_through_execute)
{
    Runtime runtime;
    Interpreter context(&runtime);

    BOOST_REQUIRE(context.execute("score = 10"));
    BOOST_REQUIRE(context.execute("score = \"ten\""));

    const auto* score = context.getVariableValue("score");
    BOOST_REQUIRE(score != nullptr);
    BOOST_TEST(context.getVariableType("score") == runtime.getStringType());
    BOOST_TEST(score->getStringValue() == "ten");
}

BOOST_AUTO_TEST_CASE(interpreter_context_defines_variables_in_console_function)
{
    Runtime runtime;
    Interpreter context(&runtime);

    BOOST_REQUIRE(context.execute("first = 1"));
    BOOST_REQUIRE(context.execute("second = true"));

    const auto* function = context.getFunction();
    BOOST_REQUIRE(function != nullptr);
    BOOST_TEST(function->getId() == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.getFunctionId(function->getName()) == NOMAD_INVALID_ID);
    BOOST_TEST(function->getVariableCount() == 2U);
    BOOST_TEST(function->getVariableId("first") == NomadId{0});
    BOOST_TEST(function->getVariableId("second") == NomadId{1});
    BOOST_TEST(function->getVariableType(1) == runtime.getBooleanType());
}

BOOST_AUTO_TEST_CASE(interpreter_context_failed_line_does_not_assign_variables)
{
    Runtime runtime;
    Interpreter context(&runtime);

    BOOST_REQUIRE(context.execute("kept = 1"));
    BOOST_TEST(!context.execute("broken = missing + 1"));

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
    Interpreter context(&runtime);

    BOOST_TEST(!context.execute("missing + 1"));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "Unknown identifier: missing");

    BOOST_TEST(!context.execute("1 + true"));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(context.getError()->starts_with("Invalid binary operator '+'"));

    BOOST_TEST(!context.execute("pi = 3.0"));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(*context.getError() == "Cannot assign value to 'pi' in the interpreter");

    BOOST_TEST(!context.execute("remember true"));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(context.getError()->starts_with("Argument type mismatch."));
    BOOST_TEST(calledValue == 0);

    BOOST_TEST(!context.execute("value = remember 1"));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(context.getError()->find("does not return a value") != NomadString::npos);
    BOOST_TEST(calledValue == 0);
    BOOST_TEST(context.getVariableValue("value") == nullptr);
}
