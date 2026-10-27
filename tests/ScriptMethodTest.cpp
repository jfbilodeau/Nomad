// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/script/Closure.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/ScriptObject.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

using namespace nomad;
using namespace nomad::test;

BOOST_AUTO_TEST_SUITE(language_script_methods)

BOOST_AUTO_TEST_CASE(overloads_resolve_by_owner_arity_and_argument_type) {
    const auto outcome = runSource(
        "class Calculator\nbase = 10\n"
        "fun value\nreturn this.base\nend\n"
        "fun value number:int\nreturn this.base + number\nend\n"
        "fun value number:float\nreturn number + 0.5\nend\n"
        "fun value left:int right:int\nreturn this.base + left + right\nend\nend\n"
        "class Other\nfun value text:string\nreturn text.toUpper\nend\nend\n"
        "owner = new Calculator\nother = new Other\n"
        "assert (owner.value) == 10\nassert (owner.value 1.0) == 1.5\n"
        "assert (other.value \"text\") == \"TEXT\"\n"
        "assert ((new Calculator).value 5) == 15\n"
        "return (owner.value 2) + (owner.value 3 4)"
    );
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_REQUIRE(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 29);
    BOOST_TEST(outcome.leakedStrings == 0);
}

BOOST_AUTO_TEST_CASE(methods_restore_receivers_after_nested_calls_and_argument_evaluation) {
    const auto outcome = runSource(
        "class Child\nvalue = 2\n"
        "fun accept number:int\nthis.value = number\nreturn this\nend\n"
        "fun read\nreturn this.value\nend\nend\n"
        "class Owner\nvalue = 7\nchild = new Child\n"
        "fun read\nreturn this.value\nend\n"
        "fun nested\nsaved = this.value\nother = this.child.accept (this.read)\n"
        "assert this.value == saved\nreturn this.value + other.read\nend\nend\n"
        "owner = new Owner\nreturn owner.nested"
    );
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_REQUIRE(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 14);
    BOOST_TEST(outcome.leakedStrings == 0);
}

BOOST_AUTO_TEST_CASE(forward_and_recursive_method_calls_use_ordinary_returns) {
    const auto outcome = runSource(
        "class Calculator\n"
        "fun first\nreturn this.second\nend\n"
        "fun second\nreturn 42\nend\n"
        "fun factorial value:int\nif value <= 1\nreturn 1\nend\n"
        "return value * (this.factorial (value - 1))\nend\nend\n"
        "owner = new Calculator\nassert (owner.first) == 42\nreturn owner.factorial 5"
    );
    BOOST_REQUIRE_MESSAGE(outcome.compiled, outcome.diagnostics);
    BOOST_REQUIRE(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 120);
}

