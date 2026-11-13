// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/interpreter/Interpreter.hpp>

#include <nomad/script/ClassType.hpp>
#include <nomad/script/Closure.hpp>
#include <nomad/script/FormatString.hpp>
#include <nomad/script/InstructionDump.hpp>
#include <nomad/script/NativeFunction.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/StructType.hpp>
#include <nomad/script/ValueType.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
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

BOOST_AUTO_TEST_CASE(compiled_and_dynamic_converters_receive_complete_storage_not_dummy_values) {
    class WideFormatType : public ValueType {
    public:
        using ValueType::ValueType;
        using Type::toString;

        [[nodiscard]] NomadString getTypeName() const override {

            return "WideFormatValue";
        }

        [[nodiscard]] NomadIndex getSize() const override {

            return 2;
        }

        void initValue(RuntimeValue* value) const override {
            std::fill_n(value, 2, RuntimeValue{});
        }

        void copyValue(const RuntimeValue* source, RuntimeValue* destination) const override {
            std::copy_n(source, 2, destination);
        }

        void toString(const RuntimeValue* value, NomadString& text) const override {
            BOOST_REQUIRE(value != nullptr);
            text = std::to_string(value[0].getIntegerValue()) + ":" +
                std::to_string(value[1].getIntegerValue());
        }
    };

    class EmptyFormatType final : public WideFormatType {
    public:
        using WideFormatType::WideFormatType;

        [[nodiscard]] NomadString getTypeName() const override {

            return "EmptyFormatValue";
        }

        [[nodiscard]] NomadIndex getSize() const override {

            return 0;
        }

        void initValue(RuntimeValue*) const override {}
        void copyValue(const RuntimeValue*, RuntimeValue*) const override {}

        void toString(const RuntimeValue* value, NomadString& text) const override {
            BOOST_TEST(value == nullptr);
            text = "empty";
        }
    };

    Runtime runtime;
    auto& manager = runtime.getTypeManager();
    const auto wideId = manager.reserveTypeId();
    manager.registerType(std::make_unique<WideFormatType>(&runtime, wideId));
    const auto emptyId = manager.reserveTypeId();
    manager.registerType(std::make_unique<EmptyFormatType>(&runtime, emptyId));
    const auto* wide = runtime.getType(wideId).value();
    const auto* empty = runtime.getType(emptyId).value();
    auto rawGetterCalls = 0;
    const auto rawVariable = runtime.registerDynamicVariable(
        "unavailableWideFormatValue",
        {},
        [&](VirtualMachine*, RuntimeValue&) { ++rawGetterCalls; },
        wide,
        ""
    );
    BOOST_REQUIRE(rawVariable.isValid());
    FormatString incomplete("", CallableId{});
    incomplete.addVariable(IdentifierType::DynamicVariable, wide, rawVariable.id);
    const auto functionId = runtime.registerScriptFunction("formatCompleteStorage", "storage.nomad", "");
    auto* function = runtime.getScriptFunction(functionId);
    function->addParameter("head", runtime.getIntegerType(), "");
    function->addParameter("wide", wide, "");
    function->addParameter("empty", empty, "");
    function->addParameter("tail", runtime.getIntegerType(), "");
    const auto localId = function->registerVariable("local", wide);
    const auto emptyLocalId = function->registerVariable("emptyLocal", empty);
    function->setReturnType(runtime.getStringType());
    const auto formatId = runtime.registerFormatString("complete-storage", functionId);
    auto* format = runtime.getFormatString(formatId);
    format->addVariable(IdentifierType::Parameter, wide, 1);
    format->addLiteral("|");
    format->addVariable(IdentifierType::FunctionVariable, wide, localId.id);
    format->addLiteral("|");
    format->addVariable(IdentifierType::Parameter, empty, 2);
    format->addLiteral("|");
    format->addVariable(IdentifierType::FunctionVariable, empty, emptyLocalId.id);
    format->addLiteral("|");
    format->addVariable(IdentifierType::Parameter, runtime.getIntegerType(), 0);
    format->addLiteral("|");
    format->addVariable(IdentifierType::Parameter, runtime.getIntegerType(), 3);
    const auto initialize = runtime.registerInstruction(
        "test.initializeFormatStorage",
        [](VirtualMachine* machine) {
            const auto* callable = machine->getCurrentCallable();
            const auto storage = machine->getLocalStorage(
                callable->getVariableOffset(callable->getVariableId("local")),
                2
            );
            storage[0].setIntegerValue(31);
            storage[1].setIntegerValue(64);
        },
        "",
        {}
    );
    auto compiler = runtime.createCompiler();
    function->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(function);
    compiler->addOpCode(initialize);
    compiler->addOpCode(OpCodes::op_format_string_execute);
    compiler->addId(formatId.id);
    compiler->addFunctionReturn(function);
    function->setFunctionEnd(runtime.getInstructions().size());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    for (auto iteration = 0; iteration < 3; ++iteration) {
        {
            auto machine = runtime.createVirtualMachine();
            const std::array values{RuntimeValue{NomadInteger{11}}, RuntimeValue{NomadInteger{42}}};
            machine->pushInteger(77);
            machine->pushStorage(values);
            machine->pushInteger(3);
            machine->enterScriptFrame(function->getFunctionStart());
            machine->executeScript();
            BOOST_TEST(machine->getResult().getStringValue() == "11:42|31:64|empty|empty|3|77");
            runtime.getStringType()->freeValue(&machine->getResult());
            machine->popN(4);
            const Interpreter interpreter(*machine);
            InterpreterContext context(interpreter);
            context.setVariable("wide", wide, values);
            context.setVariable("empty", empty, std::span<const RuntimeValue>{});
            const auto dynamic = interpreter.formatString("{wide}|{empty}", context, "dynamic.nomad", 1, 1);
            BOOST_TEST(dynamic.text == "11:42|empty");
            BOOST_TEST(dynamic.diagnostics.empty());
            BOOST_CHECK_EXCEPTION(
                static_cast<void>(incomplete.format(machine.get())),
                VirtualMachineException,
                [](const auto& error) {

                    return NomadString{error.what()} == "Format lookup does not provide complete value storage";
                }
            );
            BOOST_TEST(rawGetterCalls == 0);
        }

        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    }

}

