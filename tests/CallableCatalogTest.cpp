// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <LanguageTestFixture.hpp>

#include <nomad/script/Callable.hpp>
#include <nomad/script/NativeFunction.hpp>
#include <nomad/script/Runtime.hpp>

#include <vector>

using namespace nomad;
using namespace nomad::test;

namespace {

NomadId registerVoidNativeFunction(
    Runtime& runtime,
    const NomadString& name,
    const std::vector<NativeFunctionParameterDefinition>& parameters = {}
) {
    return runtime.registerNativeFunction(
        name,
        [](VirtualMachine*) {},
        parameters,
        runtime.getVoidType(),
        NomadDoc("Test native function")
    );
}

NomadId registerNativeFunction(
    Runtime& runtime,
    const NomadString& name,
    const std::vector<NativeFunctionParameterDefinition>& parameters,
    const Type* returnType
) {
    return runtime.registerNativeFunction(
        name,
        [](VirtualMachine*) {},
        parameters,
        returnType,
        NomadDoc("Test native function")
    );
}

} // namespace

BOOST_AUTO_TEST_SUITE(callable_catalog)

BOOST_AUTO_TEST_CASE(unknown_name_resolves_to_invalid_callable)
{
    const Runtime runtime;

    const auto callable = runtime.getCallableId("missing");

    BOOST_TEST(!callable.isValid());
    BOOST_TEST(runtime.getCallableOverloadCount("missing") == NomadIndex{0});
}

BOOST_AUTO_TEST_CASE(native_function_is_registered_in_catalog)
{
    Runtime runtime;

    const auto nativeFunctionId = registerVoidNativeFunction(
        runtime,
        "test.greet",
        {defParameter("name", runtime.getStringType(), NomadDoc("Name"))}
    );

    BOOST_REQUIRE(nativeFunctionId != NOMAD_INVALID_ID);

    const auto callable = runtime.getCallableId("test.greet");

    BOOST_REQUIRE(callable.isValid());
    BOOST_TEST((callable.kind == CallableKind::NativeFunction));
    BOOST_TEST(callable.id == nativeFunctionId);
    BOOST_TEST(runtime.getCallableName(callable) == "test.greet");
    BOOST_TEST(runtime.getCallableParameterCount(callable) == NomadIndex{1});
    BOOST_TEST(runtime.getCallableParameterName(callable, 0) == "name");
    BOOST_TEST(runtime.getCallableParameterType(callable, 0) == runtime.getStringType());
    BOOST_TEST(runtime.getCallableReturnType(callable) == runtime.getVoidType());
}

BOOST_AUTO_TEST_CASE(function_is_registered_in_catalog)
{
    Runtime runtime;

    const auto functionId = runtime.registerFunction("entities.player", "entities/player.nomad", "return 1");

    BOOST_REQUIRE(functionId != NOMAD_INVALID_ID);

    const auto callable = runtime.getCallableId("entities.player");

    BOOST_REQUIRE(callable.isValid());
    BOOST_TEST((callable.kind == CallableKind::Function));
    BOOST_TEST(callable.id == functionId);
    BOOST_TEST(runtime.getCallableName(callable) == "entities.player");
    BOOST_TEST(runtime.getCallableParameterCount(callable) == NomadIndex{0});
}

// The catalog reads signatures from the owning definition, so a return type inferred after registration is visible.
BOOST_AUTO_TEST_CASE(function_signature_is_read_live)
{
    Runtime runtime;

    const auto functionId = runtime.registerFunction("compute", "compute.nomad", "return 1");
    const auto callable = runtime.getCallableId("compute");

    BOOST_REQUIRE(callable.isValid());
    BOOST_TEST(runtime.getCallableReturnType(callable) == nullptr);

    auto* function = runtime.getFunction(functionId);
    BOOST_REQUIRE(function != nullptr);

    function->addParameter("value", runtime.getIntegerType());
    function->setReturnType(runtime.getFloatType());

    BOOST_TEST(runtime.getCallableParameterCount(callable) == NomadIndex{1});
    BOOST_TEST(runtime.getCallableParameterName(callable, 0) == "value");
    BOOST_TEST(runtime.getCallableParameterType(callable, 0) == runtime.getIntegerType());
    BOOST_TEST(runtime.getCallableReturnType(callable) == runtime.getFloatType());
}

