// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/compiler/Compiler.hpp>

#include <nomad/interpreter/Interpreter.hpp>

#include <nomad/script/Closure.hpp>
#include <nomad/script/Event.hpp>
#include <nomad/script/Hub.hpp>
#include <nomad/script/NativeFunction.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/ScriptObject.hpp>
#include <nomad/script/StructType.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <array>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace nomad;
using namespace nomad::test;

static_assert(std::is_same_v<decltype(EventDefinition{}.id), EventId>);
static_assert(std::is_same_v<decltype(EventDispatch{}.eventId), EventId>);
static_assert(std::is_same_v<decltype(IdentifierDefinition{}.eventId), EventId>);
static_assert(!std::is_convertible_v<NomadId, EventId>);
static_assert(!std::is_convertible_v<EventId, NomadId>);
static_assert(!std::is_convertible_v<CallableId, EventId>);
static_assert(!std::is_convertible_v<EventId, CallableId>);
static_assert(!std::is_convertible_v<ConstantId, EventId>);
static_assert(!std::is_convertible_v<EventId, ConstantId>);
static_assert(!std::is_copy_constructible_v<Closure>);
static_assert(!std::is_copy_assignable_v<Closure>);
static_assert(!std::is_convertible_v<TypeId, EventId>);
static_assert(!std::is_invocable_v<decltype(&Runtime::getEventDefinition), Runtime&, NomadId>);

namespace {

struct CallbackStore {
    std::unique_ptr<Closure> closure;
    EventId eventId = EventId::INVALID;
    std::vector<EventId> dispatchedEvents;
    std::vector<NomadInteger> dispatchedIntegers;
};

// `test.store callback:(int) -> int` keeps the callback so the test can invoke it once the function has finished.
void registerCallbackNativeFunctions(Runtime& runtime, CallbackStore& store) {
    runtime.registerNativeFunction(
        "test.store",
        [&runtime, &store](VirtualMachine* virtualMachine) {
            store.closure = virtualMachine->createClosure(&runtime, CallableId{virtualMachine->getIdParameter(0)});
        },
        {
            defParameter(
                "callback",
                runtime.getCallbackType({runtime.getIntegerType()}, runtime.getIntegerType()),
                NomadParamDoc("Callback to store")
            ),
        },
        runtime.getVoidType(),
        NomadDoc("Store a callback")
    );

    runtime.registerNativeFunction(
        "test.storeVoid",
        [&runtime, &store](VirtualMachine* virtualMachine) {
            store.closure = virtualMachine->createClosure(&runtime, CallableId{virtualMachine->getIdParameter(0)});
        },
        {
            defParameter("callback", runtime.getCallbackType({}, runtime.getVoidType()), NomadParamDoc("Callback to store")),
        },
        runtime.getVoidType(),
        NomadDoc("Store a callback without parameters")
    );

    runtime.registerNativeFunction(
        "test.on",
        [&runtime, &store](VirtualMachine* virtualMachine) {
            store.eventId = EventId{virtualMachine->getParameter(0).getIdValue()};
            store.closure = virtualMachine->createClosure(&runtime, CallableId{virtualMachine->getParameter(1).getIdValue()});
        },
        {
            defParameter("event", runtime.getEventCallbackType(), NomadParamDoc("Event to handle")),
        },
        runtime.getVoidType(),
        NomadDoc("Register an event handler")
    );

    runtime.registerNativeFunction(
        "test.trigger",
        [&runtime, &store](VirtualMachine* virtualMachine) {
            const auto eventId = EventId{virtualMachine->getIdParameter(0)};
            const auto event = runtime.getEventDefinition(eventId);

            store.dispatchedEvents.push_back(eventId);

            if (!event) {
                return;
            }

            // Mirrors the entity `trigger` nativeFunction: event arguments follow the event id on the stack.
            for (NomadIndex i = 0; i < event->parameters.size(); ++i) {
                store.dispatchedIntegers.push_back(virtualMachine->peekStack(i + 1).getIntegerValue());
            }
        },
        {
            defParameter("event", runtime.getEventDispatchType(), NomadParamDoc("Event to dispatch")),
        },
        runtime.getVoidType(),
        NomadDoc("Dispatch an event")
    );
}

FunctionOutcome invokeStoredClosure(Runtime& runtime, const CallbackStore& store, const std::vector<RuntimeValue>& arguments) {
    FunctionOutcome outcome;
    outcome.compiled = true;

    if (store.closure == nullptr) {
        outcome.fault = "No callback stored";

        return outcome;
    }

    RuntimeValue result;

    try {
        runtime.executeFunction(store.closure.get(), arguments, result);
    } catch (const VirtualMachineException& exception) {
        outcome.fault = exception.what();

        return outcome;
    }

    const auto returnType = store.closure->getFunction()->getReturnType();

    if (returnType != nullptr && !returnType->isVoid()) {
        outcome.returnTypeName = returnType->getTypeName();
        outcome.text = returnType->toString(&result);

        if (returnType == runtime.getIntegerType()) {
            outcome.integerValue = result.getIntegerValue();
        }

        returnType->freeValue(&result);
    }

    return outcome;
}

} // namespace

BOOST_AUTO_TEST_SUITE(language_callbacks)

