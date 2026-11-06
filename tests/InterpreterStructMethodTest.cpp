#include <LanguageTestFixture.hpp>

#include <nomad/compiler/Compiler.hpp>

#include <nomad/script/Interpreter.hpp>
#include <nomad/script/NativeCallable.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/ScriptObject.hpp>
#include <nomad/script/StructType.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <array>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

using namespace nomad;
using namespace nomad::test;

namespace {

void addStructMethodSources(LanguageTestFixture& fixture) {
    fixture.addFunction("structMethodTypes",
        "class MethodSharedOwner\nnumber = 5\nend\n"
        "struct HostMethodEmpty\n"
        "fun emptyReturn\nreturn this\nend\n"
        "fun make number:int\nreturn new HostMethodValue number \"made\"\nend\n"
        "fun scalar\nreturn 7\nend\n"
        "fun clear\nreturn\nend\nend\n"
        "struct HostMethodValue\nnumber = 7\ntext = \"default\"\nowner = new MethodSharedOwner\n"
        "init number:int text:string\nthis.number = number\nthis.text = text\nend\n"
        "fun mutate snapshot:HostMethodValue empty:HostMethodEmpty delta:int\n"
        "this.number = this.number + delta\nthis.text = \"updated\"\n"
        "this.owner.number = this.number\nsnapshot.number = 99\nsnapshot.text = \"argument copy\"\nreturn this\nend\n"
        "fun readText\nreturn this.text\nend\n"
        "fun readOwner\nreturn this.owner\nend\n"
        "fun set number:int\nthis.number = number\nreturn\nend\n"
        "fun empty\nthis.number = this.number + 1\nreturn new HostMethodEmpty\nend\nend\n"
        "struct HostMethodOuter\nleaf = new HostMethodValue 9 \"nested\"\nlabel = \"outer\"\n"
        "fun update number:int\nthis.leaf.number = number\nthis.leaf.text = \"nested update\"\nreturn this.leaf\nend\nend\n"
        "return 0");
}

} // namespace

BOOST_AUTO_TEST_SUITE(interpreter_struct_methods)

