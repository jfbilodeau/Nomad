// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/script/Closure.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptObject.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <utility>

using namespace nomad;

namespace {

class ConstructedObject final : public NomadObject {
};

const ObjectType* registerObjectType(Runtime& runtime, const NomadString& name) {
    const auto descriptorId = runtime.createObjectType<ObjectType>(name, ObjectTypeMode::Closed, std::vector<const Type*>{}, "A constructed object."
    );
    BOOST_REQUIRE(descriptorId.isValid());
    const auto* type = runtime.getType(descriptorId).value()->asObjectType();
    return type;
}

CallableId compileConstruction(Runtime& runtime, const NomadString& source, const NomadString& name = "main") {
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{name, {name + ".nomad", source}}}, {}});
    const auto compiled = compiler->compileFunctions(&context);
    BOOST_REQUIRE_MESSAGE(compiled, (context.getFirstError() ? formatDiagnostic(*context.getFirstError()) : "Compilation failed"));
    return runtime.getFunctionId(name);
}

std::vector<NomadString> callSequence(const Runtime& runtime, const ScriptCallableBase* function) {
    std::vector<NomadString> sequence;
    for (auto index = function->getFunctionStart(); index < function->getFunctionEnd();) {
        const auto id = runtime.getInstructionId(runtime.getInstructions()[index].fn);
        BOOST_REQUIRE(id.isValid());
        const auto& name = runtime.getInstructionName(id);
        if (name == OpCodes::op_call_native_function) {
            sequence.push_back(OpCodes::op_call_script_function);
        } else if (name == OpCodes::op_call_native_method) {
            sequence.push_back(OpCodes::op_call_script_method);
        } else if (name == OpCodes::op_call_native_constructor) {
            sequence.push_back(OpCodes::op_call_script_constructor);
        } else {
            sequence.push_back(name);
        }
        index += 1 + runtime.getInstructionOperands(id).size();
        BOOST_REQUIRE(index <= function->getFunctionEnd());
    }
    return sequence;
}

} // namespace

BOOST_AUTO_TEST_SUITE(object_construction)

BOOST_AUTO_TEST_CASE(call_opcodes_have_specialized_operands) {
    Runtime runtime;
    for (const auto& name : {OpCodes::op_call_script_function, OpCodes::op_call_script_method}) {
        const auto id = runtime.getInstructionId(name);
        BOOST_REQUIRE(id.isValid());
        const auto operands = runtime.getInstructionOperands(id);
        BOOST_REQUIRE(operands.size() == 1u);
        BOOST_CHECK(operands[0] == Operand::Function);
    }
    const auto constructorId = runtime.getInstructionId(OpCodes::op_call_script_constructor);
    BOOST_REQUIRE(constructorId.isValid());
    const auto operands = runtime.getInstructionOperands(constructorId);
    BOOST_REQUIRE(operands.size() == 2u);
    BOOST_CHECK(operands[0] == Operand::Function);
    BOOST_CHECK(operands[1] == Operand::Id);
    for (const auto& name : {
        OpCodes::op_call_native_function, OpCodes::op_call_native_method, OpCodes::op_call_native_constructor
    }) {
        const auto id = runtime.getInstructionId(name);
        BOOST_REQUIRE(id.isValid());
        const auto nativeOperands = runtime.getInstructionOperands(id);
        BOOST_REQUIRE(nativeOperands.size() == 2u);
        BOOST_CHECK(nativeOperands[1] == Operand::Index);
    }
}

BOOST_AUTO_TEST_CASE(native_and_script_calls_share_argument_and_construction_bytecode) {
    Runtime runtime;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "nativeEcho", [](VirtualMachine* machine) {
            BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
            BOOST_TEST(machine->peekStack(0).getObjectValue() == machine->getStringParameter(0));
            machine->getResult().copyObjectValue(machine->getParameter(0));
        }, {defParameter("text", runtime.getStringType(), "")}, runtime.getStringType(), ""
    ).isValid());
    const auto* nativeType = registerObjectType(runtime, "NativeOwner");
    BOOST_REQUIRE(runtime.addConstructor(nativeType->getId(), {
        .parameters = {{runtime.getStringType()->getId(), "text", ""}},
        .function = [](VirtualMachine* machine) {
            BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
            BOOST_TEST(NomadString(machine->peekStack(0).getStringValue()) == "value");
            machine->getResult().setObjectValue(new ConstructedObject);
        }
    }).isValid());
    compileConstruction(runtime,
        "class ScriptOwner\ninit text:string\nthis.text = text\nend\nend\n"
        "fun scriptEcho text:string\nreturn text\nend\n"
        "fun callScript\nreturn scriptEcho \"value\"\nend\n"
        "fun callNative\nreturn nativeEcho \"value\"\nend\n"
        "fun newScript\nreturn new ScriptOwner \"value\"\nend\n"
        "fun newNative\nreturn new NativeOwner \"value\"\nend"
    );
    for (const auto& names : {
        std::pair{"callScript", "callNative"}, std::pair{"newScript", "newNative"}
    }) {
        const auto* script = runtime.getScriptFunction(runtime.getFunctionId(names.first));
        const auto* native = runtime.getScriptFunction(runtime.getFunctionId(names.second));
        BOOST_TEST(callSequence(runtime, script) == callSequence(runtime, native), boost::test_tools::per_element());
    }
    const auto before = NomadObject::getLiveObjectCount();
    for (const auto* name : {"callScript", "callNative", "newScript", "newNative"}) {
        RuntimeValue result;
        runtime.executeFunction(runtime.getFunctionId(name), {}, result);
        BOOST_REQUIRE(result.getObjectValue() != nullptr);
        result.freeObjectValue();
        BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    }
    for (const auto* name : {"op_call_function", "op_call_method", "op_native_method_call"}) {
        BOOST_TEST(runtime.getInstructionId(name).isInvalid());
    }
}

BOOST_AUTO_TEST_CASE(script_construction_bytecode_uses_linked_calls_and_ordinary_returns) {
    Runtime runtime;
    const auto id = compileConstruction(runtime,
        "hub model\nclass Point\nvalue = 10\n"
        "init value:int\nthis.value = value\nend\nend\nend\n"
        "return new model.Point 42"
    );
    const auto* type = runtime.getTypeByName("model.Point")->asObjectType();
    BOOST_TEST(type->getInitializerName() == "model.Point$init");
    const auto* constructor = type->getConstructors()[0]->asScriptCallable();
    BOOST_REQUIRE(constructor != nullptr);
    BOOST_TEST(constructor->getReturnType() == runtime.getVoidType());
    const auto start = constructor->getFunctionStart();
    const auto& instructions = runtime.getInstructions();
    BOOST_TEST(runtime.getInstructionId("op_function_enter").isInvalid());
    BOOST_TEST(constructor->getVariableId("this").isInvalid());
    BOOST_TEST(constructor->getVariableId("value").isInvalid());
    BOOST_TEST(instructions[start].fn ==
        runtime.getInstructionFn(runtime.getInstructionId(OpCodes::op_push_this)));
    BOOST_TEST(instructions[start + 1].fn ==
        runtime.getInstructionFn(runtime.getInstructionId(OpCodes::op_call_script_method)));
    const auto* initializer = type->getMethods()[0]->asScriptCallable();
    BOOST_REQUIRE(initializer != nullptr);
    BOOST_TEST(initializer->getName() == type->getInitializerName());
    BOOST_TEST(initializer->getObjectType() == type);
    BOOST_TEST(instructions[start + 2].value.getIndexValue() == initializer->getFunctionStart());
    BOOST_TEST(constructor->getVariableCount() == 0u);
    BOOST_TEST(instructions[constructor->getFunctionEnd() - 1].fn ==
        runtime.getInstructionFn(runtime.getInstructionId(OpCodes::op_return)));
    const auto* main = runtime.getScriptFunction(id);
    auto foundConstructorCall = false;
    const auto callInstruction = runtime.getInstructionFn(runtime.getInstructionId(OpCodes::op_call_script_constructor));
    for (auto index = main->getFunctionStart(); index < main->getFunctionEnd(); ++index) {
        if (instructions[index].fn == callInstruction) {
            BOOST_TEST(instructions[index + 1].value.getIndexValue() == constructor->getFunctionStart());
            BOOST_TEST(instructions[index + 2].value.getIdValue() == type->getId().id);
            BOOST_TEST(instructions[index + 3].fn ==
                runtime.getInstructionFn(runtime.getInstructionId(OpCodes::op_pop_1)));
            BOOST_TEST(instructions[index + 4].fn ==
                runtime.getInstructionFn(runtime.getInstructionId(OpCodes::op_move_this_to_r)));
            BOOST_TEST(instructions[index + 5].fn ==
                runtime.getInstructionFn(runtime.getInstructionId(OpCodes::op_pop_this)));
            foundConstructorCall = true;
        }
    }
    BOOST_TEST(foundConstructorCall);
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    auto* object = dynamic_cast<ScriptObject*>(result.getObjectValue());
    BOOST_REQUIRE(object != nullptr);
    BOOST_TEST(object->getField(0).getIntegerValue() == 42);
    BOOST_TEST(object->getReferenceCount() == 1u);
    result.freeObjectValue();
}

BOOST_AUTO_TEST_CASE(script_returns_select_compact_opcodes_and_release_owned_locals_once) {
    const auto returnNames = std::vector<NomadString>{
        OpCodes::op_return, OpCodes::op_return_1, OpCodes::op_return_2,
        OpCodes::op_return_3, OpCodes::op_return_n
    };
    for (auto localCount = NomadIndex{0}; localCount < returnNames.size(); ++localCount) {
        BOOST_TEST_CONTEXT("Local count: " << localCount) {
            Runtime runtime;
            const auto* type = registerObjectType(runtime, "LocalObject");
            RuntimeValue owner;
            owner.setObjectValue(new ConstructedObject());
            BOOST_REQUIRE(runtime.registerNativeFunction(
                "makeLocal", [&owner](VirtualMachine* machine) {
                    owner.getObjectValue()->addRef();
                    machine->setResult(owner);
                }, {}, type, ""
            ).isValid());
            auto source = NomadString{"fun target\n"};
            for (auto index = NomadIndex{0}; index < localCount; ++index) {
                source += "local" + std::to_string(index) + " = makeLocal\n";
            }
            source += "return 23\nend\nreturn target";
            const auto id = compileConstruction(runtime, source);
            const auto* target = runtime.getScriptFunction(runtime.getFunctionId("target"));
            BOOST_REQUIRE(target != nullptr);
            BOOST_TEST(target->getVariableCount() == localCount);
            const auto instructionId = runtime.getInstructionId(returnNames[localCount]);
            const auto returnIndex = target->getFunctionEnd() - (localCount < 4 ? 1 : 2);
            BOOST_TEST(runtime.getInstructions()[returnIndex].fn == runtime.getInstructionFn(instructionId));
            BOOST_TEST(runtime.getInstructionOperands(instructionId).size() == (localCount < 4 ? 0u : 1u));
            if (localCount == 4) {
                BOOST_TEST(runtime.getInstructions()[returnIndex + 1].value.getIndexValue() == localCount);
            }
            const auto before = NomadObject::getLiveObjectCount();
            RuntimeValue result;
            for (auto attempt = 0; attempt < 2; ++attempt) {
                runtime.executeFunction(id, {}, result);
                BOOST_TEST(result.getIntegerValue() == 23);
                BOOST_TEST(owner.getObjectValue()->getReferenceCount() == 1u);
                BOOST_TEST(NomadObject::getLiveObjectCount() == before);
            }
            owner.freeObjectValue();
        }
    }
}