BOOST_AUTO_TEST_CASE(source_struct_captures_are_snapshots_across_nested_and_repeated_callback_invocation) {
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    std::vector<std::unique_ptr<Closure>> callbacks;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "test.keepStructCapture",
        [&](VirtualMachine* active) {
            callbacks.push_back(active->createClosure(&runtime, active->getCallableIdParameter(0)));
        },
        {defParameter("callback", runtime.getCallbackType({runtime.getIntegerType()}, runtime.getIntegerType()), "")},
        runtime.getVoidType(),
        ""
    ).isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "test.reenterStructCapture",
        [&](VirtualMachine* active) {
            const auto* callable = active->getCurrentCallable();
            const auto savedResult = active->getResult();
            const auto savedIntermediate = active->getIntermediate();
            const Interpreter interpreter(*active);
            const InterpreterValue argument(runtime.getIntegerType(), active->getParameter(0));
            const auto result = interpreter.runFunction(*callbacks[0], {&argument, 1});
            BOOST_TEST(active->getCurrentCallable() == callable);
            BOOST_TEST(active->getIntegerParameter(0) == 2);
            BOOST_TEST(active->getResult().getIntegerValue() == savedResult.getIntegerValue());
            BOOST_TEST(active->getIntermediate().getIntegerValue() == savedIntermediate.getIntegerValue());
            active->setIntegerResult(result.getValue().getIntegerValue());
        },
        {defParameter("value", runtime.getIntegerType(), "")},
        runtime.getIntegerType(),
        ""
    ).isValid());
    fixture.addFunction("reenterStructCapture", "return test.reenterStructCapture 2");
    fixture.addFunction(
        "main",
        "class CapturedShared\nnumber = 5\nend\n"
        "struct CapturedEmpty\nend\n"
        "struct CapturedSingle\nnumber = 11\nend\n"
        "struct CapturedLeaf\nnumber = 7\ntext = \"captured\"\nshared = new CapturedShared\nend\n"
        "struct CapturedRoot\nleaf = new CapturedLeaf\nempty = new CapturedEmpty\nend\n"
        "fun createStructCallback parameter:CapturedRoot tail:int\n"
        "local = parameter\nempty = new CapturedEmpty\nsingle = new CapturedSingle\n"
        "test.keepStructCapture fun delta\n"
        "local.leaf.number = local.leaf.number + delta\n"
        "parameter.leaf.number = parameter.leaf.number + 1\ncopy = empty\nsingle.number = 53\n"
        "local.leaf.shared.number = local.leaf.shared.number + 1\n"
        "test.keepStructCapture fun amount\n"
        "local.leaf.number = local.leaf.number + amount\n"
        "return local.leaf.number + parameter.leaf.number + tail\nend\n"
        "return local.leaf.number + parameter.leaf.number + tail\nend\n"
        "local.leaf.number = 99\nparameter.leaf.number = 101\nend\n"
        "root = new CapturedRoot\ncreateStructCallback root 3\nroot.leaf.number = 201\nreturn 0"
    );
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto iteration = 0; iteration < 3; ++iteration) {
        {
            const auto outcome = fixture.execute("main");
            BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
            BOOST_REQUIRE(callbacks.size() == 1U);
            const auto* original = callbacks[0].get();
            const auto* function = original->getFunction();
            const auto localId = function->getCaptureId("local");
            const auto parameterId = function->getCaptureId("parameter");
            const auto emptyId = function->getCaptureId("empty");
            const auto singleId = function->getCaptureId("single");
            BOOST_REQUIRE(localId != NOMAD_INVALID_ID);
            BOOST_REQUIRE(parameterId != NOMAD_INVALID_ID);
            BOOST_REQUIRE(emptyId != NOMAD_INVALID_ID);
            BOOST_REQUIRE(singleId != NOMAD_INVALID_ID);
            BOOST_TEST(original->getCaptureStorage(toNomadIndex(localId))[0].getIntegerValue() == 7);
            BOOST_TEST(original->getCaptureStorage(toNomadIndex(parameterId))[0].getIntegerValue() == 7);
            BOOST_TEST(original->getCaptureStorage(toNomadIndex(emptyId)).data() == nullptr);
            BOOST_TEST(original->getCaptureStorage(toNomadIndex(singleId)).size() == 1U);
            auto machine = runtime.createVirtualMachine();
            const Interpreter interpreter(*machine);
            const InterpreterValue delta(runtime.getIntegerType(), RuntimeValue{NomadInteger{2}});
            machine->pushInteger(1234);
            machine->setIntegerResult(41);
            machine->setIntegerIntermediate(43);
            for (auto invocation = 0; invocation < 2; ++invocation) {
                const auto result = interpreter.runFunction(*original, {&delta, 1});
                BOOST_TEST(result.getValue().getIntegerValue() == 20);
                BOOST_TEST(original->getCaptureStorage(toNomadIndex(localId))[0].getIntegerValue() == 7);
                BOOST_TEST(original->getCaptureStorage(toNomadIndex(parameterId))[0].getIntegerValue() == 7);
                BOOST_TEST(original->getCaptureStorage(toNomadIndex(singleId))[0].getIntegerValue() == 11);
                const auto nested = interpreter.runFunction(*callbacks.back(), {&delta, 1});
                BOOST_TEST(nested.getValue().getIntegerValue() == 22);
                BOOST_TEST(machine->getIntegerResult() == 41);
                BOOST_TEST(machine->getIntegerIntermediate() == 43);
                BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
            }

            RuntimeValue rawResult;
            runtime.executeFunction(original, {RuntimeValue{NomadInteger{2}}}, rawResult);
            BOOST_TEST(rawResult.getIntegerValue() == 20);
            BOOST_TEST(original->getCaptureStorage(toNomadIndex(localId))[2].getObjectValue() ==
                original->getCaptureStorage(toNomadIndex(parameterId))[2].getObjectValue());
            auto* shared = static_cast<ScriptObject*>(
                original->getCaptureStorage(toNomadIndex(localId))[2].getObjectValue()
            );
            BOOST_TEST(shared->getFieldStorage(0)[0].getIntegerValue() == 8);
            const auto reentered = fixture.execute("reenterStructCapture");
            BOOST_REQUIRE_MESSAGE(!reentered.fault.has_value(), reentered.fault.value_or(""));
            BOOST_TEST(reentered.integerValue == 20);
            BOOST_TEST(shared->getFieldStorage(0)[0].getIntegerValue() == 9);
            machine->pop1();
        }

        callbacks.clear();
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }

}

BOOST_AUTO_TEST_CASE(source_struct_and_empty_receiver_captures_copy_this_without_writeback) {
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    std::vector<std::unique_ptr<Closure>> callbacks;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "test.keepReceiverCapture",
        [&](VirtualMachine* active) {
            callbacks.push_back(active->createClosure(&runtime, active->getCallableIdParameter(0)));
        },
        {defParameter("callback", runtime.getCallbackType({}, runtime.getIntegerType()), "")},
        runtime.getVoidType(),
        ""
    ).isValid());
    fixture.addFunction(
        "main",
        "struct SourceReceiver\nnumber = 7\ntext = \"receiver\"\n"
        "fun keep\n"
        "test.keepReceiverCapture fun\nthis.number = this.number + 1\nreturn this.number\nend\n"
        "this.number = 99\nend\nend\n"
        "struct SourceEmptyReceiver\n"
        "fun keep\n"
        "test.keepReceiverCapture fun\ncopy = this\nreturn 17\nend\nend\nend\n"
        "receiver = new SourceReceiver\nreceiver.keep\nassert receiver.number == 99\n"
        "(new SourceEmptyReceiver).keep\nreturn 0"
    );
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto iteration = 0; iteration < 3; ++iteration) {
        {
            const auto outcome = fixture.execute("main");
            BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
            BOOST_REQUIRE(callbacks.size() == 2U);
            auto machine = runtime.createVirtualMachine();
            const Interpreter interpreter(*machine);
            for (auto invocation = 0; invocation < 2; ++invocation) {
                BOOST_TEST(interpreter.runFunction(*callbacks[0], {}).getValue().getIntegerValue() == 8);
                BOOST_TEST(interpreter.runFunction(*callbacks[1], {}).getValue().getIntegerValue() == 17);
            }

            BOOST_TEST(callbacks[0]->getCaptureStorage(0)[0].getIntegerValue() == 7);
            BOOST_TEST(callbacks[1]->getCaptureStorage(0).data() == nullptr);
            RuntimeValue result;
            runtime.executeFunction(callbacks[0].get(), {}, result);
            BOOST_TEST(result.getIntegerValue() == 8);
            runtime.executeFunction(callbacks[1].get(), {}, result);
            BOOST_TEST(result.getIntegerValue() == 17);
        }

        callbacks.clear();
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }

}