BOOST_AUTO_TEST_CASE(compiled_format_restores_execution_state_before_conversion_and_conversion_failure) {
    class ObservedFormatType final : public ClassType {
    public:
        using ClassType::ClassType;

        void toString(const RuntimeValue* value, NomadString& text) const override {
            inspect(value);
            text = "checked";
        }

        std::function<void(const RuntimeValue*)> inspect;
    };

    Runtime runtime;
    auto& manager = runtime.getTypeManager();
    const auto typeId = manager.reserveTypeId();
    auto type = std::make_unique<ObservedFormatType>(&runtime, typeId, "ObservedFormatValue", ClassMode::Closed);
    auto* descriptor = type.get();
    manager.registerType(std::move(type));
    const auto makeValue = runtime.registerNativeFunction("makeObservedFormat", [](VirtualMachine* active) {
        active->setStringResult("owned format state result");
    }, {}, descriptor, "");
    BOOST_REQUIRE(makeValue.isValid());
    const auto functionId = runtime.registerScriptFunction("observedFormatExpression", "observed.nomad", "");
    auto* function = runtime.getScriptFunction(functionId);
    function->setReturnType(descriptor);
    auto compiler = runtime.createCompiler();
    function->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(function);
    compiler->addOpCode(OpCodes::op_call_native_function);
    compiler->addId(makeValue.id);
    compiler->addFunctionReturn(function);
    function->setFunctionEnd(runtime.getInstructions().size());
    runtime.initializeScripts();
    FormatString format("", CallableId{});
    format.addExpression(descriptor, functionId);
    auto conversions = 0;
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (const auto initiallyRunning : {false, true}) {
        for (const auto failConversion : {false, true}) {
            auto machine = runtime.createVirtualMachine();
            machine->pushInteger(1234);
            machine->setIntegerResult(41);
            machine->setIntegerIntermediate(43);
            auto before = machine->captureExecutionState();
            before.running = initiallyRunning;
            machine->restoreExecutionState(before);
            const auto expectRestored = [&]() {
                const auto after = machine->captureExecutionState();
                BOOST_TEST(after.instructionIndex == before.instructionIndex);
                BOOST_TEST(after.stackIndex == before.stackIndex);
                BOOST_TEST(after.parameterIndex == before.parameterIndex);
                BOOST_TEST(after.variableIndex == before.variableIndex);
                BOOST_TEST(after.running == before.running);
                BOOST_TEST(after.receiver.getObjectValue() == before.receiver.getObjectValue());
            };
            descriptor->inspect = [&](const RuntimeValue* value) {
                ++conversions;
                expectRestored();
                BOOST_TEST(value->getStringValue() == "owned format state result");
                if (failConversion) {
                    throw NomadException("Expected observed format conversion failure");
                }
            };
            if (failConversion) {
                BOOST_CHECK_THROW(static_cast<void>(format.format(machine.get())), NomadException);
            } else {
                BOOST_TEST(format.format(machine.get()) == "checked");
            }

            descriptor->inspect = nullptr;
            expectRestored();
            BOOST_TEST(machine->getIntegerResult() == 41);
            BOOST_TEST(machine->getIntegerIntermediate() == 43);
            BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
            BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
            BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
            machine->pop1();
        }
    }

    BOOST_TEST(conversions == 4);
}

