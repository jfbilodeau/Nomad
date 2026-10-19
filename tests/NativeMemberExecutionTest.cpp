// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/system/String.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Expression.hpp>

#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

using namespace nomad;

namespace {

class TestObject : public NomadObject {
public:
    NomadInteger value = 4;
};

const ObjectTypeBase* registerTestObject(Runtime& runtime) {
    auto descriptor = std::make_unique<ObjectType>("TestObject");
    const auto* type = descriptor.get();
    runtime.registerType(std::move(descriptor));
    runtime.registerNativeConstructor(
        type, {}, [](std::span<const RuntimeValue>) { return new TestObject; }, ""
    );
    runtime.registerNativeProperty(
        type, "value", runtime.getIntegerType(),
        [](VirtualMachine* machine, RuntimeValue& result) {
            result.setIntegerValue(machine->getThisAs<TestObject>()->value);
        },
        [](VirtualMachine* machine, const RuntimeValue& value) {
            machine->getThisAs<TestObject>()->value = value.getIntegerValue();
        }, ""
    );
    runtime.registerNativeMethod(
        type, "increment",
        [](VirtualMachine* machine) {
            auto* instance = machine->getThisAs<TestObject>();
            instance->value += machine->getIntegerParameter(0);
            machine->setIntegerResult(instance->value);
        },
        {defParameter("amount", runtime.getIntegerType(), "")}, runtime.getIntegerType(), ""
    );
    return type;
}

NomadId compileFunction(Runtime& runtime, const NomadString& source) {
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"test", {"test.nomad", source}}}, {}});
    const auto functionId = runtime.getFunctionId("test");

    const auto compiled = compiler->compileFunctions(&context);
    const auto* error = context.getFirstError();
    const auto message = error == nullptr ? NomadString("Unknown compilation error") : error->message;
    BOOST_REQUIRE_MESSAGE(compiled, message);
    return functionId;
}

} // namespace

BOOST_AUTO_TEST_SUITE(native_member_execution)

BOOST_AUTO_TEST_CASE(resolved_method_and_constructor_survive_catalog_growth) {
    Runtime runtime;
    const auto* type = registerTestObject(runtime);
    auto compiler = runtime.createCompiler();
    CompilerContext compilation(compiler.get());
    auto receiver = std::make_unique<ConstructorExpression>(
        nullptr, 1, 0, "TestObject", type, ArgumentList{}
    );
    ArgumentList arguments;
    arguments.addExpressionArgument(1, 20, runtime.getIntegerType(),
        std::make_unique<IntegerLiteral>(nullptr, 1, 20, 3));
    NativeMemberExpression expression(nullptr, 1, 0, "TestObject.increment", "increment",
        std::move(receiver), std::move(arguments));
    BOOST_REQUIRE(expression.resolve(&compilation, nullptr));

    BOOST_REQUIRE(runtime.registerNativeConstructor(
        type, {defParameter("value", runtime.getIntegerType(), "")},
        [](std::span<const RuntimeValue>) {
            auto* object = new TestObject;
            object->value = 100;
            return object;
        }, ""
    ) != NOMAD_INVALID_ID);
    BOOST_REQUIRE(runtime.registerNativeMethod(
        runtime.getStringType(), "increment",
        [](VirtualMachine* machine) { machine->setIntegerResult(200); },
        {defParameter("amount", runtime.getIntegerType(), "")}, runtime.getIntegerType(), ""
    ) != NOMAD_INVALID_ID);
    for (auto index = NomadInteger{0}; index < 64; ++index) {
        BOOST_REQUIRE(runtime.registerNativeFunction(
            "growth" + toString(index), [](VirtualMachine*) {}, {}, runtime.getVoidType(), ""
        ) != NOMAD_INVALID_ID);
    }

    const auto liveObjects = NomadObject::getLiveObjectCount();
    {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext evaluation(*machine);
        expression.evaluate(evaluation);
        BOOST_REQUIRE(!evaluation.hasError());
        BOOST_REQUIRE(evaluation.getResult() != nullptr);
        BOOST_TEST(evaluation.getResult()->getValue().getIntegerValue() == 7);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == liveObjects);

    const auto functionId = compileFunction(runtime,
        "object = new TestObject\nreturn object.increment 3");
    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);
    BOOST_TEST(result.getIntegerValue() == 7);
}