BOOST_AUTO_TEST_CASE(typed_closures_accept_complete_arguments_and_return_owned_structs_after_the_vm_is_destroyed) {
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto valueId = runtime.registerStruct("TypedCallbackValue");
    const auto* type = runtime.getTypeByName("TypedCallbackValue");
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(valueId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(valueId, "text", runtime.getStringType()).isValid());
    const auto emptyId = runtime.registerStruct("TypedCallbackEmpty");
    const auto* emptyType = runtime.getTypeByName("TypedCallbackEmpty");
    runtime.getTypeManager().finalizeCompositeFieldLayout(valueId);
    runtime.getTypeManager().finalizeCompositeFieldLayout(emptyId);
    BOOST_REQUIRE(runtime.registerNativeStructConstructor(valueId, {
        .parameters = {},
        .function = [](VirtualMachine* active) {
            active->getStorageParameter(0, 0).setIntegerValue(7);
            active->getStorageParameter(0, 1).setStringValue("callback");
        }
    }).isValid());
    BOOST_REQUIRE(runtime.registerNativeStructConstructor(emptyId, {
        .parameters = {}, .function = [](VirtualMachine*) {}
    }).isValid());
    std::vector<std::unique_ptr<Closure>> callbacks;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "test.keepTypedCallback",
        [&](VirtualMachine* active) {
            callbacks.push_back(active->createClosure(&runtime, active->getCallableIdParameter(0)));
        },
        {defParameter("callback", runtime.getCallbackType({type, emptyType, runtime.getIntegerType()}, type), "")},
        runtime.getVoidType(),
        ""
    ).isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "test.keepEmptyCallback",
        [&](VirtualMachine* active) {
            callbacks.push_back(active->createClosure(&runtime, active->getCallableIdParameter(0)));
        },
        {defParameter("callback", runtime.getCallbackType({emptyType}, emptyType), "")},
        runtime.getVoidType(),
        ""
    ).isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "test.keepVoidCallback",
        [&](VirtualMachine* active) {
            callbacks.push_back(active->createClosure(&runtime, active->getCallableIdParameter(0)));
        },
        {defParameter("callback", runtime.getCallbackType({type}, runtime.getVoidType()), "")},
        runtime.getVoidType(),
        ""
    ).isValid());
    fixture.addFunction(
        "main",
        "captured = new TypedCallbackValue\nempty = new TypedCallbackEmpty\n"
        "test.keepTypedCallback fun value emptyArgument delta\n"
        "value.number = value.number + captured.number + delta\n"
        "value.text = \"returned\"\ncaptured.number = 99\nreturn value\nend\n"
        "test.keepEmptyCallback fun value\ncopy = empty\nreturn value\nend\n"
        "test.keepVoidCallback fun value\nvalue.number = 99\ncopy = captured\nend\n"
        "captured.number = 123\nreturn 0"
    );
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto iteration = 0; iteration < 3; ++iteration) {
        {
            std::optional<InterpreterValue> retained;
            {
                const auto outcome = fixture.execute("main");
                BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
                BOOST_REQUIRE(callbacks.size() == 3U);
                auto machine = runtime.createVirtualMachine();
                const Interpreter interpreter(*machine);
                const auto input = interpreter.construct(type->asCompositeType()->getConstructors()[0]->getId(), {});
                const InterpreterValue empty(emptyType, std::span<const RuntimeValue>{});
                const InterpreterValue delta(runtime.getIntegerType(), RuntimeValue{NomadInteger{3}});
                const std::array arguments{input, empty, delta};
                machine->pushInteger(1234);
                machine->setIntegerResult(41);
                machine->setIntegerIntermediate(43);
                retained = interpreter.runFunction(*callbacks[0], arguments);
                BOOST_TEST(retained->getStorage()[0].getIntegerValue() == 17);
                BOOST_TEST(retained->getStorage()[1].getStringValue() == "returned");
                BOOST_TEST(retained->getStorage().data() != input.getStorage().data());
                BOOST_TEST(input.getStorage()[0].getIntegerValue() == 7);
                BOOST_TEST(input.getStorage()[1].getStringValue() == "callback");
                BOOST_TEST(callbacks[0]->getCaptureStorage(0)[0].getIntegerValue() == 7);
                const auto repeated = interpreter.runFunction(*callbacks[0], arguments);
                BOOST_TEST(repeated.getStorage()[0].getIntegerValue() == 17);
                const auto emptyResult = interpreter.runFunction(*callbacks[1], {&empty, 1});
                BOOST_TEST(emptyResult.getType() == emptyType);
                BOOST_TEST(emptyResult.getStorage().data() == nullptr);
                BOOST_TEST(interpreter.runFunction(*callbacks[2], {&input, 1}).getType()->isVoid());
                BOOST_TEST(input.getStorage()[0].getIntegerValue() == 7);
                BOOST_CHECK_THROW(static_cast<void>(interpreter.runFunction(*callbacks[0], {})), std::invalid_argument);
                const std::array wrongArguments{input, delta, delta};
                BOOST_CHECK_THROW(
                    static_cast<void>(interpreter.runFunction(*callbacks[0], wrongArguments)),
                    std::invalid_argument
                );
                Runtime foreignRuntime;
                auto foreignMachine = foreignRuntime.createVirtualMachine();
                BOOST_CHECK_THROW(
                    static_cast<void>(Interpreter{*foreignMachine}.runFunction(*callbacks[0], arguments)),
                    std::invalid_argument
                );
                RuntimeValue rawResult{NomadInteger{71}};
                BOOST_CHECK_THROW(runtime.executeFunction(callbacks[0].get(), {}, rawResult), std::invalid_argument);
                BOOST_TEST(rawResult.getIntegerValue() == 71);
                BOOST_CHECK_THROW(
                    runtime.executeFunction(callbacks[2].get(), {RuntimeValue{NomadInteger{3}}}),
                    std::invalid_argument
                );
                BOOST_TEST(machine->getIntegerResult() == 41);
                BOOST_TEST(machine->getIntegerIntermediate() == 43);
                BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
                machine->pop1();
            }

            callbacks.clear();
            BOOST_TEST(retained->getStorage()[0].getIntegerValue() == 17);
            BOOST_TEST(retained->getStorage()[1].getStringValue() == "returned");
            BOOST_TEST(retained->getStorage()[1].getObjectValue()->getReferenceCount() == 2U);
        }

        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }

}

BOOST_AUTO_TEST_CASE(aggregate_callbacks_are_synchronous_while_scalar_callbacks_preserve_suspension) {
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    std::vector<std::unique_ptr<Closure>> callbacks;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "test.keepSuspendingCallback",
        [&](VirtualMachine* active) {
            callbacks.push_back(active->createClosure(&runtime, active->getCallableIdParameter(0)));
        },
        {defParameter("callback", runtime.getCallbackType({}, runtime.getIntegerType()), "")},
        runtime.getVoidType(),
        ""
    ).isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "test.pauseCallback",
        [](VirtualMachine* active) { active->yield(RuntimeValue{NomadInteger{37}}); },
        {},
        runtime.getVoidType(),
        ""
    ).isValid());
    fixture.addFunction(
        "main",
        "struct SuspendedCaptureEmpty\nend\nempty = new SuspendedCaptureEmpty\n"
        "test.keepSuspendingCallback fun\ncopy = empty\nreturn 17\nend\n"
        "test.keepSuspendingCallback fun\ncopy = empty\ntest.pauseCallback\nreturn 17\nend\n"
        "test.keepSuspendingCallback fun\ntest.pauseCallback\nreturn 17\nend\n"
        "return 0"
    );
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto outcome = fixture.execute("main");
    BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
    BOOST_REQUIRE(callbacks.size() == 3U);
    for (const auto previouslyYielded : {false, true}) {
        auto machine = runtime.createVirtualMachine();
        const Interpreter interpreter(*machine);
        machine->pushInteger(1234);
        machine->setIntegerResult(41);
        machine->setIntegerIntermediate(43);
        if (previouslyYielded) {
            machine->yield(RuntimeValue{NomadInteger{41}});
        }

        const auto before = machine->captureExecutionState();
        BOOST_TEST(interpreter.runFunction(*callbacks[0], {}).getValue().getIntegerValue() == 17);
        BOOST_CHECK_EXCEPTION(
            static_cast<void>(interpreter.runFunction(*callbacks[1], {})),
            VirtualMachineException,
            [](const auto& error) { return NomadString{error.what()} == "Aggregate closure calls cannot yield"; }
        );
        const auto after = machine->captureExecutionState();
        BOOST_TEST(after.stackIndex == before.stackIndex);
        BOOST_TEST(after.parameterIndex == before.parameterIndex);
        BOOST_TEST(after.variableIndex == before.variableIndex);
        BOOST_TEST(after.instructionIndex == before.instructionIndex);
        BOOST_TEST(after.running == before.running);
        BOOST_TEST(after.yielded == before.yielded);
        BOOST_TEST(machine->getIntegerResult() == 41);
        BOOST_TEST(machine->getIntegerIntermediate() == 43);
        BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
        machine->pop1();
    }

    {
        auto machine = runtime.createVirtualMachine();
        const Interpreter interpreter(*machine);
        machine->setIntegerResult(41);
        machine->setIntegerIntermediate(43);
        const auto before = machine->captureExecutionState();
        const auto result = interpreter.runFunction(*callbacks[2], {});
        BOOST_TEST(result.getValue().getIntegerValue() == 37);
        BOOST_TEST(machine->hasYielded());
        BOOST_TEST(!machine->isRunning());
        BOOST_TEST(machine->captureExecutionState().stackIndex > before.stackIndex);
        BOOST_TEST(machine->getIntegerResult() == 41);
        BOOST_TEST(machine->getIntegerIntermediate() == 43);
    }

    RuntimeValue rawResult{NomadInteger{71}};
    BOOST_CHECK_EXCEPTION(
        runtime.executeFunction(callbacks[1].get(), {}, rawResult),
        VirtualMachineException,
        [](const auto& error) { return NomadString{error.what()} == "Aggregate closure calls cannot yield"; }
    );
    BOOST_TEST(rawResult.getIntegerValue() == 71);
    {
        auto machine = runtime.createVirtualMachine();
        const auto before = machine->captureExecutionState();
        machine->run(*callbacks[2], std::span<const RuntimeValue>{});
        BOOST_TEST(machine->getIntegerResult() == 37);
        BOOST_TEST(machine->hasYielded());
        BOOST_TEST(!machine->isRunning());
        BOOST_TEST(machine->captureExecutionState().stackIndex > before.stackIndex);
    }

    runtime.executeFunction(callbacks[2].get(), {}, rawResult);
    BOOST_TEST(rawResult.getIntegerValue() == 37);
}