BOOST_AUTO_TEST_CASE(compiled_interpreter_and_host_methods_preserve_parameter_copies) {
    LanguageTestFixture fixture;
    fixture.addFunction("main",
        "class Counter\nvalue = 0\n"
        "fun add amount:int\namount = amount + 1\nthis.value = this.value + amount\nreturn this.value\nend\n"
        "fun clear\nthis.value = 0\nreturn\nend\nend\n"
        "counter = new Counter\nnumber = 4\nanswer = counter.add number\nassert number == 4\nreturn answer"
    );
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    const auto outcome = fixture.execute("main");
    BOOST_REQUIRE(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 5);
    const auto* type = runtime.getTypeByName("Counter")->asObjectType();
    const auto resolution = runtime.resolveMethod(type, "add", {runtime.getIntegerType()});
    BOOST_REQUIRE(resolution.isResolved());
    const auto before = NomadObject::getLiveObjectCount();
    {
        auto machine = runtime.createVirtualMachine();
        RuntimeValue receiver;
        receiver.setObjectValue(machine->constructObject(type->getConstructors()[0]->getId(), {}));
        const RuntimeValue argument{NomadInteger{4}};
        machine->runScriptMethod(resolution.id, {argument}, receiver);
        BOOST_TEST(argument.getIntegerValue() == 4);
        BOOST_TEST(machine->getIntegerResult() == 5);
        BOOST_TEST(receiver.getObjectValue()->getReferenceCount() == 1u);
        BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
        BOOST_CHECK_THROW(machine->runScriptMethod(resolution.id, {}, receiver), VirtualMachineException);
        BOOST_CHECK_THROW(machine->runScriptMethod(CallableId::INVALID, {}, receiver), VirtualMachineException);
        receiver.freeObjectValue();
        InterpreterContext context(*machine);
        Interpreter interpreter;
        BOOST_REQUIRE(interpreter.execute("counter = new Counter", context));
        BOOST_REQUIRE(interpreter.execute("counter.add 4", context));
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 5);
        BOOST_REQUIRE(interpreter.execute("counter.clear", context));
        BOOST_REQUIRE(interpreter.execute("counter.add 4", context));
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 5);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(returning_a_temporary_receiver_retains_it_and_releases_final_arguments) {
    LanguageTestFixture fixture;
    fixture.addFunction("main",
        "class Box\nlabel = \"initial\"\n"
        "fun echo text:string\ntext = text.toUpper\nthis.label = text\nreturn this\nend\nend\n"
        "return (new Box).echo \"value\""
    );
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    const auto before = NomadObject::getLiveObjectCount();
    for (auto attempt = 0; attempt < 2; ++attempt) {
        RuntimeValue result;
        runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
        auto* object = result.asObject<ScriptObject>();
        BOOST_REQUIRE(object != nullptr);
        BOOST_TEST(object->getReferenceCount() == 1u);
        const auto* type = object->getObjectType();
        const auto slot = type->getFieldIndex(type->getProperty("label")->getId());
        BOOST_TEST(object->getField(slot).getStringObject()->getString() == "VALUE");
        result.freeObjectValue();
        BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    }
    {
        const auto* type = runtime.getTypeByName("Box")->asObjectType();
        const auto method = runtime.resolveMethod(type, "echo", {runtime.getStringType()});
        BOOST_REQUIRE(method.isResolved());
        auto machine = runtime.createVirtualMachine();
        RuntimeValue receiver;
        receiver.setObjectValue(machine->constructObject(type->getConstructors()[0]->getId(), {}));
        RuntimeValue text;
        text.setStringValue("borrowed");
        machine->runScriptMethod(method.id, {text}, receiver);
        BOOST_TEST(machine->getResult().getObjectValue() == receiver.getObjectValue());
        BOOST_TEST(receiver.getObjectValue()->getReferenceCount() == 2u);
        BOOST_TEST(text.getStringObject()->getString() == "borrowed");
        BOOST_TEST(text.getObjectValue()->getReferenceCount() == 1u);
        machine->getResult().freeObjectValue();
        receiver.freeObjectValue();
        text.freeObjectValue();
        InterpreterContext context(*machine);
        Interpreter interpreter;
        BOOST_REQUIRE(interpreter.execute("box = new Box", context));
        BOOST_REQUIRE(interpreter.execute("text = \"borrowed\"", context));
        BOOST_REQUIRE(interpreter.execute("box.echo text", context));
        BOOST_REQUIRE(interpreter.execute("text", context));
        BOOST_TEST(context.getResult()->getValue().getStringObject()->getString() == "borrowed");
        BOOST_REQUIRE(interpreter.execute("box.label", context));
        BOOST_TEST(context.getResult()->getValue().getStringObject()->getString() == "BORROWED");
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(native_method_reentry_restores_receiver_and_parameter_bindings) {
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    CallableId scriptMethodId;
    fixture.getCompiler().registerPhaseHook([&](CompilerContext*, const CompilationPhase phase) {
        if (phase != CompilationPhase::Parse) return;
        const auto* type = runtime.getTypeByName("Counter");
        BOOST_REQUIRE(runtime.addMethod(type->getId(), {
            .name = "invokeHost",
            .parameters = {{runtime.getIntegerType()->getId(), "amount", ""}},
            .returnTypeId = runtime.getIntegerType()->getId(),
            .function = [&](VirtualMachine* machine) {
                RuntimeValue receiver;
                receiver.setObjectValue(machine->getThis());
                machine->runScriptMethod(scriptMethodId, {machine->getParameter(0)}, receiver);
                BOOST_TEST(machine->getThis() == receiver.getObjectValue());
                BOOST_TEST(machine->getIntegerParameter(0) == 4);
                machine->setIntegerResult(machine->getIntegerResult() + machine->getIntegerParameter(0));
            }
        }).isValid());
    });
    fixture.addFunction("main",
        "class Counter\nvalue = 0\n"
        "fun add amount:int\namount = amount + 1\nthis.value = this.value + amount\nreturn this.value\nend\nend\n"
        "counter = new Counter\nreturn counter.invokeHost 4"
    );
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto resolution = runtime.resolveMethod(runtime.getTypeByName("Counter"), "add", {runtime.getIntegerType()});
    BOOST_REQUIRE(resolution.isResolved());
    scriptMethodId = resolution.id;
    const auto before = NomadObject::getLiveObjectCount();
    const auto outcome = fixture.execute("main");
    BOOST_REQUIRE(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 9);
    {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        Interpreter interpreter;
        BOOST_REQUIRE(interpreter.execute("counter = new Counter", context));
        BOOST_REQUIRE(interpreter.execute("counter.invokeHost 4", context));
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 9);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(method_callbacks_and_format_expressions_capture_the_method_receiver) {
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    std::unique_ptr<Closure> closure;
    const auto* callbackType = runtime.getCallbackType({}, runtime.getIntegerType());
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "save", [&](VirtualMachine* machine) {
            closure = machine->createClosure(&runtime, machine->getCallableIdParameter(0));
        }, {defParameter("callback", callbackType, "")}, runtime.getVoidType(), ""
    ).isValid());
    fixture.addFunction("main",
        "class Owner\nvalue = 8\nfun read\nreturn this.value\nend\n"
        "fun remember number:int\nnumber = number + 1\noffset = 3\n"
        "save fun\nreturn (this.read) + number + offset\nend\n"
        "return $\"value {this.read} number {number}\"\nend\nend\n"
        "owner = new Owner\nreturn owner.remember 4"
    );
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(result.getStringObject()->getString() == "value 8 number 5");
    result.freeObjectValue();
    BOOST_REQUIRE(closure != nullptr);
    runtime.executeFunction(closure.get(), {}, result);
    BOOST_TEST(result.getIntegerValue() == 16);
    closure.reset();
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(dynamic_formatting_reads_constructor_method_and_captured_receiver_frames) {
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    std::unique_ptr<Closure> closure;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "save", [&](VirtualMachine* machine) {
            closure = machine->createClosure(&runtime, machine->getCallableIdParameter(0));
        }, {defParameter("callback", runtime.getCallbackType({}, runtime.getStringType()), "")},
        runtime.getVoidType(), ""
    ).isValid());
    fixture.addFunction("main",
        "class Counter\nvalue = 0\n"
        "init amount:int\nlocal = amount + 1\nthis.value = local\n"
        "this.text = $(\"ctor {amount} {local} {this.value}\")\nend\n"
        "fun read amount:int\nlocal = this.value + amount\n"
        "save fun\nretained = this.value + amount\n"
        "return $(\"captured {this.value} {amount} {retained}\")\nend\n"
        "return $(\"method {amount} {local} {this.value}\")\nend\nend\n"
        "counter = new Counter 4\nassert counter.text == \"ctor 4 5 5\"\nreturn counter.read 3"
    );
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(result.getStringObject()->getString() == "method 3 8 5");
    result.freeObjectValue();
    BOOST_REQUIRE(closure != nullptr);
    runtime.executeFunction(closure.get(), {}, result);
    BOOST_TEST(result.getStringObject()->getString() == "captured 5 3 8");
    result.freeObjectValue();
    closure.reset();
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(frame_interpretation_reads_and_writes_constructor_and_method_bindings) {
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    auto constructorsSeen = 0;
    auto methodsSeen = 0;
    runtime.registerDynamicVariable("test.frame", [](VirtualMachine*, const RuntimeValue&) {},
        [&](VirtualMachine* machine, RuntimeValue& value) {
            const auto* callable = machine->getCurrentCallable();
            BOOST_REQUIRE(callable != nullptr);
            BOOST_TEST(machine->getCurrentFunction() == nullptr);
            if (runtime.getConstructor(callable->getId()) != nullptr) {
                ++constructorsSeen;
            } else {
                BOOST_REQUIRE(runtime.getMethod(callable->getId()) != nullptr);
                ++methodsSeen;
            }
            InterpreterContext context(*machine, *callable);
            BOOST_TEST(context.getCallable()->getObjectType() == callable->getObjectType());
            BOOST_TEST(context.getCallable() != callable);
            BOOST_TEST(context.getFunction() == nullptr);
            const auto variablesBefore = callable->getVariableCount();
            const auto parametersBefore = callable->getParameterCount();
            BOOST_REQUIRE(Interpreter{}.evaluateExpression("this.value + amount + local", context));
            const auto total = context.getResult()->getValue().getIntegerValue();
            BOOST_REQUIRE(Interpreter{}.execute("local = local + 1", context));
            BOOST_REQUIRE(Interpreter{}.execute("amount = amount + 2", context));
            const auto assigned = Interpreter{}.execute("this.value = this.value + 3", context);
            BOOST_REQUIRE_MESSAGE(assigned, (context.getError() ? *context.getError() : "Field assignment failed"));
            BOOST_TEST(callable->getVariableCount() == variablesBefore);
            BOOST_TEST(callable->getParameterCount() == parametersBefore);
            BOOST_TEST(machine->getCurrentCallable() == callable);
            value.setStringValue(std::to_string(total));
        }, runtime.getStringType(), ""
    );
    fixture.addFunction("main",
        "class Counter\nvalue = 0\n"
        "init amount:int\nlocal = amount + 1\nthis.value = local\nthis.report = test.frame\n"
        "assert local == 6\nassert amount == 6\nend\n"
        "fun read amount:int\nlocal = this.value + amount\ntext = test.frame\n"
        "assert local == 12\nassert amount == 5\nreturn text\nend\nend\n"
        "counter = new Counter 4\nassert counter.report == \"14\"\n"
        "text = counter.read 3\nassert counter.value == 11\nreturn text"
    );
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto outcome = fixture.execute("main");
    BOOST_REQUIRE(!outcome.fault.has_value());
    BOOST_TEST(outcome.text == "22");
    BOOST_TEST(outcome.leakedStrings == 0);
    BOOST_TEST(constructorsSeen == 1);
    BOOST_TEST(methodsSeen == 1);
}

BOOST_AUTO_TEST_CASE(invalid_method_calls_report_source_diagnostics) {
    for (const auto* body : {
        "return owner.read true", "return owner.read", "return owner.missing",
        "owner.read = 3", "return owner.clear", "return owner.read 1 2"
    }) {
        BOOST_TEST_CONTEXT(body) {
            const auto errors = compileErrors(
                NomadString("class Owner\nfun read value:int\nreturn value\nend\nfun clear\nreturn\nend\nend\n"
                    "owner = new Owner\n") + body
            );
            BOOST_TEST(!errors.empty());
            BOOST_TEST(errors.find("Internal error") == NomadString::npos);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