BOOST_AUTO_TEST_CASE(unresolved_method_and_constructor_evaluation_reports_an_error) {
    Runtime runtime;
    const auto* type = registerTestObject(runtime);
    auto machine = runtime.createVirtualMachine();
    InterpreterContext evaluation(*machine);
    ConstructorExpression constructor(nullptr, 1, 0, "TestObject", type, ArgumentList{});
    BOOST_CHECK_THROW(constructor.evaluate(evaluation), AstException);
    NativeMemberExpression method(nullptr, 1, 0, "text.toUpper", "toUpper",
        std::make_unique<StringLiteral>(nullptr, 1, 0, "text"), ArgumentList{});
    BOOST_CHECK_THROW(method.evaluate(evaluation), AstException);
}

BOOST_AUTO_TEST_CASE(typed_method_borrows_the_string_receiver) {
    Runtime runtime;
    bool called = false;
    runtime.registerNativeMethod(
        runtime.getStringType(),
        "typedLength",
        [&called](VirtualMachine* virtualMachine) {
            const auto* receiver = virtualMachine->getThisAs<const NomadStringObject>();
            called = true;
            BOOST_TEST(receiver == virtualMachine->getThis());
            virtualMachine->setIntegerResult(static_cast<NomadInteger>(receiver->getLength()));
        },
        {},
        runtime.getIntegerType(),
        NomadDoc("Read the string length")
    );

    const auto functionId = compileFunction(runtime, "return \"Nomad\".typedLength");
    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);

    BOOST_TEST(called);
    BOOST_TEST(result.getIntegerValue() == 5);
}

BOOST_AUTO_TEST_CASE(typed_method_supports_a_host_defined_object) {
    Runtime runtime;
    const auto* type = registerTestObject(runtime);
    const auto memberId = runtime.registerNativeMethod(
        type,
        "hostObject",
        [](VirtualMachine* virtualMachine) {
            auto* receiver = virtualMachine->getThisAs<TestObject>();
            BOOST_TEST(receiver->getReferenceCount() == 1);
            virtualMachine->setIntegerResult(42);
        },
        {},
        runtime.getIntegerType(),
        NomadDoc("Test a host-defined receiver")
    );
    BOOST_REQUIRE(memberId != NOMAD_INVALID_ID);
    auto virtualMachine = runtime.createVirtualMachine();
    RuntimeValue receiver;
    receiver.setObjectValue(new TestObject);
    virtualMachine->runNativeMethod(memberId, {}, receiver);
    BOOST_TEST(virtualMachine->getResult().getIntegerValue() == 42);
    receiver.freeObjectValue();
}

BOOST_AUTO_TEST_CASE(typed_receiver_requires_an_active_method) {
    Runtime runtime;
    auto virtualMachine = runtime.createVirtualMachine();
    BOOST_CHECK_THROW(
        virtualMachine->getThisAs<NomadStringObject>(),
        VirtualMachineException
    );
    BOOST_CHECK_THROW(virtualMachine->getThisAs<TestObject>(), VirtualMachineException);
}

BOOST_AUTO_TEST_CASE(object_method_borrows_instance_without_receiver_writeback) {
    Runtime runtime;
    auto descriptor = std::make_unique<ObjectType>("TestObject");
    const auto* type = descriptor.get();
    runtime.registerType(std::move(descriptor));
    auto* instance = new TestObject;
    const auto memberId = runtime.registerNativeMethod(
        type,
        "checkInstance",
        [instance](VirtualMachine* machine) {
            BOOST_TEST(machine->getThis() == instance);
            BOOST_TEST(machine->getThisAs<TestObject>() == instance);
            machine->setIntegerResult(42);
        },
        {},
        runtime.getIntegerType(),
        ""
    );
    BOOST_REQUIRE(memberId != NOMAD_INVALID_ID);
    BOOST_REQUIRE(runtime.getNativeMemberMethod(memberId) != nullptr);
    auto machine = runtime.createVirtualMachine();
    RuntimeValue receiver;
    receiver.setObjectValue(instance);
    machine->runNativeMethod(memberId, {}, receiver);
    BOOST_TEST(machine->getResult().getIntegerValue() == 42);
    receiver.freeObjectValue();
    BOOST_CHECK_THROW(machine->runNativeMethod(memberId, {}, receiver), VirtualMachineException);
}

