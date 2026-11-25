// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <FailingArgumentCopyType.hpp>
#include <LanguageTestFixture.hpp>

#include <nomad/interpreter/Interpreter.hpp>
#include <nomad/interpreter/InterpreterCallFrame.hpp>

#include <nomad/script/Closure.hpp>
#include <nomad/script/ExecutionStateGuard.hpp>
#include <nomad/script/InstructionDump.hpp>
#include <nomad/script/NativeCallable.hpp>
#include <nomad/script/NativeProperty.hpp>
#include <nomad/script/OpCode.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/RuntimeValueGuard.hpp>
#include <nomad/script/ScriptObject.hpp>
#include <nomad/script/StructType.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/scope_exit.hpp>
#include <boost/test/unit_test.hpp>

#include <array>
#include <limits>
#include <new>
#include <optional>
#include <sstream>
#include <type_traits>
#include <utility>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

using namespace nomad;

static_assert(std::is_same_v<
    decltype(std::declval<VirtualMachine&>().executeFunction(std::declval<const ScriptFunction&>())),
    void
>);
static_assert(std::is_same_v<
    decltype(std::declval<VirtualMachine&>().executeClosure(std::declval<const Closure&>())),
    void
>);

namespace {

void executeInstruction(Runtime& runtime, VirtualMachine& machine, const NomadIndex address) {
    machine.jump(address + 1);
    runtime.getInstructions()[address].fn(&machine);
}

const ScriptFunction& registerFrameFunction(Runtime& runtime) {
    const auto id = runtime.registerScriptFunction("physicalStorageFrame", "storage-frame.nomad", "");
    auto* function = runtime.getScriptFunction(id);
    BOOST_REQUIRE(function != nullptr);
    function->setFunctionStart(0);
    function->setFunctionEnd(1);

    return *function;
}

#if defined(_MSC_VER) && defined(_DEBUG)
thread_local NomadIndex callAllocations = 0;
thread_local NomadIndex catalogAllocationRequests = 0;
thread_local std::optional<NomadIndex> rejectedCatalogAllocation;
thread_local bool callAllocationRejected = false;
_CRT_ALLOC_HOOK previousAllocationHook = nullptr;

int countCallAllocations(
    const int operation,
    void* data,
    const size_t size,
    const int blockType,
    const long request,
    const unsigned char* file,
    const int line
) {
    if (operation == _HOOK_ALLOC || operation == _HOOK_REALLOC) {
        ++callAllocations;
        // Debug proxies also allocate inside noexcept container constructors.
        if (rejectedCatalogAllocation.has_value() && size != sizeof(std::_Container_proxy)) {
            ++catalogAllocationRequests;
            if (*rejectedCatalogAllocation != 0 && catalogAllocationRequests == *rejectedCatalogAllocation) {
                callAllocationRejected = true;
                _CrtSetAllocHook(previousAllocationHook);

                return false;
            }
        }
    }

    return previousAllocationHook == nullptr ||
        previousAllocationHook(operation, data, size, blockType, request, file, line);
}
#endif

}

BOOST_AUTO_TEST_SUITE(virtual_machine_storage)

BOOST_AUTO_TEST_CASE(current_callable_belongs_to_the_frame_through_jumps_reentry_and_unwinding) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "raiseFrameFailure",
        [&](VirtualMachine* active) {
            const auto* failure = runtime.getScriptFunction(runtime.getScriptFunctionId("frameFailure"));
            BOOST_TEST(active->getCurrentCallable() == failure);
            active->jump(NOMAD_INVALID_INDEX);
            BOOST_TEST(active->getCurrentCallable() == failure);
            active->fault("Expected frame failure");
        },
        {},
        runtime.getVoidType(),
        ""
    ).isValid());
    fixture.addFunction("frameFirst", "return 1");
    fixture.addFunction("frameSecond", "return 2");
    fixture.addFunction("frameFailure", "raiseFrameFailure\nreturn 0");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    runtime.initializeScripts();
    const auto* first = runtime.getScriptFunction(runtime.getScriptFunctionId("frameFirst"));
    const auto* second = runtime.getScriptFunction(runtime.getScriptFunctionId("frameSecond"));
    const auto* failure = runtime.getScriptFunction(runtime.getScriptFunctionId("frameFailure"));
    VirtualMachine machine(&runtime, 64);
    machine.jump(first->getFunctionStart());
    BOOST_TEST(runtime.getScriptCallableAtInstructionIndex(machine.getInstructionIndex()) == first);
    BOOST_TEST(machine.getCurrentCallable() == nullptr);
    machine.enterScriptFrame(*first);
    const auto firstState = machine.captureExecutionState();
    {
        const ExecutionStateGuard state(machine);
        machine.jump(second->getFunctionStart());
        BOOST_TEST(runtime.getScriptCallableAtInstructionIndex(machine.getInstructionIndex()) == second);
        BOOST_TEST(machine.getCurrentCallable() == first);
        machine.enterNativeFrame();
        BOOST_TEST(machine.getCurrentCallable() == first);
    }

    const auto compiledEnd = first->getFunctionEnd();
    runtime.getScriptFunction(first->getId())->setFunctionEnd(NOMAD_INVALID_INDEX);
    BOOST_TEST(runtime.getScriptCallableAtInstructionIndex(first->getFunctionStart()) == nullptr);
    BOOST_TEST(machine.getCurrentCallable() == first);
    runtime.getScriptFunction(first->getId())->setFunctionEnd(compiledEnd);
    machine.enterScriptFrame(*second);
    BOOST_TEST(machine.getCurrentCallable() == second);
    machine.returnFunction(0);
    BOOST_TEST(machine.getCurrentCallable() == first);
    machine.restoreExecutionState(firstState);
    const Closure closure(second);
    machine.executeClosure(closure);
    BOOST_TEST(machine.getIntegerResult() == 2);
    BOOST_TEST(machine.getCurrentCallable() == first);
    BOOST_CHECK_THROW(machine.executeFunction(*failure), VirtualMachineException);
    BOOST_TEST(machine.getCurrentCallable() == first);
    const auto restored = machine.captureExecutionState();
    BOOST_TEST(restored.instructionIndex == firstState.instructionIndex);
    BOOST_TEST(restored.stackIndex == firstState.stackIndex);
    BOOST_TEST(restored.parameterIndex == firstState.parameterIndex);
    BOOST_TEST(restored.variableIndex == firstState.variableIndex);
    BOOST_TEST(restored.running == firstState.running);
    machine.returnFunction(0);
    machine.jump(second->getFunctionStart());
    BOOST_TEST(machine.getCurrentCallable() == nullptr);
}

BOOST_AUTO_TEST_CASE(constant_loads_copy_owned_values_for_all_expression_targets) {
    Runtime runtime;
    const auto* type = runtime.getStringType();
    RuntimeValue source(NomadString{"owned constant load"});
    const RuntimeValueGuard sourceOwner(*type, &source);
    const auto id = runtime.registerConstant("constantText", source, type);
    BOOST_REQUIRE(id.isValid());
    auto compiler = runtime.createCompiler();
    const auto resultOperand = compiler->addLoadConstant(id, ExpressionTarget::Result);
    const auto intermediateOperand = compiler->addLoadConstant(id, ExpressionTarget::Intermediate);
    const auto stackOperand = compiler->addLoadConstant(id, ExpressionTarget::Stack);
    const auto references = source.getObjectValue()->getReferenceCount();
    {
        VirtualMachine machine(&runtime, 32);
        const RuntimeValueGuard resultOwner(*type, &machine.getResult());
        const RuntimeValueGuard intermediateOwner(*type, &machine.getIntermediate());
        RuntimeValue borrowed;
        BOOST_REQUIRE(machine.getVariableValue(IdentifierType::Constant, id.id, borrowed));
        BOOST_TEST(borrowed.getObjectValue() == source.getObjectValue());
        BOOST_TEST(source.getObjectValue()->getReferenceCount() == references);
        executeInstruction(runtime, machine, resultOperand - 1);
        BOOST_TEST(machine.getResult().getObjectValue() == source.getObjectValue());
        BOOST_TEST(source.getObjectValue()->getReferenceCount() == references + 1);
        executeInstruction(runtime, machine, intermediateOperand - 1);
        BOOST_TEST(machine.getIntermediate().getObjectValue() == source.getObjectValue());
        BOOST_TEST(source.getObjectValue()->getReferenceCount() == references + 2);
        executeInstruction(runtime, machine, stackOperand - 1);
        auto stackValue = machine.popValue();
        const RuntimeValueGuard stackOwner(*type, &stackValue);
        BOOST_TEST(stackValue.getObjectValue() == source.getObjectValue());
        BOOST_TEST(source.getObjectValue()->getReferenceCount() == references + 3);
    }

    BOOST_TEST(source.getObjectValue()->getReferenceCount() == references);
}

BOOST_AUTO_TEST_CASE(dynamic_callbacks_transfer_owned_results_and_borrow_setter_inputs) {
    Runtime runtime;
    const auto* type = runtime.getStringType();
    RuntimeValue source(NomadString{"dynamic callback text"});
    const RuntimeValueGuard sourceOwner(*type, &source);
    RuntimeValue retained;
    const RuntimeValueGuard retainedOwner(*type, &retained);
    auto reads = 0;
    auto writes = 0;
    const auto id = runtime.registerDynamicVariable(
        "dynamicText",
        [&](VirtualMachine*, const RuntimeValue& value) {
            ++writes;
            type->assignValue(&value, &retained);
        },
        [&](VirtualMachine*, RuntimeValue& value) {
            ++reads;
            type->copyValue(&source, &value);
        },
        type,
        NomadDoc("")
    );
    BOOST_REQUIRE(id.isValid());
    auto compiler = runtime.createCompiler();
    const auto resultOperand = compiler->addLoadDynamicVariable(id, type, ExpressionTarget::Result);
    const auto intermediateOperand = compiler->addLoadDynamicVariable(id, type, ExpressionTarget::Intermediate);
    const auto stackOperand = compiler->addLoadDynamicVariable(id, type, ExpressionTarget::Stack);
    const auto setterOperand = type->emitStoreDynamicVariable(*compiler, id.id);
    const auto references = source.getObjectValue()->getReferenceCount();
    {
        VirtualMachine machine(&runtime, 32);
        const RuntimeValueGuard resultOwner(*type, &machine.getResult());
        const RuntimeValueGuard intermediateOwner(*type, &machine.getIntermediate());
        {
            RuntimeValue formatted;
            const RuntimeValueGuard formattedOwner(*type, &formatted);
            BOOST_REQUIRE(machine.getVariableValue(IdentifierType::DynamicVariable, id.id, formatted));
            BOOST_TEST(formatted.getObjectValue() == source.getObjectValue());
            BOOST_TEST(source.getObjectValue()->getReferenceCount() == references + 1);
        }

        BOOST_TEST(source.getObjectValue()->getReferenceCount() == references);
        executeInstruction(runtime, machine, resultOperand - 1);
        BOOST_TEST(machine.getResult().getObjectValue() == source.getObjectValue());
        BOOST_TEST(source.getObjectValue()->getReferenceCount() == references + 1);
        executeInstruction(runtime, machine, intermediateOperand - 1);
        BOOST_TEST(machine.getIntermediate().getObjectValue() == source.getObjectValue());
        BOOST_TEST(source.getObjectValue()->getReferenceCount() == references + 2);
        executeInstruction(runtime, machine, stackOperand - 1);
        auto stackValue = machine.popValue();
        const RuntimeValueGuard stackOwner(*type, &stackValue);
        BOOST_TEST(stackValue.getObjectValue() == source.getObjectValue());
        BOOST_TEST(source.getObjectValue()->getReferenceCount() == references + 3);
        executeInstruction(runtime, machine, setterOperand - 1);
        BOOST_TEST(retained.getObjectValue() == source.getObjectValue());
        BOOST_TEST(machine.getResult().getObjectValue() == source.getObjectValue());
        BOOST_TEST(source.getObjectValue()->getReferenceCount() == references + 4);
        BOOST_TEST(reads == 4);
        BOOST_TEST(writes == 1);
    }

    BOOST_TEST(source.getObjectValue()->getReferenceCount() == references + 1);
    type->freeValue(&retained);
    BOOST_TEST(source.getObjectValue()->getReferenceCount() == references);
}

BOOST_AUTO_TEST_CASE(factory_virtual_machines_keep_results_and_return_ownership_independent) {
    Runtime runtime;
    auto firstMachine = runtime.createVirtualMachine();
    auto secondMachine = runtime.createVirtualMachine();
    BOOST_TEST(firstMachine.get() != secondMachine.get());
    BOOST_TEST(firstMachine->getRuntime() == &runtime);
    BOOST_TEST(secondMachine->getRuntime() == &runtime);
    {
        auto compiler = runtime.createCompiler();
        CompilerContext context(compiler.get());
        compiler->registerSources(&context, {{{"main", {"main.nomad",
            "fun increment value:int\nreturn value + 1\nend\n"
            "fun echo value:string\nreturn value\nend\n"
            "return \"discarded\""}}}, {}});
        BOOST_REQUIRE(compiler->compileFunctions(&context));
    }

    const auto increment = runtime.getScriptFunctionId("increment");
    const auto echo = runtime.getScriptFunctionId("echo");
    secondMachine->setIntegerResult(19);
    RuntimeValue firstInteger;
    firstMachine->executeFunction(increment, {RuntimeValue{NomadInteger{41}}}, firstInteger);
    BOOST_TEST(firstInteger.getIntegerValue() == 42);
    BOOST_TEST(secondMachine->getIntegerResult() == 19);
    RuntimeValue secondInteger;
    secondMachine->executeFunction(increment, {RuntimeValue{NomadInteger{6}}}, secondInteger);
    BOOST_TEST(secondInteger.getIntegerValue() == 7);
    BOOST_TEST(firstMachine->getIntegerResult() == 42);

    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    RuntimeValue argument{NomadString{"owned argument"}};
    RuntimeValue firstResult;
    RuntimeValue secondResult;
    firstMachine->executeFunction(echo, {argument}, firstResult);
    BOOST_TEST(firstResult.getObjectValue() == argument.getObjectValue());
    BOOST_TEST(argument.getObjectValue()->getReferenceCount() == 2U);
    secondMachine->executeFunction(echo, {argument}, secondResult);
    BOOST_TEST(secondResult.getObjectValue() == argument.getObjectValue());
    BOOST_TEST(argument.getObjectValue()->getReferenceCount() == 3U);
    firstMachine->executeFunction(increment, {RuntimeValue{NomadInteger{8}}});
    firstMachine->executeFunction(runtime.getScriptFunctionId("main"));
    firstMachine.reset();
    argument.freeObjectValue();
    BOOST_TEST(firstResult.getStringValue() == "owned argument");
    BOOST_TEST(secondResult.getStringValue() == "owned argument");
    BOOST_TEST(firstResult.getObjectValue()->getReferenceCount() == 2U);
    secondMachine->executeFunction(echo, {firstResult});
    BOOST_TEST(firstResult.getObjectValue()->getReferenceCount() == 2U);
    secondMachine.reset();
    BOOST_TEST(secondResult.getStringValue() == "owned argument");
    runtime.getStringType()->freeValue(&firstResult);
    BOOST_TEST(secondResult.getObjectValue()->getReferenceCount() == 1U);
    runtime.getStringType()->freeValue(&secondResult);
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
}