BOOST_AUTO_TEST_CASE(script_methods_return_independent_receiver_and_result_values_without_implicit_writeback) {
    LanguageTestFixture fixture;
    addStructMethodSources(fixture);
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    const auto* type = runtime.getTypeByName("HostMethodValue");
    const auto* emptyType = runtime.getTypeByName("HostMethodEmpty");
    const auto mutate = runtime.resolveMethod(type, "mutate", {type, emptyType, runtime.getIntegerType()});
    const auto set = runtime.resolveMethod(type, "set", {runtime.getIntegerType()});
    const auto readText = runtime.resolveMethod(type, "readText", {});
    const auto readOwner = runtime.resolveMethod(type, "readOwner", {});
    const auto emptyReturn = runtime.resolveMethod(type, "empty", {});
    BOOST_REQUIRE(mutate.isResolved() && set.isResolved() && readText.isResolved() &&
        readOwner.isResolved() && emptyReturn.isResolved());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    const auto instructionsBefore = runtime.getInstructions().size();
    {
        std::optional<StructMethodResult> retained;
        std::optional<StructMethodResult> retainedText;
        std::optional<StructMethodResult> retainedOwner;
        {
            auto machine = runtime.createVirtualMachine();
            const Interpreter interpreter(*machine);
            InterpreterContext context(interpreter);
            BOOST_REQUIRE(interpreter.execute("value = new HostMethodValue 17 \"source\"", context));
            const InterpreterValue receiver(type, context.getVariableStorage("value"));
            const InterpreterValue empty(emptyType, std::span<const RuntimeValue>{});
            const InterpreterValue delta(runtime.getIntegerType(), RuntimeValue{NomadInteger{11}});
            const std::array arguments{receiver, empty, delta};
            context.setResult(runtime.getIntegerType(), RuntimeValue{NomadInteger{99}});
            RuntimeValue savedResult{NomadString{"saved struct result"}};
            RuntimeValue savedIntermediate{NomadString{"saved struct intermediate"}};
            machine->setResult(savedResult);
            machine->setIntermediate(savedIntermediate);
            machine->pushInteger(1234);
            retained = interpreter.runStructMethod(receiver, mutate.id, arguments);
            BOOST_TEST(retained->receiver.getType() == type);
            BOOST_TEST(retained->result.getType() == type);
            BOOST_REQUIRE_EQUAL(retained->receiver.getStorage().size(), 3U);
            BOOST_REQUIRE_EQUAL(retained->result.getStorage().size(), 3U);
            BOOST_TEST(retained->receiver.getStorage().data() != receiver.getStorage().data());
            BOOST_TEST(retained->receiver.getStorage().data() != retained->result.getStorage().data());
            BOOST_TEST(retained->receiver.getStorage()[0].getIntegerValue() == 28);
            BOOST_TEST(retained->result.getStorage()[0].getIntegerValue() == 28);
            BOOST_TEST(retained->receiver.getStorage()[1].getStringValue() == "updated");
            BOOST_TEST(receiver.getStorage()[0].getIntegerValue() == 17);
            BOOST_TEST(receiver.getStorage()[1].getStringValue() == "source");
            BOOST_TEST(arguments[0].getStorage()[0].getIntegerValue() == 17);
            BOOST_TEST(context.getVariableStorage("value")[0].getIntegerValue() == 17);
            BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 99);
            BOOST_TEST(retained->receiver.getStorage()[2].getObjectValue() == receiver.getStorage()[2].getObjectValue());
            BOOST_TEST(receiver.getStorage()[2].asObject<ScriptObject>()->getField(0).getIntegerValue() == 28);
            const InterpreterValue replacement(runtime.getIntegerType(), RuntimeValue{NomadInteger{41}});
            const auto changed = interpreter.runStructMethod(retained->receiver, set.id, {&replacement, 1});
            BOOST_TEST(changed.receiver.getStorage()[0].getIntegerValue() == 41);
            BOOST_TEST(changed.result.getType()->isVoid());
            BOOST_TEST(changed.result.getStorage().data() == nullptr);
            BOOST_TEST(retained->receiver.getStorage()[0].getIntegerValue() == 28);
            const auto emptyResult = interpreter.runStructMethod(changed.receiver, emptyReturn.id, {});
            BOOST_TEST(emptyResult.receiver.getStorage()[0].getIntegerValue() == 42);
            BOOST_TEST(emptyResult.result.getType() == emptyType);
            BOOST_TEST(emptyResult.result.getStorage().data() == nullptr);
            retainedText = interpreter.runStructMethod(receiver, readText.id, {});
            retainedOwner = interpreter.runStructMethod(receiver, readOwner.id, {});
            BOOST_TEST(retainedText->result.getValue().getStringValue() == "source");
            BOOST_TEST(retainedOwner->result.getValue().getObjectValue() == receiver.getStorage()[2].getObjectValue());
            context.setVariable("value", retained->receiver.getType(), retained->receiver.getStorage());
            context.setResult(retained->result);
            BOOST_TEST(context.getVariableStorage("value")[0].getIntegerValue() == 28);
            BOOST_TEST(context.getResult()->getStorage()[0].getIntegerValue() == 28);
            BOOST_TEST(receiver.getStorage()[0].getIntegerValue() == 17);
            BOOST_TEST(machine->getResult().getObjectValue() == savedResult.getObjectValue());
            BOOST_TEST(machine->getIntermediate().getObjectValue() == savedIntermediate.getObjectValue());
            BOOST_TEST(savedResult.getObjectValue()->getReferenceCount() == 1U);
            BOOST_TEST(savedIntermediate.getObjectValue()->getReferenceCount() == 1U);
            BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
            BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
            machine->pop1();
            machine->setResult(RuntimeValue{});
            machine->setIntermediate(RuntimeValue{});
            savedResult.freeObjectValue();
            savedIntermediate.freeObjectValue();
        }
        BOOST_TEST(retained->receiver.getStorage()[1].getStringValue() == "updated");
        BOOST_TEST(retained->result.getStorage()[1].getStringValue() == "updated");
        BOOST_TEST(retainedText->result.getValue().getStringValue() == "source");
        BOOST_TEST(retainedOwner->result.getValue().asObject<ScriptObject>()->getField(0).getIntegerValue() == 28);
    }
    BOOST_TEST(runtime.getInstructions().size() == instructionsBefore);
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
}