BOOST_AUTO_TEST_CASE(nested_script_calls_restore_parameter_local_and_receiver_frames) {
    Runtime runtime;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "checkParameter", [](VirtualMachine* machine) {
            BOOST_TEST(machine->getIntegerEnclosingParameter(0) == machine->getIntegerParameter(0));
        }, {defParameter("expected", runtime.getIntegerType(), "")}, runtime.getVoidType(), ""
    ).isValid());
    const auto id = compileConstruction(runtime,
        "fun leaf value:int\n"
        "name = \"leaf\"\nassert name == \"leaf\"\ncheckParameter value\nreturn value + 1\nend\n"
        "fun middle value:int\n"
        "saved = value + 10\nreturned = leaf (value + 1)\n"
        "checkParameter value\nassert saved == value + 10\nreturn returned + saved\nend\n"
        "class Child\ninit value:int\n"
        "this.value = middle value\ncheckParameter value\nthis.after = value\nend\nend\n"
        "class Owner\ninit value:int\n"
        "this.before = value\nsaved = middle value\nchild = new Child (value + 1)\n"
        "this.child = child\nthis.value = saved + middle value\n"
        "checkParameter value\nthis.after = value\nend\nend\n"
        "owner = new Owner 20\n"
        "assert owner.before == 20\nassert owner.after == 20\n"
        "assert owner.child.after == 21\nreturn owner.value + owner.child.value"
    );
    const auto before = NomadObject::getLiveObjectCount();
    auto machine = runtime.createVirtualMachine();
    for (auto attempt = 0; attempt < 2; ++attempt) {
        machine->run(runtime.getScriptFunction(id), {});
        BOOST_TEST(machine->getIntegerResult() == 158);
        BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
        BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    }
}

BOOST_AUTO_TEST_CASE(nested_script_constructor_failure_unwinds_call_frames_and_temporary_arguments) {
    Runtime runtime;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "fail", [](VirtualMachine*) { throw NomadException("Nested initializer failed"); },
        {}, runtime.getIntegerType(), ""
    ).isValid());
    const auto id = compileConstruction(runtime,
        "class Broken\nvalue = fail\ninit name:string\nthis.name = name\nend\nend\n"
        "class Owner\nname = \"owner\"\ninit name:string\nthis.name = name\n"
        "child = new Broken (name.toUpper)\nend\nend\n"
        "fun construct name:string\nreturn new Owner name\nend\n"
        "return construct (\"temporary\".toUpper)"
    );
    const auto before = NomadObject::getLiveObjectCount();
    auto machine = runtime.createVirtualMachine();
    for (auto attempt = 0; attempt < 2; ++attempt) {
        BOOST_CHECK_THROW(machine->run(runtime.getScriptFunction(id), {}), NomadException);
        BOOST_TEST(NomadObject::getLiveObjectCount() == before);
        BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
    }
    {
        InterpreterContext context(*machine);
        BOOST_TEST(!Interpreter{}.execute("new Owner (\"temporary\".toUpper)", context));
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(script_construction_orders_defaults_initializers_and_overloaded_bodies) {
    Runtime runtime;
    auto calls = 0;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "nextValue", [&](VirtualMachine* machine) { machine->setIntegerResult(++calls); },
        {}, runtime.getIntegerType(), ""
    ).isValid());
    const auto id = compileConstruction(runtime,
        "class Child\nvalue = 1\nend\n"
        "class Point\n"
        "value = nextValue\nchild = new Child\n"
        "init\nthis.value = this.value + 10\nend\n"
        "init value:int\nthis.value = value\nvalue = 99\nthis.child.value = 7\nend\n"
        "init enabled:bool\nif enabled\nthis.conditional = 42\nend\nreturn\nend\n"
        "end\n"
        "first = new Point\nsecond = new Point 25\nthird = new Point false\n"
        "assert first.value == 11\nassert second.value == 25\n"
        "assert third.value == 3\nassert third.conditional == 0\n"
        "assert first.child.value == 1\nassert second.child.value == 7\n"
        "alias = second\nalias.value = 50\nassert second.value == 50\n"
        "return first.child.value + second.child.value"
    );
    BOOST_TEST(calls == 0);
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    BOOST_TEST(result.getIntegerValue() == 8);
    BOOST_TEST(calls == 3);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(receiver_captures_survive_native_reentry_and_outlive_the_constructor) {
    Runtime runtime;
    std::unique_ptr<Closure> closure;
    const auto* callbackType = runtime.getCallbackType({}, runtime.getIntegerType());
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "saveCallback", [&](VirtualMachine* machine) {
            BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
            const auto callbackId = machine->getCallableIdParameter(0);
            machine->run(runtime.getScriptFunction(runtime.getFunctionId("helper")), {});
            closure = machine->createClosure(&runtime, callbackId);
        }, {defParameter("callback", callbackType, "")}, runtime.getVoidType(), ""
    ).isValid());
    const auto id = compileConstruction(runtime,
        "class Owner\nvalue = 17\ninit\nsaveCallback fun\nreturn this.value\nend\nend\nend\n"
        "fun helper\nreturn 5\nend\nowner = new Owner\nreturn 0"
    );
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    BOOST_REQUIRE(closure != nullptr);
    BOOST_TEST(closure->getCaptureCount() == 1u);
    BOOST_TEST(closure->getCaptureValue(0).getObjectValue()->getReferenceCount() == 1u);
    runtime.executeFunction(closure.get(), {}, result);
    BOOST_TEST(result.getIntegerValue() == 17);
    closure.reset();
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(native_methods_capture_the_enclosing_receiver_not_the_bound_native_receiver) {
    Runtime runtime;
    std::unique_ptr<Closure> closure;
    const auto* callbackType = runtime.getCallbackType({}, runtime.getIntegerType());
    const auto* sinkType = registerObjectType(runtime, "Sink");
    BOOST_REQUIRE(runtime.addConstructor(sinkType->getId(), {
        .parameters = {},
        .function = [](VirtualMachine* machine) { machine->getResult().setObjectValue(new ConstructedObject); }
    }).isValid());
    BOOST_REQUIRE(runtime.addMethod(sinkType->getId(), {
        .name = "save",
        .parameters = {{callbackType->getId(), "callback", ""}},
        .returnTypeId = runtime.getVoidType()->getId(),
        .function = [&](VirtualMachine* machine) {
            auto* nativeReceiver = machine->getThisAs<ConstructedObject>();
            BOOST_REQUIRE(nativeReceiver != nullptr);
            const auto callbackId = machine->getCallableIdParameter(0);
            BOOST_TEST(machine->peekStack(0).getIdValue() == callbackId.id);
            machine->run(runtime.getScriptFunction(runtime.getFunctionId("helper")), {});
            BOOST_TEST(machine->getThis() == nativeReceiver);
            closure = machine->createClosure(&runtime, callbackId);
        }
    }).isValid());
    const auto id = compileConstruction(runtime,
        "class Owner\nvalue = 29\ninit\nsink = new Sink\n"
        "sink.save fun\nreturn this.value\nend\nend\nend\n"
        "fun helper\nreturn 5\nend\nowner = new Owner\nreturn 0"
    );
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    BOOST_REQUIRE(closure != nullptr);
    BOOST_TEST(closure->getCaptureValue(0).asObject<ScriptObject>()->getObjectType()->getTypeName() == "Owner");
    BOOST_TEST(closure->getCaptureValue(0).getObjectValue()->getReferenceCount() == 1u);
    runtime.executeFunction(closure.get(), {}, result);
    BOOST_TEST(result.getIntegerValue() == 29);
    closure.reset();
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(host_arguments_remain_borrowed_while_script_parameter_slots_are_reassigned) {
    Runtime runtime;
    compileConstruction(runtime,
        "class Item\nvalue = 7\nend\n"
        "fun mutate number:int text:string item:Item\n"
        "number = number + 1\ntext = text.toUpper\nitem = new Item\nreturn text\nend"
    );
    const auto* function = runtime.getScriptFunction(runtime.getFunctionId("mutate"));
    BOOST_TEST(function->getParameterCount() == 3u);
    BOOST_TEST(function->getVariableCount() == 0u);
    const auto* type = runtime.getTypeByName("Item")->asObjectType();
    auto machine = runtime.createVirtualMachine();
    RuntimeValue item;
    item.setObjectValue(machine->constructObject(type->getConstructors()[0]->getId(), {}));
    RuntimeValue text;
    text.setStringValue("original");
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(function->getId(), {RuntimeValue{NomadInteger{5}}, text, item}, result);
    BOOST_TEST(NomadString(text.getStringValue()) == "original");
    BOOST_TEST(text.getObjectValue()->getReferenceCount() == 1u);
    BOOST_TEST(item.getObjectValue()->getReferenceCount() == 1u);
    BOOST_TEST(item.asObject<ScriptObject>()->getField(0).getIntegerValue() == 7);
    BOOST_TEST(NomadString(result.getStringValue()) == "ORIGINAL");
    result.freeObjectValue();
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    text.freeObjectValue();
    item.freeObjectValue();
}

BOOST_AUTO_TEST_CASE(script_construction_returns_the_instance_after_early_return_and_retains_arguments) {
    Runtime runtime;
    const auto id = compileConstruction(runtime,
        "class Label\n"
        "name = \"default\"\n"
        "init name:string\nthis.name = name\nif name == \"supplied\"\nreturn\nend\nthis.name = \"other\"\nend\n"
        "end\nreturn new Label \"supplied\""
    );
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    auto* object = dynamic_cast<ScriptObject*>(result.getObjectValue());
    BOOST_REQUIRE(object != nullptr);
    BOOST_TEST(object->getReferenceCount() == 1u);
    BOOST_TEST(NomadString(object->getField(0).getStringValue()) == "supplied");
    result.freeObjectValue();
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(interpreter_constructs_compiled_classes_and_uses_shared_overload_resolution) {
    Runtime runtime;
    static_cast<void>(compileConstruction(runtime,
        "class Point\nvalue = 1\n"
        "init value:int\nthis.value = value\nend\n"
        "init value:float\nthis.value = 20\nend\nend\n"
        "class Empty\nend"
    ));
    const auto before = NomadObject::getLiveObjectCount();
    {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        Interpreter interpreter;
        BOOST_REQUIRE(interpreter.execute("point = new Point 7", context));
        BOOST_REQUIRE(interpreter.execute("point.value", context));
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 7);
        BOOST_REQUIRE(interpreter.execute("point = new Point 1.0", context));
        BOOST_REQUIRE(interpreter.execute("point.value", context));
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 20);
        BOOST_TEST(!interpreter.execute("new Point", context));
        BOOST_TEST(!interpreter.execute("new Point true", context));
        BOOST_REQUIRE(interpreter.execute("new Empty", context));
        BOOST_REQUIRE(context.getResult() != nullptr);
        BOOST_TEST(context.getResult()->getType() == runtime.getTypeByName("Empty"));
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(void_constructor_exit_transfers_instance_and_releases_nested_locals) {
    Runtime runtime;
    const auto id = compileConstruction(runtime,
        "class Child\nvalue = 5\nend\n"
        "class Owner\nvalue = 1\ninit early:bool\n"
        "child = new Child\nthis.child = child\n"
        "if early\nreturn\nend\nthis.value = 77\nend\nend\n"
        "first = new Owner true\nsecond = new Owner false\n"
        "assert first.value == 1\nassert second.value == 77\n"
        "assert first.child.value == 5\nassert second.child.value == 5\n"
        "return second"
    );
    const auto* type = runtime.getTypeByName("Owner")->asObjectType();
    const auto* constructor = type->getConstructors()[0];
    BOOST_TEST(constructor->getReturnType() == runtime.getVoidType());
    BOOST_TEST(constructor->getConstructedType() == type);
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    BOOST_REQUIRE(result.getObjectValue() != nullptr);
    BOOST_TEST(result.getObjectValue()->getReferenceCount() == 1u);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before + 2);
    result.freeObjectValue();
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);

    auto machine = runtime.createVirtualMachine();
    for (const auto early : {true, false}) {
        machine->setIntegerResult(41);
        machine->setIntegerIntermediate(42);
        auto* object = machine->constructObject(constructor->getId(), {RuntimeValue{early}});
        BOOST_REQUIRE(object != nullptr);
        BOOST_TEST(object->getReferenceCount() == 1u);
        BOOST_TEST(NomadObject::getLiveObjectCount() == before + 2);
        BOOST_TEST(machine->getResult().getIntegerValue() == 41);
        BOOST_TEST(machine->getIntermediate().getIntegerValue() == 42);
        BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
        object->releaseRef();
        BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    }
}

