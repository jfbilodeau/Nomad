// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/script/Closure.hpp>
#include <nomad/script/NativeFunction.hpp>
#include <nomad/script/Event.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

using namespace nomad;
using namespace nomad::test;

static_assert(std::is_same_v<decltype(EventDefinition{}.id), EventId>);
static_assert(std::is_same_v<decltype(EventDispatch{}.eventId), EventId>);
static_assert(std::is_same_v<decltype(HubEvent{}.id), EventId>);
static_assert(std::is_same_v<decltype(IdentifierDefinition{}.eventId), EventId>);
static_assert(!std::is_convertible_v<NomadId, EventId>);
static_assert(!std::is_convertible_v<EventId, NomadId>);
static_assert(!std::is_convertible_v<CallableId, EventId>);
static_assert(!std::is_convertible_v<EventId, CallableId>);
static_assert(!std::is_convertible_v<ConstantId, EventId>);
static_assert(!std::is_convertible_v<EventId, ConstantId>);
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
        outcome.text = returnType->toString(result);

        if (returnType == runtime.getIntegerType()) {
            outcome.integerValue = result.getIntegerValue();
        }

        returnType->freeValue(result);
    }

    return outcome;
}

} // namespace

BOOST_AUTO_TEST_SUITE(language_callbacks)

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

    BOOST_REQUIRE(runtime.registerHub("example").isValid());
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
    BOOST_REQUIRE(runtime.registerHub("example.child").isValid());
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
    const auto* event = std::get_if<HubEvent>(member);
    BOOST_REQUIRE(event != nullptr);
    BOOST_TEST(event->id == id);

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