BOOST_AUTO_TEST_CASE(host_execution_releases_results_on_discard_and_copy_failure) {
    auto copyCount = NomadIndex{0};
    auto failCopyAt = NomadIndex{0};
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto typeId = runtime.registerClass<nomad::test::FailingArgumentCopyType>(
        "ExecutionResult",
        ClassMode::Closed,
        copyCount,
        failCopyAt
    );
    BOOST_REQUIRE(typeId.isValid());
    const auto* type = runtime.getType(typeId).value();
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "makeExecutionResult",
        [](VirtualMachine* active) { active->getResult().setObjectValue(new NomadObject); },
        {},
        type,
        ""
    ).isValid());
    fixture.addFunction("returnExecutionResult", "return makeExecutionResult");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    runtime.initializeScripts();
    const auto functionId = runtime.getScriptFunctionId("returnExecutionResult");
    const Closure closure(runtime.getScriptFunction(functionId));
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (const auto useClosure : {false, true}) {
        VirtualMachine machine(&runtime, 128);
        const RuntimeValueGuard resultOwner(*type, &machine.getResult());
        copyCount = 0;
        failCopyAt = 1;
        if (useClosure) {
            machine.executeFunction(&closure, {});
        } else {
            machine.executeFunction(functionId);
        }

        BOOST_TEST(copyCount == 0U);
        BOOST_TEST(machine.getResult().getObjectValue() == nullptr);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
        for (const auto failure : {false, true}) {
            RuntimeValue returned;
            const RuntimeValueGuard returnedOwner(*type, &returned);
            copyCount = 0;
            failCopyAt = failure ? 1 : 0;
            if (failure) {
                if (useClosure) {
                    BOOST_CHECK_THROW(machine.executeFunction(&closure, {}, returned), NomadException);
                } else {
                    BOOST_CHECK_THROW(machine.executeFunction(functionId, {}, returned), NomadException);
                }

                BOOST_TEST(returned.getObjectValue() == nullptr);
                BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
            } else {
                if (useClosure) {
                    machine.executeFunction(&closure, {}, returned);
                } else {
                    machine.executeFunction(functionId, {}, returned);
                }

                BOOST_REQUIRE(returned.getObjectValue() != nullptr);
                BOOST_TEST(returned.getObjectValue()->getReferenceCount() == 1U);
            }

            BOOST_TEST(copyCount == 1U);
            BOOST_TEST(machine.getResult().getObjectValue() == nullptr);
        }
    }

    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
}

BOOST_AUTO_TEST_CASE(contiguous_parameter_blocks_and_local_views_keep_physical_order) {
    Runtime runtime;
    const auto& frame = registerFrameFunction(runtime);
    VirtualMachine machine(&runtime, 32);
    machine.pushInteger(5);
    machine.pushInteger(4);
    machine.pushInteger(1);
    machine.pushInteger(2);
    machine.pushInteger(3);
    machine.callScriptFunction(frame, 0);
    machine.reserveLocals(4);
    const auto& constMachine = machine;
    const auto point = machine.getParameterStorage(0, 3);
    BOOST_TEST(point[0].getIntegerValue() == 1);
    BOOST_TEST(point[2].getIntegerValue() == 3);
    BOOST_TEST(constMachine.getParameterStorage(3, 1)[0].getIntegerValue() == 4);
    BOOST_TEST(constMachine.getParameterStorage(4, 1)[0].getIntegerValue() == 5);
    BOOST_TEST(constMachine.getEnclosingParameterStorage(0, 3).data() == point.data());
    machine.getEnclosingParameterStorage(0, 3)[1].setIntegerValue(17);
    BOOST_TEST(point[1].getIntegerValue() == 17);
    const auto locals = machine.getLocalStorage(1, 3);
    locals[0].setIntegerValue(23);
    BOOST_TEST(constMachine.getLocalStorage(1, 3)[0].getIntegerValue() == 23);
    const auto* original = locals.data();
    machine.pushInteger(42);
    machine.callScriptFunction(frame, 0);
    machine.reserveLocals(2);
    BOOST_TEST(machine.getParameterStorage(0, 1)[0].getIntegerValue() == 42);
    BOOST_TEST(machine.getLocalStorage(0, 2).data() != original);
    machine.returnFunction(2);
    machine.pop1();
    BOOST_TEST(machine.getLocalStorage(1, 3).data() == original);
    BOOST_TEST(machine.getParameterStorage(0, 3).data() == point.data());
    machine.returnFunction(4);
    machine.popN(5);
}

BOOST_AUTO_TEST_CASE(native_calls_and_reentry_preserve_enclosing_script_storage) {
    Runtime runtime;
    const auto& frame = registerFrameFunction(runtime);
    VirtualMachine machine(&runtime, 32);
    machine.pushInteger(1);
    machine.pushInteger(2);
    machine.pushInteger(3);
    machine.callScriptFunction(frame, 0);
    machine.reserveLocals(1);
    machine.getLocalStorage(0, 1)[0].setIntegerValue(19);
    const auto* enclosing = machine.getEnclosingParameterStorage(0, 3).data();
    const auto id = runtime.registerNativeFunction("inspectStorage",
        [&](VirtualMachine* active) {
            BOOST_TEST(active->getParameterStorage(0, 1)[0].getIntegerValue() == 99);
            BOOST_TEST(std::as_const(*active).getEnclosingParameterStorage(0, 3).data() == enclosing);
            active->getEnclosingParameterStorage(0, 3)[0].setIntegerValue(7);
            active->pushInteger(8);
            active->callScriptFunction(frame, 0);
            active->reserveLocals(1);
            BOOST_TEST(active->getEnclosingParameterStorage(0, 1)[0].getIntegerValue() == 8);
            active->returnFunction(1);
            active->pop1();
            BOOST_TEST(active->getParameterStorage(0, 1)[0].getIntegerValue() == 99);
            BOOST_TEST(active->getEnclosingParameterStorage(0, 3).data() == enclosing);
        }, {defParameter("value", runtime.getIntegerType(), "")}, runtime.getVoidType(), "");
    BOOST_REQUIRE(id.isValid());
    machine.pushInteger(99);
    machine.callNativeFunction(id);
    machine.pop1();
    BOOST_TEST(machine.getParameterStorage(0, 3)[0].getIntegerValue() == 7);
    BOOST_TEST(machine.getLocalStorage(0, 1)[0].getIntegerValue() == 19);
    machine.returnFunction(1);
    machine.popN(3);
}

BOOST_AUTO_TEST_CASE(native_method_and_function_frames_restore_nested_receivers_and_parameters) {
    Runtime runtime;
    const auto ownerId = runtime.registerClass("GuardedNativeFrame", ClassMode::Closed);
    const auto* owner = runtime.getType(ownerId).value();
    RuntimeValue receiver;
    const RuntimeValueGuard receiverGuard(*owner, &receiver);
    receiver.setObjectValue(new NomadObject);
    auto failFunction = false;
    auto failMethod = false;
    const auto functionId = runtime.registerNativeFunction(
        "guardedNestedFunction",
        [&](VirtualMachine* active) {
            BOOST_TEST(active->getIntegerParameter(0) == 43);
            BOOST_TEST(active->captureExecutionState().receiver.getObjectValue() == nullptr);
            if (failFunction) {
                throw NomadException("Expected nested function failure");
            }
        },
        {defParameter("value", runtime.getIntegerType(), "")},
        runtime.getVoidType(),
        ""
    );
    BOOST_REQUIRE(functionId.isValid());
    const auto methodId = runtime.registerNativeClassMethod(ownerId, {
        .name = "guardedMethod",
        .parameters = {{runtime.getIntegerType()->getId(), "value", ""}},
        .returnTypeId = runtime.getVoidType()->getId(),
        .function = [&](VirtualMachine* active) {
            BOOST_TEST(active->getThis() == receiver.getObjectValue());
            BOOST_TEST(active->getIntegerParameter(0) == 41);
            const auto methodFrame = active->captureExecutionState();
            active->pushInteger(43);
            {
                BOOST_SCOPE_EXIT_ALL(&) {
                    active->pop1();
                };

                if (failFunction) {
                    BOOST_CHECK_THROW(active->callNativeFunction(functionId), NomadException);
                } else {
                    active->callNativeFunction(functionId);
                }
            }

            BOOST_TEST(active->captureExecutionState().parameterIndex == methodFrame.parameterIndex);
            BOOST_TEST(active->getIntegerParameter(0) == 41);
            BOOST_TEST(active->getThis() == receiver.getObjectValue());
            if (failMethod) {
                throw NomadException("Expected outer method failure");
            }
        }
    });
    BOOST_REQUIRE(methodId.isValid());
    for (const auto functionFailure : {false, true}) {
        failFunction = functionFailure;
        for (const auto methodFailure : {false, true}) {
            failMethod = methodFailure;
            VirtualMachine machine(&runtime, 64);
            machine.pushInteger(29);
            machine.enterNativeFrame(receiver);
            const auto enclosing = machine.captureExecutionState();
            machine.pushInteger(41);
            const auto preparedStack = machine.captureExecutionState().stackIndex;
            if (failMethod) {
                BOOST_CHECK_THROW(machine.callNativeMethod(methodId), NomadException);
            } else {
                machine.callNativeMethod(methodId);
            }

            const auto restored = machine.captureExecutionState();
            BOOST_TEST(restored.parameterIndex == enclosing.parameterIndex);
            BOOST_TEST(restored.receiver.getObjectValue() == receiver.getObjectValue());
            BOOST_TEST(restored.stackIndex == preparedStack);
            BOOST_TEST(receiver.getObjectValue()->getReferenceCount() == 1U);
            machine.pop1();
            BOOST_TEST(machine.getIntegerParameter(0) == 29);
            BOOST_TEST(machine.captureExecutionState().stackIndex == enclosing.stackIndex);
        }
    }
}

BOOST_AUTO_TEST_CASE(native_indexer_frames_restore_enclosing_receivers_and_parameters) {
    Runtime runtime;
    const auto ownerId = runtime.registerClass("GuardedNativeIndexer", ClassMode::Closed);
    const auto* owner = runtime.getType(ownerId).value();
    RuntimeValue enclosingReceiver;
    const RuntimeValueGuard enclosingGuard(*owner, &enclosingReceiver);
    enclosingReceiver.setObjectValue(new NomadObject);
    RuntimeValue indexedReceiver;
    const RuntimeValueGuard indexedGuard(*owner, &indexedReceiver);
    indexedReceiver.setObjectValue(new NomadObject);
    auto fail = false;
    const auto indexerId = runtime.registerNativeClassIndexer(ownerId, {
        .valueTypeId = runtime.getIntegerType()->getId(),
        .indexTypes = {runtime.getIntegerType()->getId()},
        .getter = [&](VirtualMachine* active) {
            BOOST_TEST(active->getThis() == indexedReceiver.getObjectValue());
            BOOST_TEST(active->getIntegerParameter(0) == 41);
            active->setIntegerResult(71);
            if (fail) {
                throw NomadException("Expected indexer read failure");
            }
        },
        .setter = [&](VirtualMachine* active) {
            BOOST_TEST(active->getThis() == indexedReceiver.getObjectValue());
            BOOST_TEST(active->getIntegerParameter(0) == 41);
            BOOST_TEST(active->getIntegerParameter(1) == 99);
            if (fail) {
                throw NomadException("Expected indexer write failure");
            }
        }
    });
    BOOST_REQUIRE(indexerId.isValid());
    for (const auto write : {false, true}) {
        for (const auto failure : {false, true}) {
            fail = failure;
            VirtualMachine machine(&runtime, 64);
            machine.pushInteger(29);
            machine.enterNativeFrame(enclosingReceiver);
            const auto enclosing = machine.captureExecutionState();
            if (write) {
                machine.pushInteger(99);
            }

            machine.pushInteger(41);
            const auto preparedStack = machine.captureExecutionState().stackIndex;
            if (fail) {
                BOOST_CHECK_THROW(machine.callNativeIndexer(indexerId, indexedReceiver, write), NomadException);
            } else {
                machine.callNativeIndexer(indexerId, indexedReceiver, write);
            }

            const auto restored = machine.captureExecutionState();
            BOOST_TEST(restored.parameterIndex == enclosing.parameterIndex);
            BOOST_TEST(restored.receiver.getObjectValue() == enclosingReceiver.getObjectValue());
            BOOST_TEST(restored.stackIndex == preparedStack);
            BOOST_TEST(indexedReceiver.getObjectValue()->getReferenceCount() == 1U);
            BOOST_TEST(enclosingReceiver.getObjectValue()->getReferenceCount() == 1U);
            machine.popN(write ? 2 : 1);
            BOOST_TEST(machine.getIntegerParameter(0) == 29);
            BOOST_TEST(machine.captureExecutionState().stackIndex == enclosing.stackIndex);
        }
    }
}