BOOST_AUTO_TEST_CASE(native_registration_keeps_methods_out_of_global_function_catalog) {
    Runtime runtime;
    auto descriptor = std::make_unique<ObjectType>("CatalogObject");
    const auto* type = descriptor.get();
    runtime.registerType(std::move(descriptor));
    const auto first = runtime.registerNativeFunction(
        "catalog.first", [](VirtualMachine*) {}, {}, runtime.getVoidType(), ""
    );
    const auto method = runtime.registerNativeMethod(
        type, "catalogMethod", [](VirtualMachine*) {}, {}, runtime.getVoidType(), ""
    );
    const auto last = runtime.registerNativeFunction(
        "catalog.last", [](VirtualMachine*) {}, {}, runtime.getVoidType(), ""
    );
    BOOST_REQUIRE(method != NOMAD_INVALID_ID);
    BOOST_TEST(last > first + 1);
    BOOST_TEST(runtime.getNextNativeFunctionId(first) == last);
    BOOST_TEST(runtime.getNativeFunctionId("catalogMethod") == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.getNativeMemberId(type, "catalogMethod") == method);
    const auto* methodCallable = runtime.getNativeMemberMethod(method);
    BOOST_REQUIRE(methodCallable != nullptr);
    BOOST_TEST(methodCallable->getObjectType() == type);
    const auto functions = runtime.getNativeFunctions();
    NomadIndex enumerated = 0;
    for (auto id = runtime.getFirstNativeFunctionId(); id != NOMAD_INVALID_ID;
         id = runtime.getNextNativeFunctionId(id)) {
        const auto* function = runtime.getNativeFunction(id);
        BOOST_REQUIRE(function != nullptr);
        BOOST_TEST(function->getObjectType() == nullptr);
        BOOST_TEST(function->getName() != "catalogMethod");
        ++enumerated;
    }
    BOOST_TEST(enumerated == functions.size());
    for (const auto* function : functions) {
        BOOST_TEST(function->getObjectType() == nullptr);
        BOOST_TEST(function->getName() != "catalogMethod");
    }
    BOOST_TEST(runtime.registerNativeMethod(
        nullptr, "invalid", [](VirtualMachine*) {}, {}, runtime.getVoidType(), ""
    ) == NOMAD_INVALID_ID);
}

BOOST_AUTO_TEST_CASE(compiled_property_getter_reads_temporary_receiver) {
    Runtime runtime;

    const auto functionId = compileFunction(runtime, "return \"Nomad\".length");
    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);

    BOOST_TEST(result.getIntegerValue() == 5);
}

BOOST_AUTO_TEST_CASE(compiled_property_setter_commits_mutated_receiver) {
    Runtime runtime;
    registerTestObject(runtime);

    const auto functionId = compileFunction(runtime,
        "object = new TestObject\nalias = object\nobject.value = \"abc\".length\nreturn alias.value");
    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);

    BOOST_TEST(result.getIntegerValue() == 3);
}

BOOST_AUTO_TEST_CASE(compiled_method_receives_receiver_and_arguments) {
    Runtime runtime;
    registerTestObject(runtime);

    const auto functionId = compileFunction(runtime, "object = new TestObject\nreturn object.increment \"abc\".length");
    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);

    BOOST_TEST(result.getIntegerValue() == 7);
}

BOOST_AUTO_TEST_CASE(compiled_properties_and_methods_mutate_parameter_instances) {
    Runtime runtime;
    registerTestObject(runtime);

    const auto functionId = compileFunction(runtime,
        "fun change object:TestObject\n"
        " object.value = 10\n"
        " object.increment 3\n"
        "end\n"
        "object = new TestObject\nchange object\nreturn object.value");
    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);

    BOOST_TEST(result.getIntegerValue() == 13);
}