BOOST_AUTO_TEST_CASE(compiled_literals_do_not_create_runtime_compilers_but_dynamic_templates_do) {
    LanguageTestFixture fixture;
    fixture.addFunction(
        "compiledFormat",
        "struct ExecutionFormatPoint\nx = 7\nend\n"
        "point = new ExecutionFormatPoint\nreturn $\"{point} {point.x + 1}\""
    );
    fixture.addFunction(
        "dynamicFormat",
        "point = new ExecutionFormatPoint\ntemplate = \"{point} {point.x + 1}\"\nreturn $template"
    );
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto compilersCreated = 0;
    fixture.getRuntime().registerCompilerSetup([&](Compiler*) { ++compilersCreated; });
    for (auto iteration = 0; iteration < 3; ++iteration) {
        compilersCreated = 0;
        const auto compiled = fixture.execute("compiledFormat");
        BOOST_REQUIRE_MESSAGE(!compiled.fault.has_value(), compiled.fault.value_or(""));
        BOOST_TEST(compiled.text == "ExecutionFormatPoint 8");
        BOOST_TEST(compilersCreated == 0);
        const auto dynamic = fixture.execute("dynamicFormat");
        BOOST_REQUIRE_MESSAGE(!dynamic.fault.has_value(), dynamic.fault.value_or(""));
        BOOST_TEST(dynamic.text == compiled.text);
        BOOST_TEST(compilersCreated == 2);
    }

}

BOOST_AUTO_TEST_CASE(conversion_failures_release_owned_dynamic_getter_values_in_both_modes) {
    class ThrowingFormatType final : public ClassType {
    public:
        using ClassType::ClassType;

        void toString(const RuntimeValue* value, NomadString&) const override {
            BOOST_REQUIRE(value != nullptr);
            BOOST_TEST(value->getStringValue() == "owned converter input");
            throw NomadException("Formatting conversion failed");
        }
    };

    Runtime runtime;
    const auto typeId = runtime.getTypeManager().registerClass<ThrowingFormatType>("ThrowingFormatValue", ClassMode::Closed);
    BOOST_REQUIRE(typeId.isValid());
    const auto* type = runtime.getType(typeId).value();
    const auto variableId = runtime.registerDynamicVariable(
        "throwingFormatValue",
        {},
        [](VirtualMachine*, RuntimeValue& value) { value.setStringValue("owned converter input"); },
        type,
        ""
    );
    BOOST_REQUIRE(variableId.isValid());
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    InterpreterContext context(interpreter);
    FormatString compiled("", CallableId{});
    compiled.addVariable(IdentifierType::DynamicVariable, type, variableId.id);
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    for (auto iteration = 0; iteration < 3; ++iteration) {
        BOOST_CHECK_THROW(static_cast<void>(compiled.format(machine.get())), NomadException);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_CHECK_THROW(
            static_cast<void>(interpreter.formatString("{throwingFormatValue}", context, "conversion.nomad", 1, 1)),
            NomadException
        );
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    }

}