BOOST_AUTO_TEST_CASE(host_indexer_arguments_release_every_partial_copy_and_callback_failure) {
    auto copyCount = NomadIndex{0};
    auto failCopyAt = NomadIndex{0};
    Runtime runtime;
    const auto argumentId = runtime.registerClass<nomad::test::FailingArgumentCopyType>(
        "IndexerArgument", ClassMode::Closed, copyCount, failCopyAt
    );
    BOOST_REQUIRE(argumentId.isValid());
    const auto* argumentType = runtime.getType(argumentId).value();
    const auto ownerId = runtime.registerClass("ArgumentIndexer", ClassMode::Closed);
    BOOST_REQUIRE(ownerId.isValid());
    const auto* owner = runtime.getType(ownerId).value();
    RuntimeValue receiver;
    const RuntimeValueGuard receiverGuard(*owner, &receiver);
    receiver.setObjectValue(new NomadObject);
    RuntimeValue enclosingReceiver;
    const RuntimeValueGuard enclosingGuard(*owner, &enclosingReceiver);
    enclosingReceiver.setObjectValue(new NomadObject);
    std::array<RuntimeValue, 4> values;
    for (auto& value : values) {
        value.setObjectValue(new NomadObject);
    }

    BOOST_SCOPE_EXIT_ALL(&) {
        for (auto& value : values) {
            argumentType->freeValue(&value);
        }
    };

    auto failCallback = false;
    auto callbackCalls = 0;
    const auto inspectArguments = [&](VirtualMachine* active, const bool write) {
        ++callbackCalls;
        BOOST_TEST(active->getThis() == receiver.getObjectValue());
        for (auto index = NomadIndex{0}; index < (write ? 4U : 3U); ++index) {
            BOOST_TEST(active->getParameter(index).getObjectValue() == values[index].getObjectValue());
            BOOST_TEST(values[index].getObjectValue()->getReferenceCount() == 2U);
        }

        BOOST_TEST(receiver.getObjectValue()->getReferenceCount() == 2U);
        if (!write) {
            active->getResult().copyObjectValue(values[3]);
        }

        if (failCallback) {
            throw NomadException("Expected host indexer failure");
        }
    };
    const auto indexerId = runtime.registerNativeClassIndexer(ownerId, {
        .valueTypeId = argumentId,
        .indexTypes = {argumentId, argumentId, argumentId},
        .getter = [&](VirtualMachine* active) { inspectArguments(active, false); },
        .setter = [&](VirtualMachine* active) { inspectArguments(active, true); }
    });
    BOOST_REQUIRE(indexerId.isValid());
    for (const auto write : {false, true}) {
        const auto argumentCount = write ? NomadIndex{4} : NomadIndex{3};
        const std::vector<RuntimeValue> arguments(values.begin(), values.begin() + argumentCount);
        for (auto failure = NomadIndex{0}; failure <= argumentCount + 1; ++failure) {
            copyCount = 0;
            failCopyAt = failure <= argumentCount ? failure : 0;
            failCallback = failure == argumentCount + 1;
            callbackCalls = 0;
            VirtualMachine machine(&runtime, 64);
            machine.pushInteger(29);
            machine.enterNativeFrame(enclosingReceiver);
            const auto before = machine.captureExecutionState();
            if (failure == 0) {
                machine.runNativeIndexer(indexerId, arguments, receiver, write);
            } else {
                BOOST_CHECK_THROW(
                    machine.runNativeIndexer(indexerId, arguments, receiver, write),
                    NomadException
                );
            }

            BOOST_TEST(copyCount == (failCopyAt == 0 ? argumentCount : failCopyAt));
            BOOST_TEST(callbackCalls == (failCopyAt == 0 ? 1 : 0));
            const auto after = machine.captureExecutionState();
            BOOST_TEST(after.stackIndex == before.stackIndex);
            BOOST_TEST(after.parameterIndex == before.parameterIndex);
            BOOST_TEST(after.variableIndex == before.variableIndex);
            BOOST_TEST(after.receiver.getObjectValue() == enclosingReceiver.getObjectValue());
            BOOST_TEST(machine.getIntegerParameter(0) == 29);
            BOOST_TEST(receiver.getObjectValue()->getReferenceCount() == 1U);
            BOOST_TEST(enclosingReceiver.getObjectValue()->getReferenceCount() == 1U);
            if (!write && failCopyAt == 0) {
                BOOST_TEST(machine.getResult().getObjectValue() == values[3].getObjectValue());
                BOOST_TEST(values[3].getObjectValue()->getReferenceCount() == 2U);
            }

            argumentType->freeValue(&machine.getResult());
            for (const auto& value : values) {
                BOOST_TEST(value.getObjectValue()->getReferenceCount() == 1U);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(indexer_opcodes_release_arguments_and_only_discard_failed_read_results) {
    Runtime runtime;
    const auto ownerId = runtime.registerClass("OpcodeIndexer", ClassMode::Closed);
    BOOST_REQUIRE(ownerId.isValid());
    const auto* owner = runtime.getType(ownerId).value();
    RuntimeValue receiver;
    const RuntimeValueGuard receiverGuard(*owner, &receiver);
    receiver.setObjectValue(new NomadObject);
    RuntimeValue enclosingReceiver;
    const RuntimeValueGuard enclosingGuard(*owner, &enclosingReceiver);
    enclosingReceiver.setObjectValue(new NomadObject);
    RuntimeValue argument{NomadString{"index"}};
    const RuntimeValueGuard argumentGuard(*runtime.getStringType(), &argument);
    RuntimeValue output{NomadString{"value"}};
    const RuntimeValueGuard outputGuard(*runtime.getStringType(), &output);
    auto fail = false;
    auto callbackCalls = 0;
    const auto inspectArguments = [&](VirtualMachine* active, const bool write) {
        ++callbackCalls;
        BOOST_TEST(active->getThis() == receiver.getObjectValue());
        for (auto index = NomadIndex{0}; index < 3; ++index) {
            BOOST_TEST(active->getParameter(index).getObjectValue() == argument.getObjectValue());
        }

        if (write) {
            BOOST_TEST(active->getParameter(3).getObjectValue() == output.getObjectValue());
        } else {
            active->getResult().copyObjectValue(output);
        }

        if (fail) {
            throw NomadException("Expected opcode indexer failure");
        }
    };
    const auto stringId = runtime.getStringType()->getId();
    const auto indexerId = runtime.registerNativeClassIndexer(ownerId, {
        .valueTypeId = stringId,
        .indexTypes = {stringId, stringId, stringId},
        .getter = [&](VirtualMachine* active) { inspectArguments(active, false); },
        .setter = [&](VirtualMachine* active) { inspectArguments(active, true); }
    });
    BOOST_REQUIRE(indexerId.isValid());
    auto compiler = runtime.createCompiler();
    const auto readAddress = compiler->addOpCode(OpCodes::op_native_index_get);
    compiler->addId(indexerId.id);
    const auto writeAddress = compiler->addOpCode(OpCodes::op_native_index_set);
    compiler->addId(indexerId.id);
    for (const auto write : {false, true}) {
        for (const auto failure : {false, true}) {
            fail = failure;
            callbackCalls = 0;
            VirtualMachine machine(&runtime, 64);
            machine.pushInteger(29);
            machine.enterNativeFrame(enclosingReceiver);
            const auto before = machine.captureExecutionState();
            machine.pushThis();
            machine.pushObjectValue(receiver);
            if (write) {
                machine.pushObjectValue(output);
            }

            for (auto index = 0; index < 3; ++index) {
                machine.pushObjectValue(argument);
            }

            const auto address = write ? writeAddress : readAddress;
            if (fail) {
                BOOST_CHECK_THROW(executeInstruction(runtime, machine, address), NomadException);
            } else {
                executeInstruction(runtime, machine, address);
            }

            BOOST_TEST(callbackCalls == 1);
            const auto after = machine.captureExecutionState();
            BOOST_TEST(after.stackIndex == before.stackIndex);
            BOOST_TEST(after.parameterIndex == before.parameterIndex);
            BOOST_TEST(after.variableIndex == before.variableIndex);
            BOOST_TEST(after.receiver.getObjectValue() == enclosingReceiver.getObjectValue());
            BOOST_TEST(machine.getIntegerParameter(0) == 29);
            BOOST_TEST(receiver.getObjectValue()->getReferenceCount() == 1U);
            BOOST_TEST(enclosingReceiver.getObjectValue()->getReferenceCount() == 1U);
            BOOST_TEST(argument.getObjectValue()->getReferenceCount() == 1U);
            if (!write && !fail) {
                BOOST_TEST(machine.getResult().getObjectValue() == output.getObjectValue());
                BOOST_TEST(output.getObjectValue()->getReferenceCount() == 2U);
                runtime.getStringType()->freeValue(&machine.getResult());
            } else {
                BOOST_TEST(machine.getResult().getObjectValue() == nullptr);
            }

            BOOST_TEST(output.getObjectValue()->getReferenceCount() == 1U);
        }
    }
}

BOOST_AUTO_TEST_CASE(native_struct_creation_restores_hidden_slots_frames_and_owned_registers) {
    enum class Failure {
        None = 1,
        Initializer,
        Constructor
    };

    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    auto failure = Failure::None;
    auto defaultCalls = 0;
    auto valueConstructorCalls = 0;
    auto emptyConstructorCalls = 0;
    BOOST_REQUIRE(runtime.registerNativeFunction("guardedDefault", [&](VirtualMachine* active) {
        ++defaultCalls;
        if (failure == Failure::Initializer) {
            throw NomadException("Expected struct initializer failure");
        }

        active->setIntegerResult(17);
    }, {}, runtime.getIntegerType(), "").isValid());
    fixture.addFunction("guardedTypes",
        "struct GuardedNativeValue\nnumber = guardedDefault\nend\n"
        "struct GuardedNativeEmpty\nend\nreturn 0");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto* valueType = runtime.getTypeByName("GuardedNativeValue")->asStructType();
    const auto* emptyType = runtime.getTypeByName("GuardedNativeEmpty")->asStructType();
    BOOST_REQUIRE(valueType->getSize() == 1U);
    BOOST_REQUIRE(emptyType->getSize() == 0U);
    BOOST_REQUIRE(valueType->getInitializer() != nullptr);
    BOOST_REQUIRE(emptyType->getInitializer() != nullptr);
    const auto valueConstructor = runtime.registerNativeStructConstructor(valueType->getId(), {
        .parameters = {{runtime.getIntegerType()->getId(), "value", ""}},
        .function = [&](VirtualMachine* active) {
            ++valueConstructorCalls;
            BOOST_TEST(active->getIntegerParameter(0) == 23);
            BOOST_TEST(active->captureExecutionState().receiver.getObjectValue() == nullptr);
            BOOST_TEST(active->getStorageParameter(1, 0).getIntegerValue() == 17);
            active->getStorageParameter(1, 0).setIntegerValue(23);
            active->setIntegerResult(991);
            active->setIntegerIntermediate(997);
            if (failure == Failure::Constructor) {
                throw NomadException("Expected native struct failure");
            }
        }
    });
    const auto emptyConstructor = runtime.registerNativeStructConstructor(emptyType->getId(), {
        .parameters = {{runtime.getIntegerType()->getId(), "value", ""}},
        .function = [&](VirtualMachine* active) {
            ++emptyConstructorCalls;
            BOOST_TEST(active->getIntegerParameter(0) == 23);
            BOOST_TEST(active->captureExecutionState().receiver.getObjectValue() == nullptr);
            active->setIntegerResult(991);
            active->setIntegerIntermediate(997);
            if (failure == Failure::Constructor) {
                throw NomadException("Expected empty native struct failure");
            }
        }
    });
    BOOST_REQUIRE(valueConstructor.isValid() && emptyConstructor.isValid());
    runtime.initializeScripts();
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    {
        RuntimeValue receiver{NomadString{"enclosing receiver"}};
        const RuntimeValueGuard receiverGuard(*runtime.getStringType(), &receiver);
        RuntimeValue savedResult{NomadString{"enclosing result"}};
        const RuntimeValueGuard resultGuard(*runtime.getStringType(), &savedResult);
        RuntimeValue savedIntermediate{NomadString{"enclosing intermediate"}};
        const RuntimeValueGuard intermediateGuard(*runtime.getStringType(), &savedIntermediate);
        for (const auto empty : {false, true}) {
            for (const auto stage : {Failure::None, Failure::Initializer, Failure::Constructor}) {
                if (empty && stage == Failure::Initializer) {
                    continue;
                }

                failure = stage;
                const auto* type = empty ? emptyType : valueType;
                const auto constructor = empty ? emptyConstructor : valueConstructor;
                std::array<RuntimeValue, 3> storage{
                    RuntimeValue{NomadInteger{37}},
                    RuntimeValue{NomadInteger{83}},
                    RuntimeValue{NomadInteger{41}}
                };
                auto* destination = empty ? nullptr : storage.data() + 1;
                const RuntimeValueGuard storageGuard(*type, destination);
                VirtualMachine machine(&runtime, 128);
                machine.pushInteger(29);
                machine.enterNativeFrame(receiver);
                machine.setResult(savedResult);
                machine.setIntermediate(savedIntermediate);
                if (!empty) {
                    machine.pushStorageAddress(destination);
                }

                machine.pushInteger(23);
                const auto before = machine.captureExecutionState();
                if (stage == Failure::None) {
                    machine.createNativeStruct(destination, constructor);
                } else {
                    BOOST_CHECK_THROW(machine.createNativeStruct(destination, constructor), NomadException);
                }

                const auto after = machine.captureExecutionState();
                BOOST_TEST(after.stackIndex == before.stackIndex);
                BOOST_TEST(after.parameterIndex == before.parameterIndex);
                BOOST_TEST(after.receiver.getObjectValue() == receiver.getObjectValue());
                BOOST_TEST(machine.getResult().getObjectValue() == savedResult.getObjectValue());
                BOOST_TEST(machine.getIntermediate().getObjectValue() == savedIntermediate.getObjectValue());
                BOOST_TEST(savedResult.getObjectValue()->getReferenceCount() == 1U);
                BOOST_TEST(savedIntermediate.getObjectValue()->getReferenceCount() == 1U);
                BOOST_TEST(receiver.getObjectValue()->getReferenceCount() == 1U);
                BOOST_TEST(storage.front().getIntegerValue() == 37);
                BOOST_TEST(storage.back().getIntegerValue() == 41);
                BOOST_TEST(storage[1].getIntegerValue() ==
                    (empty ? 83 : stage == Failure::Initializer ? 0 : 23));
                machine.popN(empty ? 1 : 2);
                BOOST_TEST(machine.getIntegerParameter(0) == 29);
            }
        }
    }

    BOOST_TEST(defaultCalls == 3);
    BOOST_TEST(valueConstructorCalls == 2);
    BOOST_TEST(emptyConstructorCalls == 2);
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
}

BOOST_AUTO_TEST_CASE(constructor_opcodes_preserve_arguments_and_release_failed_struct_destinations) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    auto fail = false;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "rollbackConstructorProbe",
        [&](VirtualMachine*) {
            if (fail) {
                throw NomadException("Expected constructor rollback failure");
            }
        },
        {},
        runtime.getVoidType(),
        ""
    ).isValid());
    fixture.addFunction("rollbackConstructorTypes",
        "class ScriptRollbackObject\ntext = \"\"\n"
        "init input:string\nthis.text = input\nrollbackConstructorProbe\nend\nend\n"
        "struct ScriptRollbackStruct\ntext = \"\"\n"
        "init input:string\nthis.text = input\nrollbackConstructorProbe\nend\nend\n"
        "struct ScriptRollbackEmpty\n"
        "init input:string\nrollbackConstructorProbe\nend\nend\n"
        "class NativeRollbackObject\ntext = \"\"\nend\n"
        "struct NativeRollbackStruct\ntext = \"\"\nend\n"
        "struct NativeRollbackEmpty\nend\nreturn 0");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto* objectType = runtime.getTypeByName("NativeRollbackObject")->asClassType();
    const auto* structType = runtime.getTypeByName("NativeRollbackStruct")->asStructType();
    const auto* emptyType = runtime.getTypeByName("NativeRollbackEmpty")->asStructType();
    const auto stringId = runtime.getStringType()->getId();
    const auto nativeObjectId = runtime.registerNativeClassConstructor(objectType->getId(), {
        .parameters = {{stringId, "input", ""}},
        .function = [&](VirtualMachine* active) {
            auto* object = new ScriptObject(objectType);
            active->getResult().setObjectValue(object);
            runtime.getStringType()->copyValue(&active->getParameter(0), &object->getStorageValue(0));
            if (fail) {
                throw NomadException("Expected constructor rollback failure");
            }
        }
    });
    const auto nativeStructId = runtime.registerNativeStructConstructor(structType->getId(), {
        .parameters = {{stringId, "input", ""}},
        .function = [&](VirtualMachine* active) {
            runtime.getStringType()->copyValue(
                &active->getParameter(0), &active->getStorageParameter(1, 0)
            );
            if (fail) {
                throw NomadException("Expected constructor rollback failure");
            }
        }
    });
    const auto nativeEmptyId = runtime.registerNativeStructConstructor(emptyType->getId(), {
        .parameters = {{stringId, "input", ""}},
        .function = [&](VirtualMachine*) {
            if (fail) {
                throw NomadException("Expected constructor rollback failure");
            }
        }
    });
    BOOST_REQUIRE(nativeObjectId.isValid() && nativeStructId.isValid() && nativeEmptyId.isValid());
    runtime.initializeScripts();
    auto compiler = runtime.createCompiler();
    const std::array constructors{
        runtime.getTypeByName("ScriptRollbackObject")->asCompositeType()->getConstructors()[0]->getId(),
        runtime.getTypeByName("ScriptRollbackStruct")->asCompositeType()->getConstructors()[0]->getId(),
        runtime.getTypeByName("ScriptRollbackEmpty")->asCompositeType()->getConstructors()[0]->getId(),
        nativeObjectId,
        nativeStructId,
        nativeEmptyId
    };
    for (const auto constructorId : constructors) {
        const auto* constructor = runtime.getConstructor(constructorId);
        const auto* type = constructor->getConstructedType();
        const auto structure = type->isStruct();
        const auto native = constructor->asNativeConstructor() != nullptr;
        const auto layout = constructor->getCallStorageLayout();
        const auto address = compiler->addOpCode(structure
            ? (native ? OpCodes::op_create_native_struct : OpCodes::op_create_script_struct)
            : (native ? OpCodes::op_create_native_object : OpCodes::op_create_script_object));
        compiler->addId(constructorId.id);
        if (structure) {
            compiler->addIndex(layout.receiverOffset.value_or(NOMAD_INVALID_INDEX));
        }

        for (const auto failure : {false, true}) {
            fail = failure;
            RuntimeValue argument{NomadString{"constructor argument"}};
            const RuntimeValueGuard argumentGuard(*runtime.getStringType(), &argument);
            std::array<RuntimeValue, 3> destination;
            destination.front().setIntegerValue(37);
            destination.back().setIntegerValue(41);
            const auto objectsBefore = NomadObject::getLiveObjectCount();
            VirtualMachine machine(&runtime, 64);
            machine.pushInteger(29);
            machine.pushThis();
            if (layout.receiverOffset.has_value()) {
                machine.pushStorageAddress(&destination[1]);
            }

            machine.pushObjectValue(argument);
            const auto prepared = machine.captureExecutionState();
            if (fail) {
                BOOST_CHECK_EXCEPTION(
                    executeInstruction(runtime, machine, address),
                    NomadException,
                    [](const auto& error) {

                        return NomadString{error.what()} == "Expected constructor rollback failure";
                    }
                );
                BOOST_TEST(argument.getObjectValue()->getReferenceCount() == 2U);
                BOOST_TEST(machine.peekStack(0).getObjectValue() == argument.getObjectValue());
                if (structure && type->getSize() != 0) {
                    BOOST_TEST(destination[1].getObjectValue() == nullptr);
                }
            } else {
                executeInstruction(runtime, machine, address);
                BOOST_TEST(machine.peekStack(0).getObjectValue() == argument.getObjectValue());
                const auto empty = structure && type->getSize() == 0;
                BOOST_TEST(argument.getObjectValue()->getReferenceCount() == (empty ? 2U : 3U));
                if (structure) {
                    if (!empty) {
                        BOOST_TEST(destination[1].getObjectValue() == argument.getObjectValue());
                    }

                    type->freeValue(empty ? nullptr : &destination[1]);
                } else {
                    BOOST_REQUIRE(machine.getResult().getObjectValue() != nullptr);
                    type->freeValue(&machine.getResult());
                }

            }

            auto copiedArgument = machine.popValue();
            runtime.getStringType()->freeValue(&copiedArgument);

            if (layout.receiverOffset.has_value()) {
                machine.pop1();
            }

            machine.popThis();
            BOOST_TEST(machine.peekStack(0).getIntegerValue() == 29);
            const auto restored = machine.captureExecutionState();
            BOOST_TEST(restored.parameterIndex == prepared.parameterIndex);
            BOOST_TEST(restored.variableIndex == prepared.variableIndex);
            BOOST_TEST(restored.receiver.getObjectValue() == prepared.receiver.getObjectValue());
            BOOST_TEST(restored.stackIndex == 1U);
            BOOST_TEST(argument.getObjectValue()->getReferenceCount() == 1U);
            BOOST_TEST(destination.front().getIntegerValue() == 37);
            BOOST_TEST(destination.back().getIntegerValue() == 41);
            BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
        }
    }
}

BOOST_AUTO_TEST_CASE(prepared_and_raw_calls_complete_and_release_owned_arguments) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    fixture.addFunction("completeOwnedFrame", "params text:string\nreturn text");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    runtime.initializeScripts();
    BOOST_TEST(!runtime.getInstructionId("op_yield").isValid());
    const auto* function = runtime.getScriptFunction(runtime.getScriptFunctionId("completeOwnedFrame"));
    RuntimeValue argument{NomadString{"owned synchronous argument"}};
    const RuntimeValueGuard argumentOwner(*runtime.getStringType(), &argument);
    for (const auto prepared : {false, true}) {
        VirtualMachine machine(&runtime, 128);
        const auto before = machine.captureExecutionState();
        {
            std::optional<RuntimeValueGuard> retainedArgument;
            if (prepared) {
                machine.pushObjectValue(argument);
                retainedArgument.emplace(*runtime.getStringType(), machine.getStackAddress(0));
                machine.executeFunction(*function);
            } else {
                machine.run(function, {argument});
            }

            const RuntimeValueGuard resultOwner(*runtime.getStringType(), &machine.getResult());
            BOOST_TEST(!machine.isRunning());
            BOOST_TEST(machine.getResult().getObjectValue() == argument.getObjectValue());
            BOOST_TEST(machine.getResult().getStringValue() == "owned synchronous argument");
            BOOST_TEST(argument.getObjectValue()->getReferenceCount() == (prepared ? 3U : 2U));
            BOOST_TEST(machine.captureExecutionState().stackIndex == before.stackIndex + (prepared ? 1U : 0U));
        }

        machine.restoreExecutionState(before);
        BOOST_TEST(argument.getObjectValue()->getReferenceCount() == 1U);
    }

}