BOOST_AUTO_TEST_CASE(catalog_separates_native_functions_from_functions)
{
    Runtime runtime;

    registerVoidNativeFunction(runtime, "native.only");
    runtime.registerFunction("function.only", "function.nomad", "return 1");

    BOOST_TEST(runtime.getNativeFunctionId("native.only") != NOMAD_INVALID_ID);
    BOOST_TEST(runtime.getFunctionId("native.only") == NOMAD_INVALID_ID);

    BOOST_TEST(runtime.getFunctionId("function.only") != NOMAD_INVALID_ID);
    BOOST_TEST(runtime.getNativeFunctionId("function.only") == NOMAD_INVALID_ID);
}

BOOST_AUTO_TEST_CASE(duplicate_native_function_name_is_rejected)
{
    Runtime runtime;

    BOOST_REQUIRE(registerVoidNativeFunction(runtime, "duplicate") != NOMAD_INVALID_ID);

    BOOST_TEST(registerVoidNativeFunction(runtime, "duplicate") == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.getCallableOverloadCount("duplicate") == NomadIndex{1});
}

BOOST_AUTO_TEST_CASE(duplicate_function_name_is_rejected)
{
    Runtime runtime;

    BOOST_REQUIRE(runtime.registerFunction("duplicate", "duplicate.nomad", "return 1") != NOMAD_INVALID_ID);

    BOOST_TEST(runtime.registerFunction("duplicate", "other.nomad", "return 2") == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.getCallableOverloadCount("duplicate") == NomadIndex{1});
}

// A function may not shadow a native function, and vice versa: one name means one callable.
BOOST_AUTO_TEST_CASE(function_cannot_shadow_native_function)
{
    Runtime runtime;

    // `log.info` is registered as a built-in native function when the runtime is constructed.
    BOOST_REQUIRE(runtime.getNativeFunctionId("log.info") != NOMAD_INVALID_ID);

    BOOST_TEST(runtime.registerFunction("log.info", "log/info.nomad", "return 1") == NOMAD_INVALID_ID);

    const auto callable = runtime.getCallableId("log.info");

    BOOST_REQUIRE(callable.isValid());
    BOOST_TEST((callable.kind == CallableKind::NativeFunction));
}

BOOST_AUTO_TEST_CASE(native_function_cannot_shadow_function)
{
    Runtime runtime;

    BOOST_REQUIRE(runtime.registerFunction("entities.player", "entities/player.nomad", "return 1") != NOMAD_INVALID_ID);

    BOOST_TEST(registerVoidNativeFunction(runtime, "entities.player") == NOMAD_INVALID_ID);

    const auto callable = runtime.getCallableId("entities.player");

    BOOST_REQUIRE(callable.isValid());
    BOOST_TEST((callable.kind == CallableKind::Function));
}

BOOST_AUTO_TEST_CASE(overloads_are_reported_for_a_registered_name)
{
    Runtime runtime;

    const auto nativeFunctionId = registerVoidNativeFunction(runtime, "test.single");

    std::vector<CallableId> overloads;
    runtime.getCallableOverloads("test.single", overloads);

    BOOST_REQUIRE(overloads.size() == 1);
    BOOST_TEST(overloads[0].id == nativeFunctionId);
    BOOST_TEST((overloads[0].kind == CallableKind::NativeFunction));

    runtime.getCallableOverloads("missing", overloads);
    BOOST_TEST(overloads.empty());
}