BOOST_AUTO_TEST_CASE(event_callback_adapters_copy_struct_captures_and_release_them_after_failures) {
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    CallbackStore store;
    registerCallbackNativeFunctions(runtime, store);
    auto failCallback = false;
    std::vector<NomadInteger> observed;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "test.observeStructEvent",
        [&](VirtualMachine* active) {
            observed.push_back(active->getIntegerParameter(0));
            if (failCallback) {
                throw NomadException("Captured event callback failed");
            }

        },
        {defParameter("value", runtime.getIntegerType(), "")},
        runtime.getVoidType(),
        ""
    ).isValid());
    fixture.addFunction(
        "main",
        "event StructSnapshotEvent value:int\n"
        "struct EventCaptureValue\nnumber = 7\ntext = \"event\"\nend\n"
        "captured = new EventCaptureValue\n"
        "test.on StructSnapshotEvent fun value\n"
        "captured.number = captured.number + value\ntest.observeStructEvent captured.number\nend\n"
        "captured.number = 99\nreturn 0"
    );
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto iteration = 0; iteration < 3; ++iteration) {
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_REQUIRE(store.closure != nullptr);
        BOOST_TEST(store.eventId.isValid());
        observed.clear();
        const auto live = RuntimeValue::getLiveStringCount();
        runtime.executeFunction(store.closure.get(), {RuntimeValue{NomadInteger{2}}});
        runtime.executeFunction(store.closure.get(), {RuntimeValue{NomadInteger{2}}});
        failCallback = true;
        BOOST_CHECK_THROW(
            runtime.executeFunction(store.closure.get(), {RuntimeValue{NomadInteger{2}}}),
            NomadException
        );
        failCallback = false;
        BOOST_TEST(observed.size() == 3U);
        for (const auto value : observed) {
            BOOST_TEST(value == 9);
        }

        BOOST_TEST(store.closure->getCaptureStorage(0)[0].getIntegerValue() == 7);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
        store.closure.reset();
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }

}

BOOST_AUTO_TEST_CASE(closure_storage_adopts_complete_slots_and_preserves_logical_empty_and_struct_captures) {
    Runtime runtime;
    const auto emptyId = runtime.registerStruct("ClosureStorageEmpty");
    const auto* empty = runtime.getTypeByName("ClosureStorageEmpty");
    const auto singleId = runtime.registerStruct("ClosureStorageSingle");
    const auto* single = runtime.getTypeByName("ClosureStorageSingle");
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(singleId, "number", runtime.getIntegerType()).isValid());
    const auto valueId = runtime.registerStruct("ClosureStorageValue");
    const auto* type = runtime.getTypeByName("ClosureStorageValue");
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(valueId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(valueId, "text", runtime.getStringType()).isValid());
    runtime.getTypeManager().finalizeCompositeFieldLayout(emptyId);
    runtime.getTypeManager().finalizeCompositeFieldLayout(singleId);
    runtime.getTypeManager().finalizeCompositeFieldLayout(valueId);
    ScriptFunction function(CallableId{}, "captureStorage", "", "");
    function.addParameter("explicit", runtime.getIntegerType(), "");
    function.createCapture(VariableId{0}, "empty", empty);
    function.createCapture(VariableId{1}, "value", type);
    function.createCapture(VariableId{2}, "single", single);
    function.createCapture(VariableId{3}, "scalar", runtime.getIntegerType());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        {
            std::vector<RuntimeValue> storage{
                RuntimeValue{NomadInteger{17}},
                RuntimeValue{NomadString{"owned capture"}},
                RuntimeValue{NomadInteger{23}},
                RuntimeValue{NomadInteger{29}}
            };
            const auto* address = storage.data();
            const Closure closure(&function, std::move(storage));
            BOOST_TEST(storage.empty());
            BOOST_TEST(closure.getCaptureCount() == 4U);
            BOOST_TEST(closure.getCaptureType(0) == empty);
            BOOST_TEST(closure.getCaptureStorage(0).data() == nullptr);
            BOOST_TEST(closure.getCaptureStorage(1).data() == address);
            BOOST_TEST(closure.getCaptureStorage(1).size() == 2U);
            BOOST_TEST(closure.getCaptureStorage(1)[0].getIntegerValue() == 17);
            BOOST_TEST(closure.getCaptureStorage(1)[1].getStringValue() == "owned capture");
            BOOST_TEST(closure.getCaptureStorage(2).size() == 1U);
            BOOST_TEST(closure.getCaptureStorage(2)[0].getIntegerValue() == 23);
            BOOST_TEST(closure.getCaptureType(2) == single);
            BOOST_TEST(closure.getCaptureValue(3).getIntegerValue() == 29);
            for (auto capture = NomadIndex{0}; capture < 3; ++capture) {
                BOOST_CHECK_THROW(static_cast<void>(closure.getCaptureValue(capture)), std::invalid_argument);
            }

            BOOST_CHECK_THROW(static_cast<void>(closure.getCaptureStorage(4)), std::out_of_range);
            BOOST_CHECK_THROW(static_cast<void>(closure.getCaptureValue(4)), std::out_of_range);
            BOOST_CHECK_THROW(static_cast<void>(closure.getCaptureType(4)), std::out_of_range);
        }

        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }

    ScriptFunction emptyFunction(CallableId{}, "emptyCaptureStorage", "", "");
    emptyFunction.createCapture(VariableId{0}, "empty", empty);
    const Closure emptyClosure(&emptyFunction);
    BOOST_TEST(emptyClosure.getCaptureCount() == 1U);
    BOOST_TEST(emptyClosure.getCaptureType(0) == empty);
    BOOST_TEST(emptyClosure.getCaptureStorage(0).data() == nullptr);
    std::vector<RuntimeValue> rejected{RuntimeValue{NomadString{"still caller owned"}}};
    BOOST_CHECK_THROW((Closure{nullptr, std::move(rejected)}), std::invalid_argument);
    BOOST_TEST(rejected.size() == 1U);
    BOOST_CHECK_THROW((Closure{&function, std::move(rejected)}), std::invalid_argument);
    BOOST_TEST(rejected.size() == 1U);
    BOOST_CHECK_THROW((Closure{&emptyFunction, std::move(rejected)}), std::invalid_argument);
    ScriptFunction invalidTypeFunction(CallableId{}, "invalidCaptureStorage", "", "");
    invalidTypeFunction.createCapture(VariableId{0}, "unresolved", nullptr);
    BOOST_CHECK_THROW((Closure{&invalidTypeFunction, std::move(rejected)}), std::invalid_argument);
    ScriptFunction voidTypeFunction(CallableId{}, "voidCaptureStorage", "", "");
    voidTypeFunction.createCapture(VariableId{0}, "invalid", runtime.getVoidType());
    BOOST_CHECK_THROW((Closure{&voidTypeFunction, std::move(rejected)}), std::invalid_argument);
    BOOST_TEST(rejected.size() == 1U);
    BOOST_TEST(rejected[0].getObjectValue()->getReferenceCount() == 1U);
    rejected[0].freeObjectValue();
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
}

