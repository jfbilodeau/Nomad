// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/script/Hub.hpp>
#include <nomad/script/Interpreter.hpp>

#include <boost/test/unit_test.hpp>

#include <memory>

using namespace nomad;
using namespace nomad::test;

namespace {

template<typename Function>
concept RegistersTypedNativeFunction = requires(Runtime& runtime, Function function) {
    runtime.registerNativeFunction("typed", function, std::vector<TypedParameterDefinition>{}, NomadDoc(""));
};

static_assert(RegistersTypedNativeFunction<NomadInteger(*)(NomadString, NomadFloat)>);
static_assert(RegistersTypedNativeFunction<void(*)(const NomadString&, const NomadInteger&)>);
static_assert(RegistersTypedNativeFunction<NomadBoolean(*)() noexcept>);
static_assert(!RegistersTypedNativeFunction<void(*)(NomadInteger&)>);
static_assert(!RegistersTypedNativeFunction<void(*)(NomadString&&)>);
static_assert(!RegistersTypedNativeFunction<void(*)(NomadObject*)>);
static_assert(!RegistersTypedNativeFunction<void(*)(LogLevel)>);
static_assert(!RegistersTypedNativeFunction<NomadString&(*)()>);
static_assert(!RegistersTypedNativeFunction<double(*)()>);
static_assert(!RegistersTypedNativeFunction<decltype([](auto value) { return value; })>);
static_assert(!RegistersTypedNativeFunction<decltype([value = std::make_unique<NomadInteger>(1)] {
    return *value;
})>);

NomadString describeValues(NomadString text, NomadInteger count, NomadFloat scale, NomadBoolean enabled) {
    return text + std::to_string(count) + (scale == 2.5f && enabled ? ":ok" : ":wrong");
}

NomadInteger measureText(const NomadString& text, NomadFloat scale) noexcept {
    return static_cast<NomadInteger>(text.size()) * static_cast<NomadInteger>(scale);
}

} // namespace

BOOST_AUTO_TEST_SUITE(native_typed_functions)

BOOST_AUTO_TEST_CASE(free_functions_infer_types_and_preserve_parameter_metadata) {
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto id = runtime.registerNativeFunction("describe", describeValues, {
        {"text", NomadParamDoc("The input text.")},
        {"count", NomadParamDoc("The count.")},
        {"scale", NomadParamDoc("The scale.")},
        {"enabled", NomadParamDoc("Whether enabled.")}
    }, NomadDoc("Describes typed values."));
    BOOST_REQUIRE(id.isValid());
    const auto* function = runtime.getNativeFunction(id);
    BOOST_REQUIRE(function != nullptr);
    BOOST_TEST(function->getReturnType() == runtime.getStringType());
    BOOST_TEST(function->getDoc() == "Describes typed values.");
    BOOST_REQUIRE_EQUAL(function->getParameterCount(), 4U);
    BOOST_TEST(function->getParameters()[0].name == "text");
    BOOST_TEST(function->getParameters()[0].doc == "The input text.");
    BOOST_TEST(function->getParameters()[0].type == runtime.getStringType());
    BOOST_TEST(function->getParameters()[1].type == runtime.getIntegerType());
    BOOST_TEST(function->getParameters()[2].type == runtime.getFloatType());
    BOOST_TEST(function->getParameters()[3].type == runtime.getBooleanType());
    fixture.addFunction("main", "return describe \"value:\" 7 2.5 true");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto outcome = fixture.execute("main");
    BOOST_REQUIRE(!outcome.fault.has_value());
    BOOST_TEST(outcome.text == "value:7:ok");
    BOOST_TEST(outcome.leakedStrings == 0);
}

BOOST_AUTO_TEST_CASE(noexcept_functions_and_captured_lambdas_return_primitive_values) {
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    BOOST_REQUIRE(runtime.registerNativeFunction("measure", measureText,
        {{"text", ""}, {"scale", ""}}, NomadDoc("")).isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction("scale", [factor = NomadFloat{2.0f}](NomadFloat value) {
        return value * factor;
    }, {{"value", ""}}, NomadDoc("")).isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction("invert", [](NomadBoolean value) noexcept {
        return !value;
    }, {{"value", ""}}, NomadDoc("")).isValid());
    fixture.addFunction("integerResult", "return measure \"sample\" 3.0");
    fixture.addFunction("floatResult", "return scale 2.5");
    fixture.addFunction("booleanResult", "return invert false");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(fixture.execute("integerResult").integerValue == 18);
    BOOST_TEST(fixture.execute("floatResult").floatValue == 5.0f);
    BOOST_TEST(fixture.execute("booleanResult").booleanValue);
}