BOOST_AUTO_TEST_CASE(interpreter_executes_properties_and_methods) {
    Runtime runtime;
    registerTestObject(runtime);

    auto machine = runtime.createVirtualMachine();
    InterpreterContext interpreter(*machine);
    BOOST_REQUIRE(Interpreter{}.execute("object = new TestObject", interpreter));
    BOOST_REQUIRE(Interpreter{}.execute("object.value = \"abc\".length", interpreter));
    const auto methodExecuted = Interpreter{}.execute("object.increment 3", interpreter);
    const auto* error = interpreter.getError();
    const auto message = error == nullptr ? NomadString("Unknown interpreter error") : *error;
    BOOST_REQUIRE_MESSAGE(methodExecuted, message);
    BOOST_REQUIRE(Interpreter{}.execute("object.value", interpreter));
    BOOST_REQUIRE(interpreter.getResult() != nullptr);

    BOOST_TEST(interpreter.getResult()->getValue().getIntegerValue() == 6);
    BOOST_CHECK_THROW(machine->getThis(), VirtualMachineException);
}

BOOST_AUTO_TEST_CASE(failed_method_preserves_object_mutations_and_restores_this) {
    Runtime runtime;
    const auto* type = registerTestObject(runtime);
    runtime.registerNativeMethod(
        type,
        "fail",
        [](VirtualMachine* virtualMachine) {
            virtualMachine->getThisAs<TestObject>()->value = 99;
            virtualMachine->fault("Expected failure");
        },
        {},
        runtime.getVoidType(),
        NomadDoc("Fail")
    );

    auto machine = runtime.createVirtualMachine();
    InterpreterContext interpreter(*machine);
    BOOST_REQUIRE(Interpreter{}.execute("object = new TestObject", interpreter));
    BOOST_TEST(!Interpreter{}.execute("object.fail", interpreter));
    BOOST_REQUIRE(interpreter.getVariableValue("object") != nullptr);
    BOOST_TEST(static_cast<TestObject*>(interpreter.getVariableValue("object")->getObjectValue())->value == 99);
    BOOST_CHECK_THROW(machine->getThis(), VirtualMachineException);
}

BOOST_AUTO_TEST_CASE(string_indexing_handles_unicode_chains_and_precedence) {
    for (const auto& [source, expected] : std::vector<std::pair<NomadString, NomadString>>{
        {"return \"Nomad\"[1]", "o"},
        {"return \"new\"[0]", "n"},
        {"return \"[\"[0]", "["},
        {"return \"]\"[0]", "]"},
        {"return \"=\"[0]", "="},
        {"return \"a\xC3\xA9\xF0\x9F\x98\x80\"[1]", "\xC3\xA9"},
        {"return \"a\xC3\xA9\xF0\x9F\x98\x80\"[2]", "\xF0\x9F\x98\x80"},
        {"return \"e\xCC\x81\"[1]", "\xCC\x81"},
        {"return (\"nomad\".toUpper)[0]", "N"},
        {"return \"Nomad\"[1 + 2].toUpper[0]", "A"},
        {"fun name\nreturn \"Nomad\"\nend\nreturn (name)[4]", "d"},
        {"return $\"letter={\"Nomad\"[1]}\"", "letter=o"},
    }) {
        BOOST_TEST_CONTEXT(source) {
            Runtime runtime;
            const auto id = compileFunction(runtime, source);
            const auto before = NomadObject::getLiveObjectCount();
            RuntimeValue result;
            runtime.executeFunction(id, {}, result);
            BOOST_TEST(result.getStringObject()->getString() == expected);
            runtime.getStringType()->freeValue(result);
            BOOST_TEST(NomadObject::getLiveObjectCount() == before);
        }
    }
}

BOOST_AUTO_TEST_CASE(string_indexer_faults_and_interpreter_recovers) {
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);
    const auto before = NomadObject::getLiveObjectCount();
    for (const auto* source : {"\"\"[0]", "\"abc\"[-1]", "\"abc\"[3]"}) {
        BOOST_TEST(!Interpreter{}.execute(source, context));
        BOOST_REQUIRE(context.getError() != nullptr);
        BOOST_TEST(context.getError()->find("out of bounds for size") != NomadString::npos);
        BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    }
    runtime.registerNativeFunction(
        "invalidText", [](VirtualMachine* vm) { vm->setStringResult("\xFF"); }, {}, runtime.getStringType(), ""
    );
    BOOST_TEST(!Interpreter{}.execute("(invalidText)[0]", context));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(context.getError()->find("UTF-8") != NomadString::npos);
    BOOST_REQUIRE(Interpreter{}.evaluateExpression("\"hello\"[1].length", context));
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 1);
}