BOOST_AUTO_TEST_CASE(script_run_failure_after_stop_restores_state_and_releases_owned_arguments) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    BOOST_REQUIRE(runtime.registerNativeFunction("stopThenFailFrame", [](VirtualMachine* active) {
        active->setIntegerIntermediate(55);
        active->stop(RuntimeValue{NomadInteger{37}});
        throw NomadException("Expected stopped script failure");
    }, {}, runtime.getVoidType(), "").isValid());
    fixture.addFunction("failOwnedFrame", "params text:string\nstopThenFailFrame\nreturn 0");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    runtime.initializeScripts();
    const auto* function = runtime.getScriptFunction(runtime.getScriptFunctionId("failOwnedFrame"));
    RuntimeValue argument{NomadString{"owned failure argument"}};
    const RuntimeValueGuard argumentOwner(*runtime.getStringType(), &argument);
    RuntimeValue receiver{NomadString{"borrowed enclosing receiver"}};
    const RuntimeValueGuard receiverOwner(*runtime.getStringType(), &receiver);
    for (const auto initiallyRunning : {false, true}) {
        VirtualMachine machine(&runtime, 128);
        machine.pushInteger(29);
        machine.enterNativeFrame(receiver);
        auto before = machine.captureExecutionState();
        before.running = initiallyRunning;
        machine.restoreExecutionState(before);
        BOOST_CHECK_THROW(machine.run(function, {argument}), NomadException);
        const auto after = machine.captureExecutionState();
        BOOST_TEST(after.instructionIndex == before.instructionIndex);
        BOOST_TEST(after.stackIndex == before.stackIndex);
        BOOST_TEST(after.parameterIndex == before.parameterIndex);
        BOOST_TEST(after.variableIndex == before.variableIndex);
        BOOST_TEST(after.receiver.getObjectValue() == receiver.getObjectValue());
        BOOST_TEST(after.running == before.running);
        BOOST_TEST(machine.getIntegerParameter(0) == 29);
        BOOST_TEST(machine.getIntegerResult() == 37);
        BOOST_TEST(machine.getIntegerIntermediate() == 55);
        BOOST_TEST(argument.getObjectValue()->getReferenceCount() == 1U);
        BOOST_TEST(receiver.getObjectValue()->getReferenceCount() == 1U);
        machine.pop1();
    }
}

BOOST_AUTO_TEST_CASE(empty_views_do_not_require_storage_or_address_arithmetic) {
    Runtime runtime;
    VirtualMachine machine(&runtime, 16);
    const auto maximum = std::numeric_limits<NomadIndex>::max();
    const auto& constMachine = machine;
    BOOST_TEST(machine.getLocalStorage(maximum, 0).empty());
    BOOST_TEST(machine.getParameterStorage(maximum, 0).empty());
    BOOST_TEST(machine.getEnclosingParameterStorage(maximum, 0).empty());
    BOOST_TEST(constMachine.getLocalStorage(maximum, 0).empty());
    BOOST_TEST(constMachine.getParameterStorage(maximum, 0).empty());
    BOOST_TEST(constMachine.getEnclosingParameterStorage(maximum, 0).empty());
    machine.pushInteger(1);
    const auto& frame = registerFrameFunction(runtime);
    machine.callScriptFunction(frame, 0);
    for (const auto offset : {NomadIndex{0}, NomadIndex{1}, maximum}) {
        BOOST_TEST(machine.getLocalStorage(offset, 0).empty());
        BOOST_TEST(machine.getParameterStorage(offset, 0).empty());
        BOOST_TEST(machine.getEnclosingParameterStorage(offset, 0).empty());
        BOOST_TEST(constMachine.getLocalStorage(offset, 0).empty());
        BOOST_TEST(constMachine.getParameterStorage(offset, 0).empty());
        BOOST_TEST(constMachine.getEnclosingParameterStorage(offset, 0).empty());
    }
    BOOST_TEST(machine.getParameterStorage(0, 1)[0].getIntegerValue() == 1);
    machine.returnFunction(0);
    machine.pop1();
}

BOOST_AUTO_TEST_CASE(logical_local_views_preserve_complete_values_ownership_and_native_reentry) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    auto visits = 0;
    auto nestedVisits = 0;
    auto emptyVisits = 0;
    BOOST_REQUIRE(runtime.registerNativeFunction("inspectNestedLogicalLocal", [&](VirtualMachine* active) {
        const auto* callable = active->getCurrentCallable();
        BOOST_REQUIRE(callable != nullptr);
        BOOST_TEST(callable->getName() == "nestedLogicalLocal");
        const auto id = callable->getVariableId("prefix");
        const auto storage = active->getFunctionVariableStorage(id);
        BOOST_REQUIRE_EQUAL(storage.size(), 1U);
        BOOST_TEST(storage[0].getIntegerValue() == 19);
        BOOST_TEST(std::as_const(*active).getFunctionVariableStorage(id).data() == storage.data());
        active->setFunctionVariableValue(id, RuntimeValue{NomadInteger{20}});
        BOOST_TEST(active->getFunctionVariableValue(id).getIntegerValue() == 20);
        ++nestedVisits;
    }, {}, runtime.getVoidType(), "").isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction("inspectEmptyLogicalLocal", [&](VirtualMachine* active) {
        const auto* callable = active->getCurrentCallable();
        BOOST_REQUIRE(callable != nullptr);
        BOOST_TEST(callable->getName() == "emptyLogicalLocal");
        BOOST_TEST(callable->getVariableStorageSize() == 0U);
        const auto id = callable->getVariableId("empty");
        BOOST_TEST(active->getFunctionVariableStorage(id).empty());
        BOOST_TEST(active->getFunctionVariableStorage(id).data() == nullptr);
        BOOST_TEST(std::as_const(*active).getFunctionVariableStorage(id).empty());
        BOOST_TEST(std::as_const(*active).getFunctionVariableStorage(id).data() == nullptr);
        ++emptyVisits;
    }, {}, runtime.getVoidType(), "").isValid());
    const auto inspectId = runtime.registerNativeFunction("inspectLogicalLocals", [&](VirtualMachine* active) {
        const auto* callable = active->getCurrentCallable();
        BOOST_REQUIRE(callable != nullptr);
        BOOST_TEST(active->getIntegerParameter(0) == 123);
        const auto prefixId = callable->getVariableId("prefix");
        const auto emptyId = callable->getVariableId("empty");
        const auto singleId = callable->getVariableId("single");
        const auto outerId = callable->getVariableId("outer");
        const auto suffixId = callable->getVariableId("suffix");
        const auto prefix = active->getFunctionVariableStorage(prefixId);
        const auto empty = active->getFunctionVariableStorage(emptyId);
        const auto single = active->getFunctionVariableStorage(singleId);
        const auto outer = active->getFunctionVariableStorage(outerId);
        const auto suffix = active->getFunctionVariableStorage(suffixId);
        BOOST_REQUIRE_EQUAL(prefix.size(), 1U);
        BOOST_REQUIRE_EQUAL(single.size(), 1U);
        BOOST_REQUIRE_EQUAL(outer.size(), 3U);
        BOOST_REQUIRE_EQUAL(suffix.size(), 1U);
        BOOST_TEST(prefix[0].getIntegerValue() == 5);
        BOOST_TEST(empty.empty());
        BOOST_TEST(empty.data() == nullptr);
        BOOST_TEST(single[0].getIntegerValue() == 7);
        BOOST_TEST(outer[0].getIntegerValue() == 11);
        BOOST_TEST(outer[1].getStringValue() == "logical local");
        BOOST_REQUIRE(outer[2].getObjectValue() != nullptr);
        BOOST_TEST(suffix[0].getIntegerValue() == 9);
        const auto stringReferences = outer[1].getObjectValue()->getReferenceCount();
        const auto objectReferences = outer[2].getObjectValue()->getReferenceCount();
        for (const auto id : {prefixId, emptyId, singleId, outerId, suffixId}) {
            const auto storage = active->getFunctionVariableStorage(id);
            const auto constant = std::as_const(*active).getFunctionVariableStorage(id);
            BOOST_TEST(storage.size() == callable->getVariableType(id)->getSize());
            BOOST_TEST(constant.size() == storage.size());
            BOOST_TEST(constant.data() == storage.data());
            BOOST_TEST(storage.data() == active->getLocalStorage(
                callable->getVariableOffset(id), callable->getVariableType(id)->getSize()).data());
        }
        BOOST_TEST(outer[1].getObjectValue()->getReferenceCount() == stringReferences);
        BOOST_TEST(outer[2].getObjectValue()->getReferenceCount() == objectReferences);
        const Interpreter interpreter(*active);
        InterpreterContext frame(interpreter, *callable);
        BOOST_TEST(frame.getVariableStorage("outer").data() == outer.data());
        BOOST_TEST(frame.getMutableVariableStorageById(outerId).data() == outer.data());
        const InterpreterValue snapshot(callable->getVariableType(outerId), outer);
        single[0].setIntegerValue(13);
        outer[0].setIntegerValue(23);
        frame.setVariable("suffix", runtime.getIntegerType(), RuntimeValue{NomadInteger{42}});
        BOOST_TEST(snapshot.getStorage()[0].getIntegerValue() == 11);
        BOOST_TEST(suffix[0].getIntegerValue() == 42);
        const InterpreterValue argument(runtime.getIntegerType(), RuntimeValue{NomadInteger{19}});
        const auto nested = interpreter.runFunction(runtime.getScriptFunctionId("nestedLogicalLocal"), {&argument, 1});
        BOOST_TEST(nested.getValue().getIntegerValue() == 20);
        BOOST_TEST(interpreter.runFunction(runtime.getScriptFunctionId("emptyLogicalLocal"), {}).getValue().getIntegerValue() == 0);
        BOOST_TEST(active->getCurrentCallable() == callable);
        BOOST_TEST(active->getIntegerParameter(0) == 123);
        BOOST_TEST(active->getFunctionVariableStorage(outerId).data() == outer.data());
        BOOST_TEST(frame.getVariableStorage("outer")[0].getIntegerValue() == 23);
        BOOST_TEST(active->getFunctionVariableValue(prefixId).getIntegerValue() == 5);
        ++visits;
    }, {defParameter("marker", runtime.getIntegerType(), "")}, runtime.getVoidType(), "");
    BOOST_REQUIRE(inspectId.isValid());
    fixture.addFunction("logicalLocalTypes",
        "class LogicalLocalOwner\nnumber = 3\nend\n"
        "struct LogicalLocalEmpty\nend\n"
        "struct LogicalLocalSingle\nnumber = 7\nend\n"
        "struct LogicalLocalInner\nnumber = 11\ntext = \"logical local\"\nend\n"
        "struct LogicalLocalOuter\ninner = new LogicalLocalInner\nowner = new LogicalLocalOwner\nend\nreturn 0");
    fixture.addFunction("nestedLogicalLocal", "params expected:int\nprefix = expected\ninspectNestedLogicalLocal\nreturn prefix");
    fixture.addFunction("emptyLogicalLocal", "empty = new LogicalLocalEmpty\ninspectEmptyLogicalLocal\nreturn 0");
    fixture.addFunction("main",
        "prefix = 5\nempty = new LogicalLocalEmpty\nsingle = new LogicalLocalSingle\n"
        "outer = new LogicalLocalOuter\nsuffix = 9\ninspectLogicalLocals 123\n"
        "assert prefix == 5\nassert single.number == 13\n"
        "assert outer.inner.number == 23\nassert outer.inner.text == \"logical local\"\n"
        "assert outer.owner.number == 3\nreturn suffix");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 42);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
    BOOST_TEST(visits == 3);
    BOOST_TEST(nestedVisits == 3);
    BOOST_TEST(emptyVisits == 3);
}