BOOST_AUTO_TEST_CASE(constructor_arguments_use_the_outer_receiver_before_allocating_nested_instances) {
    Runtime runtime;
    const auto id = compileConstruction(runtime,
        "class Child\ninit value:int\nthis.value = value\nend\nend\n"
        "class Owner\nvalue = 42\n"
        "init\n"
        "this.child = new Child this.value\n"
        "other = new Child this.child.value\n"
        "this.value = this.value + other.value\n"
        "end\nend\n"
        "owner = new Owner\nreturn owner.value + owner.child.value"
    );
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    BOOST_TEST(result.getIntegerValue() == 126);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        BOOST_REQUIRE(Interpreter{}.execute("owner = new Owner", context));
        BOOST_REQUIRE(Interpreter{}.execute("owner.value + owner.child.value", context));
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 126);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(script_constructor_failure_releases_instance_fields_and_arguments_and_recovers) {
    Runtime runtime;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "fail", [](VirtualMachine*) { throw NomadException("Constructor failed"); },
        {}, runtime.getVoidType(), ""
    ).isValid());
    const auto id = compileConstruction(runtime,
        "class Child\nend\n"
        "class Owner\nchild = new Child\n"
        "init name:string enabled:bool\nthis.name = name\nif enabled\nfail\nend\nend\n"
        "end\nreturn new Owner \"owned\" true"
    );
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    BOOST_CHECK_THROW(runtime.executeFunction(id, {}, result), NomadException);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        BOOST_TEST(!Interpreter{}.execute("new Owner \"failed\" true", context));
        BOOST_REQUIRE(Interpreter{}.execute("new Owner \"success\" false", context));
        const auto* object = dynamic_cast<ScriptObject*>(context.getResult()->getValue().getObjectValue());
        BOOST_REQUIRE(object != nullptr);
        BOOST_TEST(object->getReferenceCount() == 1u);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(script_constructor_defaults_are_synthesized_only_without_source_init) {
    for (const auto* source : {
        "class Point\ninit value:int\nend\nend\nreturn new Point",
        "class Point\ninit value:int\nend\nend\nreturn new Point true"
    }) {
        Runtime runtime;
        auto compiler = runtime.createCompiler();
        CompilerContext context(compiler.get());
        compiler->registerSources(&context, {{{"main", {"main.nomad", source}}}, {}});
        BOOST_TEST(!compiler->compileFunctions(&context));
        BOOST_TEST(context.hasError());
        const auto* type = runtime.getTypeByName("Point")->asObjectType();
        BOOST_REQUIRE(type != nullptr);
        BOOST_REQUIRE(type->getConstructors().size() == 1u);
        BOOST_TEST(type->getConstructors()[0]->getParameters().size() == 1u);
    }
    Runtime runtime;
    const auto id = compileConstruction(runtime, "class Empty\nend\nreturn new Empty");
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    BOOST_REQUIRE(result.getObjectValue() != nullptr);
    BOOST_TEST(result.getObjectValue()->getReferenceCount() == 1u);
    result.freeObjectValue();
}

BOOST_AUTO_TEST_CASE(script_construction_resolves_hub_types_across_files_in_either_order) {
    for (const auto reversed : {false, true}) {
        Runtime runtime;
        auto compiler = runtime.createCompiler();
        CompilerContext context(compiler.get());
        compiler->registerSources(&context, {{
            {reversed ? "a" : "z", {"types.nomad",
                "hub model\nclass Value\nvalue = 42\nend\n"
                "class Box\ninit value:model.Value\nthis.value = value\nend\nend\nend"}},
            {reversed ? "z" : "a", {"use.nomad",
                "box = new model.Box (new model.Value)\nreturn box.value.value"}}
        }, {}});
        const auto compiled = compiler->compileFunctions(&context);
        BOOST_REQUIRE_MESSAGE(compiled,
            (context.getFirstError() ? formatDiagnostic(*context.getFirstError()) : "Compilation failed"));
        const auto before = NomadObject::getLiveObjectCount();
        RuntimeValue result;
        runtime.executeFunction(runtime.getFunctionId(reversed ? "z" : "a"), {}, result);
        BOOST_TEST(result.getIntegerValue() == 42);
        BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    }
}

BOOST_AUTO_TEST_CASE(add_constructor_resolves_ids_and_owns_metadata_and_callback) {
    Runtime runtime;
    const auto* type = registerObjectType(runtime, "DefinitionObject");
    auto callbackCalls = 0;
    NomadInteger argument = 0;
    NativeConstructorDefinition definition{
        .parameters = {{runtime.getIntegerType()->getId(), "value", "Input value."}},
        .function = [&](VirtualMachine* machine) {
            ++callbackCalls;
            argument = machine->getIntegerParameter(0);
            machine->getResult().setObjectValue(new ConstructedObject);
        },
        .documentation = "Creates a definition object."
    };
    const auto id = runtime.addConstructor(type->getId(), definition);
    BOOST_REQUIRE(id.isValid());
    definition.parameters.clear();
    definition.function = {};
    definition.documentation = "Changed.";

    const auto* constructor = runtime.getNativeConstructor(id);
    BOOST_REQUIRE(constructor != nullptr);
    BOOST_TEST(constructor->getId() == id);
    BOOST_TEST(constructor->getName() == "DefinitionObject");
    BOOST_TEST(constructor->getObjectType() == type);
    BOOST_TEST(constructor->getReturnType() == type);
    BOOST_TEST(constructor->getDoc() == "Creates a definition object.");
    BOOST_REQUIRE(constructor->getParameters().size() == 1u);
    BOOST_TEST(constructor->getParameters()[0].type == runtime.getIntegerType());
    BOOST_TEST(constructor->getParameters()[0].name == "value");
    BOOST_TEST(constructor->getParameters()[0].doc == "Input value.");
    BOOST_TEST(runtime.getConstructor(id) == constructor);
    BOOST_REQUIRE(type->getConstructors().size() == 1u);
    BOOST_TEST(type->getConstructors()[0] == constructor);
    BOOST_REQUIRE(runtime.getConstructors(type).size() == 1u);
    BOOST_TEST(runtime.getConstructors(type)[0] == constructor);
    BOOST_TEST(callbackCalls == 0);

    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);
    BOOST_REQUIRE(Interpreter{}.execute("new DefinitionObject 23", context));
    BOOST_TEST(callbackCalls == 1);
    BOOST_TEST(argument == 23);
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getType() == type);
    BOOST_TEST(context.getResult()->getValue().getObjectValue() != nullptr);
}

BOOST_AUTO_TEST_CASE(add_constructor_preserves_distinct_created_types_and_shared_overload_rules) {
    Runtime runtime;
    const auto* owner = registerObjectType(runtime, "DefinitionFactory");
    const auto* createdType = registerObjectType(runtime, "DefinitionResult");
    const auto callback = [](VirtualMachine* machine) { machine->getResult().setObjectValue(new ConstructedObject); };
    const auto id = runtime.addConstructor(owner->getId(), createdType->getId(), {
        .parameters = {},
        .function = callback,
        .documentation = "Creates a different object type."
    });
    BOOST_REQUIRE(id.isValid());
    const auto* constructor = runtime.getNativeConstructor(id);
    BOOST_REQUIRE(constructor != nullptr);
    BOOST_TEST(constructor->getObjectType() == owner);
    BOOST_TEST(constructor->getReturnType() == createdType);
    BOOST_TEST(constructor->getConstructedType() == createdType);
    BOOST_REQUIRE(owner->getConstructors().size() == 1u);
    BOOST_TEST(owner->getConstructors()[0] == constructor);
    BOOST_TEST(createdType->getConstructors().empty());
    BOOST_TEST(runtime.getConstructors(createdType).empty());
    BOOST_TEST(runtime.addConstructor(owner->getId(), {
        .parameters = {},
        .function = callback,
        .documentation = ""
    }).isInvalid());
    const auto parameters = std::vector<ParameterDefinition>{{runtime.getIntegerType()->getId(), "value", ""}};
    BOOST_REQUIRE(runtime.addConstructor(owner->getId(), {
        .parameters = parameters,
        .function = callback,
        .documentation = ""
    }).isValid());
    BOOST_TEST(runtime.addConstructor(owner->getId(), createdType->getId(), {
        .parameters = {{runtime.getIntegerType()->getId(), "renamed", ""}},
        .function = callback,
        .documentation = ""
    }).isInvalid());
    BOOST_REQUIRE(runtime.addConstructor(owner->getId(), {
        .parameters = {{runtime.getFloatType()->getId(), "value", ""}},
        .function = callback,
        .documentation = ""
    }).isValid());
    BOOST_TEST(owner->getConstructors().size() == 3u);
    BOOST_TEST(runtime.resolveConstructor(owner, {}).id == id);

    const auto functionId = compileConstruction(runtime, "return new DefinitionFactory");
    BOOST_TEST(runtime.getFunction(functionId)->getReturnType() == createdType);
    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);
    BOOST_REQUIRE(result.getObjectValue() != nullptr);
    createdType->freeValue(result);

    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);
    BOOST_REQUIRE(Interpreter{}.execute("new DefinitionFactory", context));
    BOOST_TEST(context.getResult()->getType() == createdType);
}

