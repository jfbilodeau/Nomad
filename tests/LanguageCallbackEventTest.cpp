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
#include <vector>

using namespace nomad;
using namespace nomad::test;

namespace {

struct CallbackStore {
    std::unique_ptr<Closure> closure;
    NomadId eventId = NOMAD_INVALID_ID;
    std::vector<NomadId> dispatchedEvents;
    std::vector<NomadInteger> dispatchedIntegers;
};

// `test.store callback:(int) -> int` keeps the callback so the test can invoke it once the function has finished.
void registerCallbackNativeFunctions(Runtime& runtime, CallbackStore& store) {
    runtime.registerNativeFunction(
        "test.store",
        [&runtime, &store](VirtualMachine* virtualMachine) {
            store.closure = virtualMachine->createClosure(&runtime, virtualMachine->getIdParameter(0));
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
            store.closure = virtualMachine->createClosure(&runtime, virtualMachine->getIdParameter(0));
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
            store.eventId = virtualMachine->getParameter(0).getIdValue();
            store.closure = virtualMachine->createClosure(&runtime, virtualMachine->getParameter(1).getIdValue());
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
            const auto eventId = virtualMachine->getIdParameter(0);
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

BOOST_AUTO_TEST_CASE(event_declaration_registers_event)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main", "event bespokeEvent x:int y:float z:string");

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

    auto& runtime = fixture.getRuntime();
    const auto eventId = runtime.getEventId("bespokeEvent");
    BOOST_REQUIRE(eventId != NOMAD_INVALID_ID);

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
