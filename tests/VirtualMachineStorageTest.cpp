// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/script/InstructionDump.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptObject.hpp>
#include <nomad/script/StructType.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <array>
#include <limits>
#include <sstream>
#include <utility>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

using namespace nomad;

namespace {

void executeInstruction(Runtime& runtime, VirtualMachine& machine, const NomadIndex address) {
    machine.jump(address + 1);
    runtime.getInstructions()[address].fn(&machine);
}

#if defined(_MSC_VER) && defined(_DEBUG)
thread_local NomadIndex callAllocations = 0;
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
    }
    return previousAllocationHook == nullptr ||
        previousAllocationHook(operation, data, size, blockType, request, file, line);
}
#endif

}

BOOST_AUTO_TEST_SUITE(virtual_machine_storage)

BOOST_AUTO_TEST_CASE(contiguous_parameter_blocks_and_local_views_keep_physical_order) {
    Runtime runtime;
    VirtualMachine machine(&runtime, 32);
    machine.pushInteger(5);
    machine.pushInteger(4);
    machine.pushInteger(1);
    machine.pushInteger(2);
    machine.pushInteger(3);
    machine.callScriptFunction(0);
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
    machine.callScriptFunction(0);
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
    VirtualMachine machine(&runtime, 32);
    machine.pushInteger(1);
    machine.pushInteger(2);
    machine.pushInteger(3);
    machine.callScriptFunction(0);
    machine.reserveLocals(1);
    machine.getLocalStorage(0, 1)[0].setIntegerValue(19);
    const auto* enclosing = machine.getEnclosingParameterStorage(0, 3).data();
    const auto id = runtime.registerNativeFunction("inspectStorage",
        [&](VirtualMachine* active) {
            BOOST_TEST(active->getParameterStorage(0, 1)[0].getIntegerValue() == 99);
            BOOST_TEST(std::as_const(*active).getEnclosingParameterStorage(0, 3).data() == enclosing);
            active->getEnclosingParameterStorage(0, 3)[0].setIntegerValue(7);
            active->pushInteger(8);
            active->callScriptFunction(0);
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
    machine.callScriptFunction(0);
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
    machine.callScriptFunction(0);
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
    auto compiler = runtime.createCompiler();
    std::array<RuntimeValue, 3> storage;
    storage[0].setIntegerValue(17);
    storage[1].setStringValue("source");
    storage[2].setStringValue("replaced");
    VirtualMachine machine(&runtime, 32);
    machine.pushStorageAddress(storage.data());
    machine.callScriptFunction(0);
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
    const auto* success = runtime.getFunction(runtime.getFunctionId("success"))->asScriptFunction();
    const auto* failure = runtime.getFunction(runtime.getFunctionId("failure"))->asScriptFunction();
    std::array<RuntimeValue, 2> receiver;
    std::array<RuntimeValue, 2> destination;
    VirtualMachine machine(&runtime, 128);
    machine.pushStorageAddress(destination.data());
    machine.pushStorageAddress(receiver.data());
    machine.pushInteger(7);
    machine.callScriptFunction(0);
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
    machine.callScriptMethod(0);
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
    const auto typeId = runtime.registerStruct("CallStorage");
    const auto* type = runtime.getTypeByName("CallStorage")->asStructType();
    BOOST_REQUIRE(runtime.registerProperty(typeId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(typeId, "label", runtime.getStringType()).isValid());
    runtime.finalizeCompositeFieldLayout(typeId);
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
    compiler->addIndex(callee->getFunctionStart());
    compiler->addOpCode(OpCodes::op_pop_3);
    compiler->addLoadFunctionVariable(caller->getVariableOffset(destinationId), runtime.getIntegerType(),
        ExpressionTarget::Result);
    compiler->addFunctionReturn(caller);
    caller->setFunctionEnd(runtime.getInstructions().size());
    for (auto index = 0; index < 3; ++index) {
        RuntimeValue result;
        runtime.executeFunction(callerId, {}, result);
        BOOST_TEST(result.getIntegerValue() == 19);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == before);
    }
}

BOOST_AUTO_TEST_CASE(calls_need_only_existing_frames_and_requested_pointer_slots) {
    Runtime runtime;
    VirtualMachine machine(&runtime, 8);
    machine.pushStorageAddress(machine.getParameterAddress(0));
    machine.callScriptFunction(0);
    BOOST_CHECK_THROW(machine.callScriptFunction(0), VirtualMachineException);
    machine.returnFunction(0);
    machine.pop1();
    BOOST_TEST(machine.peekStack(0).getIntegerValue() == 0);
    machine.callScriptFunction(0);
    machine.returnFunction(0);
    BOOST_TEST(machine.peekStack(0).getIntegerValue() == 0);
    VirtualMachine emptyCall(&runtime, 5);
    emptyCall.callScriptFunction(0);
    BOOST_CHECK_THROW(emptyCall.reserveLocals(1), VirtualMachineException);
    emptyCall.returnFunction(0);
    BOOST_TEST(emptyCall.peekStack(0).getIntegerValue() == 0);
}

#if defined(_MSC_VER) && defined(_DEBUG)
BOOST_AUTO_TEST_CASE(struct_creation_uses_prepared_stack_storage_without_allocating) {
    Runtime runtime;
    const auto typeId = runtime.registerStruct("AllocationFreeStruct");
    const auto* type = runtime.getTypeByName("AllocationFreeStruct");
    BOOST_REQUIRE(runtime.registerScriptStructProperty(typeId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerScriptStructProperty(typeId, "text", runtime.getStringType()).isValid());
    runtime.finalizeCompositeFieldLayout(typeId);
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
    try {
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
    } catch (...) {
        _CrtSetAllocHook(previousAllocationHook);
        throw;
    }
    _CrtSetAllocHook(previousAllocationHook);
    BOOST_TEST(callAllocations == 0U);
    BOOST_TEST(storage[0].getIntegerValue() == 19);
    BOOST_TEST(machine.peekStack(0).getIntegerValue() == 0);
}

BOOST_AUTO_TEST_CASE(object_creation_adds_no_allocations_beyond_object_storage) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto nativeTypeId = runtime.registerClass("CountedNative", ClassMode::Closed);
    const auto* nativeType = runtime.getTypeByName("CountedNative")->asClassType();
    runtime.finalizeCompositeFieldLayout(nativeTypeId);
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
    try {
        for (auto index = 0; index < 100; ++index) {
            (new ScriptObject(nativeType))->releaseRef();
            (new ScriptObject(scriptType))->releaseRef();
        }
    } catch (...) {
        _CrtSetAllocHook(previousAllocationHook);
        throw;
    }
    _CrtSetAllocHook(previousAllocationHook);
    const auto objectAllocations = callAllocations;
    callAllocations = 0;
    previousAllocationHook = _CrtSetAllocHook(countCallAllocations);
    try {
        for (auto index = 0; index < 100; ++index) {
            machine.createNativeObject(nativeId)->releaseRef();
            machine.createScriptObject(scriptId)->releaseRef();
        }
    } catch (...) {
        _CrtSetAllocHook(previousAllocationHook);
        throw;
    }
    _CrtSetAllocHook(previousAllocationHook);
    BOOST_TEST(callAllocations == objectAllocations);
    BOOST_TEST(machine.peekStack(0).getIntegerValue() == 0);
}

BOOST_AUTO_TEST_CASE(script_and_native_call_bookkeeping_does_not_allocate) {
    Runtime runtime;
    VirtualMachine machine(&runtime, 64);
    std::array<RuntimeValue, 2> storage;
    const auto callback = runtime.registerNativeFunction("writeStorage", [](VirtualMachine* active) {
        active->getStorageParameter(0, 0).setIntegerValue(17);
    }, {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(callback.isValid());
    callAllocations = 0;
    previousAllocationHook = _CrtSetAllocHook(countCallAllocations);
    try {
        for (auto index = 0; index < 100; ++index) {
            machine.callScriptFunction(0);
            machine.pushStorageAddress(storage.data());
            machine.callScriptFunction(0);
            machine.pushStorageAddress(storage.data() + 1);
            machine.callNativeFunction(callback);
            machine.pop1();
            machine.returnFunction(0);
            machine.pop1();
            machine.returnFunction(0);
        }
    } catch (...) {
        _CrtSetAllocHook(previousAllocationHook);
        throw;
    }
    _CrtSetAllocHook(previousAllocationHook);
    BOOST_TEST(callAllocations == 0U);
    BOOST_TEST(storage[1].getIntegerValue() == 17);
    BOOST_TEST(machine.peekStack(0).getIntegerValue() == 0);
}
#endif

BOOST_AUTO_TEST_SUITE_END()