BOOST_AUTO_TEST_CASE(add_constructor_rejects_invalid_definitions_without_partial_publication) {
    Runtime runtime;
    const auto* type = registerObjectType(runtime, "InvalidDefinitionObject");
    const auto callback = [](VirtualMachine* machine) { machine->getResult().setObjectValue(new ConstructedObject); };
    const auto originalCount = runtime.getFunctionTableSize();
    const auto originalConstructors = runtime.getConstructors().size();
    const NativeConstructorDefinition definition{{}, callback, ""};
    for (const auto invalid : {
        TypeId::INVALID, TypeId{NOMAD_ID_MAX}, runtime.getIntegerType()->getId(),
        runtime.getVoidType()->getId()
    }) {
        BOOST_TEST(runtime.addConstructor(invalid, definition).isInvalid());
        BOOST_TEST(runtime.addConstructor(invalid, type->getId(), definition).isInvalid());
        BOOST_TEST(runtime.addConstructor(type->getId(), invalid, definition).isInvalid());
    }
    for (const auto invalid : {TypeId::INVALID, TypeId{NOMAD_ID_MAX}, runtime.getVoidType()->getId()}) {
        BOOST_TEST(runtime.addConstructor(type->getId(), {
            .parameters = {
                {runtime.getIntegerType()->getId(), "valid", ""},
                {invalid, "invalid", ""}
            },
            .function = callback,
            .documentation = ""
        }).isInvalid());
    }
    BOOST_TEST(runtime.addConstructor(type->getId(), {}).isInvalid());
    BOOST_TEST(runtime.getFunctionTableSize() == originalCount);
    BOOST_TEST(runtime.getConstructors().size() == originalConstructors);
    BOOST_TEST(type->getConstructors().empty());
    BOOST_TEST((runtime.resolveConstructor(type, {}).status == ResolveStatus::UnknownName));
    BOOST_REQUIRE(runtime.addConstructor(type->getId(), definition).isValid());
}

BOOST_AUTO_TEST_CASE(add_constructor_preserves_compiler_supplied_parameters) {
    Runtime runtime;
    const auto* type = registerObjectType(runtime, "DefinitionLocated");
    NomadInteger line = 0;
    BOOST_REQUIRE(runtime.addConstructor(type->getId(), {
        .parameters = {{runtime.getLineNumberType()->getId(), "line", "Source line."}},
        .function = [&](VirtualMachine* machine) {
            line = machine->getIntegerParameter(0);
            machine->getResult().setObjectValue(new ConstructedObject);
        },
        .documentation = ""
    }).isValid());
    const auto functionId = compileConstruction(runtime, "\nreturn new DefinitionLocated");
    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);
    BOOST_TEST(line == 2);
    type->freeValue(result);
}

BOOST_AUTO_TEST_CASE(inferred_constructors_resolve_execute_and_reuse_specializations) {
    Runtime runtime;
    auto specializations = NomadIndex{0};
    auto constructions = NomadIndex{0};
    BOOST_REQUIRE(runtime.registerParameterizedType(
        "Box", 1,
        [&](Runtime* owningRuntime, TypeId id, const std::vector<const Type*>& arguments) {
            ++specializations;
            return std::make_unique<ObjectType>(owningRuntime, id, "Box<" + arguments[0]->getTypeName() + ">", ObjectTypeMode::Closed, arguments);
        },
        [&](Runtime* owningRuntime, const TypeId id) {
            const auto* type = owningRuntime->getType(id).value();
            const auto* objectType = static_cast<const ObjectType*>(type);
            const auto* elementType = objectType->getTypeArguments()[0];
            BOOST_REQUIRE(runtime.addConstructor(objectType->getId(), {
                .parameters = {{elementType->getId(), "value", ""}},
                .function = [&](VirtualMachine* machine) {
                    ++constructions;
                    machine->getResult().setObjectValue(new ConstructedObject);
                },
                .documentation = ""
            }).isValid());
        }
    ));
    BOOST_REQUIRE(runtime.registerConstructorTemplate(
        "Box", {defTemplateParameter("value", TypeParameterIndex{0}, "")}
    ));
    auto caseIndex = NomadIndex{0};
    for (const auto& [source, expected] : std::vector<std::pair<NomadString, NomadString>>{
        {"new Box 42", "Box<int>"},
        {"new Box true", "Box<bool>"},
        {"new Box \"text\"", "Box<string>"},
        {"new Box new Box 42", "Box<Box<int>>"},
        {"new Box<int> 42", "Box<int>"}
    }) {
        BOOST_TEST_CONTEXT(source) {
            const auto id = compileConstruction(runtime, "return " + source, "case" + std::to_string(caseIndex++));
            const auto* expectedType = runtime.getTypeByName(expected);
            BOOST_REQUIRE(expectedType != nullptr);
            BOOST_TEST(runtime.getScriptFunction(id)->getReturnType() == expectedType);
            const auto before = NomadObject::getLiveObjectCount();
            RuntimeValue result;
            runtime.executeFunction(id, {}, result);
            BOOST_REQUIRE(result.getObjectValue() != nullptr);
            expectedType->freeValue(result);
            {
                auto machine = runtime.createVirtualMachine();
                InterpreterContext context(*machine);
                BOOST_REQUIRE(Interpreter{}.execute(source, context));
                BOOST_TEST(context.getResult()->getType() == expectedType);
            }
            BOOST_TEST(NomadObject::getLiveObjectCount() == before);
        }
    }
    const auto id = compileConstruction(runtime,
        "fun makeNumber\n"
        "    return 42\n"
        "end\n"
        "number = makeNumber\n"
        "return new Box number"
    );
    BOOST_TEST(runtime.getScriptFunction(id)->getReturnType() == runtime.getTypeByName("Box<int>"));
    {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        BOOST_REQUIRE(Interpreter{}.execute("number = 42", context));
        BOOST_REQUIRE(Interpreter{}.execute("new Box number", context));
        BOOST_TEST(context.getResult()->getType() == runtime.getTypeByName("Box<int>"));
    }
    BOOST_TEST(specializations == 4u);
    BOOST_TEST(constructions == 13u);
}

BOOST_AUTO_TEST_CASE(inferred_constructors_validate_bindings_and_concrete_constructor_signatures) {
    Runtime runtime;
    BOOST_REQUIRE(runtime.registerParameterizedType(
        "Same", 1,
        [](Runtime* owningRuntime, TypeId id, const std::vector<const Type*>& arguments) {
            return std::make_unique<ObjectType>(owningRuntime, id, "Same<" + arguments[0]->getTypeName() + ">", ObjectTypeMode::Closed, arguments);
        },
        [&](Runtime* owningRuntime, const TypeId id) {
            const auto* type = owningRuntime->getType(id).value();
            const auto* objectType = static_cast<const ObjectType*>(type);
            const auto* valueType = objectType->getTypeArguments()[0];
            BOOST_REQUIRE(runtime.addConstructor(objectType->getId(), {
                .parameters = {{valueType->getId(), "first", ""}, {valueType->getId(), "second", ""}},
                .function = [](VirtualMachine* machine) { machine->getResult().setObjectValue(new ConstructedObject); },
                .documentation = ""
            }).isValid());
        }
    ));
    BOOST_REQUIRE(runtime.registerConstructorTemplate(
        "Same",
        {
            defTemplateParameter("first", TypeParameterIndex{0}, ""),
            defTemplateParameter("second", TypeParameterIndex{0}, "")
        }
    ));
    auto caseIndex = NomadIndex{0};
    for (const auto* source : {
        "new Same 1 \"text\"", "new Same 1", "new Same 1 2 3",
        "new Same missing 1", "new Same<void> 1 2"
    }) {
        BOOST_TEST_CONTEXT(source) {
            auto compiler = runtime.createCompiler();
            CompilerContext context(compiler.get());
            const auto name = "rejected" + std::to_string(caseIndex++);
            compiler->registerSources(&context, {{{name, {name + ".nomad", "return " + NomadString(source)}}}, {}});
            BOOST_TEST(!compiler->compileFunctions(&context));
            BOOST_REQUIRE(context.getFirstError() != nullptr);
            auto machine = runtime.createVirtualMachine();
            InterpreterContext evaluation(*machine);
            BOOST_TEST(!Interpreter{}.execute(source, evaluation));
            BOOST_TEST(evaluation.hasError());
        }
    }
    BOOST_TEST(runtime.getTypeByName("Same<int>") == nullptr);
    const auto id = compileConstruction(runtime, "return new Same 1 2");
    BOOST_TEST(runtime.getScriptFunction(id)->getReturnType() == runtime.getTypeByName("Same<int>"));
}

BOOST_AUTO_TEST_CASE(inferred_constructor_arguments_preserve_execution_mode_evaluation_order) {
    Runtime runtime;
    std::vector<NomadInteger> markers;
    runtime.registerNativeFunction(
        "mark",
        [&](VirtualMachine* machine) {
            const auto value = machine->getIntegerParameter(0);
            markers.push_back(value);
            machine->setIntegerResult(value);
        },
        {defParameter("value", runtime.getIntegerType(), "")}, runtime.getIntegerType(), ""
    );
    BOOST_REQUIRE(runtime.registerParameterizedType(
        "Pair", 2,
        [](Runtime* owningRuntime, TypeId id, const std::vector<const Type*>& arguments) {
            return std::make_unique<ObjectType>(owningRuntime, id, "Pair<int int>", ObjectTypeMode::Closed, arguments);
        },
        [&](Runtime* owningRuntime, const TypeId id) {
            const auto* type = owningRuntime->getType(id).value();
            const auto* objectType = static_cast<const ObjectType*>(type);
            BOOST_REQUIRE(runtime.addConstructor(objectType->getId(), {
                .parameters = {{runtime.getIntegerType()->getId(), "first", ""}, {runtime.getIntegerType()->getId(), "second", ""}},
                .function = [](VirtualMachine* machine) {
                    BOOST_TEST(machine->getIntegerParameter(0) == 1);
                    BOOST_TEST(machine->getIntegerParameter(1) == 2);
                    machine->getResult().setObjectValue(new ConstructedObject);
                },
                .documentation = ""
            }).isValid());
        }
    ));
    BOOST_REQUIRE(runtime.registerConstructorTemplate(
        "Pair",
        {defTemplateParameter("first", TypeParameterIndex{0}, ""), defTemplateParameter("second", TypeParameterIndex{1}, "")}
    ));
    const auto id = compileConstruction(runtime, "return new Pair (mark 1) (mark 2)");
    BOOST_TEST(markers.empty());
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    result.freeObjectValue();
    BOOST_REQUIRE(markers.size() == 2u);
    BOOST_TEST(markers[0] == 2);
    BOOST_TEST(markers[1] == 1);
    markers.clear();
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);
    BOOST_REQUIRE(Interpreter{}.execute("new Pair (mark 1) (mark 2)", context));
    BOOST_REQUIRE(markers.size() == 2u);
    BOOST_TEST(markers[0] == 1);
    BOOST_TEST(markers[1] == 2);
}