BOOST_AUTO_TEST_CASE(frame_capture_snapshots_copy_complete_physical_ranges_and_keep_empty_capture_positions) {
    Runtime runtime;
    const auto sharedId = runtime.registerClass("FrameCaptureShared", ClassMode::Closed);
    const auto* shared = runtime.getTypeByName("FrameCaptureShared")->asClassType();
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptClassProperty(sharedId, "number", runtime.getIntegerType()).isValid());
    const auto leafId = runtime.registerStruct("FrameCaptureLeaf");
    const auto* leaf = runtime.getTypeByName("FrameCaptureLeaf");
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(leafId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(leafId, "text", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(leafId, "shared", shared).isValid());
    const auto valueId = runtime.registerStruct("FrameCaptureValue");
    const auto* type = runtime.getTypeByName("FrameCaptureValue");
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(valueId, "leaf", leaf).isValid());
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(valueId, "suffix", runtime.getIntegerType()).isValid());
    const auto emptyId = runtime.registerStruct("FrameCaptureEmpty");
    const auto* empty = runtime.getTypeByName("FrameCaptureEmpty");
    runtime.getTypeManager().finalizeCompositeFieldLayout(sharedId);
    runtime.getTypeManager().finalizeCompositeFieldLayout(valueId);
    runtime.getTypeManager().finalizeCompositeFieldLayout(emptyId);
    const auto parentId = runtime.registerScriptFunction(
        "captureStorageParent",
        "captures.nomad",
        ""
    );
    auto* parent = runtime.getScriptFunction(parentId);
    parent->addParameter("head", runtime.getIntegerType(), "");
    parent->addParameter("value", type, "");
    parent->addParameter("empty", empty, "");
    parent->addParameter("tail", runtime.getIntegerType(), "");
    const auto emptyVariable = parent->registerVariable("localEmpty", empty);
    const auto valueVariable = parent->registerVariable("localValue", type);
    const auto tailVariable = parent->registerVariable("localTail", runtime.getIntegerType());
    parent->setReturnType(runtime.getVoidType());
    const auto descriptorId = runtime.registerScriptFunction(
        "captureStorageDescriptor",
        "captures.nomad",
        ""
    );
    auto* descriptor = runtime.getScriptFunction(descriptorId);
    descriptor->setParentId(parentId);
    descriptor->createCapture(NomadId{3}, "tail", runtime.getIntegerType());
    descriptor->createCapture(emptyVariable, "localEmpty", empty);
    descriptor->createCapture(valueVariable, "localValue", type);
    descriptor->createCapture(NomadId{2}, "empty", empty);
    descriptor->createCapture(NomadId{1}, "value", type);
    descriptor->createCapture(tailVariable, "localTail", runtime.getIntegerType());
    auto currentFrame = true;
    std::span<const RuntimeValue> localSource;
    std::vector<InterpreterValue> snapshots;
    std::unique_ptr<Closure> closure;
    const auto probeId = runtime.registerNativeFunction(
        "test.captureStorageProbe",
        [&](VirtualMachine* active) {
            const auto savedResult = active->getResult();
            const auto savedIntermediate = active->getIntermediate();
            if (currentFrame) {
                std::vector<RuntimeValue> scratch(type->getAssignmentScratchSize());
                type->assignValue(
                    localSource.data(),
                    active->getLocalStorage(parent->getVariableOffset(valueVariable), type->getSize()).data(),
                    scratch.data()
                );
                active->getLocalStorage(parent->getVariableOffset(tailVariable), 1)[0].setIntegerValue(79);
                snapshots = active->captureCurrentFrame(descriptor);
                currentFrame = false;

                return;
            }

            closure = active->createClosure(&runtime, descriptorId);
            active->getLocalStorage(parent->getVariableOffset(valueVariable), type->getSize())[0].setIntegerValue(99);
            active->getEnclosingParameterStorage(parent->getParameterOffset(1), type->getSize())[0].setIntegerValue(101);
            auto* object = static_cast<ScriptObject*>(closure->getCaptureStorage(2)[2].getObjectValue());
            object->getFieldStorage(0)[0].setIntegerValue(123);
            BOOST_TEST(active->getResult().getIntegerValue() == savedResult.getIntegerValue());
            BOOST_TEST(active->getIntermediate().getIntegerValue() == savedIntermediate.getIntegerValue());
        },
        {},
        runtime.getVoidType(),
        ""
    );
    BOOST_REQUIRE(probeId.isValid());
    const auto probe = runtime.registerInstruction(
        "test.captureCurrentStorage",
        [](VirtualMachine* active) {
            const auto* function = active->getRuntime()->getFunction(
                active->getRuntime()->getCallableId("test.captureStorageProbe")
            )->asNativeFunction();
            function->getFn()(active);
        },
        "",
        {}
    );
    auto compiler = runtime.createCompiler();
    parent->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(parent);
    compiler->addOpCode(probe);
    compiler->addOpCode(OpCodes::op_call_native_function);
    compiler->addId(probeId.id);
    compiler->addFunctionReturn(parent);
    parent->setFunctionEnd(runtime.getInstructions().size());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto iteration = 0; iteration < 3; ++iteration) {
        {
            RuntimeValue object;
            object.setObjectValue(new ScriptObject(shared));
            std::array<RuntimeValue, 4> parameterValues{
                RuntimeValue{NomadInteger{17}},
                RuntimeValue{NomadString{"parameter owned"}},
                RuntimeValue{},
                RuntimeValue{NomadInteger{43}}
            };
            std::array<RuntimeValue, 4> localValues{
                RuntimeValue{NomadInteger{31}},
                RuntimeValue{NomadString{"local owned"}},
                RuntimeValue{},
                RuntimeValue{NomadInteger{47}}
            };
            parameterValues[2].copyObjectValue(object);
            localValues[2].copyObjectValue(object);
            const InterpreterValue parameter(type, parameterValues);
            const InterpreterValue local(type, localValues);
            type->freeValue(parameterValues.data());
            type->freeValue(localValues.data());
            object.freeObjectValue();
            localSource = local.getStorage();
            const std::array arguments{
                InterpreterValue(runtime.getIntegerType(), RuntimeValue{NomadInteger{19}}),
                parameter,
                InterpreterValue(empty, std::span<const RuntimeValue>{}),
                InterpreterValue(runtime.getIntegerType(), RuntimeValue{NomadInteger{23}})
            };
            auto machine = runtime.createVirtualMachine();
            machine->setIntegerResult(41);
            machine->setIntegerIntermediate(43);
            const Interpreter interpreter(*machine);
            currentFrame = true;
            BOOST_TEST(interpreter.runFunction(parentId, arguments).getType()->isVoid());
            BOOST_TEST(machine->getIntegerResult() == 41);
            BOOST_TEST(machine->getIntegerIntermediate() == 43);
            BOOST_REQUIRE(closure != nullptr);
            BOOST_TEST(closure->getCaptureCount() == 6U);
            BOOST_REQUIRE(snapshots.size() == 6U);
            BOOST_TEST(closure->getCaptureValue(0).getIntegerValue() == 23);
            BOOST_TEST(closure->getCaptureType(1) == empty);
            BOOST_TEST(closure->getCaptureStorage(1).data() == nullptr);
            BOOST_TEST(closure->getCaptureStorage(2)[0].getIntegerValue() == 31);
            BOOST_TEST(closure->getCaptureStorage(2).size() == 4U);
            BOOST_TEST(closure->getCaptureStorage(2)[1].getStringValue() == "local owned");
            BOOST_TEST(closure->getCaptureStorage(2)[3].getIntegerValue() == 47);
            BOOST_TEST(closure->getCaptureStorage(3).data() == nullptr);
            BOOST_TEST(closure->getCaptureStorage(4)[0].getIntegerValue() == 17);
            BOOST_TEST(closure->getCaptureStorage(4)[1].getStringValue() == "parameter owned");
            BOOST_TEST(closure->getCaptureStorage(4)[3].getIntegerValue() == 43);
            BOOST_TEST(closure->getCaptureValue(5).getIntegerValue() == 79);
            BOOST_TEST(snapshots[2].getStorage()[0].getIntegerValue() == 31);
            BOOST_TEST(snapshots[4].getStorage()[0].getIntegerValue() == 17);
            BOOST_TEST(snapshots[2].getStorage().data() != closure->getCaptureStorage(2).data());
            BOOST_TEST(snapshots[2].getStorage()[2].getObjectValue() == closure->getCaptureStorage(2)[2].getObjectValue());
            BOOST_TEST(static_cast<ScriptObject*>(snapshots[2].getStorage()[2].getObjectValue())
                ->getFieldStorage(0)[0].getIntegerValue() == 123);
        }

        BOOST_TEST(closure->getCaptureStorage(2)[1].getStringValue() == "local owned");
        snapshots.clear();
        BOOST_TEST(closure->getCaptureStorage(2)[1].getObjectValue()->getReferenceCount() == 1U);
        closure.reset();
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }

}