BOOST_AUTO_TEST_CASE(partial_dynamic_getter_failures_release_owned_outputs_in_all_read_paths) {
    Runtime runtime;
    auto getterCalls = 0;
    const auto variableId = runtime.registerDynamicVariable(
        "partialGetterValue",
        {},
        [&](VirtualMachine*, RuntimeValue& value) {
            ++getterCalls;
            value.setStringValue("partial dynamic getter output");
            throw NomadException("Expected partial getter failure");
        },
        runtime.getStringType(),
        ""
    );
    BOOST_REQUIRE(variableId.isValid());
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    InterpreterContext context(interpreter);
    FormatString compiled("", CallableId{});
    compiled.addVariable(IdentifierType::DynamicVariable, runtime.getStringType(), variableId.id);
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto iteration = 0; iteration < 3; ++iteration) {
        BOOST_CHECK_THROW(static_cast<void>(compiled.format(machine.get())), NomadException);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
        BOOST_CHECK_THROW(
            static_cast<void>(interpreter.formatString("{partialGetterValue}", context, "getter.nomad", 1, 1)),
            NomadException
        );
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
        BOOST_TEST(!interpreter.execute("partialGetterValue", context));
        BOOST_REQUIRE(context.getError() != nullptr);
        BOOST_TEST(context.getError()->find("Expected partial getter failure") != NomadString::npos);
        BOOST_TEST(context.getResult() == nullptr);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }

    BOOST_TEST(getterCalls == 9);
}

BOOST_AUTO_TEST_CASE(dynamic_format_evaluation_clears_partial_results_and_preserves_successful_output) {
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    InterpreterContext context(interpreter);
    auto fail = false;
    BOOST_REQUIRE(runtime.registerDynamicVariable(
        "partialEvaluationValue",
        {},
        [&](VirtualMachine*, RuntimeValue& value) {
            value.setStringValue("evaluation output");
            context.setResult(runtime.getStringType(), value);
            if (fail) {
                throw NomadException("Expected format evaluation failure");
            }
        },
        runtime.getStringType(),
        ""
    ).isValid());
    machine->setIntegerResult(41);
    machine->setIntegerIntermediate(43);
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    for (const auto failure : {true, false, true}) {
        fail = failure;
        if (fail) {
            BOOST_CHECK_THROW(
                static_cast<void>(interpreter.formatString(
                    "{partialEvaluationValue}", context, "evaluation.nomad", 1, 1
                )),
                NomadException
            );
            BOOST_TEST(context.getResult() == nullptr);
        } else {
            const auto result = interpreter.formatString(
                "{partialEvaluationValue}", context, "evaluation.nomad", 1, 1
            );
            BOOST_TEST(result.text == "evaluation output");
            BOOST_TEST(context.getResult() == nullptr);
        }

        BOOST_TEST(machine->getIntegerResult() == 41);
        BOOST_TEST(machine->getIntegerIntermediate() == 43);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    }
}