BOOST_AUTO_TEST_CASE(inferred_constructors_require_a_matching_specialized_constructor) {
    for (const auto registerConstructor : {false, true}) {
        Runtime runtime;
        BOOST_REQUIRE(runtime.registerParameterizedType(
            "Box", 1,
            [](Runtime* owningRuntime, TypeId id, const std::vector<const Type*>& arguments) {
                return std::make_unique<ObjectType>(owningRuntime, id, "Box<int>", ObjectTypeMode::Closed, arguments);
            },
            [&](Runtime* owningRuntime, const TypeId id) {
            const auto* type = owningRuntime->getType(id).value();
                if (registerConstructor) {
                    BOOST_REQUIRE(runtime.addConstructor(type->getId(), {
                        .parameters = {{runtime.getStringType()->getId(), "value", ""}},
                        .function = [](VirtualMachine* machine) { machine->getResult().setObjectValue(new ConstructedObject); },
                        .documentation = ""
                    }).isValid());
                }
            }
        ));
        BOOST_REQUIRE(runtime.registerConstructorTemplate(
            "Box", {defTemplateParameter("value", TypeParameterIndex{0}, "")}
        ));
        auto compiler = runtime.createCompiler();
        CompilerContext context(compiler.get());
        compiler->registerSources(&context, {{{"main", {"main.nomad", "return new Box 42"}}}, {}});
        BOOST_TEST(!compiler->compileFunctions(&context));
        BOOST_REQUIRE(context.getFirstError() != nullptr);
        auto machine = runtime.createVirtualMachine();
        InterpreterContext evaluation(*machine);
        BOOST_TEST(!Interpreter{}.execute("new Box 42", evaluation));
        BOOST_TEST(evaluation.hasError());
    }
}

BOOST_AUTO_TEST_CASE(inferred_constructors_parse_from_signatures_without_specializing) {
    Runtime runtime;
    auto factoryCalls = NomadIndex{0};
    BOOST_REQUIRE(runtime.registerParameterizedType(
        "Pair", 2,
        [&factoryCalls](Runtime* owningRuntime, TypeId id, const std::vector<const Type*>& arguments) {
            ++factoryCalls;
            return std::make_unique<ObjectType>(owningRuntime, id, "Pair", ObjectTypeMode::Closed, arguments);
        }
    ));
    BOOST_REQUIRE(runtime.registerConstructorTemplate(
        "Pair",
        {
            defTemplateParameter("key", TypeParameterIndex{0}, ""),
            defTemplateParameter("value", TypeParameterIndex{1}, "")
        }
    ));
    for (const auto* source : {
        "new Pair \"key\" 1", "new Pair 123 true", "new Pair name value",
        "new Pair \"outer\" new Pair \"inner\" 42",
        "new Pair (1 + 2) (3 * 4)", "(new Pair \"key\" 1)", "(new Pair \"key\" 1)[0]"
    }) {
        BOOST_TEST_CONTEXT(source) {
            auto compiler = runtime.createCompiler();
            CompilerContext context(compiler.get());
            Tokenizer tokens(&context, "test.nomad", source);
            auto expression = parser::parseExpression(&context, nullptr, &tokens);
            BOOST_REQUIRE(expression != nullptr);
            BOOST_TEST(!context.hasError());
            BOOST_TEST(tokens.endOfLine());
        }
    }
    for (const auto* boundary : {")", "]", "extra"}) {
        auto compiler = runtime.createCompiler();
        CompilerContext context(compiler.get());
        Tokenizer tokens(&context, "test.nomad", "new Pair \"key\" 1 " + NomadString(boundary));
        auto expression = parser::parseNewExpression(&context, nullptr, &tokens, nullptr);
        BOOST_REQUIRE(expression != nullptr);
        BOOST_TEST(tokens.is(boundary));
        BOOST_TEST(!context.hasError());
    }
    BOOST_TEST(factoryCalls == 0u);
    BOOST_TEST(runtime.getConstructors().empty());
}

BOOST_AUTO_TEST_CASE(inferred_constructor_parsing_reports_missing_arguments_and_signatures) {
    Runtime runtime;
    BOOST_REQUIRE(runtime.registerParameterizedType(
        "Pair", 2, [](Runtime* owningRuntime, TypeId id, const std::vector<const Type*>& arguments) {
            return std::make_unique<ObjectType>(owningRuntime, id, "Pair", ObjectTypeMode::Closed, arguments);
        }
    ));
    auto compiler = runtime.createCompiler();
    {
        CompilerContext context(compiler.get());
        Tokenizer tokens(&context, "test.nomad", "new Pair \"key\" 1");
        BOOST_TEST(parser::parseExpression(&context, nullptr, &tokens) == nullptr);
        BOOST_REQUIRE(context.getFirstError() != nullptr);
        BOOST_TEST(context.getFirstError()->message.find("no inference signature") != NomadString::npos);
    }
    BOOST_REQUIRE(runtime.registerConstructorTemplate(
        "Pair",
        {
            defTemplateParameter("key", TypeParameterIndex{0}, ""),
            defTemplateParameter("value", TypeParameterIndex{1}, "")
        }
    ));
    for (const auto* source : {"new Pair", "new Pair \"key\"", "new Pair \"key\" )", "new Pair \"key\" ]"}) {
        CompilerContext context(compiler.get());
        Tokenizer tokens(&context, "test.nomad", source);
        BOOST_TEST(parser::parseExpression(&context, nullptr, &tokens) == nullptr);
        BOOST_REQUIRE(context.getFirstError() != nullptr);
        BOOST_TEST(context.getFirstError()->message.find("Expected arguments for") != NomadString::npos);
    }
}

BOOST_AUTO_TEST_CASE(inferred_constructor_parsing_reuses_concrete_and_hidden_parameter_shapes) {
    Runtime runtime;
    auto calls = NomadIndex{0};
    BOOST_REQUIRE(runtime.registerParameterizedType(
        "Located", 1, [](Runtime* owningRuntime, TypeId id, const std::vector<const Type*>& arguments) {
            return std::make_unique<ObjectType>(owningRuntime, id, "Located<" + arguments[0]->getTypeName() + ">", ObjectTypeMode::Closed, arguments
            );
        },
        [&](Runtime* owningRuntime, const TypeId id) {
            const auto* type = owningRuntime->getType(id).value();
            const auto* objectType = static_cast<const ObjectType*>(type);
            BOOST_REQUIRE(runtime.addConstructor(objectType->getId(), {
                .parameters = {
                    {runtime.getFileNameType()->getId(), "file", ""},
                    {objectType->getTypeArguments()[0]->getId(), "value", ""},
                    {runtime.getBooleanType()->getId(), "enabled", ""},
                    {runtime.getFunctionNameType()->getId(), "function", ""},
                    {runtime.getLineNumberType()->getId(), "line", ""}
                },
                .function = [&](VirtualMachine* machine) {
                    BOOST_TEST(machine->getStringParameter(1)->getString() == "value");
                    BOOST_TEST(machine->getBooleanParameter(2));
                    ++calls;
                    machine->getResult().setObjectValue(new ConstructedObject);
                },
                .documentation = ""
            }).isValid());
        }
    ));
    BOOST_REQUIRE(runtime.registerConstructorTemplate(
        "Located",
        {
            defTemplateParameter("file", runtime.getFileNameType(), ""),
            defTemplateParameter("value", TypeParameterIndex{0}, ""),
            defTemplateParameter("enabled", runtime.getBooleanType(), ""),
            defTemplateParameter("function", runtime.getFunctionNameType(), ""),
            defTemplateParameter("line", runtime.getLineNumberType(), "")
        }
    ));
    const auto id = compileConstruction(runtime, "return 0");
    auto* function = runtime.getScriptFunction(id);
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    Tokenizer tokens(&context, "test.nomad", "new Located \"value\" true");
    auto expression = parser::parseExpression(&context, function, &tokens);
    BOOST_REQUIRE(expression != nullptr);
    BOOST_TEST(tokens.endOfLine());
    BOOST_TEST(!context.hasError());
    BOOST_TEST(runtime.getTypeByName("Located<string>") == nullptr);
    const auto inferredId = compileConstruction(runtime, "return new Located \"value\" true", "located");
    RuntimeValue result;
    runtime.executeFunction(inferredId, {}, result);
    result.freeObjectValue();
    {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext evaluation(*machine);
        BOOST_REQUIRE(Interpreter{}.execute("new Located \"value\" true", evaluation));
        BOOST_TEST(evaluation.getResult()->getType() == runtime.getTypeByName("Located<string>"));
    }
    BOOST_TEST(calls == 2u);
}

BOOST_AUTO_TEST_CASE(constructor_templates_store_generic_and_concrete_parameters_without_specializing) {
    Runtime runtime;
    auto factoryCalls = NomadIndex{0};
    BOOST_REQUIRE(runtime.registerParameterizedType(
        "Example", 2,
        [&factoryCalls](Runtime* owningRuntime, TypeId id, const std::vector<const Type*>& arguments) {
            ++factoryCalls;
            return std::make_unique<ObjectType>(owningRuntime, id, "Example", ObjectTypeMode::Closed, arguments);
        }
    ));
    BOOST_TEST(!runtime.getConstructorTemplate("Missing").has_value());
    BOOST_TEST(!runtime.getConstructorTemplate("Example").has_value());
    BOOST_REQUIRE(runtime.registerConstructorTemplate(
        "Example",
        {
            defTemplateParameter("key", TypeParameterIndex{0}, "The key."),
            defTemplateParameter("value", TypeParameterIndex{1}, "The value."),
            defTemplateParameter("sameKey", TypeParameterIndex{0}, ""),
            defTemplateParameter("enabled", runtime.getBooleanType(), "Whether enabled."),
            defTemplateParameter("file", runtime.getFileNameType(), "")
        }
    ));
    auto signature = runtime.getConstructorTemplate("Example");
    BOOST_REQUIRE(signature.has_value());
    BOOST_REQUIRE(signature->size() == 5u);
    BOOST_TEST((*signature)[0].name == "key");
    BOOST_TEST((*signature)[0].doc == "The key.");
    BOOST_TEST(std::get<TypeParameterIndex>((*signature)[0].type).index == 0u);
    BOOST_TEST(std::get<TypeParameterIndex>((*signature)[1].type).index == 1u);
    BOOST_TEST(std::get<TypeParameterIndex>((*signature)[2].type).index == 0u);
    BOOST_TEST(std::get<const Type*>((*signature)[3].type) == runtime.getBooleanType());
    BOOST_TEST(std::get<const Type*>((*signature)[4].type) == runtime.getFileNameType());
    signature->clear();
    BOOST_TEST(runtime.getConstructorTemplate("Example")->size() == 5u);
    BOOST_TEST(factoryCalls == 0u);
    BOOST_TEST(runtime.getTypeByName("Example") == nullptr);
}