BOOST_AUTO_TEST_CASE(address_opcodes_use_full_width_offsets_and_folded_parameter_distances) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    const auto wideOffset = toNomadIndex(NOMAD_ID_MAX) + 1;
    const auto localAddress = compiler->addOpCode(OpCodes::op_local_address_push);
    const auto functionId = runtime.registerScriptFunction("addressEmission", "address.nomad", "");
    auto* function = runtime.getScriptFunction(functionId);
    function->setReturnType(runtime.getVoidType());
    function->setFunctionStart(localAddress);
    compiler->addIndex(wideOffset);
    const auto parameterAddress = compiler->addOpCode(OpCodes::op_parameter_address_push);
    compiler->addIndex(wideOffset + 2);
    VirtualMachine machine(&runtime, wideOffset * 2 + 32);
    machine.pushInteger(1);
    machine.pushInteger(2);
    machine.pushInteger(3);
    machine.pushN(wideOffset);
    machine.callScriptFunction(*function, 0);
    machine.reserveLocals(wideOffset + 3);
    executeInstruction(runtime, machine, localAddress);
    BOOST_TEST(machine.peekStack(0).getStorageAddress() == machine.getLocalStorage(wideOffset, 1).data());
    machine.pop1();
    executeInstruction(runtime, machine, parameterAddress);
    BOOST_TEST(machine.peekStack(0).getStorageAddress() == machine.getParameterStorage(wideOffset, 3).data());
    BOOST_TEST(machine.peekStack(0).getStorageAddress()[2].getIntegerValue() == 3);
    machine.pop1();
    machine.getLocalStorage(wideOffset, 1)[0].setIntegerValue(29);
    machine.getParameterStorage(wideOffset + 2, 1)[0].setStorageAddress(machine.getLocalAddress(0));
    const auto indirectLoad = compiler->addOpCode(OpCodes::op_storage_parameter_load_r);
    compiler->addIndex(wideOffset + 2);
    compiler->addIndex(wideOffset);
    executeInstruction(runtime, machine, indirectLoad);
    BOOST_TEST(machine.getIntegerResult() == 29);
    function->setFunctionEnd(runtime.getInstructions().size());
    machine.returnFunction(wideOffset + 3);
    machine.popN(wideOffset + 3);
    std::ostringstream dump;
    dumpInstructions(runtime, dump);
    BOOST_TEST(dump.str().find(OpCodes::op_local_address_push) != NomadString::npos);
    BOOST_TEST(dump.str().find(std::to_string(wideOffset + 2)) != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(indirect_opcodes_preserve_scalar_and_reference_ownership) {
    Runtime runtime;
    const auto& frame = registerFrameFunction(runtime);
    auto compiler = runtime.createCompiler();
    std::array<RuntimeValue, 3> storage;
    storage[0].setIntegerValue(17);
    storage[1].setStringValue("source");
    storage[2].setStringValue("replaced");
    VirtualMachine machine(&runtime, 32);
    machine.pushStorageAddress(storage.data());
    machine.callScriptFunction(frame, 0);
    const auto emit = [&](const NomadString& name, const NomadIndex field) {
        const auto address = compiler->addOpCode(name);
        compiler->addIndex(0);
        compiler->addIndex(field);
        executeInstruction(runtime, machine, address);
    };
    emit(OpCodes::op_storage_parameter_load_r, 0);
    BOOST_TEST(machine.getIntegerResult() == 17);
    emit(OpCodes::op_storage_parameter_load_i, 0);
    BOOST_TEST(machine.getIntegerIntermediate() == 17);
    emit(OpCodes::op_storage_parameter_push, 0);
    BOOST_TEST(machine.peekStack(0).getIntegerValue() == 17);
    machine.pop1();
    machine.setIntegerResult(23);
    emit(OpCodes::op_storage_parameter_set, 0);
    BOOST_TEST(storage[0].getIntegerValue() == 23);
    emit(OpCodes::op_storage_parameter_address_push, 1);
    BOOST_TEST(machine.peekStack(0).getStorageAddress() == storage.data() + 1);
    machine.pop1();
    emit(OpCodes::op_object_storage_parameter_load_r, 1);
    BOOST_TEST(storage[1].getObjectValue()->getReferenceCount() == 2U);
    emit(OpCodes::op_object_free_storage_parameter, 2);
    BOOST_TEST(storage[2].getObjectValue() == nullptr);
    emit(OpCodes::op_storage_parameter_set, 2);
    BOOST_TEST(storage[2].getObjectValue() == storage[1].getObjectValue());
    machine.setResult(RuntimeValue{});
    emit(OpCodes::op_object_storage_parameter_load_i, 1);
    BOOST_TEST(storage[1].getObjectValue()->getReferenceCount() == 3U);
    machine.freeObjectIntermediate();
    emit(OpCodes::op_object_storage_parameter_push, 1);
    BOOST_TEST(storage[1].getObjectValue()->getReferenceCount() == 3U);
    machine.freeObjectStack(0);
    machine.pop1();
    emit(OpCodes::op_object_free_storage_parameter, 2);
    BOOST_TEST(storage[1].getObjectValue()->getReferenceCount() == 1U);
    machine.returnFunction(0);
    machine.pop1();
    BOOST_TEST(storage[1].getObjectValue()->getReferenceCount() == 1U);
    storage[1].freeObjectValue();
}

BOOST_AUTO_TEST_CASE(hidden_pointers_survive_nested_calls_and_host_reentry_failures) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    BOOST_REQUIRE(runtime.registerNativeFunction("fail", [](VirtualMachine* active) {
        active->fault("Expected script failure");
    }, {}, runtime.getVoidType(), "").isValid());
    fixture.addFunction("success", "return 42");
    fixture.addFunction("failure", "fun nested\nfail\nend\nnested\nreturn 0");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto* success = runtime.getFunction(runtime.getScriptFunctionId("success"))->asScriptFunction();
    const auto* failure = runtime.getFunction(runtime.getScriptFunctionId("failure"))->asScriptFunction();
    std::array<RuntimeValue, 2> receiver;
    std::array<RuntimeValue, 2> destination;
    VirtualMachine machine(&runtime, 128);
    machine.pushStorageAddress(destination.data());
    machine.pushStorageAddress(receiver.data());
    machine.pushInteger(7);
    machine.callScriptFunction(*success, 0);
    BOOST_TEST(machine.getIntegerParameter(0) == 7);
    BOOST_TEST(machine.getParameter(1).getStorageAddress() == receiver.data());
    BOOST_TEST(machine.getParameter(2).getStorageAddress() == destination.data());
    const auto callback = runtime.registerNativeFunction("reenter", [&](VirtualMachine* active) {
        BOOST_TEST(active->getIntegerParameter(0) == 9);
        active->getStorageParameter(1, 0).setIntegerValue(23);
        active->getStorageParameter(2, 1).setIntegerValue(31);
        for (auto index = 0; index < 3; ++index) {
            BOOST_CHECK_THROW(active->run(failure, {}), VirtualMachineException);
            BOOST_TEST(active->getParameter(1).getStorageAddress() == receiver.data());
            active->run(success, {});
            BOOST_TEST(active->getIntegerResult() == 42);
            BOOST_TEST(active->getParameter(2).getStorageAddress() == destination.data());
        }
        active->fault("Expected native failure");
    }, {defParameter("value", runtime.getIntegerType(), "")}, runtime.getVoidType(), "");
    BOOST_REQUIRE(callback.isValid());
    machine.pushStorageAddress(destination.data());
    machine.pushStorageAddress(receiver.data());
    machine.pushInteger(9);
    BOOST_CHECK_THROW(machine.callNativeFunction(callback), VirtualMachineException);
    machine.popN(3);
    BOOST_TEST(machine.getIntegerParameter(0) == 7);
    BOOST_TEST(std::as_const(machine).getStorageParameter(1, 0).getIntegerValue() == 23);
    BOOST_TEST(std::as_const(machine).getStorageParameter(2, 1).getIntegerValue() == 31);
    machine.pushInteger(11);
    machine.callScriptMethod(*success, 0);
    BOOST_TEST(machine.getIntegerParameter(0) == 11);
    machine.returnFunction(0);
    machine.pop1();
    BOOST_TEST(machine.getParameter(1).getStorageAddress() == receiver.data());
    machine.returnFunction(0);
    machine.popN(3);
    BOOST_TEST(machine.peekStack(0).getIntegerValue() == 0);
}

BOOST_AUTO_TEST_CASE(bytecode_calls_forward_hidden_pointers_and_release_only_owned_fields) {
    const auto before = RuntimeValue::getLiveStringCount();
    Runtime runtime;
    auto virtualMachine = runtime.createVirtualMachine();
    const auto typeId = runtime.registerStruct("CallStorage");
    const auto* type = runtime.getTypeByName("CallStorage")->asStructType();
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(typeId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(typeId, "label", runtime.getStringType()).isValid());
    runtime.getTypeManager().finalizeCompositeFieldLayout(typeId);
    const auto callerId = runtime.registerScriptFunction("storageCaller", "storage.nomad", "");
    const auto calleeId = runtime.registerScriptFunction("storageCallee", "storage.nomad", "");
    auto* caller = runtime.getScriptFunction(callerId);
    auto* callee = runtime.getScriptFunction(calleeId);
    caller->setReturnType(runtime.getIntegerType());
    callee->setReturnType(runtime.getVoidType());
    callee->addParameter("number", runtime.getIntegerType(), "");
    const auto receiverId = caller->registerVariable("receiver", type);
    const auto destinationId = caller->registerVariable("destination", type);
    BOOST_TEST(caller->getVariableStorageSize() == 4U);
    BOOST_TEST(callee->getParameterCount() == 1U);
    BOOST_TEST(callee->getParameterStorageSize() == 1U);
    const auto initialize = runtime.registerNativeFunction("initialize", [&](VirtualMachine* active) {
        active->getLocalStorage(caller->getVariableOffset(receiverId), 2)[1].freeObjectValue();
        active->getLocalStorage(caller->getVariableOffset(receiverId), 2)[1].setStringValue("copied");
    }, {}, runtime.getVoidType(), "");
    const auto inspect = runtime.registerNativeFunction("inspectForwarded", [&](VirtualMachine* active) {
        BOOST_TEST(active->getIntegerParameter(0) == 7);
        BOOST_TEST(active->getStorageParameter(1, 0).getIntegerValue() == 7);
        BOOST_TEST(active->getStorageParameter(2, 0).getIntegerValue() == 7);
        BOOST_TEST(active->getStorageParameter(1, 1).getObjectValue() ==
            active->getStorageParameter(2, 1).getObjectValue());
        BOOST_TEST(active->getStorageParameter(1, 1).getObjectValue()->getReferenceCount() == 2U);
        active->getStorageParameter(2, 0).setIntegerValue(19);
    }, {defParameter("number", runtime.getIntegerType(), "")}, runtime.getVoidType(), "");
    BOOST_REQUIRE(initialize.isValid());
    BOOST_REQUIRE(inspect.isValid());
    auto compiler = runtime.createCompiler();
    const auto indirect = [&](const NomadString& name, const NomadIndex pointer, const NomadIndex field) {
        compiler->addOpCode(name);
        compiler->addIndex(pointer);
        compiler->addIndex(field);
    };
    callee->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(callee);
    compiler->addOpCode(OpCodes::op_parameter_load_r);
    compiler->addIndex(0);
    indirect(OpCodes::op_storage_parameter_set, 1, 0);
    indirect(OpCodes::op_storage_parameter_set, 2, 0);
    indirect(OpCodes::op_object_storage_parameter_load_r, 1, 1);
    indirect(OpCodes::op_object_free_storage_parameter, 2, 1);
    indirect(OpCodes::op_storage_parameter_set, 2, 1);
    indirect(OpCodes::op_storage_parameter_address_push, 2, 0);
    indirect(OpCodes::op_storage_parameter_address_push, 1, 0);
    compiler->addOpCode(OpCodes::op_parameter_push);
    compiler->addIndex(0);
    compiler->addOpCode(OpCodes::op_call_native_function);
    compiler->addId(inspect.id);
    compiler->addOpCode(OpCodes::op_pop_3);
    compiler->addFunctionReturn(callee);
    callee->setFunctionEnd(runtime.getInstructions().size());
    caller->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(caller);
    compiler->addOpCode(OpCodes::op_call_native_function);
    compiler->addId(initialize.id);
    compiler->addOpCode(OpCodes::op_local_address_push);
    compiler->addIndex(caller->getVariableOffset(destinationId));
    compiler->addOpCode(OpCodes::op_local_address_push);
    compiler->addIndex(caller->getVariableOffset(receiverId));
    compiler->addOpCode(OpCodes::op_integer_push_n);
    compiler->addInteger(7);
    compiler->addOpCode(OpCodes::op_call_script_function);
    compiler->addId(callee->getId().id);
    compiler->addIndex(0);
    compiler->addOpCode(OpCodes::op_pop_3);
    compiler->addLoadFunctionVariable(caller->getVariableOffset(destinationId), runtime.getIntegerType(),
        ExpressionTarget::Result);
    compiler->addFunctionReturn(caller);
    caller->setFunctionEnd(runtime.getInstructions().size());
    for (auto index = 0; index < 3; ++index) {
        RuntimeValue result;
        virtualMachine->executeFunction(callerId, {}, result);
        BOOST_TEST(result.getIntegerValue() == 19);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == before);
    }
}