BOOST_AUTO_TEST_CASE(receiver_captures_copy_borrowed_struct_storage_and_preserve_class_reference_identity) {
    Runtime runtime;
    const auto valueId = runtime.registerStruct("ReceiverCaptureValue");
    const auto* type = runtime.getTypeByName("ReceiverCaptureValue");
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(valueId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(valueId, "text", runtime.getStringType()).isValid());
    const auto emptyId = runtime.registerStruct("ReceiverCaptureEmpty");
    const auto* empty = runtime.getTypeByName("ReceiverCaptureEmpty");
    const auto ownerId = runtime.registerClass("ReceiverCaptureOwner", ClassMode::Closed);
    const auto* owner = runtime.getTypeByName("ReceiverCaptureOwner")->asClassType();
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptClassProperty(ownerId, "number", runtime.getIntegerType()).isValid());
    runtime.getTypeManager().finalizeCompositeFieldLayout(valueId);
    runtime.getTypeManager().finalizeCompositeFieldLayout(emptyId);
    runtime.getTypeManager().finalizeCompositeFieldLayout(ownerId);
    std::unique_ptr<Closure> closure;
    std::vector<InterpreterValue> snapshots;
    CallableId descriptorId;
    auto currentFrame = true;
    const auto probeId = runtime.registerNativeFunction(
        "test.captureReceiverProbe",
        [&](VirtualMachine* active) {
            const auto* descriptor = runtime.getScriptFunction(descriptorId);
            if (currentFrame) {
                snapshots = active->captureCurrentFrame(descriptor);
                currentFrame = false;

                return;
            }

            closure = active->createClosure(&runtime, descriptorId);
            if (closure->getCaptureType(0) == type) {
                const auto* parent = active->getCurrentCallable();
                const auto pointer = active->getEnclosingParameterStorage(
                    *parent->getCallStorageLayout().receiverOffset,
                    1
                );
                pointer[0].getStorageAddress()[0].setIntegerValue(99);
            }

        },
        {},
        runtime.getVoidType(),
        ""
    );
    BOOST_REQUIRE(probeId.isValid());
    const auto probe = runtime.registerInstruction(
        "test.captureCurrentReceiver",
        [](VirtualMachine* active) {
            const auto* function = active->getRuntime()->getFunction(
                active->getRuntime()->getCallableId("test.captureReceiverProbe")
            )->asNativeFunction();
            function->getFn()(active);
        },
        "",
        {}
    );
    auto compiler = runtime.createCompiler();
    std::vector<CallableId> methods;
    std::vector<CallableId> descriptors;
    for (const auto* receiverType : std::array<const Type*, 3>{type, empty, owner}) {
        const auto methodId = runtime.registerScriptMethod(
            receiverType->getId(),
            "capture",
            {},
            "receiver-capture.nomad",
            ""
        );
        BOOST_REQUIRE(methodId.isValid());
        auto* method = runtime.getScriptCallable(methodId);
        method->setReturnType(runtime.getVoidType());
        method->setFunctionStart(runtime.getInstructions().size());
        compiler->addFunctionEntry(method);
        compiler->addOpCode(probe);
        compiler->addOpCode(OpCodes::op_call_native_function);
        compiler->addId(probeId.id);
        compiler->addFunctionReturn(method);
        method->setFunctionEnd(runtime.getInstructions().size());
        methods.push_back(methodId);
        const auto id = runtime.registerScriptFunction(
            "receiverDescriptor" + receiverType->getTypeName(),
            "receiver-capture.nomad",
            ""
        );
        auto* descriptor = runtime.getScriptFunction(id);
        descriptor->setParentId(methodId);
        descriptor->createReceiverCapture(receiverType);
        descriptors.push_back(id);
    }

    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto iteration = 0; iteration < 3; ++iteration) {
        auto machine = runtime.createVirtualMachine();
        const Interpreter interpreter(*machine);
        {
            std::array<RuntimeValue, 2> source{
                RuntimeValue{NomadInteger{43}}, RuntimeValue{NomadString{"receiver owned"}}
            };
            const InterpreterValue receiver(type, source);
            type->freeValue(source.data());
            descriptorId = descriptors[0];
            currentFrame = true;
            const auto result = interpreter.runStructMethod(receiver, methods[0], {});
            BOOST_TEST(result.receiver.getStorage()[0].getIntegerValue() == 99);
            BOOST_TEST(receiver.getStorage()[0].getIntegerValue() == 43);
            BOOST_REQUIRE(closure != nullptr);
            BOOST_TEST(closure->getCaptureCount() == 1U);
            BOOST_TEST(closure->getCaptureType(0) == type);
            BOOST_TEST(closure->getCaptureStorage(0)[0].getIntegerValue() == 43);
            BOOST_TEST(closure->getCaptureStorage(0)[1].getStringValue() == "receiver owned");
            BOOST_TEST(snapshots[0].getStorage()[0].getIntegerValue() == 43);
        }

        snapshots.clear();
        closure.reset();
        descriptorId = descriptors[1];
        currentFrame = true;
        const InterpreterValue emptyReceiver(empty, std::span<const RuntimeValue>{});
        const auto emptyResult = interpreter.runStructMethod(emptyReceiver, methods[1], {});
        BOOST_TEST(emptyResult.receiver.getStorage().data() == nullptr);
        BOOST_TEST(closure->getCaptureCount() == 1U);
        BOOST_TEST(closure->getCaptureType(0) == empty);
        BOOST_TEST(closure->getCaptureStorage(0).data() == nullptr);
        BOOST_TEST(snapshots[0].getType() == empty);
        BOOST_TEST(snapshots[0].getStorage().data() == nullptr);
        snapshots.clear();
        closure.reset();
        {
            RuntimeValue object;
            object.setObjectValue(new ScriptObject(owner));
            const InterpreterValue receiver(owner, object);
            object.freeObjectValue();
            descriptorId = descriptors[2];
            currentFrame = true;
            BOOST_TEST(interpreter.runObjectMethod(receiver, methods[2], {}).getType()->isVoid());
            BOOST_TEST(closure->getCaptureValue(0).getObjectValue() == receiver.getValue().getObjectValue());
            BOOST_TEST(snapshots[0].getValue().getObjectValue() == receiver.getValue().getObjectValue());
        }

        snapshots.clear();
        BOOST_TEST(closure->getCaptureValue(0).getObjectValue()->getReferenceCount() == 1U);
        closure.reset();
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }

}

BOOST_AUTO_TEST_CASE(loop_callbacks_capture_each_iteration_value)
{
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    std::vector<std::unique_ptr<Closure>> callbacks;
    runtime.registerNativeFunction(
        "test.keep",
        [&runtime, &callbacks](VirtualMachine* machine) {
            callbacks.push_back(machine->createClosure(&runtime, CallableId{machine->getIdParameter(0)}));
        },
        {defParameter("callback", runtime.getCallbackType({}, runtime.getIntegerType()), "")},
        runtime.getVoidType(), ""
    );
    fixture.addFunction("main",
        "for value in range 1 4\n"
        "    test.keep fun\n"
        "        return value\n"
        "    end\n"
        "end"
    );
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_REQUIRE(!fixture.execute("main").fault.has_value());
    BOOST_REQUIRE(callbacks.size() == 3);
    for (auto index = size_t{0}; index < callbacks.size(); ++index) {
        RuntimeValue result;
        runtime.executeFunction(callbacks[index].get(), {}, result);
        BOOST_TEST(result.getIntegerValue() == static_cast<NomadInteger>(index + 1));
    }
}

BOOST_AUTO_TEST_CASE(callbacks_capture_mutated_parameters_and_allow_parameter_reassignment)
{
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    std::vector<std::unique_ptr<Closure>> callbacks;
    runtime.registerNativeFunction(
        "test.keep",
        [&runtime, &callbacks](VirtualMachine* machine) {
            callbacks.push_back(machine->createClosure(&runtime, CallableId{machine->getIdParameter(0)}));
        },
        {defParameter("callback", runtime.getCallbackType({runtime.getIntegerType()}, runtime.getIntegerType()), "")},
        runtime.getVoidType(), ""
    );
    fixture.addFunction("main",
        "params value:int\n"
        "for value in range 1 4\n"
        "    value = value + 10\n"
        "    test.keep fun offset\n"
        "        offset = offset + 100\n"
        "        return value + offset\n"
        "    end\n"
        "    value = 99\n"
        "end\n"
        "return value"
    );
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    const auto outcome = fixture.execute("main", {RuntimeValue{NomadInteger{0}}});
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 99);
    BOOST_REQUIRE(callbacks.size() == 3);
    for (auto index = size_t{0}; index < callbacks.size(); ++index) {
        for (auto invocation = 0; invocation < 2; ++invocation) {
            RuntimeValue result;
            runtime.executeFunction(callbacks[index].get(), {RuntimeValue{NomadInteger{0}}}, result);
            BOOST_TEST(result.getIntegerValue() == static_cast<NomadInteger>(index + 111));
        }
    }
}