BOOST_AUTO_TEST_CASE(struct_formatting_uses_type_names_and_complete_field_captures_in_both_modes) {
    const NomadString declarations =
        "struct FormatEmpty\nend\n"
        "class FormatObject\nnumber = 5\nend\n"
        "struct FormatPoint\nx = 7\nname = \"point\"\nempty = new FormatEmpty\nobject = new FormatObject\n"
        "fun next\nthis.x = this.x + 1\nreturn this.x\nend\n"
        "fun render\nreturn $\"{this} {this.x + 1} {this.name}\"\nend\nend\n"
        "struct FormatOuter\npoint = new FormatPoint\nlabel = \"outer\"\nend\n"
        "class FormatHolder\npoint = new FormatPoint\n"
        "fun copy\nreturn this.point\nend\nend\n"
        "fun makeFormatPoint\nreturn new FormatPoint\nend\n"
        "fun renderFormatParams head:int point:FormatPoint empty:FormatEmpty tail:int\n"
        "return $\"{head} {point} {empty} {tail} {point.x + tail}\"\nend\n"
        "fun renderDynamicFormatParams head:int point:FormatPoint empty:FormatEmpty tail:int\n"
        "template = \"{head} {point} {empty} {tail} {point.x + tail}\"\nreturn $template\nend\n";
    const std::vector<std::pair<NomadString, NomadString>> cases{
        {"point = new FormatPoint\nreturn $\"{point} {point.x} {point.name} {point.empty} {point.object}\"",
            "FormatPoint 7 point FormatEmpty FormatObject"},
        {"point = new FormatPoint\ntemplate = \"{point} {point.x} {point.name} {point.empty} {point.object}\"\nreturn $template",
            "FormatPoint 7 point FormatEmpty FormatObject"},
        {"outer = new FormatOuter\nreturn $\"{outer} {outer.point} {outer.point.x + 2} {outer.point.name}\"",
            "FormatOuter FormatPoint 9 point"},
        {"outer = new FormatOuter\ntemplate = \"{outer} {outer.point} {outer.point.x + 2} {outer.point.name}\"\nreturn $template",
            "FormatOuter FormatPoint 9 point"},
        {"return $\"{makeFormatPoint} {(makeFormatPoint).x} {(new FormatPoint).name} {new FormatEmpty}\"",
            "FormatPoint 7 point FormatEmpty"},
        {"template = \"{makeFormatPoint} {(makeFormatPoint).x} {(new FormatPoint).name} {new FormatEmpty}\"\nreturn $template",
            "FormatPoint 7 point FormatEmpty"},
        {"return renderFormatParams 11 (new FormatPoint) (new FormatEmpty) 13",
            "11 FormatPoint FormatEmpty 13 20"},
        {"return renderDynamicFormatParams 11 (new FormatPoint) (new FormatEmpty) 13",
            "11 FormatPoint FormatEmpty 13 20"},
        {"point = new FormatPoint\nreturn point.render", "FormatPoint 8 point"},
        {"holder = new FormatHolder\nreturn $\"{holder} {holder.point.x} {holder.copy} {(holder.copy).name}\"",
            "FormatHolder 7 FormatPoint point"},
        {"holder = new FormatHolder\ntemplate = \"{holder} {holder.point.x} {holder.copy} {(holder.copy).name}\"\nreturn $template",
            "FormatHolder 7 FormatPoint point"},
        {"struct FormatEmptyReceiver\nfun render\nreturn $\"{this} {new FormatEmpty}\"\nend\nend\n"
            "return (new FormatEmptyReceiver).render", "FormatEmptyReceiver FormatEmpty"}
    };
    for (const auto& [body, expected] : cases) {
        BOOST_TEST_CONTEXT(body) {
            LanguageTestFixture fixture;
            fixture.addFunction("main", declarations + body);
            BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
            const auto stringsBefore = RuntimeValue::getLiveStringCount();
            const auto objectsBefore = NomadObject::getLiveObjectCount();
            for (auto iteration = 0; iteration < 3; ++iteration) {
                const auto outcome = fixture.execute("main");
                BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
                BOOST_TEST(outcome.text == expected);
                BOOST_TEST(outcome.leakedStrings == 0);
                BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
                BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
            }

        }

    }

}

BOOST_AUTO_TEST_CASE(struct_formatting_preserves_compiled_capture_snapshots_and_live_dynamic_evaluation) {
    LanguageTestFixture fixture;
    fixture.addFunction(
        "main",
        "struct FormatCounter\nvalue = 0\ntext = \"owned\"\n"
        "fun next\nthis.value = this.value + 1\nreturn this.value\nend\nend\n"
        "counter = new FormatCounter\n"
        "compiled = $\"{counter.next} {counter.value} {counter.next}\"\n"
        "assert compiled == \"1 0 1\"\nassert counter.value == 0\n"
        "template = \"{counter.next} {counter.value} {counter.next}\"\n"
        "formatted = $template\nassert formatted == \"1 1 2\"\nassert counter.value == 2\n"
        "return $\"{compiled} / {formatted}\""
    );
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto iteration = 0; iteration < 3; ++iteration) {
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.text == "1 0 1 / 1 1 2");
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }

}

BOOST_AUTO_TEST_CASE(interpreter_struct_formatting_handles_empty_values_and_retains_error_locations) {
    LanguageTestFixture fixture;
    fixture.addFunction(
        "types",
        "struct InterpreterFormatEmpty\nend\n"
        "struct InterpreterFormatValue\nnumber = 7\ntext = \"value\"\nend\nreturn 0"
    );
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    InterpreterContext context(interpreter);
    BOOST_REQUIRE(interpreter.execute(
        "value = new InterpreterFormatValue\nempty = new InterpreterFormatEmpty",
        context
    ));
    machine->setIntegerResult(41);
    machine->setIntegerIntermediate(43);
    const auto instructionsBefore = runtime.getInstructions().size();
    const auto result = interpreter.formatString(
        "{value} {empty} {value.number + 1} {value.missing} {value.text}",
        context,
        "struct-format.nomad",
        3,
        5
    );
    BOOST_TEST(result.text.starts_with("InterpreterFormatValue InterpreterFormatEmpty 8 {"));
    BOOST_TEST(result.text.ends_with("} value"));
    BOOST_REQUIRE(result.diagnostics.size() == 1U);
    BOOST_TEST(result.diagnostics[0].sourceName == "struct-format.nomad");
    BOOST_TEST(result.diagnostics[0].line == 3U);
    BOOST_TEST(result.diagnostics[0].column == 5U);
    BOOST_TEST(!context.hasError());
    BOOST_TEST(machine->getIntegerResult() == 41);
    BOOST_TEST(machine->getIntegerIntermediate() == 43);
    BOOST_TEST(runtime.getInstructions().size() == instructionsBefore);
}