BOOST_AUTO_TEST_CASE(nested_script_receivers_and_empty_receiver_result_combinations_keep_complete_types) {
    LanguageTestFixture fixture;
    addStructMethodSources(fixture);
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    const auto* outerType = runtime.getTypeByName("HostMethodOuter");
    const auto* valueType = runtime.getTypeByName("HostMethodValue");
    const auto* emptyType = runtime.getTypeByName("HostMethodEmpty");
    const auto update = runtime.resolveMethod(outerType, "update", {runtime.getIntegerType()});
    const auto emptyReturn = runtime.resolveMethod(emptyType, "emptyReturn", {});
    const auto make = runtime.resolveMethod(emptyType, "make", {runtime.getIntegerType()});
    const auto scalar = runtime.resolveMethod(emptyType, "scalar", {});
    const auto clear = runtime.resolveMethod(emptyType, "clear", {});
    BOOST_REQUIRE(update.isResolved() && emptyReturn.isResolved() && make.isResolved() &&
        scalar.isResolved() && clear.isResolved());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    {
        auto machine = runtime.createVirtualMachine();
        const Interpreter interpreter(*machine);
        const auto outer = interpreter.construct(outerType->asStructType()->getConstructors().front()->getId(), {});
        const InterpreterValue number(runtime.getIntegerType(), RuntimeValue{NomadInteger{41}});
        const auto updated = interpreter.runStructMethod(outer, update.id, {&number, 1});
        BOOST_TEST(updated.receiver.getType() == outerType);
        BOOST_REQUIRE_EQUAL(updated.receiver.getStorage().size(), 4U);
        BOOST_TEST(updated.result.getType() == valueType);
        BOOST_REQUIRE_EQUAL(updated.result.getStorage().size(), 3U);
        BOOST_TEST(updated.receiver.getStorage()[0].getIntegerValue() == 41);
        BOOST_TEST(updated.result.getStorage()[0].getIntegerValue() == 41);
        BOOST_TEST(updated.result.getStorage()[1].getStringValue() == "nested update");
        BOOST_TEST(updated.receiver.getStorage()[3].getStringValue() == "outer");
        BOOST_TEST(outer.getStorage()[0].getIntegerValue() == 9);
        BOOST_TEST(outer.getStorage()[1].getStringValue() == "nested");
        const InterpreterValue empty(emptyType, std::span<const RuntimeValue>{});
        const auto emptyResult = interpreter.runStructMethod(empty, emptyReturn.id, {});
        BOOST_TEST(emptyResult.receiver.getType() == emptyType);
        BOOST_TEST(emptyResult.result.getType() == emptyType);
        BOOST_TEST(emptyResult.receiver.getStorage().data() == nullptr);
        BOOST_TEST(emptyResult.result.getStorage().data() == nullptr);
        const auto created = interpreter.runStructMethod(empty, make.id, {&number, 1});
        BOOST_TEST(created.receiver.getType() == emptyType);
        BOOST_TEST(created.receiver.getStorage().data() == nullptr);
        BOOST_TEST(created.result.getType() == valueType);
        BOOST_REQUIRE_EQUAL(created.result.getStorage().size(), 3U);
        BOOST_TEST(created.result.getStorage()[0].getIntegerValue() == 41);
        BOOST_TEST(created.result.getStorage()[1].getStringValue() == "made");
        const auto scalarResult = interpreter.runStructMethod(empty, scalar.id, {});
        BOOST_TEST(scalarResult.receiver.getStorage().data() == nullptr);
        BOOST_TEST(scalarResult.result.getValue().getIntegerValue() == 7);
        const auto cleared = interpreter.runStructMethod(empty, clear.id, {});
        BOOST_TEST(cleared.receiver.getType() == emptyType);
        BOOST_TEST(cleared.result.getType()->isVoid());
        BOOST_TEST(cleared.receiver.getStorage().data() == nullptr);
        BOOST_TEST(cleared.result.getStorage().data() == nullptr);
    }
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
}