BOOST_AUTO_TEST_CASE(calls_need_only_existing_frames_and_requested_pointer_slots) {
    Runtime runtime;
    const auto& frame = registerFrameFunction(runtime);
    VirtualMachine machine(&runtime, 8);
    machine.pushStorageAddress(machine.getParameterAddress(0));
    machine.callScriptFunction(frame, 0);
    BOOST_CHECK_THROW(machine.callScriptFunction(frame, 0), VirtualMachineException);
    machine.returnFunction(0);
    machine.pop1();
    BOOST_TEST(machine.peekStack(0).getIntegerValue() == 0);
    machine.callScriptFunction(frame, 0);
    machine.returnFunction(0);
    BOOST_TEST(machine.peekStack(0).getIntegerValue() == 0);
    VirtualMachine emptyCall(&runtime, 6);
    emptyCall.callScriptFunction(frame, 0);
    BOOST_CHECK_THROW(emptyCall.reserveLocals(1), VirtualMachineException);
    emptyCall.returnFunction(0);
    BOOST_TEST(emptyCall.peekStack(0).getIntegerValue() == 0);
}

BOOST_AUTO_TEST_CASE(raw_fallback_calls_preserve_reference_captures_and_cleanup_during_reentry_and_failure) {
    auto copyCount = NomadIndex{0};
    auto failCopyAt = NomadIndex{0};
    Runtime runtime;
    const auto typeId = runtime.registerClass<test::FailingArgumentCopyType>(
        "RawFallbackReference", ClassMode::Closed, copyCount, failCopyAt
    );
    BOOST_REQUIRE(typeId.isValid());
    const auto* type = runtime.getType(typeId).value();
    RuntimeValue source;
    const RuntimeValueGuard sourceOwner(*type, &source);
    source.setObjectValue(new NomadObject);
    RuntimeValue receiver{NomadString{"borrowed fallback receiver"}};
    const RuntimeValueGuard receiverOwner(*runtime.getStringType(), &receiver);
    const std::vector<RuntimeValue> arguments(6, source);
    const std::vector<RuntimeValue> captures(2, source);
    const auto functionId = runtime.registerScriptFunction("rawFallback", "fallback.nomad", "");
    auto* function = runtime.getScriptFunction(functionId);
    BOOST_REQUIRE(function != nullptr);
    for (auto index = NomadIndex{0}; index < arguments.size(); ++index) {
        function->addParameter("value" + std::to_string(index), type, "");
    }

    for (auto index = NomadIndex{0}; index < captures.size(); ++index) {
        function->createCapture(VariableId{toNomadId(index)}, "capture" + std::to_string(index), type);
    }

    function->setReturnType(runtime.getIntegerType());
    auto depth = NomadIndex{0};
    auto throwBody = false;
    const auto probeId = runtime.registerNativeFunction(
        "inspectRawFallback",
        [&](VirtualMachine* active) {
            ++depth;
            BOOST_SCOPE_EXIT_ALL(&) {
                --depth;
            };

            BOOST_TEST(active->getCurrentCallable() == function);
            BOOST_TEST(source.getObjectValue()->getReferenceCount() ==
                1 + depth * (arguments.size() + captures.size()));
            for (auto index = NomadIndex{0}; index < arguments.size() + captures.size(); ++index) {
                BOOST_TEST(active->getEnclosingParameter(index).getObjectValue() == source.getObjectValue());
            }

            if (throwBody) {
                active->fault("Expected raw fallback failure");
            }

            if (depth == 1) {
                const auto before = active->captureExecutionState();
                active->run(function, arguments, captures);
                BOOST_TEST(active->getCurrentCallable() == function);
                BOOST_TEST(active->captureExecutionState().variableIndex == before.variableIndex);
                BOOST_TEST(source.getObjectValue()->getReferenceCount() == 9U);
            }
        },
        {},
        runtime.getVoidType(),
        ""
    );
    BOOST_REQUIRE(probeId.isValid());
    auto compiler = runtime.createCompiler();
    function->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(function);
    compiler->addOpCode(OpCodes::op_call_native_function);
    compiler->addId(probeId.id);
    compiler->addLoadIntegerValue(42, ExpressionTarget::Result);
    compiler->addFunctionReturn(function);
    function->setFunctionEnd(runtime.getInstructions().size());
    runtime.initializeScripts();
    VirtualMachine machine(&runtime, 128);
    machine.pushInteger(91);
    machine.enterNativeFrame(receiver);
    const auto before = machine.captureExecutionState();
    const auto checkRestored = [&] {
        const auto after = machine.captureExecutionState();
        BOOST_TEST(after.instructionIndex == before.instructionIndex);
        BOOST_TEST(after.stackIndex == before.stackIndex);
        BOOST_TEST(after.parameterIndex == before.parameterIndex);
        BOOST_TEST(after.variableIndex == before.variableIndex);
        BOOST_TEST(after.running == before.running);
        BOOST_TEST(after.receiver.getObjectValue() == before.receiver.getObjectValue());
        BOOST_TEST(machine.peekStack(0).getIntegerValue() == 91);
        BOOST_TEST(source.getObjectValue()->getReferenceCount() == 1U);
        BOOST_TEST(receiver.getObjectValue()->getReferenceCount() == 1U);
        BOOST_TEST(depth == 0U);
    };
    machine.run(function, arguments, captures);
    BOOST_TEST(machine.getIntegerResult() == 42);
    BOOST_TEST(copyCount == 16U);
    checkRestored();
    for (const auto failure : {NomadIndex{4}, NomadIndex{9}}) {
        copyCount = 0;
        failCopyAt = failure;
        BOOST_CHECK_EXCEPTION(
            machine.run(function, arguments, captures),
            NomadException,
            [](const auto& error) {

                return NomadString{error.what()} == "Argument copy failed";
            }
        );
        checkRestored();
    }

    failCopyAt = 0;
    throwBody = true;
    BOOST_CHECK_EXCEPTION(
        machine.run(function, arguments, captures),
        VirtualMachineException,
        [](const auto& error) {

            return NomadString{error.what()} == "Expected raw fallback failure";
        }
    );
    checkRestored();
}