BOOST_AUTO_TEST_CASE(constructor_templates_reject_invalid_definitions_without_reserving_signature) {
    Runtime runtime;
    Runtime other;
    const auto factory = [](Runtime* owningRuntime, TypeId id, const std::vector<const Type*>& arguments) {
        return std::make_unique<ObjectType>(owningRuntime, id, "Example", ObjectTypeMode::Closed, arguments);
    };
    BOOST_REQUIRE(runtime.registerParameterizedType("Example", 2, factory));
    BOOST_TEST(!runtime.registerConstructorTemplate(
        "Missing", {defTemplateParameter("value", TypeParameterIndex{0}, "")}
    ));
    BOOST_TEST(!runtime.registerConstructorTemplate("Example", {}));
    for (const auto& parameters : std::vector<std::vector<TemplateParameter>>{
        {defTemplateParameter("value", TypeParameterIndex{0}, "")},
        {defTemplateParameter("key", TypeParameterIndex{0}, ""), defTemplateParameter("value", TypeParameterIndex{2}, "")},
        {
            defTemplateParameter("key", TypeParameterIndex{0}, ""),
            defTemplateParameter("value", TypeParameterIndex{1}, ""),
            defTemplateParameter("invalid", static_cast<const Type*>(nullptr), "")
        },
        {
            defTemplateParameter("key", TypeParameterIndex{0}, ""),
            defTemplateParameter("value", TypeParameterIndex{1}, ""),
            defTemplateParameter("invalid", runtime.getVoidType(), "")
        },
        {
            defTemplateParameter("key", TypeParameterIndex{0}, ""),
            defTemplateParameter("value", TypeParameterIndex{1}, ""),
            defTemplateParameter("invalid", other.getIntegerType(), "")
        }
    }) {
        BOOST_TEST(!runtime.registerConstructorTemplate("Example", parameters));
        BOOST_TEST(!runtime.getConstructorTemplate("Example").has_value());
    }
    BOOST_REQUIRE(runtime.registerNativeParameterizedConstructor(
        "Alias", 1, [](const NomadString&, const std::vector<const Type*>&) {}
    ));
    BOOST_TEST(!runtime.registerConstructorTemplate(
        "Alias", {defTemplateParameter("value", TypeParameterIndex{0}, "")}
    ));
    registerObjectType(runtime, "Concrete");
    BOOST_TEST(!runtime.registerConstructorTemplate(
        "Concrete", {defTemplateParameter("value", TypeParameterIndex{0}, "")}
    ));
    BOOST_REQUIRE(runtime.registerConstructorTemplate(
        "Example",
        {defTemplateParameter("key", TypeParameterIndex{0}, ""), defTemplateParameter("value", TypeParameterIndex{1}, "")}
    ));
    BOOST_TEST(!runtime.registerConstructorTemplate(
        "Example",
        {defTemplateParameter("value", TypeParameterIndex{1}, ""), defTemplateParameter("key", TypeParameterIndex{0}, "")}
    ));
    const auto signature = runtime.getConstructorTemplate("Example");
    BOOST_REQUIRE(signature.has_value());
    BOOST_TEST((*signature)[0].name == "key");
    BOOST_TEST(std::get<TypeParameterIndex>((*signature)[0].type).index == 0u);
}

BOOST_AUTO_TEST_CASE(constructor_template_registration_preserves_explicit_construction) {
    Runtime runtime;
    auto calls = NomadIndex{0};
    BOOST_REQUIRE(runtime.registerParameterizedType(
        "Box", 1,
        [](Runtime* owningRuntime, TypeId id, const std::vector<const Type*>& arguments) {
            const auto* elementType = arguments[0];
            return std::make_unique<ObjectType>(owningRuntime, id, "Box<" + elementType->getTypeName() + ">", ObjectTypeMode::Closed, arguments);
        },
        [&](Runtime* owningRuntime, const TypeId id) {
            const auto* type = owningRuntime->getType(id).value();
            const auto* objectType = static_cast<const ObjectType*>(type);
            const auto* elementType = objectType->getTypeArguments()[0];
            BOOST_REQUIRE(runtime.addConstructor(objectType->getId(), {
                .parameters = {{elementType->getId(), "value", ""}},
                .function = [&](VirtualMachine* machine) {
                    BOOST_TEST(machine->getIntegerParameter(0) == 42);
                    ++calls;
                    machine->getResult().setObjectValue(new ConstructedObject);
                },
                .documentation = ""
            }).isValid());
        }
    ));
    BOOST_REQUIRE(runtime.registerConstructorTemplate(
        "Box", {defTemplateParameter("value", TypeParameterIndex{0}, "")}
    ));
    const auto id = compileConstruction(runtime, "return new Box<int> 42");
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    result.freeObjectValue();
    {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        BOOST_REQUIRE(Interpreter{}.execute("new Box<int> 42", context));
    }
    BOOST_TEST(calls == 2u);
}

BOOST_AUTO_TEST_CASE(constructor_inference_binds_static_types_without_creating_specializations) {
    Runtime runtime;
    auto factoryCalls = NomadIndex{0};
    BOOST_REQUIRE(runtime.registerParameterizedType(
        "Pair", 2,
        [&factoryCalls](Runtime* owningRuntime, TypeId id, const std::vector<const Type*>& arguments) {
            ++factoryCalls;
            return std::make_unique<ObjectType>(owningRuntime, id, "Pair", ObjectTypeMode::Closed, arguments);
        }
    ));
    BOOST_REQUIRE(runtime.registerConstructorTemplate(
        "Pair",
        {
            defTemplateParameter("key", TypeParameterIndex{0}, ""),
            defTemplateParameter("value", TypeParameterIndex{1}, "")
        }
    ));
    for (const auto& argumentTypes : std::vector<std::vector<const Type*>>{
        {runtime.getStringType(), runtime.getIntegerType()},
        {runtime.getIntegerType(), runtime.getBooleanType()},
        {runtime.getBooleanType(), runtime.getFloatType()}
    }) {
        const auto result = runtime.inferConstructorTypeArguments("Pair", argumentTypes);
        BOOST_REQUIRE(result.isResolved());
        BOOST_CHECK(result.status == ResolveStatus::Resolved);
        BOOST_TEST(result.error.empty());
        BOOST_REQUIRE(result.typeArguments.size() == 2u);
        BOOST_TEST(result.typeArguments[0] == argumentTypes[0]);
        BOOST_TEST(result.typeArguments[1] == argumentTypes[1]);
    }
    const auto* nested = registerObjectType(runtime, "List<int>");
    const auto result = runtime.inferConstructorTypeArguments("Pair", {runtime.getStringType(), nested});
    BOOST_REQUIRE(result.isResolved());
    BOOST_TEST(result.typeArguments[1] == nested);
    BOOST_TEST(factoryCalls == 0u);
    BOOST_TEST(runtime.getConstructors().empty());
    BOOST_TEST(runtime.getTypeByName("Pair<string int>") == nullptr);
}

BOOST_AUTO_TEST_CASE(constructor_inference_supports_reordered_repeated_and_concrete_parameters) {
    Runtime runtime;
    BOOST_REQUIRE(runtime.registerParameterizedType(
        "Example", 2, [](Runtime* owningRuntime, TypeId id, const std::vector<const Type*>& arguments) {
            return std::make_unique<ObjectType>(owningRuntime, id, "Example", ObjectTypeMode::Closed, arguments);
        }
    ));
    BOOST_REQUIRE(runtime.registerConstructorTemplate(
        "Example",
        {
            defTemplateParameter("value", TypeParameterIndex{1}, ""),
            defTemplateParameter("key", TypeParameterIndex{0}, ""),
            defTemplateParameter("sameValue", TypeParameterIndex{1}, ""),
            defTemplateParameter("enabled", runtime.getBooleanType(), ""),
            defTemplateParameter("file", runtime.getFileNameType(), ""),
            defTemplateParameter("function", runtime.getFunctionNameType(), ""),
            defTemplateParameter("line", runtime.getLineNumberType(), "")
        }
    ));
    const auto result = runtime.inferConstructorTypeArguments(
        "Example",
        {
            runtime.getIntegerType(), runtime.getStringType(), runtime.getIntegerType(),
            runtime.getBooleanType(), nullptr, nullptr, nullptr
        }
    );
    BOOST_REQUIRE(result.isResolved());
    BOOST_REQUIRE(result.typeArguments.size() == 2u);
    BOOST_TEST(result.typeArguments[0] == runtime.getStringType());
    BOOST_TEST(result.typeArguments[1] == runtime.getIntegerType());
}

BOOST_AUTO_TEST_CASE(constructor_inference_reports_failures_without_partial_bindings) {
    Runtime runtime;
    Runtime other;
    BOOST_REQUIRE(runtime.registerParameterizedType(
        "Example", 1, [](Runtime* owningRuntime, TypeId id, const std::vector<const Type*>& arguments) {
            return std::make_unique<ObjectType>(owningRuntime, id, "Example", ObjectTypeMode::Closed, arguments);
        }
    ));
    const auto unknown = runtime.inferConstructorTypeArguments("Missing", {});
    BOOST_CHECK(unknown.status == ResolveStatus::UnknownName);
    BOOST_TEST(!unknown.isResolved());
    BOOST_TEST(unknown.typeArguments.empty());
    BOOST_TEST(unknown.error.find("Missing") != NomadString::npos);
    const auto missingSignature = runtime.inferConstructorTypeArguments("Example", {});
    BOOST_CHECK(missingSignature.status == ResolveStatus::UnknownName);
    BOOST_TEST(!missingSignature.error.empty());
    BOOST_REQUIRE(runtime.registerConstructorTemplate(
        "Example",
        {
            defTemplateParameter("first", TypeParameterIndex{0}, ""),
            defTemplateParameter("second", TypeParameterIndex{0}, ""),
            defTemplateParameter("number", runtime.getIntegerType(), "")
        }
    ));
    for (const auto& argumentTypes : std::vector<std::vector<const Type*>>{
        {}, {runtime.getIntegerType()},
        {runtime.getIntegerType(), runtime.getIntegerType(), runtime.getIntegerType(), runtime.getIntegerType()}
    }) {
        const auto result = runtime.inferConstructorTypeArguments("Example", argumentTypes);
        BOOST_CHECK(result.status == ResolveStatus::NoMatchingArity);
        BOOST_TEST(!result.isResolved());
        BOOST_TEST(result.typeArguments.empty());
        BOOST_TEST(result.error.find("expects 3 argument(s)") != NomadString::npos);
    }
    for (const auto* invalid : {
        static_cast<const Type*>(nullptr), static_cast<const Type*>(runtime.getVoidType()),
        runtime.getFileNameType(), static_cast<const Type*>(other.getIntegerType())
    }) {
        const auto result = runtime.inferConstructorTypeArguments(
            "Example", {runtime.getIntegerType(), invalid, runtime.getIntegerType()}
        );
        BOOST_CHECK(result.status == ResolveStatus::NoMatchingTypes);
        BOOST_TEST(result.typeArguments.empty());
        BOOST_TEST(result.error.find("argument 2 ('second')") != NomadString::npos);
        BOOST_TEST(result.error.find("explicit type arguments") != NomadString::npos);
    }
    const auto conflict = runtime.inferConstructorTypeArguments(
        "Example", {runtime.getIntegerType(), runtime.getFloatType(), runtime.getIntegerType()}
    );
    BOOST_CHECK(conflict.status == ResolveStatus::NoMatchingTypes);
    BOOST_TEST(conflict.typeArguments.empty());
    BOOST_TEST(conflict.error.find("conflicting types for type parameter 0") != NomadString::npos);
    BOOST_TEST(conflict.error.find("'int'") != NomadString::npos);
    BOOST_TEST(conflict.error.find("'float'") != NomadString::npos);
    for (const auto* incompatible : {
        static_cast<const Type*>(nullptr), static_cast<const Type*>(runtime.getFloatType()),
        static_cast<const Type*>(other.getIntegerType())
    }) {
        const auto result = runtime.inferConstructorTypeArguments(
            "Example", {runtime.getIntegerType(), runtime.getIntegerType(), incompatible}
        );
        BOOST_CHECK(result.status == ResolveStatus::NoMatchingTypes);
        BOOST_TEST(result.typeArguments.empty());
        BOOST_TEST(result.error.find("argument 3 ('number')") != NomadString::npos);
        BOOST_TEST(result.error.find("expected exact type 'int'") != NomadString::npos);
    }
    const auto recovered = runtime.inferConstructorTypeArguments(
        "Example", {runtime.getIntegerType(), runtime.getIntegerType(), runtime.getIntegerType()}
    );
    BOOST_REQUIRE(recovered.isResolved());
    BOOST_REQUIRE(recovered.typeArguments.size() == 1u);
    BOOST_TEST(recovered.typeArguments[0] == runtime.getIntegerType());
}