BOOST_AUTO_TEST_CASE(void_and_zero_argument_functions_work_with_mutable_captures) {
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    NomadString received;
    BOOST_REQUIRE(runtime.registerNativeFunction("record", [&received](const NomadString& text) {
        received = text;
    }, {{"text", ""}}, NomadDoc("")).isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction("next", [count = NomadInteger{40}]() mutable {
        return ++count;
    }, {}, NomadDoc("")).isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction("clear", [&received] {
        received.clear();
    }, {}, NomadDoc("")).isValid());
    fixture.addFunction("recordValue", "record \"captured\"\nfirst = next\nsecond = next\nreturn first + second");
    fixture.addFunction("clearValue", "clear");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(fixture.execute("recordValue").integerValue == 83);
    BOOST_TEST(received == "captured");
    const auto outcome = fixture.execute("clearValue");
    BOOST_REQUIRE(!outcome.fault.has_value());
    BOOST_TEST(outcome.returnTypeName == runtime.getVoidType()->getTypeName());
    BOOST_TEST(received.empty());
}

BOOST_AUTO_TEST_CASE(hub_functions_share_overloads_with_the_vm_callback_api) {
    for (const auto mode : {HubMode::Opened, HubMode::Closed}) {
        LanguageTestFixture fixture;
        auto& runtime = fixture.getRuntime();
        const auto hubId = runtime.registerHub("example.child", mode);
        BOOST_REQUIRE(hubId.isValid());
        const auto typedId = runtime.registerHubNativeFunction(hubId, "convert", [](NomadInteger value) {
            return value + 1;
        }, {{"value", ""}}, NomadDoc("Typed integer overload."));
        BOOST_REQUIRE(typedId.isValid());
        const auto manualId = runtime.registerHubNativeFunction(hubId, "convert", [](VirtualMachine* machine) {
            machine->setIntegerResult(static_cast<NomadInteger>(machine->getStringParameter(0)->getString().size()));
        }, {defParameter("value", runtime.getStringType(), "")}, runtime.getIntegerType(), NomadDoc(""));
        BOOST_REQUIRE(manualId.isValid());
        const auto* member = runtime.getHub(hubId)->getMember("convert");
        BOOST_REQUIRE(member != nullptr);
        const auto* functions = std::get_if<HubFunctions>(member);
        BOOST_REQUIRE(functions != nullptr);
        BOOST_REQUIRE_EQUAL(functions->ids.size(), 2U);
        BOOST_TEST(functions->ids[0] == typedId);
        BOOST_TEST(functions->ids[1] == manualId);
        BOOST_TEST(runtime.getNativeFunctionId("convert").isInvalid());
        fixture.addFunction("main",
            "integerValue = example.child.convert 40\n"
            "stringValue = example.child.convert \"x\"\n"
            "return integerValue + stringValue"
        );
        BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
        BOOST_TEST(fixture.execute("main").integerValue == 42);
    }
}

BOOST_AUTO_TEST_CASE(invalid_registration_does_not_publish_a_callable) {
    Runtime runtime;
    const auto hubId = runtime.registerHub("example", HubMode::Opened);
    BOOST_REQUIRE(hubId.isValid());
    const auto before = runtime.getFunctionTableSize();
    const auto callback = [](NomadInteger value) { return value; };
    BOOST_TEST(runtime.registerNativeFunction("invalid", callback, {}, NomadDoc("")).isInvalid());
    BOOST_TEST(runtime.registerNativeFunction("invalid", callback,
        {{"first", ""}, {"second", ""}}, NomadDoc("")).isInvalid());
    BOOST_TEST(runtime.registerNativeFunction("invalid", callback, {{"", ""}}, NomadDoc("")).isInvalid());
    NomadInteger (*missing)(NomadInteger) = nullptr;
    BOOST_TEST(runtime.registerNativeFunction("invalid", missing, {{"value", ""}}, NomadDoc("")).isInvalid());
    BOOST_TEST(runtime.registerHubNativeFunction(HubId::INVALID, "invalid", callback,
        {{"value", ""}}, NomadDoc("")).isInvalid());
    BOOST_TEST(runtime.registerHubNativeFunction(hubId, "child.invalid", callback,
        {{"value", ""}}, NomadDoc("")).isInvalid());
    BOOST_TEST(runtime.registerHubNativeFunction(hubId, "invalid", callback, {}, NomadDoc("")).isInvalid());
    BOOST_TEST(runtime.registerHubNativeFunction(hubId, "invalid", missing,
        {{"value", ""}}, NomadDoc("")).isInvalid());
    BOOST_TEST(runtime.getFunctionTableSize() == before);
    BOOST_TEST(runtime.getFunctionId("invalid").isInvalid());
    BOOST_TEST(runtime.getHub(hubId)->getMembers().empty());
    BOOST_REQUIRE(runtime.registerHubNativeFunction(hubId, "valid", callback,
        {{"value", ""}}, NomadDoc("")).isValid());
    const auto after = runtime.getFunctionTableSize();
    BOOST_TEST(runtime.registerHubNativeFunction(hubId, "valid", callback,
        {{"value", ""}}, NomadDoc("")).isInvalid());
    BOOST_TEST(runtime.getFunctionTableSize() == after);
}