///////////////////////////////////////////////////////////////////////////////
/// Overloading
///////////////////////////////////////////////////////////////////////////////
BOOST_AUTO_TEST_CASE(native_functions_can_be_overloaded_on_parameter_type)
{
    Runtime runtime;

    const auto fromFloat = registerNativeFunction(
        runtime,
        "test.convert",
        {defParameter("value", runtime.getFloatType(), NomadDoc("Value"))},
        runtime.getIntegerType()
    );
    const auto fromString = registerNativeFunction(
        runtime,
        "test.convert",
        {defParameter("value", runtime.getStringType(), NomadDoc("Value"))},
        runtime.getIntegerType()
    );

    BOOST_REQUIRE(fromFloat != NOMAD_INVALID_ID);
    BOOST_REQUIRE(fromString != NOMAD_INVALID_ID);
    BOOST_TEST(runtime.getCallableOverloadCount("test.convert") == NomadIndex{2});

    NomadString error;

    const auto selectedFloat = runtime.resolveCallableOverload("test.convert", {runtime.getFloatType()}, error);
    BOOST_REQUIRE_MESSAGE(selectedFloat.isValid(), error);
    BOOST_TEST(selectedFloat.id == fromFloat);

    const auto selectedString = runtime.resolveCallableOverload("test.convert", {runtime.getStringType()}, error);
    BOOST_REQUIRE_MESSAGE(selectedString.isValid(), error);
    BOOST_TEST(selectedString.id == fromString);
}

// `$stringref` accepts a `string`, so both overloads are viable; the exact one has to win.
BOOST_AUTO_TEST_CASE(overload_selection_prefers_an_exact_match)
{
    Runtime runtime;

    registerNativeFunction(
        runtime,
        "test.pick",
        {defParameter("text", runtime.getStringRefType(), NomadDoc("Text"))},
        runtime.getIntegerType()
    );
    const auto takesString = registerNativeFunction(
        runtime,
        "test.pick",
        {defParameter("text", runtime.getStringType(), NomadDoc("Text"))},
        runtime.getIntegerType()
    );

    BOOST_REQUIRE(takesString != NOMAD_INVALID_ID);

    NomadString error;
    const auto selected = runtime.resolveCallableOverload("test.pick", {runtime.getStringType()}, error);

    BOOST_REQUIRE_MESSAGE(selected.isValid(), error);
    BOOST_TEST(selected.id == takesString);
}

BOOST_AUTO_TEST_CASE(overload_resolution_reports_when_nothing_matches)
{
    Runtime runtime;

    registerNativeFunction(
        runtime,
        "test.convert",
        {defParameter("value", runtime.getFloatType(), NomadDoc("Value"))},
        runtime.getIntegerType()
    );

    NomadString error;
    const auto selected = runtime.resolveCallableOverload("test.convert", {runtime.getBooleanType()}, error);

    BOOST_TEST(!selected.isValid());
    BOOST_TEST(!error.empty());
}

BOOST_AUTO_TEST_CASE(overload_with_a_different_parameter_count_is_rejected)
{
    Runtime runtime;

    BOOST_REQUIRE(
        registerNativeFunction(
            runtime,
            "test.convert",
            {defParameter("value", runtime.getFloatType(), NomadDoc("Value"))},
            runtime.getIntegerType()
        ) != NOMAD_INVALID_ID
    );

    BOOST_TEST(
        registerNativeFunction(
            runtime,
            "test.convert",
            {
                defParameter("value", runtime.getFloatType(), NomadDoc("Value")),
                defParameter("fallback", runtime.getIntegerType(), NomadDoc("Fallback"))
            },
            runtime.getIntegerType()
        ) == NOMAD_INVALID_ID
    );

    BOOST_TEST(runtime.getCallableOverloadCount("test.convert") == NomadIndex{1});
}