BOOST_AUTO_TEST_CASE(write_only_indexer_supports_assignment_but_not_reads) {
    Runtime runtime;
    const auto* type = registerTestObject(runtime);
    runtime.registerNativeIndexer1(
        type, runtime.getIntegerType(), runtime.getIntegerType(), {},
        [](VirtualMachine* vm, const RuntimeValue& index, const RuntimeValue& value) {
            vm->getThisAs<TestObject>()->value = index.getIntegerValue() + value.getIntegerValue();
        }, ""
    );
    const auto id = compileFunction(runtime, "value = new TestObject\nvalue[2] = 5\nreturn value.value");
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    BOOST_TEST(result.getIntegerValue() == 7);
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);
    BOOST_REQUIRE(Interpreter{}.execute("value = new TestObject", context));
    BOOST_REQUIRE(Interpreter{}.execute("value[3] = 8", context));
    BOOST_REQUIRE(Interpreter{}.evaluateExpression("value.value", context));
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 11);
    BOOST_TEST(!Interpreter{}.evaluateExpression("value[0]", context));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(context.getError()->find("write-only") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(indexer_callbacks_release_operands_and_partial_results_on_fault) {
    for (const auto* source : {"return (new TestObject)[0]", "(new TestObject)[0] = \"assigned\""}) {
        Runtime runtime;
        const auto* type = registerTestObject(runtime);
        runtime.registerNativeIndexer1(
            type, runtime.getIntegerType(), runtime.getStringType(),
            [](VirtualMachine* vm, const RuntimeValue&, RuntimeValue& result) {
                result.setStringValue("partial");
                vm->fault("Indexer getter failed");
            },
            [](VirtualMachine* vm, const RuntimeValue&, const RuntimeValue&) {
                vm->fault("Indexer setter failed");
            }, ""
        );
        const auto id = compileFunction(runtime, source);
        auto machine = runtime.createVirtualMachine();
        const auto before = NomadObject::getLiveObjectCount();
        for (auto attempt = 0; attempt < 2; ++attempt) {
            BOOST_CHECK_THROW(machine->run(runtime.getScriptFunction(id), {}), VirtualMachineException);
            BOOST_TEST(NomadObject::getLiveObjectCount() == before);
            BOOST_CHECK_THROW(machine->getThis(), VirtualMachineException);
        }
    }
}

BOOST_AUTO_TEST_CASE(indexers_support_object_valued_keys_and_literal_delimiters) {
    Runtime runtime;
    const auto* type = registerTestObject(runtime);
    runtime.registerNativeIndexer1(
        type, runtime.getStringType(), runtime.getIntegerType(),
        [](VirtualMachine* vm, const RuntimeValue& index, RuntimeValue& result) {
            BOOST_TEST(index.getStringObject()->getLength() == 1);
            result.setIntegerValue(vm->getThisAs<TestObject>()->value);
        },
        [](VirtualMachine* vm, const RuntimeValue& index, const RuntimeValue& value) {
            BOOST_TEST(index.getStringObject()->getString() == "]");
            vm->getThisAs<TestObject>()->value = value.getIntegerValue();
        }, ""
    );
    const auto id = compileFunction(runtime, "value = new TestObject\nvalue[\"]\"] = 9\nreturn value[\"[\"]");
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    BOOST_TEST(result.getIntegerValue() == 9);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        BOOST_REQUIRE(Interpreter{}.execute("value = new TestObject", context));
        BOOST_REQUIRE(Interpreter{}.execute("value[\"]\"] = 10", context));
        BOOST_REQUIRE(Interpreter{}.execute("value[\"=\"]", context));
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 10);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(two_dimensional_indexers_evaluate_once_in_order_with_distinct_key_types) {
    Runtime runtime;
    const auto* type = registerTestObject(runtime);
    NomadString calls;
    runtime.registerNativeIndexer2(
        type, runtime.getStringType(), runtime.getIntegerType(), runtime.getIntegerType(),
        [&calls](VirtualMachine* machine, const RuntimeValue& index1, const RuntimeValue& index2, RuntimeValue& result) {
            calls += "get";
            result.setIntegerValue(machine->getThisAs<TestObject>()->value +
                static_cast<NomadInteger>(index1.getStringObject()->getLength()) + index2.getIntegerValue());
        },
        [&calls](VirtualMachine* machine, const RuntimeValue& index1, const RuntimeValue& index2, const RuntimeValue& value) {
            calls += "set";
            machine->getThisAs<TestObject>()->value = value.getIntegerValue() +
                static_cast<NomadInteger>(index1.getStringObject()->getLength()) + index2.getIntegerValue();
        }, ""
    );
    runtime.registerNativeFunction("receiver", [&calls](VirtualMachine* machine) {
        calls += "receiver";
        machine->getResult().setObjectValue(new TestObject);
    }, {}, type, "");
    runtime.registerNativeFunction("first", [&calls](VirtualMachine* machine) {
        calls += "first";
        machine->setStringResult("key");
    }, {}, runtime.getStringType(), "");
    runtime.registerNativeFunction("second", [&calls](VirtualMachine* machine) {
        calls += "second";
        machine->setIntegerResult(2);
    }, {}, runtime.getIntegerType(), "");
    runtime.registerNativeFunction("assigned", [&calls](VirtualMachine* machine) {
        calls += "assigned";
        machine->setIntegerResult(7);
    }, {}, runtime.getIntegerType(), "");
    const auto readId = compileFunction(runtime, "return (receiver)[(first) (second)]");
    auto compiler = runtime.createCompiler();
    CompilerContext compilation(compiler.get());
    compiler->registerSources(&compilation, {{{"write", {"write.nomad",
        "(receiver)[(first) (second)] = assigned"
    }}}, {}});
    BOOST_REQUIRE(compiler->compileFunctions(&compilation));
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(readId, {}, result);
    BOOST_TEST(result.getIntegerValue() == 9);
    BOOST_TEST(calls == "receiverfirstsecondget");
    calls.clear();
    runtime.executeFunction(runtime.getFunctionId("write"), {}, result);
    BOOST_TEST(calls == "receiverfirstsecondassignedset");
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);
    calls.clear();
    BOOST_REQUIRE(Interpreter{}.evaluateExpression("(receiver)[(first) (second)]", context));
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 9);
    BOOST_TEST(calls == "receiverfirstsecondget");
    calls.clear();
    BOOST_REQUIRE(Interpreter{}.execute("(receiver)[(first) (second)] = assigned", context));
    BOOST_TEST(calls == "receiverfirstsecondassignedset");
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(two_dimensional_access_permissions_are_checked_in_both_execution_modes) {
    for (const auto readOnly : {false, true}) {
        Runtime runtime;
        const auto* type = registerTestObject(runtime);
        const NativeIndexerGetFn2 getter = [](VirtualMachine* machine, const RuntimeValue& first,
            const RuntimeValue& second, RuntimeValue& result) {
            result.setIntegerValue(machine->getThisAs<TestObject>()->value + first.getIntegerValue() + second.getIntegerValue());
        };
        const NativeIndexerSetFn2 setter = [](VirtualMachine* machine, const RuntimeValue& first,
            const RuntimeValue& second, const RuntimeValue& value) {
            machine->getThisAs<TestObject>()->value = first.getIntegerValue() + second.getIntegerValue() + value.getIntegerValue();
        };
        runtime.registerNativeIndexer2(type, runtime.getIntegerType(), runtime.getIntegerType(), runtime.getIntegerType(),
            readOnly ? getter : NativeIndexerGetFn2{}, readOnly ? NativeIndexerSetFn2{} : setter, "");
        const auto id = compileFunction(runtime, readOnly
            ? "object = new TestObject\nreturn object[1 2]"
            : "object = new TestObject\nobject[1 2] = 7\nreturn object.value");
        RuntimeValue result;
        runtime.executeFunction(id, {}, result);
        BOOST_TEST(result.getIntegerValue() == (readOnly ? 7 : 10));
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        BOOST_REQUIRE(Interpreter{}.execute("object = new TestObject", context));
        BOOST_REQUIRE(Interpreter{}.execute(readOnly ? "object[1 2]" : "object[1 2] = 7", context));
        const auto* rejected = readOnly ? "object[0 0] = 1" : "object[0 0]";
        BOOST_TEST(!Interpreter{}.execute(rejected, context));
        BOOST_REQUIRE(context.getError() != nullptr);
        BOOST_TEST(context.getError()->find(readOnly ? "read-only" : "write-only") != NomadString::npos);
        auto compiler = runtime.createCompiler();
        CompilerContext compilation(compiler.get());
        compiler->registerSources(&compilation, {{{"rejected", {"rejected.nomad",
            NomadString("object = new TestObject\n") + rejected
        }}}, {}});
        BOOST_TEST(!compiler->compileFunctions(&compilation));
        BOOST_REQUIRE(compilation.getFirstError() != nullptr);
        BOOST_TEST(compilation.getFirstError()->message.find(readOnly ? "read-only" : "write-only") != NomadString::npos);
    }
}