BOOST_AUTO_TEST_CASE(then_is_restricted_to_the_current_function_root)
{
    for (const auto& source : std::vector<NomadString>{
        "if true\ntest.storeVoid then\nend",
        "while false\ntest.storeVoid then\nend",
        "until true\ntest.storeVoid then\nend",
        "do\ntest.storeVoid then\nend until true",
        "for value in range 1 2\ntest.storeVoid then\nend"
    }) {
        LanguageTestFixture fixture;
        CallbackStore store;
        registerCallbackNativeFunctions(fixture.getRuntime(), store);
        fixture.addFunction("main", source);
        BOOST_TEST_CONTEXT(source) {
            BOOST_TEST(!fixture.compile());
            BOOST_TEST(fixture.hasError("root"));
        }
    }
}

BOOST_AUTO_TEST_CASE(then_in_a_named_function_stops_at_its_end)
{
    LanguageTestFixture fixture;
    CallbackStore store;
    registerCallbackNativeFunctions(fixture.getRuntime(), store);
    fixture.addFunction("main",
        "fun nested\n"
        "    test.storeVoid then\n"
        "    captured = 10\n"
        "end\n"
        "nested\n"
        "return 42"
    );
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto outcome = fixture.execute("main");
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 42);
    BOOST_REQUIRE(store.closure != nullptr);
    BOOST_TEST(!invokeStoredClosure(fixture.getRuntime(), store, {}).fault.has_value());
}

BOOST_AUTO_TEST_CASE(explicit_callback_inside_a_loop_has_a_fresh_then_root)
{
    LanguageTestFixture fixture;
    CallbackStore store;
    registerCallbackNativeFunctions(fixture.getRuntime(), store);
    fixture.addFunction("main",
        "while true\n"
        "    test.storeVoid fun\n"
        "        test.storeVoid then\n"
        "        captured = 10\n"
        "    end\n"
        "    break\n"
        "end"
    );
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(!fixture.execute("main").fault.has_value());
}

BOOST_AUTO_TEST_CASE(named_file_function_callback)
{
    LanguageTestFixture fixture;
    CallbackStore store;
    registerCallbackNativeFunctions(fixture.getRuntime(), store);
    fixture.addFunction("handler", "params value:int\nreturn value * 10");
    fixture.addFunction("main", "test.store handler");

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_REQUIRE(!fixture.execute("main").fault.has_value());

    const auto outcome = invokeStoredClosure(fixture.getRuntime(), store, {RuntimeValue(NomadInteger{4})});
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 40);
}

BOOST_AUTO_TEST_CASE(named_function_callback)
{
    LanguageTestFixture fixture;
    CallbackStore store;
    registerCallbackNativeFunctions(fixture.getRuntime(), store);
    fixture.addFunction(
        "main",
        "fun afterStore value:int\n"
        "    return value + 1\n"
        "end\n"
        "test.store afterStore"
    );

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_REQUIRE(!fixture.execute("main").fault.has_value());
    BOOST_TEST(invokeStoredClosure(fixture.getRuntime(), store, {RuntimeValue(NomadInteger{1})}).integerValue == 2);
}

BOOST_AUTO_TEST_CASE(inline_fun_callback_captures_variables)
{
    LanguageTestFixture fixture;
    CallbackStore store;
    registerCallbackNativeFunctions(fixture.getRuntime(), store);
    fixture.addFunction(
        "main",
        "params base:int\n"
        "offset = 100\n"
        "test.store fun value\n"
        "    return value + base + offset\n"
        "end\n"
        "return offset"
    );

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

    const auto mainOutcome = fixture.execute("main", {RuntimeValue(NomadInteger{20})});
    BOOST_REQUIRE(!mainOutcome.fault.has_value());
    BOOST_TEST(mainOutcome.integerValue == 100);

    const auto outcome = invokeStoredClosure(fixture.getRuntime(), store, {RuntimeValue(NomadInteger{3})});
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 123);
}

BOOST_AUTO_TEST_CASE(inline_callback_passes_captured_parameter_to_function)
{
    LanguageTestFixture fixture;
    CallbackStore store;
    registerCallbackNativeFunctions(fixture.getRuntime(), store);
    fixture.addFunction("combine", "params first:int second:int\nreturn first + second");
    fixture.addFunction(
        "main",
        "params base:int\n"
        "test.store fun value\n"
        "    return combine base value\n"
        "end"
    );

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_REQUIRE(!fixture.execute("main", {RuntimeValue(NomadInteger{20})}).fault.has_value());

    const auto outcome = invokeStoredClosure(fixture.getRuntime(), store, {RuntimeValue(NomadInteger{3})});
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 23);
}

BOOST_AUTO_TEST_CASE(then_callback_consumes_rest_of_function)
{
    LanguageTestFixture fixture;
    CallbackStore store;
    registerCallbackNativeFunctions(fixture.getRuntime(), store);
    fixture.addFunction(
        "main",
        "base = 7\n"
        "global.beforeThen = true\n"
        "test.store then value\n"
        "return value * base"
    );

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_REQUIRE(!fixture.execute("main").fault.has_value());

    const auto outcome = invokeStoredClosure(fixture.getRuntime(), store, {RuntimeValue(NomadInteger{6})});
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 42);
}

BOOST_AUTO_TEST_CASE(void_inline_callback)
{
    LanguageTestFixture fixture;
    CallbackStore store;
    registerCallbackNativeFunctions(fixture.getRuntime(), store);
    fixture.addFunction(
        "main",
        "global.callbackRan = false\n"
        "test.storeVoid fun\n"
        "    global.callbackRan = true\n"
        "end"
    );
    fixture.addFunction("ran", "return global.callbackRan");

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_REQUIRE(!fixture.execute("main").fault.has_value());
    BOOST_TEST(fixture.execute("ran").booleanValue == false);

    BOOST_TEST(!invokeStoredClosure(fixture.getRuntime(), store, {}).fault.has_value());
    BOOST_TEST(fixture.execute("ran").booleanValue == true);
}

BOOST_AUTO_TEST_CASE(invalid_callbacks_are_rejected)
{
    const std::vector<std::string> sources = {
        // Unknown callback function.
        "test.store missingHandler",
        // Wrong parameter count.
        "test.store fun\n    return 1\nend",
        "test.store fun a b\n    return a\nend",
        // Wrong return type.
        "test.store fun value\n    return \"text\"\nend",
        // Missing return value.
        "test.store fun value\n    value = value\nend",
        // Named function with the wrong return type.
        "fun handler value:int\n    return 1.0\nend\ntest.store handler",
        // Missing `end`.
        "test.store fun value\n    return value",
        // Not a callback.
        "test.store 1",
        // `then` must match the callback signature too.
        "test.store then\nreturn 1",
        "test.store then value\nreturn 1.0",
    };

    for (const auto& source: sources) {
        BOOST_TEST_CONTEXT(source) {
            LanguageTestFixture fixture;
            CallbackStore store;
            registerCallbackNativeFunctions(fixture.getRuntime(), store);
            fixture.addFunction("main", source);

            BOOST_TEST(!fixture.compile());
        }
    }
}