BOOST_AUTO_TEST_CASE(string_results_copy_values_and_release_temporary_arguments) {
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    BOOST_REQUIRE(runtime.registerNativeFunction("copyText", [](const NomadString& text) -> NomadString {
        return text;
    }, {{"text", ""}}, NomadDoc("")).isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction("emptyText", []() -> NomadString {
        return {};
    }, {}, NomadDoc("")).isValid());
    fixture.addFunction("main", "return copyText (copyText $\"temporary {7}\")");
    fixture.addFunction("empty", "return emptyText");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    for (auto index = 0; index < 20; ++index) {
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE(!outcome.fault.has_value());
        BOOST_TEST(outcome.text == "temporary 7");
        BOOST_TEST(outcome.leakedStrings == 0);
    }
    const auto empty = fixture.execute("empty");
    BOOST_TEST(empty.text.empty());
    BOOST_TEST(empty.leakedStrings == 0);
}

BOOST_AUTO_TEST_CASE(callback_faults_preserve_existing_exception_and_cleanup_behavior) {
    auto manualRetainedStrings = NomadInteger{0};
    for (const auto typed : {false, true}) {
        LanguageTestFixture fixture;
        auto& runtime = fixture.getRuntime();
        if (typed) {
            BOOST_REQUIRE(runtime.registerNativeFunction("fail", [](NomadString) {
                throw VirtualMachineException("Callback failed");
            }, {{"text", ""}}, NomadDoc("")).isValid());
        } else {
            BOOST_REQUIRE(runtime.registerNativeFunction("fail", [](VirtualMachine*) {
                throw VirtualMachineException("Callback failed");
            }, {defParameter("text", runtime.getStringType(), "")}, runtime.getVoidType(), NomadDoc("")).isValid());
        }
        fixture.addFunction("main", "fail $\"temporary {7}\"");
        fixture.addFunction("recovery", "return 42");
        const auto compiled = fixture.compile();
        BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
        const auto before = RuntimeValue::getLiveStringCount();
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE(outcome.fault.has_value());
        BOOST_TEST(outcome.fault->find("Callback failed") != NomadString::npos);
        const auto retainedStrings = RuntimeValue::getLiveStringCount() - before;
        if (typed) {
            BOOST_TEST(retainedStrings == manualRetainedStrings);
        } else {
            manualRetainedStrings = retainedStrings;
        }
        BOOST_TEST(fixture.execute("recovery").integerValue == 42);
    }
}

BOOST_AUTO_TEST_CASE(host_calls_release_string_arguments_when_typed_callbacks_throw) {
    Runtime runtime;
    const auto id = runtime.registerNativeFunction("fail", [](NomadString) {
        throw VirtualMachineException("Callback failed");
    }, {{"text", ""}}, NomadDoc(""));
    BOOST_REQUIRE(id.isValid());
    auto machine = runtime.createVirtualMachine();
    const auto before = RuntimeValue::getLiveStringCount();
    RuntimeValue argument(NomadString("temporary argument"));
    BOOST_CHECK_THROW(machine->runNativeFunction(id, {argument}), VirtualMachineException);
    BOOST_TEST(argument.getObjectValue()->getReferenceCount() == 1U);
    argument.freeObjectValue();
    BOOST_TEST(RuntimeValue::getLiveStringCount() == before);
}

BOOST_AUTO_TEST_CASE(typed_functions_are_available_to_the_interpreter) {
    Runtime runtime;
    BOOST_REQUIRE(runtime.registerNativeFunction("measure", measureText,
        {{"text", ""}, {"scale", ""}}, NomadDoc("")).isValid());
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);
    BOOST_REQUIRE(Interpreter{}.execute("measure \"abc\" 2.0", context));
    BOOST_REQUIRE(context.getResult() != nullptr);
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 6);
}

BOOST_AUTO_TEST_SUITE_END()