BOOST_AUTO_TEST_CASE(format_expressions_read_complete_struct_receiver_captures_in_callbacks) {
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    auto* virtualMachine = &fixture.getVirtualMachine();
    std::unique_ptr<Closure> callback;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "test.keepFormatCallback",
        [&](VirtualMachine* active) {
            callback = active->createClosure(&runtime, active->getCallableIdParameter(0));
        },
        {defParameter("callback", runtime.getTypeManager().getCallbackType({}, runtime.getStringType()), "")},
        runtime.getVoidType(),
        ""
    ).isValid());
    fixture.addFunction(
        "main",
        "struct FormatCapturedPoint\nx = 7\ntext = \"owned\"\n"
        "fun keep\n"
        "test.keepFormatCallback fun\nreturn $\"{this} {this.x + 1} {this.text}\"\nend\n"
        "this.x = 99\nend\nend\n"
        "point = new FormatCapturedPoint\npoint.keep\nreturn 0"
    );
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto iteration = 0; iteration < 3; ++iteration) {
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_REQUIRE(callback != nullptr);
        for (auto invocation = 0; invocation < 2; ++invocation) {
            RuntimeValue result;
            virtualMachine->executeFunction(callback.get(), {}, result);
            BOOST_TEST(result.getStringValue() == "FormatCapturedPoint 8 owned");
            runtime.getStringType()->freeValue(&result);
        }

        callback.reset();
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }

}

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
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);
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
    BOOST_REQUIRE(context.getInterpreter().execute("$(\"{score}\")", context));
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

BOOST_AUTO_TEST_CASE(interpreter_format_entry_point_preserves_strict_placeholders_and_free_function_contract)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    InterpreterContext context(interpreter);
    BOOST_REQUIRE(interpreter.execute("score = 10", context));
    const auto instructionsBefore = runtime.getInstructions().size();
    const NomadString format{"value {score + 1} invalid {score = 99} tail {score}"};
    const auto result = interpreter.formatString(format, context, "bridge.nomad", 4, 7);
    const auto compatibility = formatDynamicString(format, context, "bridge.nomad", 4, 7);
    BOOST_TEST(result.text == compatibility.text);
    BOOST_TEST(result.text.starts_with("value 11 invalid {"));
    BOOST_TEST(result.text.ends_with("} tail 10"));
    BOOST_REQUIRE_EQUAL(result.diagnostics.size(), 1U);
    BOOST_REQUIRE_EQUAL(compatibility.diagnostics.size(), 1U);
    const auto& diagnostic = result.diagnostics[0];
    BOOST_TEST(diagnostic.message == compatibility.diagnostics[0].message);
    BOOST_TEST(diagnostic.sourceName == "bridge.nomad");
    BOOST_TEST(diagnostic.line == 4U);
    BOOST_TEST(diagnostic.column == 7U);
    BOOST_TEST(diagnostic.offset == compatibility.diagnostics[0].offset);
    BOOST_TEST(diagnostic.length == compatibility.diagnostics[0].length);
    BOOST_TEST(context.getVariableValue("score")->getIntegerValue() == 10);
    BOOST_TEST(context.getResult() == nullptr);
    BOOST_TEST(!context.hasError());
    BOOST_TEST(runtime.getInstructions().size() == instructionsBefore);
}

BOOST_AUTO_TEST_CASE(dynamic_format_supports_nested_templates_and_quoted_braces)
{
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);
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
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);
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
        const Interpreter contextEvaluator(*machine);
        InterpreterContext context(contextEvaluator);
        BOOST_CHECK_THROW(static_cast<void>(formatDynamicString(format, context, "fault.nomad", 1, 1)),
            VirtualMachineException);
        BOOST_REQUIRE(context.getInterpreter().execute("2 + 3", context));
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
    runtime.getStringType()->freeValue(&machine->getResult());
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
    dumpInstructions(runtime, disassembly);
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