#if defined(_MSC_VER) && defined(_DEBUG)
BOOST_AUTO_TEST_CASE(native_catalog_publication_survives_allocation_failures_and_retries) {
    constexpr auto batchSize = NomadIndex{16};
    const NomadString name = "failureCatalog";
    for (const auto methodCase : {false, true}) {
        for (const auto seedCount : {NomadIndex{0}, NomadIndex{4}}) {
            auto allocationCount = NomadIndex{0};
            for (auto failureAt = NomadIndex{0}; failureAt <= allocationCount; ++failureAt) {
                BOOST_TEST_CONTEXT("method=" << methodCase << " seeds=" << seedCount << " allocation=" << failureAt) {
                    auto calls = NomadIndex{0};
                    Runtime runtime;
                    auto& manager = runtime.getTypeManager();
                    std::vector<const ClassType*> owners;
                    const auto prepare = [&](const NomadString& ownerName) -> MemberReference {
                        const auto ownerId = runtime.registerClass(ownerName, ClassMode::Closed);
                        BOOST_REQUIRE(ownerId.isValid());
                        const auto* owner = runtime.getType(ownerId).value()->asClassType();
                        owners.push_back(owner);
                        if (methodCase) {
                            auto method = std::make_unique<NativeMethod>(
                                runtime.getNextCallableId(),
                                name,
                                owner,
                                [&calls](VirtualMachine* machine) {
                                    ++calls;
                                    machine->setIntegerResult(42);
                                },
                                std::vector<Parameter>{},
                                runtime.getIntegerType(),
                                ""
                            );
                            const auto id = runtime.registerMethod(manager.getMutableClassType(owner), std::move(method));
                            BOOST_REQUIRE(id.isValid());

                            return id;
                        }

                        auto property = std::make_unique<NativeProperty>(
                            manager.getNextPropertyId(),
                            owner,
                            name,
                            runtime.getIntegerType(),
                            [&calls](VirtualMachine*, RuntimeValue& value) {
                                ++calls;
                                value.setIntegerValue(42);
                            },
                            NativePropertySetFn{},
                            ""
                        );
                        const auto id = manager.registerProperty(manager.getMutableClassType(owner), std::move(property));
                        BOOST_REQUIRE(id.isValid());

                        return id;
                    };
                    const auto catalog = [&](const MemberReference& member) {
                        if (const auto* propertyId = std::get_if<PropertyId>(&member)) {
                            runtime.catalogNativeProperty(*propertyId);
                        } else {
                            runtime.catalogNativeMethod(std::get<CallableId>(member));
                        }
                    };
                    for (auto index = NomadIndex{0}; index < seedCount; ++index) {
                        catalog(prepare("FailureSeed" + std::to_string(index)));
                    }

                    std::vector<MemberReference> targets;
                    for (auto index = NomadIndex{0}; index < batchSize; ++index) {
                        targets.push_back(prepare("FailureTarget" + std::to_string(index)));
                    }

                    std::vector<MemberReference> before;
                    std::vector<MemberReference> namedBefore;
                    runtime.getNativeMembers(before);
                    runtime.getNativeMemberOverloads(name, namedBefore);
                    auto completed = NomadIndex{0};
                    auto failed = false;
                    callAllocations = 0;
                    catalogAllocationRequests = 0;
                    callAllocationRejected = false;
                    const auto originalChain = previousAllocationHook;
                    const auto originalRejection = rejectedCatalogAllocation;
                    previousAllocationHook = _CrtSetAllocHook(countCallAllocations);
                    {
                        const auto previousHook = previousAllocationHook;
                        BOOST_SCOPE_EXIT_ALL(=) {
                            _CrtSetAllocHook(previousHook);
                            previousAllocationHook = originalChain;
                            rejectedCatalogAllocation = originalRejection;
                        };

                        rejectedCatalogAllocation = failureAt;
                        try {
                            for (; completed < targets.size(); ++completed) {
                                catalog(targets[completed]);
                            }
                        } catch (const std::bad_alloc&) {
                            failed = true;
                        }
                    }

                    if (failureAt == 0) {
                        allocationCount = catalogAllocationRequests;
                        BOOST_REQUIRE(allocationCount != 0);
                    }

                    BOOST_REQUIRE(failed == (failureAt != 0));
                    BOOST_REQUIRE(callAllocationRejected == (failureAt != 0));
                    auto expected = before;
                    expected.insert(expected.end(), targets.begin(), targets.begin() + completed);
                    auto namedExpected = namedBefore;
                    namedExpected.insert(namedExpected.end(), targets.begin(), targets.begin() + completed);
                    std::vector<MemberReference> actual;
                    runtime.getNativeMembers(actual);
                    BOOST_REQUIRE_MESSAGE(actual == expected, "Failed publication changed the flat native catalog");
                    runtime.getNativeMemberOverloads(name, actual);
                    BOOST_REQUIRE_MESSAGE(actual == namedExpected, "Failed publication changed native name memberships");
                    BOOST_TEST(runtime.getNativeMemberOverloadCount(name) == namedExpected.size());
                    BOOST_TEST(calls == 0U);
                    for (auto index = NomadIndex{0}; index < targets.size(); ++index) {
                        const auto* owner = owners[seedCount + index];
                        const auto member = runtime.getNativeMemberId(owner, name);
                        if (index < completed) {
                            BOOST_REQUIRE(member.has_value());
                            BOOST_TEST(*member == targets[index]);
                        } else {
                            BOOST_TEST(!member.has_value());
                        }

                        if (methodCase) {
                            const auto id = std::get<CallableId>(targets[index]);
                            const auto* method = runtime.getNativeMethod(id);
                            BOOST_REQUIRE(method != nullptr);
                            BOOST_TEST(method->getId() == id);
                            BOOST_TEST(method->getClassType() == owner);
                            BOOST_TEST(owner->getMethods().front() == method);
                        } else {
                            const auto id = std::get<PropertyId>(targets[index]);
                            const auto* property = runtime.getNativeProperty(id);
                            BOOST_REQUIRE(property != nullptr);
                            BOOST_TEST(property->getId() == id);
                            BOOST_TEST(property->getClassType() == owner);
                            BOOST_TEST(owner->getProperty(name) == property);
                        }
                    }

                    for (auto index = completed; index < targets.size(); ++index) {
                        BOOST_REQUIRE_NO_THROW(catalog(targets[index]));
                    }

                    for (const auto& member : targets) {
                        BOOST_CHECK_THROW(catalog(member), NomadException);
                    }

                    expected = before;
                    expected.insert(expected.end(), targets.begin(), targets.end());
                    namedExpected = namedBefore;
                    namedExpected.insert(namedExpected.end(), targets.begin(), targets.end());
                    runtime.getNativeMembers(actual);
                    BOOST_TEST((actual == expected));
                    runtime.getNativeMemberOverloads(name, actual);
                    BOOST_TEST((actual == namedExpected));
                    auto machine = runtime.createVirtualMachine();
                    for (const auto& member : targets) {
                        if (methodCase) {
                            runtime.getNativeMethod(std::get<CallableId>(member))->getFn()(machine.get());
                            BOOST_TEST(machine->getIntegerResult() == 42);
                        } else {
                            RuntimeValue value;
                            runtime.getNativeProperty(std::get<PropertyId>(member))->getValue(machine.get(), value);
                            BOOST_TEST(value.getIntegerValue() == 42);
                        }
                    }

                    BOOST_TEST(calls == batchSize);
                }
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(fixed_type_getters_do_not_allocate) {
    Runtime runtime;
    constexpr auto getters = std::array{
        &Runtime::getVoidType, &Runtime::getBooleanType, &Runtime::getIntegerType, &Runtime::getFloatType,
        &Runtime::getStringType, &Runtime::getFileNameType, &Runtime::getFunctionNameType,
        &Runtime::getLineNumberType, &Runtime::getEventCallbackType, &Runtime::getEventDispatchType
    };
    for (const auto getter : getters) {
        const auto* expected = (runtime.*getter)();
        BOOST_REQUIRE(expected != nullptr);
        auto matchingQueries = NomadIndex{0};
        callAllocations = 0;
        previousAllocationHook = _CrtSetAllocHook(countCallAllocations);
        {
            const auto previousHook = previousAllocationHook;
            BOOST_SCOPE_EXIT_ALL(=) {
                _CrtSetAllocHook(previousHook);
            };

            for (auto iteration = 0; iteration < 100; ++iteration) {
                matchingQueries += (runtime.*getter)() == expected;
            }
        }

        BOOST_TEST(callAllocations == 0U);
        BOOST_TEST(matchingQueries == 100U);
    }
}

BOOST_AUTO_TEST_CASE(stored_field_assignment_allocates_scratch_only_for_multi_slot_values) {
    Runtime runtime;
    auto& manager = runtime.getTypeManager();
    std::vector<const Type*> valueTypes{runtime.getIntegerType(), runtime.getStringType()};
    for (const auto size : {NomadIndex{0}, NomadIndex{2}, NomadIndex{8}}) {
        const auto id = runtime.registerStruct("ScratchValue" + std::to_string(size));
        BOOST_REQUIRE(id.isValid());
        for (auto index = NomadIndex{0}; index < size; ++index) {
            const auto* fieldType = index + 1 == size ? runtime.getStringType() : runtime.getIntegerType();
            BOOST_REQUIRE(manager.registerScriptStructProperty(
                id,
                "field" + std::to_string(index),
                fieldType
            ).isValid());
        }

        manager.finalizeCompositeFieldLayout(id);
        valueTypes.push_back(runtime.getType(id).value());
    }

    VirtualMachine machine(&runtime, 64);
    const Interpreter interpreter(machine);
    machine.pushInteger(91);
    machine.setIntegerResult(71);
    machine.setIntegerIntermediate(83);
    for (const auto* type : valueTypes) {
        const auto size = type->getSize();
        const auto referenceValue = type == runtime.getStringType() || size > 1;
        std::vector<RuntimeValue> source(size);
        const RuntimeValueGuard sourceOwner(*type, source.empty() ? nullptr : source.data());
        for (auto index = NomadIndex{0}; index < size; ++index) {
            if (referenceValue && index + 1 == size) {
                source[index].setStringValue("owned assignment source");
            } else {
                source[index].setIntegerValue(static_cast<NomadInteger>(index) + 17);
            }
        }

        const InterpreterValue replacement(type, source);
        const auto suffix = std::to_string(type->getId().id);
        const auto ownerId = runtime.registerStruct("ScratchOwner" + suffix);
        BOOST_REQUIRE(ownerId.isValid());
        BOOST_REQUIRE(manager.registerScriptStructProperty(
            ownerId,
            "before",
            runtime.getIntegerType()
        ).isValid());
        const auto propertyId = manager.registerScriptStructProperty(ownerId, "value", type);
        BOOST_REQUIRE(propertyId.isValid());
        BOOST_REQUIRE(manager.registerScriptStructProperty(
            ownerId,
            "after",
            runtime.getIntegerType()
        ).isValid());
        manager.finalizeCompositeFieldLayout(ownerId);
        const auto* owner = runtime.getType(ownerId).value()->asStructType();
        const auto outerId = runtime.registerStruct("ScratchOuter" + suffix);
        BOOST_REQUIRE(outerId.isValid());
        BOOST_REQUIRE(manager.registerScriptStructProperty(
            outerId,
            "before",
            runtime.getIntegerType()
        ).isValid());
        const auto innerId = manager.registerScriptStructProperty(outerId, "inner", owner);
        BOOST_REQUIRE(innerId.isValid());
        BOOST_REQUIRE(manager.registerScriptStructProperty(
            outerId,
            "after",
            runtime.getIntegerType()
        ).isValid());
        manager.finalizeCompositeFieldLayout(outerId);
        const auto* outer = runtime.getType(outerId).value()->asStructType();
        std::vector<RuntimeValue> receiver(outer->getSize());
        const RuntimeValueGuard receiverOwner(*outer, receiver.data());
        outer->initValue(receiver.data());
        const auto ownerStorage = std::span{receiver}.subspan(
            outer->getFieldOffset(innerId),
            owner->getSize()
        );
        const auto valueStorage = ownerStorage.subspan(owner->getFieldOffset(propertyId), size);
        receiver.front().setIntegerValue(101);
        receiver.back().setIntegerValue(103);
        ownerStorage.front().setIntegerValue(107);
        ownerStorage.back().setIntegerValue(109);
        const auto expectedAllocations = type->getAssignmentScratchSize() == 0 ? 0U : 100U;
        for (const auto nested : {false, true}) {
            const auto assign = [&] {
                if (nested) {
                    interpreter.setProperty(*outer, receiver, "inner.value", replacement);
                } else {
                    interpreter.setProperty(*owner, ownerStorage, propertyId, replacement);
                }
            };
            assign();
            auto completedAssignments = NomadIndex{0};
            callAllocations = 0;
            previousAllocationHook = _CrtSetAllocHook(countCallAllocations);
            {
                const auto previousHook = previousAllocationHook;
                BOOST_SCOPE_EXIT_ALL(=) {
                    _CrtSetAllocHook(previousHook);
                };

                for (auto iteration = 0; iteration < 100; ++iteration) {
                    assign();
                    auto matches = true;
                    for (auto index = NomadIndex{0}; index < size; ++index) {
                        if (referenceValue && index + 1 == size) {
                            matches &= valueStorage[index].getObjectValue() == source[index].getObjectValue();
                        } else {
                            matches &= valueStorage[index].getIntegerValue() == source[index].getIntegerValue();
                        }
                    }

                    completedAssignments += matches;
                }
            }

            BOOST_TEST(callAllocations == expectedAllocations);
            BOOST_TEST(completedAssignments == 100U);
            BOOST_TEST(receiver.front().getIntegerValue() == 101);
            BOOST_TEST(receiver.back().getIntegerValue() == 103);
            BOOST_TEST(ownerStorage.front().getIntegerValue() == 107);
            BOOST_TEST(ownerStorage.back().getIntegerValue() == 109);
            BOOST_TEST(machine.peekStack(0).getIntegerValue() == 91);
            BOOST_TEST(machine.captureExecutionState().stackIndex == 1U);
            BOOST_TEST(machine.getIntegerResult() == 71);
            BOOST_TEST(machine.getIntegerIntermediate() == 83);
            if (referenceValue) {
                BOOST_TEST(source.back().getObjectValue()->getReferenceCount() == 3U);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(argument_snapshot_capacity_counts_logical_values_instead_of_storage_slots) {
    Runtime runtime;
    const auto emptyId = runtime.registerStruct("SnapshotEmpty");
    const auto pairId = runtime.registerStruct("SnapshotPair");
    BOOST_REQUIRE(emptyId.isValid() && pairId.isValid());
    auto& manager = runtime.getTypeManager();
    BOOST_REQUIRE(manager.registerScriptStructProperty(pairId, "first", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(manager.registerScriptStructProperty(pairId, "second", runtime.getIntegerType()).isValid());
    manager.finalizeCompositeFieldLayout(emptyId);
    manager.finalizeCompositeFieldLayout(pairId);
    const std::array<const Type*, 3> types{
        runtime.getIntegerType(), runtime.getType(emptyId).value(), runtime.getType(pairId).value()
    };
    const std::array storage{RuntimeValue{NomadInteger{17}}, RuntimeValue{NomadInteger{25}}};
    VirtualMachine machine(&runtime, 64);
    machine.pushInteger(91);
    machine.setIntegerResult(71);
    machine.setIntegerIntermediate(83);
    for (const auto* type : types) {
        const auto size = type->getSize();
        const InterpreterValue value(type, std::span<const RuntimeValue>{storage.data(), size});
        for (const auto count : {NomadIndex{0}, NomadIndex{1}, NomadIndex{2}}) {
            std::vector<Parameter> parameters;
            for (auto index = NomadIndex{0}; index < count; ++index) {
                parameters.push_back(defParameter("value" + std::to_string(index), type, ""));
            }

            const auto id = runtime.registerNativeFunction(
                "snapshot" + std::to_string(type->getId().id) + "_" + std::to_string(count),
                [](VirtualMachine*) {},
                parameters,
                runtime.getVoidType(),
                ""
            );
            const auto* callable = runtime.getCallable(id);
            BOOST_REQUIRE(callable != nullptr);
            const std::vector<InterpreterValue> arguments(count, value);
            {
                const InterpreterCallFrame frame(machine, *callable, arguments, runtime.getVoidType());
            }

            auto completedFrames = NomadIndex{0};
            const auto* returnType = runtime.getVoidType();
            callAllocations = 0;
            previousAllocationHook = _CrtSetAllocHook(countCallAllocations);
            {
                const auto previousHook = previousAllocationHook;
                BOOST_SCOPE_EXIT_ALL(=) {
                    _CrtSetAllocHook(previousHook);
                };

                for (auto iteration = 0; iteration < 100; ++iteration) {
                    const InterpreterCallFrame frame(machine, *callable, arguments, returnType);
                    const auto expectedTop = size == 0 || count == 0 ? NomadInteger{91} :
                        storage[size - 1].getIntegerValue();
                    completedFrames += frame.getResult() == nullptr &&
                        machine.peekStack(0).getIntegerValue() == expectedTop;
                }
            }

            BOOST_TEST(callAllocations == (count <= 1 ? 0U : 100U));
            BOOST_TEST(completedFrames == 100U);
            BOOST_TEST(machine.peekStack(0).getIntegerValue() == 91);
            BOOST_TEST(machine.captureExecutionState().stackIndex == 1U);
            BOOST_TEST(machine.getIntegerResult() == 71);
            BOOST_TEST(machine.getIntegerIntermediate() == 83);
        }
    }
}

BOOST_AUTO_TEST_CASE(frame_result_allocation_excludes_unused_result_containers) {
    Runtime runtime;
    std::vector<const Type*> types{
        runtime.getVoidType(), runtime.getIntegerType(), runtime.getStringType()
    };
    for (const auto count : {NomadIndex{0}, NomadIndex{2}, NomadIndex{8}}) {
        const auto id = runtime.registerStruct("AllocationResult" + std::to_string(count));
        BOOST_REQUIRE(id.isValid());
        for (auto index = NomadIndex{0}; index < count; ++index) {
            BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(
                id,
                "field" + std::to_string(index),
                runtime.getIntegerType()
            ).isValid());
        }

        runtime.getTypeManager().finalizeCompositeFieldLayout(id);
        types.push_back(runtime.getType(id).value());
    }

    VirtualMachine machine(&runtime, 64);
    machine.setIntegerResult(71);
    machine.setIntegerIntermediate(83);
    for (auto index = NomadIndex{0}; index < types.size(); ++index) {
        const auto* type = types[index];
        const auto id = runtime.registerNativeFunction(
            "allocationResult" + std::to_string(index),
            [](VirtualMachine*) {},
            {},
            type,
            ""
        );
        const auto* callable = runtime.getCallable(id);
        BOOST_REQUIRE(callable != nullptr);
        {
            const InterpreterCallFrame frame(machine, *callable, {}, type);
        }

        const auto nonemptyAggregate = type->isStruct() && type->getSize() != 0;
        auto completedFrames = NomadIndex{0};
        callAllocations = 0;
        previousAllocationHook = _CrtSetAllocHook(countCallAllocations);
        {
            const auto previousHook = previousAllocationHook;
            BOOST_SCOPE_EXIT_ALL(=) {
                _CrtSetAllocHook(previousHook);
            };

            for (auto iteration = 0; iteration < 100; ++iteration) {
                const InterpreterCallFrame frame(machine, *callable, {}, type);
                completedFrames += (frame.getResult() != nullptr) == (type->getSize() != 0);
            }
        }

        BOOST_TEST(callAllocations == (nonemptyAggregate ? 100U : 0U));
        BOOST_TEST(completedFrames == 100U);
        BOOST_TEST(machine.getIntegerResult() == 71);
        BOOST_TEST(machine.getIntegerIntermediate() == 83);
        BOOST_TEST(machine.captureExecutionState().stackIndex == 0U);
    }
}

BOOST_AUTO_TEST_CASE(raw_argument_bookkeeping_uses_inline_storage_and_heap_fallback) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    for (const auto count : {NomadIndex{0}, NomadIndex{1}, NomadIndex{4}, NomadIndex{8}}) {
        const auto functionId = runtime.registerScriptFunction(
            "rawAllocation" + std::to_string(count), "raw-allocation.nomad", ""
        );
        auto* function = runtime.getScriptFunction(functionId);
        BOOST_REQUIRE(function != nullptr);
        function->setReturnType(runtime.getIntegerType());
        for (auto index = NomadIndex{0}; index < count; ++index) {
            function->addParameter("value" + std::to_string(index), runtime.getIntegerType(), "");
        }

        function->setFunctionStart(runtime.getInstructions().size());
        compiler->addFunctionEntry(function);
        compiler->addLoadIntegerValue(42, ExpressionTarget::Result);
        compiler->addFunctionReturn(function);
        function->setFunctionEnd(runtime.getInstructions().size());
        const std::vector<RuntimeValue> arguments(count, RuntimeValue{NomadInteger{7}});
        runtime.initializeScripts();
        VirtualMachine machine(&runtime, 64);
        machine.run(function, arguments);
        callAllocations = 0;
        previousAllocationHook = _CrtSetAllocHook(countCallAllocations);
        {
            const auto previousHook = previousAllocationHook;
            BOOST_SCOPE_EXIT_ALL(=) {
                _CrtSetAllocHook(previousHook);
            };

            for (auto iteration = 0; iteration < 100; ++iteration) {
                machine.run(function, arguments);
            }
        }

        BOOST_TEST(callAllocations == (count <= 4 ? 0U : 100U));
        BOOST_TEST(machine.getIntegerResult() == 42);
        BOOST_TEST(machine.captureExecutionState().stackIndex == 0U);
    }
}

BOOST_AUTO_TEST_CASE(stored_property_paths_add_no_allocations_beyond_returned_value_storage) {
    Runtime runtime;
    auto& manager = runtime.getTypeManager();
    const auto emptyId = manager.registerStruct("LookupEmpty");
    const auto nestedId = manager.registerStruct("LookupNested");
    const auto rootId = manager.registerStruct("LookupRoot");
    const auto* emptyType = runtime.getTypeByName("LookupEmpty");
    const auto* nestedType = runtime.getTypeByName("LookupNested");
    const auto* rootType = runtime.getTypeByName("LookupRoot");
    const NomadString nestedName = "nested_property_name_longer_than_inline_string_storage";
    const NomadString leafName = "empty_property_name_longer_than_inline_string_storage";
    BOOST_REQUIRE(manager.registerScriptStructProperty(nestedId, leafName, emptyType).isValid());
    BOOST_REQUIRE(manager.registerScriptStructProperty(rootId, nestedName, nestedType).isValid());
    const auto directId = manager.registerScriptStructProperty(rootId, "direct", emptyType);
    BOOST_REQUIRE(directId.isValid());
    manager.finalizeCompositeFieldLayout(rootId);
    const InterpreterValue receiver(rootType, std::span<const RuntimeValue>{});
    const NomadString decoratedPath = "[" + nestedName + "." + leafName + "]";
    const auto path = NomadStringView{decoratedPath}.substr(1, decoratedPath.size() - 2);
    VirtualMachine machine(&runtime);
    const Interpreter interpreter(machine);
    auto completedReads = 0;
    callAllocations = 0;
    previousAllocationHook = _CrtSetAllocHook(countCallAllocations);
    {
        const auto previousHook = previousAllocationHook;
        BOOST_SCOPE_EXIT_ALL(=) {
            _CrtSetAllocHook(previousHook);
        };

        for (auto index = 0; index < 100; ++index) {
            const auto value = interpreter.getProperty(receiver, directId);
            completedReads += value.getType() == emptyType && value.getStorage().empty();
        }
    }

    const auto resultStorageAllocations = callAllocations;
    callAllocations = 0;
    previousAllocationHook = _CrtSetAllocHook(countCallAllocations);
    {
        const auto previousHook = previousAllocationHook;
        BOOST_SCOPE_EXIT_ALL(=) {
            _CrtSetAllocHook(previousHook);
        };

        for (auto index = 0; index < 100; ++index) {
            const auto value = interpreter.getProperty(receiver, path);
            completedReads += value.getType() == emptyType && value.getStorage().empty();
        }
    }

    BOOST_TEST(callAllocations == resultStorageAllocations);
    BOOST_TEST(completedReads == 200);
}

BOOST_AUTO_TEST_CASE(prepared_script_execution_guards_do_not_allocate) {
    Runtime runtime;
    const auto functionId = runtime.registerScriptFunction("allocationFreeExecution", "allocation.nomad", "");
    auto* function = runtime.getScriptFunction(functionId);
    function->setReturnType(runtime.getIntegerType());
    auto compiler = runtime.createCompiler();
    function->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(function);
    compiler->addLoadValue(runtime.getIntegerType(), RuntimeValue{NomadInteger{17}}, ExpressionTarget::Result);
    compiler->addFunctionReturn(function);
    function->setFunctionEnd(runtime.getInstructions().size());
    runtime.initializeScripts();
    VirtualMachine machine(&runtime, 64);
    machine.executeFunction(*function);
    callAllocations = 0;
    previousAllocationHook = _CrtSetAllocHook(countCallAllocations);
    {
        const auto previousHook = previousAllocationHook;
        BOOST_SCOPE_EXIT_ALL(=) {
            _CrtSetAllocHook(previousHook);
        };

        for (auto index = 0; index < 100; ++index) {
            machine.executeFunction(*function);
        }
    }

    BOOST_TEST(callAllocations == 0U);
    BOOST_TEST(machine.getIntegerResult() == 17);
}

BOOST_AUTO_TEST_CASE(indexer_argument_guards_do_not_allocate) {
    Runtime runtime;
    const auto ownerId = runtime.registerClass("AllocationIndexer", ClassMode::Closed);
    BOOST_REQUIRE(ownerId.isValid());
    const auto* owner = runtime.getType(ownerId).value();
    RuntimeValue receiver;
    const RuntimeValueGuard receiverGuard(*owner, &receiver);
    receiver.setObjectValue(new NomadObject);
    const auto integerId = runtime.getIntegerType()->getId();
    const auto indexerId = runtime.registerNativeClassIndexer(ownerId, {
        .valueTypeId = integerId,
        .indexTypes = {integerId},
        .getter = [](VirtualMachine* active) { active->setIntegerResult(71); },
        .setter = [](VirtualMachine*) {}
    });
    BOOST_REQUIRE(indexerId.isValid());
    auto compiler = runtime.createCompiler();
    const auto readAddress = compiler->addOpCode(OpCodes::op_native_index_get);
    compiler->addId(indexerId.id);
    const auto writeAddress = compiler->addOpCode(OpCodes::op_native_index_set);
    compiler->addId(indexerId.id);
    const std::vector<RuntimeValue> readArguments{RuntimeValue{NomadInteger{17}}};
    const std::vector<RuntimeValue> writeArguments{
        RuntimeValue{NomadInteger{17}}, RuntimeValue{NomadInteger{23}}
    };
    VirtualMachine machine(&runtime, 64);
    callAllocations = 0;
    previousAllocationHook = _CrtSetAllocHook(countCallAllocations);
    {
        const auto previousHook = previousAllocationHook;
        BOOST_SCOPE_EXIT_ALL(=) {
            _CrtSetAllocHook(previousHook);
        };

        for (auto index = 0; index < 100; ++index) {
            for (const auto write : {false, true}) {
                machine.runNativeIndexer(
                    indexerId, write ? writeArguments : readArguments, receiver, write
                );
                machine.pushThis();
                machine.pushObjectValue(receiver);
                if (write) {
                    machine.pushInteger(23);
                }

                machine.pushInteger(17);
                executeInstruction(runtime, machine, write ? writeAddress : readAddress);
            }
        }
    }

    BOOST_TEST(callAllocations == 0U);
    BOOST_TEST(machine.captureExecutionState().stackIndex == 0U);
    BOOST_TEST(receiver.getObjectValue()->getReferenceCount() == 1U);
}

BOOST_AUTO_TEST_CASE(runtime_value_and_scope_exit_guard_bookkeeping_does_not_allocate) {
    Runtime runtime;
    const auto* type = runtime.getIntegerType();
    RuntimeValue value{NomadInteger{17}};
    auto scopeCleanups = 0;
    callAllocations = 0;
    previousAllocationHook = _CrtSetAllocHook(countCallAllocations);
    {
        const auto previousHook = previousAllocationHook;
        BOOST_SCOPE_EXIT_ALL(=) {
            _CrtSetAllocHook(previousHook);
        };

        for (auto index = 0; index < 100; ++index) {
            {
                const RuntimeValueGuard guard(*type, &value);
            }

            {
                RuntimeValueGuard guard(*type, &value);
                guard.release();
            }

            BOOST_SCOPE_EXIT_ALL(&) {
                ++scopeCleanups;
            };
        }
    }

    BOOST_TEST(callAllocations == 0U);
    BOOST_TEST(scopeCleanups == 100);
    BOOST_TEST(value.getIntegerValue() == 17);
}

BOOST_AUTO_TEST_CASE(allocation_hook_restoration_runs_before_failure_escapes) {
    const auto originalHook = _CrtSetAllocHook(nullptr);
    BOOST_SCOPE_EXIT_ALL(=) {
        _CrtSetAllocHook(originalHook);
    };

    const auto originalChain = previousAllocationHook;
    previousAllocationHook = nullptr;
    BOOST_SCOPE_EXIT_ALL(=) {
        previousAllocationHook = originalChain;
    };

    const auto failWithHook = [] {
        const auto previousHook = _CrtSetAllocHook(countCallAllocations);
        BOOST_SCOPE_EXIT_ALL(=) {
            _CrtSetAllocHook(previousHook);
        };

        throw NomadException("Expected allocation-hook scope failure");
    };

    BOOST_CHECK_THROW(failWithHook(), NomadException);
    const auto restoredHook = _CrtSetAllocHook(nullptr);
    BOOST_CHECK(restoredHook == nullptr);
}

BOOST_AUTO_TEST_CASE(struct_creation_uses_prepared_stack_storage_without_allocating) {
    Runtime runtime;
    const auto typeId = runtime.registerStruct("AllocationFreeStruct");
    const auto* type = runtime.getTypeByName("AllocationFreeStruct");
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(typeId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(typeId, "text", runtime.getStringType()).isValid());
    runtime.getTypeManager().finalizeCompositeFieldLayout(typeId);
    const auto nativeId = runtime.registerNativeStructConstructor(typeId, {
        .parameters = {{runtime.getIntegerType()->getId(), "number", ""}},
        .function = [](VirtualMachine* active) {
            active->getStorageParameter(1, 0) = active->getParameter(0);
        }
    });
    const auto scriptId = runtime.registerScriptStructConstructor(typeId,
        {defParameter("number", runtime.getIntegerType(), ""), defParameter("unused", runtime.getIntegerType(), "")},
        "", "");
    BOOST_REQUIRE(nativeId.isValid());
    BOOST_REQUIRE(scriptId.isValid());
    auto* script = runtime.getScriptCallable(scriptId);
    auto compiler = runtime.createCompiler();
    script->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(script);
    compiler->addOpCode(OpCodes::op_parameter_load_r);
    compiler->addIndex(0);
    compiler->addOpCode(OpCodes::op_storage_parameter_set);
    compiler->addIndex(2);
    compiler->addIndex(0);
    compiler->addFunctionReturn(script);
    script->setFunctionEnd(runtime.getInstructions().size());
    VirtualMachine machine(&runtime, 64);
    std::array<RuntimeValue, 2> storage;
    type->initValue(storage.data());
    type->freeValue(storage.data());
    callAllocations = 0;
    previousAllocationHook = _CrtSetAllocHook(countCallAllocations);
    {
        const auto previousHook = previousAllocationHook;
        BOOST_SCOPE_EXIT_ALL(=) {
            _CrtSetAllocHook(previousHook);
        };

        for (auto index = 0; index < 100; ++index) {
            machine.pushStorageAddress(storage.data());
            machine.pushInteger(17);
            machine.createNativeStruct(storage.data(), nativeId);
            machine.pop2();
            type->freeValue(storage.data());
            machine.pushStorageAddress(storage.data());
            machine.pushInteger(0);
            machine.pushInteger(19);
            machine.createScriptStruct(storage.data(), scriptId);
            machine.pop3();
            type->freeValue(storage.data());
        }
    }

    BOOST_TEST(callAllocations == 0U);
    BOOST_TEST(storage[0].getIntegerValue() == 19);
    BOOST_TEST(machine.peekStack(0).getIntegerValue() == 0);
}

BOOST_AUTO_TEST_CASE(object_creation_adds_no_allocations_beyond_object_storage) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto nativeTypeId = runtime.registerClass("CountedNative", ClassMode::Closed);
    const auto* nativeType = runtime.getTypeByName("CountedNative")->asClassType();
    runtime.getTypeManager().finalizeCompositeFieldLayout(nativeTypeId);
    const auto nativeId = runtime.registerNativeClassConstructor(nativeTypeId, {
        .parameters = {},
        .function = [nativeType](VirtualMachine* active) {
            active->getResult().setObjectValue(new ScriptObject(nativeType));
        }
    });
    BOOST_REQUIRE(nativeId.isValid());
    fixture.addFunction("main", "class CountedScript\nend");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto* scriptType = runtime.getTypeByName("CountedScript")->asClassType();
    const auto scriptId = scriptType->getConstructors()[0]->getId();
    VirtualMachine machine(&runtime, 64);
    // Debug standard-library containers also allocate iterator proxies inside each object.
    callAllocations = 0;
    previousAllocationHook = _CrtSetAllocHook(countCallAllocations);
    {
        const auto previousHook = previousAllocationHook;
        BOOST_SCOPE_EXIT_ALL(=) {
            _CrtSetAllocHook(previousHook);
        };

        for (auto index = 0; index < 100; ++index) {
            (new ScriptObject(nativeType))->releaseRef();
            (new ScriptObject(scriptType))->releaseRef();
        }
    }

    const auto objectAllocations = callAllocations;
    callAllocations = 0;
    previousAllocationHook = _CrtSetAllocHook(countCallAllocations);
    {
        const auto previousHook = previousAllocationHook;
        BOOST_SCOPE_EXIT_ALL(=) {
            _CrtSetAllocHook(previousHook);
        };

        for (auto index = 0; index < 100; ++index) {
            machine.createNativeObject(nativeId)->releaseRef();
            machine.createScriptObject(scriptId)->releaseRef();
        }
    }

    BOOST_TEST(callAllocations == objectAllocations);
    BOOST_TEST(machine.peekStack(0).getIntegerValue() == 0);
}

BOOST_AUTO_TEST_CASE(script_and_native_call_bookkeeping_does_not_allocate) {
    Runtime runtime;
    const auto& frame = registerFrameFunction(runtime);
    VirtualMachine machine(&runtime, 64);
    std::array<RuntimeValue, 2> storage;
    const auto callback = runtime.registerNativeFunction("writeStorage", [](VirtualMachine* active) {
        active->getStorageParameter(0, 0).setIntegerValue(17);
    }, {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(callback.isValid());
    callAllocations = 0;
    previousAllocationHook = _CrtSetAllocHook(countCallAllocations);
    {
        const auto previousHook = previousAllocationHook;
        BOOST_SCOPE_EXIT_ALL(=) {
            _CrtSetAllocHook(previousHook);
        };

        for (auto index = 0; index < 100; ++index) {
            machine.callScriptFunction(frame, 0);
            machine.pushStorageAddress(storage.data());
            machine.callScriptFunction(frame, 0);
            machine.pushStorageAddress(storage.data() + 1);
            machine.callNativeFunction(callback);
            machine.pop1();
            machine.returnFunction(0);
            machine.pop1();
            machine.returnFunction(0);
        }
    }

    BOOST_TEST(callAllocations == 0U);
    BOOST_TEST(storage[1].getIntegerValue() == 17);
    BOOST_TEST(machine.peekStack(0).getIntegerValue() == 0);
}
#endif

BOOST_AUTO_TEST_SUITE_END()