// The parser decides between a statement and an expression from the name alone, so a set cannot mix the two.
BOOST_AUTO_TEST_CASE(overload_mixing_void_and_value_returns_is_rejected)
{
    Runtime runtime;

    BOOST_REQUIRE(
        registerNativeFunction(
            runtime,
            "test.convert",
            {defParameter("value", runtime.getFloatType(), NomadDoc("Value"))},
            runtime.getIntegerType()
        ) != NOMAD_INVALID_ID
    );

    BOOST_TEST(
        registerVoidNativeFunction(
            runtime,
            "test.convert",
            {defParameter("value", runtime.getStringType(), NomadDoc("Value"))}
        ) == NOMAD_INVALID_ID
    );

    BOOST_TEST(runtime.getCallableOverloadCount("test.convert") == NomadIndex{1});
}

// A callback is parsed differently from an expression, so it cannot be part of an overload set.
BOOST_AUTO_TEST_CASE(overload_with_a_callback_parameter_is_rejected)
{
    Runtime runtime;

    const auto* predicateType = runtime.getPredicateType();

    BOOST_REQUIRE(
        registerVoidNativeFunction(
            runtime,
            "test.run",
            {defParameter("condition", predicateType, NomadDoc("Condition"))}
        ) != NOMAD_INVALID_ID
    );

    BOOST_TEST(
        registerVoidNativeFunction(
            runtime,
            "test.run",
            {defParameter("condition", runtime.getBooleanType(), NomadDoc("Condition"))}
        ) == NOMAD_INVALID_ID
    );

    BOOST_TEST(runtime.getCallableOverloadCount("test.run") == NomadIndex{1});
}

BOOST_AUTO_TEST_SUITE_END()

///////////////////////////////////////////////////////////////////////////////
/// Overload resolution in compiled code
///////////////////////////////////////////////////////////////////////////////
BOOST_AUTO_TEST_SUITE(callable_overload_compilation)

namespace {

// Records which overload of `test.record` the compiled code selected.
NomadString s_selectedOverload;

void registerRecordOverloads(Runtime& runtime) {
    runtime.registerNativeFunction(
        "test.record",
        [](VirtualMachine*) { s_selectedOverload = "int"; },
        {defParameter("value", runtime.getIntegerType(), NomadDoc("Value"))},
        runtime.getVoidType(),
        NomadDoc("Record an integer")
    );

    runtime.registerNativeFunction(
        "test.record",
        [](VirtualMachine*) { s_selectedOverload = "string"; },
        {defParameter("value", runtime.getStringType(), NomadDoc("Value"))},
        runtime.getVoidType(),
        NomadDoc("Record a string")
    );
}

} // namespace

BOOST_AUTO_TEST_CASE(statement_call_selects_the_overload_matching_its_argument)
{
    {
        LanguageTestFixture fixture;
        registerRecordOverloads(fixture.getRuntime());
        fixture.addFunction("main", "test.record 1\nreturn 0");

        BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

        s_selectedOverload.clear();
        const auto outcome = fixture.execute("main");

        BOOST_TEST(!outcome.fault.has_value());
        BOOST_TEST(s_selectedOverload == "int");
    }

    {
        LanguageTestFixture fixture;
        registerRecordOverloads(fixture.getRuntime());
        fixture.addFunction("main", "test.record \"text\"\nreturn 0");

        BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

        s_selectedOverload.clear();
        const auto outcome = fixture.execute("main");

        BOOST_TEST(!outcome.fault.has_value());
        BOOST_TEST(s_selectedOverload == "string");
    }
}

BOOST_AUTO_TEST_CASE(call_matching_no_overload_is_a_compile_error)
{
    LanguageTestFixture fixture;
    registerRecordOverloads(fixture.getRuntime());
    fixture.addFunction("main", "test.record 1.5\nreturn 0");

    BOOST_TEST(!fixture.compile());
    BOOST_TEST(fixture.hasError("No overload of 'test.record'"));
}

BOOST_AUTO_TEST_SUITE_END()