BOOST_AUTO_TEST_CASE(callback_signature_mismatch_for_named_function)
{
    LanguageTestFixture fixture;
    CallbackStore store;
    registerCallbackNativeFunctions(fixture.getRuntime(), store);
    fixture.addFunction("handler", "params value:float\nreturn 1");
    fixture.addFunction("main", "test.store handler");

    BOOST_TEST(!fixture.compile());
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(language_events)

BOOST_AUTO_TEST_CASE(event_ids_support_registration_lookup_iteration_and_catalog_growth)
{
    Runtime runtime;
    BOOST_TEST(runtime.getEventId("missing") == EventId::INVALID);
    BOOST_TEST(runtime.getFirstEventId() == EventId::INVALID);
    for (const auto id : {EventId::INVALID, EventId{-2}, EventId{0}, EventId{NOMAD_ID_MAX}}) {
        BOOST_TEST(!runtime.getEventDefinition(id).has_value());
    }

    BOOST_REQUIRE(runtime.registerHub("example", HubMode::Opened).isValid());
    const auto integerId = runtime.getTypeId("int");
    const auto id = runtime.registerEvent("example.changed", {{"value", integerId, "int"}});
    BOOST_REQUIRE(id.isValid());
    BOOST_TEST(runtime.getFirstEventId() == id);
    BOOST_TEST(runtime.getEventId("example.changed") == id);
    BOOST_TEST(runtime.registerEvent("example.changed", {}) == EventId::INVALID);
    BOOST_TEST(runtime.registerEvent("example", {}) == EventId::INVALID);
    const auto definition = runtime.getEventDefinition(id);
    BOOST_REQUIRE(definition.has_value());
    BOOST_TEST(definition->id == id);
    BOOST_TEST(definition->name == "example.changed");
    BOOST_REQUIRE(definition->parameters.size() == 1U);
    BOOST_TEST(definition->parameters.front().typeId == integerId);

    auto lastId = id;
    for (auto index = 0; index < 64; ++index) {
        lastId = runtime.registerEvent("additional" + std::to_string(index), {});
        BOOST_REQUIRE(lastId.isValid());
    }
    BOOST_REQUIRE(runtime.registerHub("example.child", HubMode::Opened).isValid());
    BOOST_TEST(runtime.getEventId("example.changed") == id);
    const auto afterGrowth = runtime.getEventDefinition(id);
    BOOST_REQUIRE(afterGrowth.has_value());
    BOOST_TEST(afterGrowth->id == id);
    BOOST_TEST(afterGrowth->name == definition->name);
    BOOST_REQUIRE(afterGrowth->parameters.size() == 1U);
    BOOST_TEST(afterGrowth->parameters.front().name == "value");
    BOOST_TEST(afterGrowth->parameters.front().typeId == integerId);
    const auto* member = runtime.getHub(runtime.getHubId("example"))->getMember("changed");
    BOOST_REQUIRE(member != nullptr);
    const auto* event = std::get_if<EventId>(member);
    BOOST_REQUIRE(event != nullptr);
    BOOST_TEST(*event == id);

    auto count = NomadIndex{0};
    auto visitedId = EventId::INVALID;
    for (auto currentId = runtime.getFirstEventId(); currentId.isValid();
         currentId = runtime.getNextEventId(currentId)) {
        BOOST_REQUIRE(count < 65U);
        visitedId = currentId;
        ++count;
    }
    BOOST_TEST(count == 65U);
    BOOST_TEST(visitedId == lastId);
    BOOST_TEST(runtime.getNextEventId(lastId) == EventId::INVALID);
    BOOST_TEST(!runtime.getEventDefinition(EventId{NOMAD_ID_MAX - 1}).has_value());

    const auto compiler = runtime.createCompiler();
    IdentifierDefinition identifier{};
    compiler->getIdentifierDefinition("example.changed", nullptr, identifier);
    BOOST_TEST((identifier.identifierType == IdentifierType::Event));
    BOOST_TEST(identifier.eventId == id);
}

BOOST_AUTO_TEST_CASE(event_declaration_registers_event)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main", "event bespokeEvent x:int y:float z:string");

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

    auto& runtime = fixture.getRuntime();
    const auto eventId = runtime.getEventId("bespokeEvent");
    BOOST_REQUIRE(eventId.isValid());

    const auto event = runtime.getEventDefinition(eventId);
    BOOST_REQUIRE(event.has_value());
    BOOST_REQUIRE(event->parameters.size() == 3u);
    BOOST_TEST(event->parameters[0].name == "x");
    BOOST_TEST(event->parameters[0].typeName == "int");
    BOOST_TEST(event->parameters[1].name == "y");
    BOOST_TEST(event->parameters[1].typeName == "float");
    BOOST_TEST(event->parameters[2].name == "z");
    BOOST_TEST(event->parameters[2].typeName == "string");
}

BOOST_AUTO_TEST_CASE(event_without_parameters)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main", "event simpleEvent");

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

    const auto event = fixture.getRuntime().getEventDefinition(fixture.getRuntime().getEventId("simpleEvent"));
    BOOST_REQUIRE(event.has_value());
    BOOST_TEST(event->parameters.empty());
}

BOOST_AUTO_TEST_CASE(invalid_event_declarations_are_rejected)
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"event", ""},
        {"event dup\nevent dup", "already declared"},
        {"event bad x:unknownType", "Unknown type"},
        {"event bad x", ""},
        {"event bad x:", ""},
    };

    for (const auto& [source, fragment]: cases) {
        BOOST_TEST_CONTEXT(source) {
            const auto errors = compileErrors(source);

            BOOST_TEST(!errors.empty());
            BOOST_TEST(errors.find(fragment) != std::string::npos, errors);
        }
    }
}

BOOST_AUTO_TEST_CASE(events_are_shared_across_functions)
{
    LanguageTestFixture fixture;
    fixture.addFunction("a", "event shared value:int");
    fixture.addFunction("b", "event shared value:int");

    BOOST_TEST(!fixture.compile());
    BOOST_TEST(fixture.hasError("already declared"), fixture.getDiagnostics());
}

BOOST_AUTO_TEST_CASE(event_handler_inline_fun)
{
    LanguageTestFixture fixture;
    CallbackStore store;
    registerCallbackNativeFunctions(fixture.getRuntime(), store);
    fixture.addFunction(
        "main",
        "event scored points:int\n"
        "test.on scored fun points\n"
        "    global.total = points\n"
        "end"
    );
    fixture.addFunction("total", "return global.total");

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_REQUIRE(!fixture.execute("main").fault.has_value());
    BOOST_TEST(store.eventId == fixture.getRuntime().getEventId("scored"));

    BOOST_TEST(!invokeStoredClosure(fixture.getRuntime(), store, {RuntimeValue(NomadInteger{12})}).fault.has_value());
    BOOST_TEST(fixture.execute("total").integerValue == 12);
}

BOOST_AUTO_TEST_CASE(event_handler_named_function)
{
    LanguageTestFixture fixture;
    CallbackStore store;
    registerCallbackNativeFunctions(fixture.getRuntime(), store);
    fixture.addFunction(
        "main",
        "event scored points:int\n"
        "fun handleScored points:int\n"
        "    global.total = points * 2\n"
        "end\n"
        "test.on scored handleScored"
    );
    fixture.addFunction("total", "return global.total");

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_REQUIRE(!fixture.execute("main").fault.has_value());
    BOOST_TEST(!invokeStoredClosure(fixture.getRuntime(), store, {RuntimeValue(NomadInteger{5})}).fault.has_value());
    BOOST_TEST(fixture.execute("total").integerValue == 10);
}

BOOST_AUTO_TEST_CASE(event_dispatch_passes_typed_arguments)
{
    LanguageTestFixture fixture;
    CallbackStore store;
    registerCallbackNativeFunctions(fixture.getRuntime(), store);
    fixture.addFunction(
        "main",
        "event moved x:int y:int\n"
        "a = 3\n"
        "test.trigger moved (a + 1) 20"
    );

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_REQUIRE(!fixture.execute("main").fault.has_value());

    BOOST_REQUIRE(store.dispatchedEvents.size() == 1u);
    BOOST_TEST(store.dispatchedEvents[0] == fixture.getRuntime().getEventId("moved"));
    BOOST_REQUIRE(store.dispatchedIntegers.size() == 2u);
    BOOST_TEST(store.dispatchedIntegers[0] == 4);
    BOOST_TEST(store.dispatchedIntegers[1] == 20);
}

BOOST_AUTO_TEST_CASE(invalid_event_usage_is_rejected)
{
    const std::vector<std::string> sources = {
        "test.on unknownEvent fun\nend",
        "test.trigger unknownEvent",
        "event moved x:int\ntest.trigger moved",
        "event moved x:int\ntest.trigger moved 1.0",
        "event moved x:int\ntest.trigger moved 1 2",
        "event moved x:int\ntest.on moved fun\nend",
        "event moved x:int\nfun handler x:float\nend\ntest.on moved handler",
    };

    for (const auto& source: sources) {
        BOOST_TEST_CONTEXT(source) {
            LanguageTestFixture fixture;
            CallbackStore store;
            registerCallbackNativeFunctions(fixture.getRuntime(), store);
            fixture.addFunction("main", source);

            BOOST_TEST(!fixture.compile());
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