BOOST_AUTO_TEST_CASE(native_methods_use_distinct_receiver_argument_and_result_storage_and_allow_reentry) {
    Runtime runtime;
    const auto valueId = runtime.registerStruct("NativeHostMethodValue");
    const auto emptyId = runtime.registerStruct("NativeHostMethodEmpty");
    const auto* type = runtime.getTypeByName("NativeHostMethodValue");
    const auto* emptyType = runtime.getTypeByName("NativeHostMethodEmpty");
    BOOST_REQUIRE(runtime.registerProperty(valueId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(valueId, "text", runtime.getStringType()).isValid());
    runtime.finalizeCompositeFieldLayout(valueId);
    runtime.finalizeCompositeFieldLayout(emptyId);
    const auto readId = runtime.addMethod(valueId, {
        .name = "read", .parameters = {}, .returnTypeId = runtime.getIntegerType()->getId(),
        .function = [](VirtualMachine* active) { active->setIntegerResult(active->getStorageParameter(0, 0).getIntegerValue()); }
    });
    const auto mutateId = runtime.addMethod(valueId, {
        .name = "mutate",
        .parameters = {{valueId, "snapshot", ""}, {emptyId, "empty", ""}, {runtime.getIntegerType()->getId(), "delta", ""}},
        .returnTypeId = valueId,
        .function = [type, readId](VirtualMachine* active) {
            auto* receiver = &active->getStorageParameter(3, 0);
            auto* destination = &active->getStorageParameter(4, 0);
            const auto argument = active->getParameterStorage(0, 2);
            BOOST_TEST(receiver != argument.data());
            BOOST_TEST(receiver != destination);
            BOOST_TEST(destination != argument.data());
            receiver[0].setIntegerValue(receiver[0].getIntegerValue() + active->getIntegerParameter(2));
            receiver[1].freeObjectValue();
            receiver[1].setStringValue("native update");
            argument[0].setIntegerValue(99);
            argument[1].freeObjectValue();
            argument[1].setStringValue("argument copy");
            const InterpreterValue snapshot(type, std::span<const RuntimeValue>{receiver, 2});
            const Interpreter nested(*active);
            const auto read = nested.runStructMethod(snapshot, readId, {});
            BOOST_TEST(read.result.getValue().getIntegerValue() == 28);
            BOOST_TEST(active->getIntegerParameter(2) == 11);
            BOOST_TEST(&active->getStorageParameter(3, 0) == receiver);
            BOOST_TEST(&active->getStorageParameter(4, 0) == destination);
            BOOST_CHECK_THROW(static_cast<void>(active->getThis()), VirtualMachineException);
            type->copyValue(receiver, destination);
            destination[0].setIntegerValue(destination[0].getIntegerValue() + 1000);
        }
    });
    BOOST_REQUIRE(readId.isValid() && mutateId.isValid());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    {
        std::optional<StructMethodResult> retained;
        {
            auto machine = runtime.createVirtualMachine();
            const Interpreter interpreter(*machine);
            std::array<RuntimeValue, 2> source{RuntimeValue{NomadInteger{17}}, RuntimeValue{NomadString{"source"}}};
            const InterpreterValue receiver(type, source);
            type->freeValue(source.data());
            const InterpreterValue empty(emptyType, std::span<const RuntimeValue>{});
            const std::array arguments{
                receiver, empty, InterpreterValue(runtime.getIntegerType(), RuntimeValue{NomadInteger{11}})
            };
            machine->pushInteger(1234);
            machine->setIntegerResult(41);
            machine->setIntegerIntermediate(43);
            retained = interpreter.runStructMethod(receiver, mutateId, arguments);
            BOOST_TEST(retained->receiver.getStorage()[0].getIntegerValue() == 28);
            BOOST_TEST(retained->result.getStorage()[0].getIntegerValue() == 1028);
            BOOST_TEST(retained->receiver.getStorage()[1].getStringValue() == "native update");
            BOOST_TEST(receiver.getStorage()[0].getIntegerValue() == 17);
            BOOST_TEST(receiver.getStorage()[1].getStringValue() == "source");
            BOOST_TEST(arguments[0].getStorage()[0].getIntegerValue() == 17);
            BOOST_TEST(arguments[0].getStorage()[1].getStringValue() == "source");
            BOOST_TEST(machine->getIntegerResult() == 41);
            BOOST_TEST(machine->getIntegerIntermediate() == 43);
            BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
            machine->pop1();
        }
        BOOST_TEST(retained->receiver.getStorage()[1].getStringValue() == "native update");
        BOOST_TEST(retained->result.getStorage()[1].getStringValue() == "native update");
    }
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
}

BOOST_AUTO_TEST_CASE(native_empty_and_one_slot_receivers_keep_hidden_pointer_layouts_distinct) {
    Runtime runtime;
    const auto emptyId = runtime.registerStruct("NativeMethodEmpty");
    const auto oneId = runtime.registerStruct("NativeMethodOne");
    const auto* emptyType = runtime.getTypeByName("NativeMethodEmpty");
    const auto* oneType = runtime.getTypeByName("NativeMethodOne");
    BOOST_REQUIRE(runtime.registerProperty(oneId, "number", runtime.getIntegerType()).isValid());
    runtime.finalizeCompositeFieldLayout(emptyId);
    runtime.finalizeCompositeFieldLayout(oneId);
    auto emptyCalls = 0;
    const auto emptyMethodId = runtime.addMethod(emptyId, {
        .name = "empty", .parameters = {{emptyId, "argument", ""}}, .returnTypeId = emptyId,
        .function = [&](VirtualMachine*) { ++emptyCalls; }
    });
    const auto makeId = runtime.addMethod(emptyId, {
        .name = "make", .parameters = {}, .returnTypeId = oneId,
        .function = [](VirtualMachine* active) { active->getStorageParameter(0, 0).setIntegerValue(9); }
    });
    const auto clearId = runtime.addMethod(oneId, {
        .name = "clear", .parameters = {}, .returnTypeId = runtime.getVoidType()->getId(),
        .function = [](VirtualMachine* active) { active->getStorageParameter(0, 0).setIntegerValue(0); }
    });
    BOOST_REQUIRE(emptyMethodId.isValid() && makeId.isValid() && clearId.isValid());
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    const InterpreterValue empty(emptyType, std::span<const RuntimeValue>{});
    const auto same = interpreter.runStructMethod(empty, emptyMethodId, {&empty, 1});
    BOOST_TEST(emptyCalls == 1);
    BOOST_TEST(same.receiver.getType() == emptyType);
    BOOST_TEST(same.result.getType() == emptyType);
    BOOST_TEST(same.receiver.getStorage().data() == nullptr);
    BOOST_TEST(same.result.getStorage().data() == nullptr);
    const auto made = interpreter.runStructMethod(empty, makeId, {});
    BOOST_TEST(made.receiver.getStorage().data() == nullptr);
    BOOST_TEST(made.result.getType() == oneType);
    BOOST_REQUIRE_EQUAL(made.result.getStorage().size(), 1U);
    BOOST_TEST(made.result.getStorage()[0].getIntegerValue() == 9);
    const auto cleared = interpreter.runStructMethod(made.result, clearId, {});
    BOOST_TEST(cleared.receiver.getType() == oneType);
    BOOST_TEST(cleared.receiver.getStorage()[0].getIntegerValue() == 0);
    BOOST_TEST(cleared.result.getType()->isVoid());
    BOOST_TEST(cleared.result.getStorage().data() == nullptr);
    BOOST_TEST(made.result.getStorage()[0].getIntegerValue() == 9);
}

BOOST_AUTO_TEST_CASE(validation_rejects_bad_calls_before_execution_and_source_struct_calls_remain_disabled) {
    Runtime runtime;
    const auto valueId = runtime.registerStruct("CheckedStructReceiver");
    const auto otherId = runtime.registerStruct("OtherStructReceiver");
    const auto* type = runtime.getTypeByName("CheckedStructReceiver");
    BOOST_REQUIRE(runtime.registerProperty(valueId, "number", runtime.getIntegerType()).isValid());
    runtime.finalizeCompositeFieldLayout(valueId);
    runtime.finalizeCompositeFieldLayout(otherId);
    auto calls = 0;
    const auto methodId = runtime.addMethod(valueId, {
        .name = "read", .parameters = {{runtime.getIntegerType()->getId(), "number", ""}},
        .returnTypeId = runtime.getIntegerType()->getId(),
        .function = [&](VirtualMachine* active) { ++calls; active->setIntegerResult(active->getIntegerParameter(0)); }
    });
    const auto functionId = runtime.registerNativeFunction("notStructMethod", [](VirtualMachine*) {},
        {}, runtime.getVoidType(), "");
    const auto uncompiledId = runtime.registerScriptMethod(valueId, "uncompiled", {},
        "uncompiled.nomad", "return 1");
    const auto classId = runtime.registerClass<ClassType>("ClassMethodReceiver", ClassMode::Closed);
    const auto* classType = runtime.getType(classId).value();
    const auto classMethodId = runtime.addMethod(classId, {
        .name = "classOnly", .parameters = {}, .returnTypeId = runtime.getVoidType()->getId(),
        .function = [&](VirtualMachine*) { ++calls; }
    });
    BOOST_REQUIRE(methodId.isValid() && functionId.isValid() && uncompiledId.isValid() && classMethodId.isValid());
    runtime.getScriptCallable(uncompiledId)->setReturnType(runtime.getIntegerType());
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    const std::array<RuntimeValue, 1> source{RuntimeValue{NomadInteger{17}}};
    const InterpreterValue receiver(type, source);
    const InterpreterValue number(runtime.getIntegerType(), RuntimeValue{NomadInteger{3}});
    const InterpreterValue boolean(runtime.getBooleanType(), RuntimeValue{true});
    const InterpreterValue other(runtime.getTypeByName("OtherStructReceiver"), std::span<const RuntimeValue>{});
    const InterpreterValue classReceiver(classType, RuntimeValue{});
    Runtime foreignRuntime;
    const auto foreignId = foreignRuntime.registerStruct("CheckedStructReceiver");
    BOOST_REQUIRE(foreignRuntime.registerProperty(foreignId, "number", foreignRuntime.getIntegerType()).isValid());
    foreignRuntime.finalizeCompositeFieldLayout(foreignId);
    const InterpreterValue foreignReceiver(foreignRuntime.getTypeByName("CheckedStructReceiver"), source);
    const InterpreterValue foreignArgument(foreignRuntime.getIntegerType(), RuntimeValue{NomadInteger{3}});
    auto moved = receiver;
    const auto movedDestination = std::move(moved);
    machine->pushInteger(1234);
    machine->setIntegerResult(41);
    machine->setIntegerIntermediate(43);
    BOOST_CHECK_THROW(static_cast<void>(interpreter.runStructMethod(receiver, CallableId{}, {})),
        std::invalid_argument);
    BOOST_CHECK_THROW(static_cast<void>(interpreter.runStructMethod(receiver, functionId, {})),
        std::invalid_argument);
    BOOST_CHECK_THROW(static_cast<void>(interpreter.runStructMethod(receiver, methodId, {})),
        std::invalid_argument);
    const std::array extraArguments{number, number};
    BOOST_CHECK_THROW(static_cast<void>(interpreter.runStructMethod(receiver, methodId, extraArguments)),
        std::invalid_argument);
    for (const auto* argument : std::array{&boolean, &foreignArgument}) {
        BOOST_CHECK_THROW(static_cast<void>(interpreter.runStructMethod(receiver, methodId, {argument, 1})),
            std::invalid_argument);
    }
    const std::array<const InterpreterValue*, 4> invalidReceivers{&number, &other, &foreignReceiver, &moved};
    for (const auto* invalid : invalidReceivers) {
        BOOST_CHECK_THROW(static_cast<void>(interpreter.runStructMethod(*invalid, methodId, {&number, 1})),
            std::invalid_argument);
    }
    BOOST_CHECK_THROW(static_cast<void>(interpreter.runStructMethod(classReceiver, classMethodId, {})),
        std::invalid_argument);
    BOOST_CHECK_EXCEPTION(static_cast<void>(interpreter.runStructMethod(receiver, uncompiledId, {})),
        std::invalid_argument,
        [](const auto& error) { return NomadString{error.what()}.find("not compiled") != NomadString::npos; });
    BOOST_CHECK_THROW(static_cast<void>(interpreter.runObjectMethod(receiver, methodId, {&number, 1})),
        std::invalid_argument);
    BOOST_TEST(calls == 0);
    BOOST_TEST(receiver.getStorage()[0].getIntegerValue() == 17);
    BOOST_TEST(movedDestination.getStorage()[0].getIntegerValue() == 17);
    BOOST_TEST(machine->getIntegerResult() == 41);
    BOOST_TEST(machine->getIntegerIntermediate() == 43);
    BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
    InterpreterContext context(interpreter);
    context.setVariable("value", type, receiver.getStorage());
    BOOST_TEST(!interpreter.execute("value.read 3", context));
    BOOST_REQUIRE(context.getError() != nullptr);
    BOOST_TEST(context.getError()->find("Struct method calls are not supported by the interpreter") != NomadString::npos);
    BOOST_TEST(calls == 0);
    BOOST_TEST(context.getVariableStorage("value")[0].getIntegerValue() == 17);
    const auto result = interpreter.runStructMethod(receiver, methodId, {&number, 1});
    BOOST_TEST(result.result.getValue().getIntegerValue() == 3);
    BOOST_TEST(result.receiver.getStorage()[0].getIntegerValue() == 17);
    BOOST_TEST(calls == 1);
    machine->pop1();
}

BOOST_AUTO_TEST_CASE(native_failures_discard_struct_replacements_but_keep_shared_object_side_effects) {
    Runtime runtime;
    const auto ownerId = runtime.registerClass<ClassType>("FailedMethodOwner", ClassMode::Closed);
    const auto* ownerType = runtime.getType(ownerId).value()->asClassType();
    BOOST_REQUIRE(runtime.registerProperty(ownerId, "number", runtime.getIntegerType()).isValid());
    runtime.finalizeCompositeFieldLayout(ownerId);
    const auto valueId = runtime.registerStruct("FailedMethodValue");
    const auto* type = runtime.getTypeByName("FailedMethodValue");
    BOOST_REQUIRE(runtime.registerProperty(valueId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(valueId, "text", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(valueId, "owner", ownerType).isValid());
    runtime.finalizeCompositeFieldLayout(valueId);
    const auto methodId = runtime.addMethod(valueId, {
        .name = "fail", .parameters = {}, .returnTypeId = valueId,
        .function = [](VirtualMachine* active) {
            auto* receiver = &active->getStorageParameter(0, 0);
            auto* destination = &active->getStorageParameter(1, 0);
            receiver[0].setIntegerValue(99);
            receiver[1].freeObjectValue();
            receiver[1].setStringValue("changed receiver");
            receiver[2].asObject<ScriptObject>()->getFieldStorage(0)[0].setIntegerValue(99);
            destination[0].setIntegerValue(77);
            destination[1].setStringValue("partial result");
            throw NomadException("Expected struct method failure");
        }
    });
    BOOST_REQUIRE(methodId.isValid());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    {
        auto machine = runtime.createVirtualMachine();
        const Interpreter interpreter(*machine);
        std::array<RuntimeValue, 3> source{
            RuntimeValue{NomadInteger{17}}, RuntimeValue{NomadString{"source"}}, RuntimeValue{}
        };
        source[2].setObjectValue(new ScriptObject(ownerType));
        source[2].asObject<ScriptObject>()->getFieldStorage(0)[0].setIntegerValue(5);
        const InterpreterValue receiver(type, source);
        type->freeValue(source.data());
        InterpreterContext context(interpreter);
        context.setResult(runtime.getIntegerType(), RuntimeValue{NomadInteger{99}});
        machine->pushInteger(1234);
        machine->setIntegerResult(41);
        machine->setIntegerIntermediate(43);
        const auto live = RuntimeValue::getLiveStringCount();
        const auto objects = NomadObject::getLiveObjectCount();
        BOOST_CHECK_THROW(static_cast<void>(interpreter.runStructMethod(receiver, methodId, {})), NomadException);
        BOOST_TEST(receiver.getStorage()[0].getIntegerValue() == 17);
        BOOST_TEST(receiver.getStorage()[1].getStringValue() == "source");
        BOOST_TEST(receiver.getStorage()[2].asObject<ScriptObject>()->getField(0).getIntegerValue() == 99);
        BOOST_TEST(receiver.getStorage()[1].getObjectValue()->getReferenceCount() == 1U);
        BOOST_TEST(receiver.getStorage()[2].getObjectValue()->getReferenceCount() == 1U);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objects);
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 99);
        BOOST_TEST(machine->getIntegerResult() == 41);
        BOOST_TEST(machine->getIntegerIntermediate() == 43);
        BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
        BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
        machine->pop1();
    }
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
}

BOOST_AUTO_TEST_CASE(struct_method_yield_rejection_preserves_native_and_script_caller_state) {
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    BOOST_REQUIRE(runtime.registerNativeFunction("pauseStructMethod", [](VirtualMachine* active) {
        active->yield(RuntimeValue{NomadInteger{37}});
    }, {}, runtime.getVoidType(), "").isValid());
    fixture.addFunction("yieldStructTypes",
        "struct HostYieldStruct\nnumber = 17\n"
        "fun pause\nthis.number = 99\npauseStructMethod\nreturn 99\nend\nend\nreturn 0");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto* type = runtime.getTypeByName("HostYieldStruct");
    const auto pause = runtime.resolveMethod(type, "pause", {});
    BOOST_REQUIRE(pause.isResolved());
    const auto nativePauseId = runtime.addMethod(type->getId(), {
        .name = "nativePause", .parameters = {}, .returnTypeId = runtime.getIntegerType()->getId(),
        .function = [](VirtualMachine* active) {
            active->getStorageParameter(0, 0).setIntegerValue(99);
            active->yield(RuntimeValue{NomadInteger{37}});
        }
    });
    const auto readId = runtime.addMethod(type->getId(), {
        .name = "read", .parameters = {}, .returnTypeId = runtime.getIntegerType()->getId(),
        .function = [](VirtualMachine* active) { active->setIntegerResult(active->getStorageParameter(0, 0).getIntegerValue()); }
    });
    BOOST_REQUIRE(nativePauseId.isValid() && readId.isValid());
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    const auto receiver = interpreter.construct(type->asStructType()->getConstructors().front()->getId(), {});
    machine->pushInteger(1234);
    for (const auto methodId : {nativePauseId, pause.id}) {
        for (const auto previouslyYielded : {false, true}) {
            if (previouslyYielded) {
                machine->yield(RuntimeValue{NomadInteger{41}});
            } else {
                machine->stop(RuntimeValue{NomadInteger{41}});
            }
            machine->setIntegerIntermediate(43);
            const auto instruction = machine->getInstructionIndex();
            BOOST_CHECK_EXCEPTION(static_cast<void>(interpreter.runStructMethod(receiver, methodId, {})),
                VirtualMachineException,
                [](const auto& error) { return NomadString{error.what()} == "Struct method calls cannot yield"; });
            BOOST_TEST(receiver.getStorage()[0].getIntegerValue() == 17);
            BOOST_TEST(machine->hasYielded() == previouslyYielded);
            BOOST_TEST(!machine->isRunning());
            BOOST_TEST(machine->getInstructionIndex() == instruction);
            BOOST_TEST(machine->getIntegerResult() == 41);
            BOOST_TEST(machine->getIntegerIntermediate() == 43);
            BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
        }
    }
    const auto read = interpreter.runStructMethod(receiver, readId, {});
    BOOST_TEST(read.result.getValue().getIntegerValue() == 17);
    BOOST_TEST(machine->hasYielded());
    machine->stop(RuntimeValue{NomadInteger{41}});
    machine->pop1();
}

BOOST_AUTO_TEST_CASE(native_struct_reentry_preserves_enclosing_class_receiver_and_borrowed_script_frame) {
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto probeId = runtime.registerStruct("HostFrameProbe");
    const auto* probeType = runtime.getTypeByName("HostFrameProbe");
    BOOST_REQUIRE(runtime.registerProperty(probeId, "number", runtime.getIntegerType()).isValid());
    runtime.finalizeCompositeFieldLayout(probeId);
    auto probes = 0;
    const auto methodId = runtime.addMethod(probeId, {
        .name = "inspect", .parameters = {}, .returnTypeId = runtime.getIntegerType()->getId(),
        .function = [&](VirtualMachine* active) {
            ++probes;
            const auto* callable = active->getCurrentCallable();
            BOOST_REQUIRE(callable != nullptr);
            const Interpreter interpreter(*active);
            InterpreterContext frame(interpreter, *callable);
            BOOST_TEST(frame.getParameterStorage(0)[0].getIntegerValue() == 5);
            BOOST_TEST(frame.getVariableStorage("local")[0].getIntegerValue() == 3);
            frame.setVariable("local", runtime.getIntegerType(), RuntimeValue{NomadInteger{4}});
            BOOST_CHECK_THROW(static_cast<void>(active->getThis()), VirtualMachineException);
            active->setIntegerResult(frame.getParameterStorage(0)[0].getIntegerValue() +
                frame.getVariableStorage("local")[0].getIntegerValue());
        }
    });
    BOOST_REQUIRE(methodId.isValid());
    const std::array<RuntimeValue, 1> source{RuntimeValue{NomadInteger{7}}};
    const InterpreterValue probe(probeType, source);
    fixture.getCompiler().registerPhaseHook([&](CompilerContext*, const CompilationPhase phase) {
        if (phase != CompilationPhase::Parse) {
            return;
        }
        const auto* owner = runtime.getTypeByName("EnclosingStructHost");
        BOOST_REQUIRE(owner != nullptr);
        BOOST_REQUIRE(runtime.addMethod(owner->getId(), {
            .name = "probeStructHost", .parameters = {}, .returnTypeId = runtime.getVoidType()->getId(),
            .function = [&](VirtualMachine* active) {
                const auto* callable = active->getCurrentCallable();
                auto* receiver = active->getThis();
                const Interpreter interpreter(*active);
                const auto result = interpreter.runStructMethod(probe, methodId, {});
                BOOST_TEST(result.result.getValue().getIntegerValue() == 9);
                BOOST_TEST(result.receiver.getStorage()[0].getIntegerValue() == 7);
                BOOST_TEST(active->getThis() == receiver);
                BOOST_TEST(active->getCurrentCallable() == callable);
            }
        }).isValid());
    });
    fixture.addFunction("enclosingStructHostTypes",
        "class EnclosingStructHost\nnumber = 17\n"
        "fun inspect counter:int\nlocal = 3\nthis.probeStructHost\nreturn this.number + local + counter\nend\nend\nreturn 0");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto* owner = runtime.getTypeByName("EnclosingStructHost")->asClassType();
    const auto inspect = runtime.resolveMethod(owner, "inspect", {runtime.getIntegerType()});
    BOOST_REQUIRE(inspect.isResolved());
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    {
        auto machine = runtime.createVirtualMachine();
        const Interpreter interpreter(*machine);
        const auto receiver = interpreter.construct(owner->getConstructors().front()->getId(), {});
        const InterpreterValue counter(runtime.getIntegerType(), RuntimeValue{NomadInteger{5}});
        machine->pushInteger(1234);
        machine->setIntegerResult(41);
        machine->setIntegerIntermediate(43);
        const auto result = interpreter.runObjectMethod(receiver, inspect.id, {&counter, 1});
        BOOST_TEST(result.getValue().getIntegerValue() == 26);
        BOOST_TEST(probes == 1);
        BOOST_TEST(probe.getStorage()[0].getIntegerValue() == 7);
        BOOST_TEST(machine->getIntegerResult() == 41);
        BOOST_TEST(machine->getIntegerIntermediate() == 43);
        BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
        BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
        machine->pop1();
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
}

BOOST_AUTO_TEST_SUITE_END()