BOOST_AUTO_TEST_CASE(constructors_accept_heterogeneous_arguments_and_type_specific_zero_values) {
    Runtime runtime;
    auto calls = NomadIndex{0};
    const auto* type = registerObjectType(runtime, "Example");
    BOOST_REQUIRE(runtime.addConstructor(type->getId(), {
        .parameters = {
            {runtime.getIntegerType()->getId(), "number", ""},
            {runtime.getStringType()->getId(), "text", ""},
            {runtime.getBooleanType()->getId(), "enabled", ""}
        },
        .function = [&calls](VirtualMachine* machine) {
            BOOST_TEST(machine->getIntegerParameter(0) == 0);
            BOOST_TEST(machine->getStringParameter(1)->getString() == "hello");
            BOOST_TEST(machine->getBooleanParameter(2));
            ++calls;
            machine->getResult().setObjectValue(new ConstructedObject);
        },
        .documentation = ""
    }).isValid());
    const auto id = compileConstruction(runtime, "return new Example 0 \"hello\" true");
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    BOOST_REQUIRE(result.getObjectValue() != nullptr);
    result.freeObjectValue();
    BOOST_TEST(calls == 1u);
    {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        BOOST_REQUIRE(Interpreter{}.execute("new Example 0 \"hello\" true", context));
        BOOST_TEST(calls == 2u);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(constructor_faults_and_null_results_release_object_arguments) {
    for (const auto returnsNull : {false, true}) {
        Runtime runtime;
        const auto* type = registerObjectType(runtime, "Example");
        BOOST_REQUIRE(runtime.addConstructor(type->getId(), {
            .parameters = {{runtime.getStringType()->getId(), "text", ""}},
            .function = [returnsNull](VirtualMachine*) {
                if (returnsNull) {
                    return;
                }
                throw NomadException("Constructor failed");
            },
            .documentation = ""
        }).isValid());
        const auto id = compileConstruction(runtime, "return new Example (\"hello\".toUpper)");
        const auto before = NomadObject::getLiveObjectCount();
        auto machine = runtime.createVirtualMachine();
        for (auto attempt = 0; attempt < 2; ++attempt) {
            if (returnsNull) {
                BOOST_CHECK_THROW(machine->run(runtime.getScriptFunction(id), {}), VirtualMachineException);
            } else {
                BOOST_CHECK_THROW(machine->run(runtime.getScriptFunction(id), {}), NomadException);
            }
            BOOST_TEST(NomadObject::getLiveObjectCount() == before);
        }
        {
            InterpreterContext context(*machine);
            BOOST_TEST(!Interpreter{}.execute("new Example (\"hello\".toUpper)", context));
            BOOST_REQUIRE(context.getError() != nullptr);
        }
        BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    }
}

BOOST_AUTO_TEST_CASE(parameterless_constructors_are_explicitly_registered) {
    Runtime runtime;
    const auto* type = registerObjectType(runtime, "Plain");
    BOOST_REQUIRE(runtime.addConstructor(type->getId(), {
        .parameters = {},
        .function = [](VirtualMachine* machine) { machine->getResult().setObjectValue(new ConstructedObject); },
        .documentation = ""
    }).isValid());
    const auto id = compileConstruction(runtime, "return new Plain");
    const auto start = runtime.getScriptFunction(id)->getFunctionStart();
    BOOST_TEST(runtime.getInstructions()[start].fn ==
        runtime.getInstructionFn(runtime.getInstructionId(OpCodes::op_push_this)));
    BOOST_TEST(runtime.getInstructions()[start + 1].fn ==
        runtime.getInstructionFn(runtime.getInstructionId(OpCodes::op_call_native_constructor)));
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    BOOST_REQUIRE(result.getObjectValue() != nullptr);
}

BOOST_AUTO_TEST_CASE(native_constructor_host_calls_preserve_registers_and_borrow_arguments) {
    Runtime runtime;
    const auto* type = registerObjectType(runtime, "HostObject");
    auto calls = 0;
    const auto constructorId = runtime.addConstructor(type->getId(), {
        .parameters = {
            {runtime.getStringType()->getId(), "text", ""},
            {runtime.getIntegerType()->getId(), "number", ""}
        },
        .function = [&](VirtualMachine* machine) {
            BOOST_TEST(machine->getStringParameter(0)->getString() == "owned");
            BOOST_TEST(machine->getIntegerParameter(1) == 23);
            ++calls;
            machine->setIntegerIntermediate(99);
            machine->getResult().setObjectValue(new ConstructedObject);
        },
        .documentation = ""
    });
    BOOST_REQUIRE(constructorId.isValid());
    auto machine = runtime.createVirtualMachine();
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue text;
    text.setStringValue("owned");
    machine->setIntegerResult(41);
    machine->setIntegerIntermediate(42);
    auto* object = machine->constructObject(constructorId, {text, RuntimeValue{NomadInteger{23}}});
    BOOST_REQUIRE(object != nullptr);
    BOOST_TEST(object->getReferenceCount() == 1u);
    BOOST_TEST(text.getStringObject()->getReferenceCount() == 1u);
    BOOST_TEST(machine->getIntegerResult() == 41);
    BOOST_TEST(machine->getIntegerIntermediate() == 42);
    BOOST_TEST(calls == 1);
    BOOST_CHECK_THROW(static_cast<void>(machine->constructObject(constructorId, {})), NomadException);
    BOOST_TEST(machine->getIntegerResult() == 41);
    object->releaseRef();
    runtime.getStringType()->freeValue(text);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(native_constructor_failure_releases_published_result_and_recovers) {
    Runtime runtime;
    const auto* type = registerObjectType(runtime, "Fallible");
    const auto constructorId = runtime.addConstructor(type->getId(), {
        .parameters = {{runtime.getBooleanType()->getId(), "fail", ""}},
        .function = [](VirtualMachine* machine) {
            machine->getResult().setObjectValue(new ConstructedObject);
            if (machine->getBooleanParameter(0)) {
                throw NomadException("Failed after allocation");
            }
        },
        .documentation = ""
    });
    BOOST_REQUIRE(constructorId.isValid());
    const auto id = compileConstruction(runtime, "return new Fallible true");
    const auto before = NomadObject::getLiveObjectCount();
    auto machine = runtime.createVirtualMachine();
    BOOST_CHECK_THROW(machine->run(runtime.getScriptFunction(id), {}), NomadException);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    machine->setIntegerResult(41);
    machine->setIntegerIntermediate(42);
    BOOST_CHECK_THROW(
        static_cast<void>(machine->constructObject(constructorId, {RuntimeValue{true}})), NomadException
    );
    BOOST_TEST(machine->getIntegerResult() == 41);
    BOOST_TEST(machine->getIntegerIntermediate() == 42);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    {
        InterpreterContext context(*machine);
        BOOST_TEST(!Interpreter{}.execute("new Fallible true", context));
        BOOST_TEST(NomadObject::getLiveObjectCount() == before);
        BOOST_REQUIRE(Interpreter{}.execute("new Fallible false", context));
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(native_constructor_reentry_restores_the_stack_parameter_frame) {
    Runtime runtime;
    const auto* innerType = registerObjectType(runtime, "InnerNative");
    const auto innerId = runtime.addConstructor(innerType->getId(), {
        .parameters = {{runtime.getIntegerType()->getId(), "value", ""}},
        .function = [](VirtualMachine* machine) {
            BOOST_TEST(machine->getIntegerParameter(0) == 99);
            machine->getResult().setObjectValue(new ConstructedObject);
        },
        .documentation = ""
    });
    BOOST_REQUIRE(innerId.isValid());
    const auto* outerType = registerObjectType(runtime, "OuterNative");
    BOOST_REQUIRE(runtime.addConstructor(outerType->getId(), {
        .parameters = {{runtime.getStringType()->getId(), "value", ""}},
        .function = [innerId](VirtualMachine* machine) {
            BOOST_TEST(machine->getStringParameter(0)->getString() == "outer");
            auto* inner = machine->constructObject(innerId, {RuntimeValue{NomadInteger{99}}});
            inner->releaseRef();
            BOOST_TEST(machine->getStringParameter(0)->getString() == "outer");
            machine->getResult().setObjectValue(new ConstructedObject);
        },
        .documentation = ""
    }).isValid());
    const auto id = compileConstruction(runtime, "return new OuterNative (\"outer\".toLower)");
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    result.freeObjectValue();
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        BOOST_REQUIRE(Interpreter{}.execute("new OuterNative (\"outer\".toLower)", context));
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(source_parameters_are_supplied_by_the_compiler) {
    Runtime runtime;
    const auto* type = registerObjectType(runtime, "Located");
    NomadString file;
    NomadString functionName;
    NomadInteger line = 0;
    NomadInteger number = 0;
    BOOST_REQUIRE(runtime.addConstructor(type->getId(), {
        .parameters = {
            {runtime.getFileNameType()->getId(), "file", ""},
            {runtime.getIntegerType()->getId(), "number", ""},
            {runtime.getFunctionNameType()->getId(), "function", ""},
            {runtime.getLineNumberType()->getId(), "line", ""}
        },
        .function = [&](VirtualMachine* machine) {
            file = machine->getStringParameter(0)->getString();
            number = machine->getIntegerParameter(1);
            functionName = machine->getStringParameter(2)->getString();
            line = machine->getIntegerParameter(3);
            machine->getResult().setObjectValue(new ConstructedObject);
        },
        .documentation = ""
    }).isValid());

    const auto id = compileConstruction(runtime, "\nreturn new Located 42");
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    BOOST_REQUIRE(result.getObjectValue() != nullptr);
    result.freeObjectValue();
    BOOST_TEST(file == "main.nomad");
    BOOST_TEST(number == 42);
    BOOST_TEST(functionName == "main");
    BOOST_TEST(line == 2);

    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);
    BOOST_TEST(Interpreter{}.execute("new Located 7", context));
    BOOST_TEST(number == 7);

    auto compiler = runtime.createCompiler();
    CompilerContext compilerContext(compiler.get());
    compiler->registerSources(&compilerContext, {{{"invalid", {"invalid.nomad", "return new Located 1 2"}}}, {}});
    BOOST_TEST(!compiler->compileFunctions(&compilerContext));
}

BOOST_AUTO_TEST_CASE(registration_rejects_collisions_invalid_types_and_missing_factories) {
    Runtime runtime;
    const auto* type = registerObjectType(runtime, "Example");
    const auto* otherType = registerObjectType(runtime, "Other");
    const auto callback = [](VirtualMachine* machine) { machine->getResult().setObjectValue(new ConstructedObject); };
    const auto parameters = std::vector<ParameterDefinition>{{runtime.getIntegerType()->getId(), "value", ""}};
    const auto id = runtime.addConstructor(type->getId(), {
        .parameters = parameters,
        .function = callback,
        .documentation = ""
    });
    BOOST_REQUIRE(id.isValid());
    BOOST_TEST(runtime.addConstructor(type->getId(), {
        .parameters = parameters,
        .function = callback,
        .documentation = ""
    }).isInvalid());
    BOOST_TEST(runtime.addConstructor(type->getId(), otherType->getId(), {
        .parameters = parameters,
        .function = callback,
        .documentation = ""
    }).isInvalid());
    BOOST_TEST(runtime.addConstructor(type->getId(), {
        .parameters = {{runtime.getIntegerType()->getId(), "renamed", ""}},
        .function = callback,
        .documentation = ""
    }).isInvalid());
    BOOST_TEST(runtime.addConstructor(type->getId(), {
        .parameters = {{TypeId{NOMAD_ID_MAX}, "value", ""}},
        .function = callback,
        .documentation = ""
    }).isInvalid());
    for (const auto invalid : {runtime.getVoidType()->getId(), TypeId::INVALID}) {
        BOOST_TEST(runtime.addConstructor(otherType->getId(), {
            .parameters = {{invalid, "value", ""}},
            .function = callback,
            .documentation = ""
        }).isInvalid());
    }
    BOOST_TEST(runtime.addConstructor(TypeId::INVALID, {
        .parameters = {},
        .function = callback,
        .documentation = ""
    }).isInvalid());
    BOOST_TEST(runtime.addConstructor(runtime.getTypeId("MissingOwner"), {
        .parameters = {},
        .function = callback,
        .documentation = ""
    }).isInvalid());
    BOOST_TEST(runtime.addConstructor(type->getId(), runtime.getTypeId("MissingCreatedType"), {
        .parameters = {},
        .function = callback,
        .documentation = ""
    }).isInvalid());
    BOOST_TEST(runtime.addConstructor(type->getId(), {
        .parameters = {},
        .function = {},
        .documentation = ""
    }).isInvalid());
    BOOST_TEST(runtime.getConstructors().size() == 1u);
    const auto* constructor = runtime.getConstructor(id);
    BOOST_REQUIRE(constructor != nullptr);
    BOOST_TEST(constructor->getReturnType() == type);
    BOOST_TEST(runtime.getConstructor(CallableId::INVALID) == nullptr);
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"invalid", {"invalid.nomad", "return new Example true"}}}, {}});
    BOOST_TEST(!compiler->compileFunctions(&context));
    BOOST_REQUIRE(context.getFirstError() != nullptr);
    BOOST_TEST(context.getFirstError()->sourceName == "invalid.nomad");
    BOOST_TEST(context.getFirstError()->message.find("No overload of 'Example'") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(overloads_select_by_argument_types_and_count_with_distinct_return_types) {
    Runtime runtime;
    const auto* singleType = registerObjectType(runtime, "Single");
    const auto* pairType = registerObjectType(runtime, "Pair");
    const auto* textType = registerObjectType(runtime, "Text");
    const auto* emptyType = registerObjectType(runtime, "Empty");
    const auto* exampleType = registerObjectType(runtime, "Example");
    NomadString calls;
    const auto registerFactory = [&](const std::vector<ParameterDefinition>& parameters,
                                     const ObjectType* type, const NomadString& marker) {
        BOOST_REQUIRE(runtime.addConstructor(exampleType->getId(), type->getId(), {
            .parameters = parameters,
            .function = [&calls, marker](VirtualMachine* machine) {
                calls += marker;
                machine->getResult().setObjectValue(new ConstructedObject);
            },
            .documentation = ""
        }).isValid());
    };
    registerFactory({}, emptyType, "E");
    registerFactory({{runtime.getIntegerType()->getId(), "value", ""}}, singleType, "I");
    registerFactory(
        {{runtime.getIntegerType()->getId(), "first", ""}, {runtime.getIntegerType()->getId(), "second", ""}},
        pairType, "P"
    );
    registerFactory({{runtime.getStringType()->getId(), "value", ""}}, textType, "S");
    runtime.registerNativeFunction(
        "consume",
        [](VirtualMachine* machine) { machine->setIntegerResult(machine->getIntegerParameter(1)); },
        {defParameter("object", singleType, ""), defParameter("value", runtime.getIntegerType(), "")},
        runtime.getIntegerType(), ""
    );
    const auto id = compileConstruction(runtime,
        "empty = (new Example)\n"
        "single = (new Example 1)\n"
        "pair = new Example 1 2\n"
        "text = new Example \"hello\"\n"
        "assert (consume (new Example 1) 2) == 2\n"
        "return pair"
    );
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    result.freeObjectValue();
    BOOST_TEST(calls == "EIPSI");
    {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        for (const auto* source : {"new Example", "(new Example 1)", "new Example 1 2", "new Example \"hello\""}) {
            BOOST_REQUIRE(Interpreter{}.execute(source, context));
        }
        BOOST_TEST(context.getResult()->getType() == textType);
        BOOST_REQUIRE(Interpreter{}.execute("new Example 1 2", context));
        BOOST_TEST(context.getResult()->getType() == pairType);
        BOOST_REQUIRE(Interpreter{}.execute("(new Example 1)", context));
        BOOST_TEST(context.getResult()->getType() == singleType);
        BOOST_REQUIRE(Interpreter{}.execute("consume (new Example 1) 2", context));
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 2);
        BOOST_TEST(!Interpreter{}.execute("new Example true", context));
        BOOST_TEST(!Interpreter{}.execute("new Example 1 true", context));
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(parameterized_constructor_registration_validates_and_reuses_specializations) {
    Runtime runtime;
    Runtime other;
    auto calls = NomadIndex{0};
    const auto initializer = [&](const NomadString& name, const std::vector<const Type*>& arguments) {
        ++calls;
        const auto* classType = registerObjectType(runtime, name);
        BOOST_REQUIRE(runtime.addConstructor(classType->getId(), {
            .parameters = {{arguments[0]->getId(), "value", ""}},
            .function = [](VirtualMachine* machine) { machine->getResult().setObjectValue(new ConstructedObject); },
            .documentation = ""
        }).isValid());
    };
    BOOST_TEST(!runtime.registerNativeParameterizedConstructor("", 1, initializer));
    BOOST_TEST(!runtime.registerNativeParameterizedConstructor("Create", 0, initializer));
    BOOST_TEST(!runtime.registerNativeParameterizedConstructor("Create", 1, {}));
    BOOST_TEST(!runtime.registerNativeParameterizedConstructor("int", 1, initializer));
    BOOST_REQUIRE(runtime.registerNativeParameterizedConstructor("Create", 1, initializer));
    BOOST_TEST(!runtime.registerNativeParameterizedConstructor("Create", 1, initializer));
    BOOST_TEST(runtime.addConstructor(registerObjectType(runtime, "Create")->getId(), {
        .parameters = {},
        .function = [](VirtualMachine* machine) { machine->getResult().setObjectValue(new ConstructedObject); },
        .documentation = ""
    }).isInvalid());
    BOOST_TEST(!runtime.registerParameterizedType("Create", 1, [](Runtime* owningRuntime, TypeId id, const auto&) {
        return std::make_unique<ObjectType>(owningRuntime, id, "Collision", ObjectTypeMode::Closed);
    }));
    BOOST_TEST(!runtime.getParameterizedConstructorArgumentCount("Missing").has_value());
    BOOST_REQUIRE(runtime.getParameterizedConstructorArgumentCount("Create").has_value());
    BOOST_TEST(*runtime.getParameterizedConstructorArgumentCount("Create") == 1u);
    BOOST_TEST(runtime.getParameterizedConstructorName("Create", {runtime.getIntegerType()}) == "Create<int>");
    BOOST_TEST(runtime.getParameterizedConstructorName("Create", {runtime.getIntegerType()}) == "Create<int>");
    BOOST_TEST(calls == 1u);
    BOOST_TEST(runtime.getParameterizedConstructorName("Create", {other.getIntegerType()}).empty());
    BOOST_TEST(runtime.getParameterizedConstructorName("Create", {}).empty());
    BOOST_TEST(runtime.getParameterizedConstructorName("Create", {runtime.getVoidType()}).empty());
    BOOST_TEST(runtime.getParameterizedConstructorName("Create", {nullptr}).empty());
    BOOST_TEST(runtime.getParameterizedConstructorName("Missing", {runtime.getIntegerType()}).empty());
    BOOST_TEST(calls == 1u);
    const auto id = compileConstruction(runtime, "return new Create<int> 42");
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    result.freeObjectValue();
    BOOST_TEST(calls == 1u);
}

BOOST_AUTO_TEST_CASE(failed_parameterized_initialization_is_not_cached_as_success) {
    Runtime runtime;
    auto attempts = 0;
    BOOST_REQUIRE(runtime.registerNativeParameterizedConstructor(
        "Create", 1,
        [&](const NomadString& name, const std::vector<const Type*>&) {
            if (attempts++ == 0) {
                BOOST_REQUIRE(runtime.addConstructor(registerObjectType(runtime, name)->getId(), {
                    .parameters = {},
                    .function = [](VirtualMachine* machine) { machine->getResult().setObjectValue(new ConstructedObject); },
                    .documentation = ""
                }).isValid());
            }
            throw NomadException("Initialization failed");
        }
    ));
    for (auto attempt = 0; attempt < 2; ++attempt) {
        BOOST_CHECK_THROW(
            static_cast<void>(runtime.getParameterizedConstructorName("Create", {runtime.getIntegerType()})),
            NomadException
        );
    }
    BOOST_TEST(attempts == 2);
}

BOOST_AUTO_TEST_SUITE_END()