BOOST_AUTO_TEST_CASE(two_dimensional_interpreter_stops_after_a_failed_index_without_leaking_operands) {
    Runtime runtime;
    const auto* type = registerTestObject(runtime);
    NomadString calls;
    runtime.registerNativeIndexer2(type, runtime.getStringType(), runtime.getStringType(), runtime.getIntegerType(),
        [&calls](VirtualMachine*, const RuntimeValue&, const RuntimeValue&, RuntimeValue& result) {
            calls += "get";
            result.setIntegerValue(1);
        },
        [&calls](VirtualMachine*, const RuntimeValue&, const RuntimeValue&, const RuntimeValue&) {
            calls += "set";
        }, "");
    runtime.registerNativeFunction("first", [&calls](VirtualMachine* machine) {
        calls += "first";
        machine->setStringResult("first");
    }, {}, runtime.getStringType(), "");
    runtime.registerNativeFunction("second", [&calls](VirtualMachine* machine) {
        calls += "second";
        machine->fault("Second index failed");
    }, {}, runtime.getStringType(), "");
    runtime.registerNativeFunction("assigned", [&calls](VirtualMachine* machine) {
        calls += "assigned";
        machine->setIntegerResult(2);
    }, {}, runtime.getIntegerType(), "");
    RuntimeValue empty;
    runtime.getStringType()->initValue(empty);
    runtime.getStringType()->freeValue(empty);
    const auto before = NomadObject::getLiveObjectCount();
    {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        for (const auto* source : {
            "(new TestObject)[(first) (second)]", "(new TestObject)[(first) (second)] = assigned"
        }) {
            calls.clear();
            BOOST_TEST(!Interpreter{}.execute(source, context));
            BOOST_REQUIRE(context.getError() != nullptr);
            BOOST_TEST(context.getError()->find("Second index failed") != NomadString::npos);
            BOOST_TEST(calls == "firstsecond");
            BOOST_TEST(NomadObject::getLiveObjectCount() == before);
        }
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(two_dimensional_indexer_faults_release_object_keys_receiver_and_partial_results) {
    for (const auto* source : {
        "return (new TestObject)[\"first\" \"second\"]",
        "(new TestObject)[\"first\" \"second\"] = \"assigned\""
    }) {
        Runtime runtime;
        const auto* type = registerTestObject(runtime);
        runtime.registerNativeIndexer2(type, runtime.getStringType(), runtime.getStringType(), runtime.getStringType(),
            [](VirtualMachine* machine, const RuntimeValue&, const RuntimeValue&, RuntimeValue& result) {
                result.setStringValue("partial");
                machine->fault("Getter failed");
            },
            [](VirtualMachine* machine, const RuntimeValue&, const RuntimeValue&, const RuntimeValue&) {
                machine->fault("Setter failed");
            }, "");
        const auto id = compileFunction(runtime, source);
        auto machine = runtime.createVirtualMachine();
        const auto before = NomadObject::getLiveObjectCount();
        for (auto attempt = 0; attempt < 2; ++attempt) {
            BOOST_CHECK_THROW(machine->run(runtime.getScriptFunction(id), {}), VirtualMachineException);
            BOOST_TEST(NomadObject::getLiveObjectCount() == before);
            BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
        }
        {
            InterpreterContext context(*machine);
            const auto* expression = NomadString(source).starts_with("return ") ? source + 7 : source;
            BOOST_TEST(!Interpreter{}.execute(expression, context));
            BOOST_REQUIRE(context.getError() != nullptr);
            BOOST_TEST(context.getError()->find("failed") != NomadString::npos);
            BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
            BOOST_REQUIRE(Interpreter{}.evaluateExpression("1 + 2", context));
            BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 3);
        }
        BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    }
}

BOOST_AUTO_TEST_SUITE_END()
