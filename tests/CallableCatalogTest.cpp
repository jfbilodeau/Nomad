// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <LanguageTestFixture.hpp>

#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/script/Callable.hpp>
#include <nomad/script/NativeCallable.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptConstructor.hpp>
#include <nomad/script/ScriptMethod.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/NativeFunctions.hpp>

#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace nomad;
using namespace nomad::test;

BOOST_TEST_DONT_PRINT_LOG_VALUE(nomad::CallableType)
BOOST_TEST_DONT_PRINT_LOG_VALUE(nomad::ResolveStatus)

static_assert(std::is_base_of_v<ScriptCallable<Function>, ScriptFunction>);
static_assert(std::is_base_of_v<ScriptCallableBase, ScriptFunction>);
static_assert(std::is_base_of_v<ScriptCallableBase, ScriptMethod>);
static_assert(std::is_base_of_v<ScriptCallableBase, ScriptConstructor>);
static_assert(!std::is_copy_constructible_v<ScriptFunction>);
static_assert(!std::is_copy_constructible_v<ScriptMethod>);
static_assert(!std::is_copy_constructible_v<ScriptConstructor>);
static_assert(std::is_same_v<decltype(std::declval<const Runtime&>().getScriptCallable(CallableId{})), const ScriptCallableBase*>);
static_assert(std::is_same_v<decltype(std::declval<Runtime&>().getScriptCallable(CallableId{})), ScriptCallableBase*>);
static_assert(std::is_same_v<decltype(std::declval<const Runtime&>().getFunction(CallableId{0})), const Function*>);
static_assert(std::is_same_v<decltype(std::declval<const Runtime&>().getMethod(CallableId{0})), const Method*>);
static_assert(std::is_same_v<decltype(std::declval<const Runtime&>().getConstructor(CallableId{0})), const Constructor*>);
static_assert(std::is_same_v<decltype(std::declval<const Runtime&>().getScriptFunction(CallableId{0})), const ScriptFunction*>);
static_assert(std::is_same_v<decltype(std::declval<Runtime&>().getScriptFunction(CallableId{0})), ScriptFunction*>);

namespace {

class ScriptCallableTestType final : public ObjectType {
public:
    ScriptCallableTestType(
        Runtime* runtime,
        const TypeId id,
        NomadString name,
        const ObjectTypeMode mode,
        ObjectType*& descriptor
    ):
        ObjectType(runtime, id, std::move(name), mode)
    {
        descriptor = this;
    }
};

CallableId registerVoidNativeFunction(
    Runtime& runtime,
    const NomadString& name,
    const std::vector<Parameter>& parameters = {}
) {
    return runtime.registerNativeFunction(
        name,
        [](VirtualMachine*) {},
        parameters,
        runtime.getVoidType(),
        NomadDoc("Test native function")
    );
}

CallableId registerNativeFunction(
    Runtime& runtime,
    const NomadString& name,
    const std::vector<Parameter>& parameters,
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

BOOST_AUTO_TEST_CASE(script_registration_binds_receiver_without_exposing_a_free_function)
{
    Runtime runtime;
    const auto typeId = runtime.createObjectType("ScriptOwner", ObjectTypeMode::Closed);
    BOOST_REQUIRE(typeId.isValid());
    const auto* owner = runtime.getTypeByName("ScriptOwner");
    const auto parameters = std::vector<Parameter>{defParameter("value", runtime.getIntegerType(), "")};
    const auto methodId = runtime.registerScriptMethod(typeId, "move", parameters, "owner.nomad", "");
    const auto constructorId = runtime.registerScriptConstructor(typeId, parameters, "owner.nomad", "");
    BOOST_REQUIRE(methodId.isValid());
    BOOST_REQUIRE(constructorId.isValid());
    for (const auto id : {methodId, constructorId}) {
        const auto* callable = runtime.getScriptCallable(id);
        BOOST_REQUIRE(callable != nullptr);
        BOOST_TEST(callable->getParameterCount() == 1u);
        BOOST_TEST(callable->getParameterId("this") == NOMAD_INVALID_ID);
        BOOST_TEST(callable->getVariableId("this").isInvalid());
        BOOST_TEST(callable->getObjectType() == owner);
        BOOST_TEST(runtime.getFunction(id) == nullptr);
    }
    BOOST_TEST(runtime.getFunctionId("move").isInvalid());
    BOOST_TEST(runtime.registerScriptMethod(typeId, "move", parameters, "owner.nomad", "").isInvalid());
    BOOST_TEST(runtime.registerScriptConstructor(typeId, parameters, "owner.nomad", "").isInvalid());
    BOOST_TEST(runtime.registerScriptMethod(runtime.getIntegerType()->getId(), "move", {}, "", "").isInvalid());
    BOOST_TEST(runtime.registerScriptConstructor(TypeId{}, {}, "", "").isInvalid());
    BOOST_TEST(runtime.registerScriptMethod(typeId, "bad",
        {defParameter("this", runtime.getIntegerType(), "")}, "", "").isInvalid());
    Runtime foreign;
    BOOST_TEST(runtime.registerScriptConstructor(typeId,
        {defParameter("value", foreign.getIntegerType(), "")}, "", "").isInvalid());
    BOOST_TEST(runtime.registerScriptMethod(typeId, "untyped",
        {defParameter("value", nullptr, "")}, "", "").isInvalid());
}

BOOST_AUTO_TEST_CASE(script_callable_lookup_preserves_function_and_native_categories)
{
    Runtime runtime;
    const auto scriptId = runtime.registerScriptFunction("script", "script.nomad", "return 1");
    const auto nativeId = registerVoidNativeFunction(runtime, "native");
    BOOST_REQUIRE(scriptId.isValid());
    BOOST_REQUIRE(nativeId.isValid());
    auto* script = runtime.getScriptFunction(scriptId);
    BOOST_TEST(runtime.getScriptCallable(scriptId) == static_cast<ScriptCallableBase*>(script));
    BOOST_TEST(std::as_const(runtime).getScriptCallable(scriptId) == static_cast<const ScriptCallableBase*>(script));
    BOOST_TEST(runtime.getCallable(scriptId)->asScriptCallable() == static_cast<const ScriptCallableBase*>(script));
    BOOST_TEST(runtime.getScriptCallable(nativeId) == nullptr);
    BOOST_TEST(runtime.getCallable(nativeId)->asScriptCallable() == nullptr);
    for (const auto id : {CallableId{}, CallableId{NOMAD_ID_MAX}}) {
        BOOST_TEST(runtime.getScriptCallable(id) == nullptr);
        BOOST_TEST(std::as_const(runtime).getScriptCallable(id) == nullptr);
    }
}

BOOST_AUTO_TEST_CASE(script_member_descriptors_are_type_owned_with_shared_compilation_state)
{
    Runtime runtime;
    ObjectType* type = nullptr;
    BOOST_REQUIRE(runtime.createObjectType<ScriptCallableTestType>(
        "Point", ObjectTypeMode::Closed, type
    ).isValid());
    BOOST_REQUIRE(type != nullptr);
    const auto parameters = std::vector<Parameter>{defParameter("value", runtime.getIntegerType(), "")};
    const auto methodId = type->registerMethod(runtime, std::make_unique<ScriptMethod>(
        "echo", type, parameters, nullptr, "point.nomad", "return value"
    ));
    const auto constructorId = type->registerConstructor(runtime, std::make_unique<ScriptConstructor>(
        type, parameters, "point.nomad", "this.value = value"
    ));
    BOOST_REQUIRE(methodId.isValid());
    BOOST_REQUIRE(constructorId.isValid());
    const auto* method = runtime.getMethod(methodId);
    const auto* constructor = runtime.getConstructor(constructorId);
    BOOST_REQUIRE(method != nullptr);
    BOOST_REQUIRE(constructor != nullptr);
    BOOST_TEST((method->getSource() == CallableSource::Script));
    BOOST_TEST((constructor->getSource() == CallableSource::Script));
    BOOST_TEST(method->getObjectType() == type);
    BOOST_TEST(constructor->getObjectType() == type);
    BOOST_TEST(constructor->getReturnType() == runtime.getVoidType());
    BOOST_TEST(constructor->getConstructedType() == type);
    BOOST_TEST(method->asNativeMethod() == nullptr);
    BOOST_TEST(constructor->asNativeConstructor() == nullptr);
    BOOST_REQUIRE(method->asScriptMethod() != nullptr);
    BOOST_REQUIRE(constructor->asScriptConstructor() != nullptr);
    BOOST_TEST(type->getMethods().front() == method);
    BOOST_TEST(type->getConstructors().front() == constructor);
    BOOST_TEST(runtime.getNativeConstructor(constructorId) == nullptr);
    BOOST_TEST(runtime.getNativeMethod(methodId) == nullptr);
    BOOST_TEST(runtime.getConstructors(type).empty());
    for (const auto* native : runtime.getConstructors()) {
        BOOST_REQUIRE(native != nullptr);
        BOOST_TEST(native->getId() != constructorId);
    }
    std::vector<ScriptFunction*> functions;
    runtime.getFunctions(functions);
    BOOST_TEST(functions.empty());

    for (const auto id : {methodId, constructorId}) {
        auto* script = runtime.getScriptCallable(id);
        BOOST_REQUIRE(script != nullptr);
        BOOST_TEST(script == runtime.getCallable(id)->asScriptCallable());
        BOOST_TEST(script == std::as_const(runtime).getScriptCallable(id));
        BOOST_TEST(script->getId() == id);
        BOOST_TEST(script->getObjectType() == type);
        BOOST_TEST(script->getPath() == "point.nomad");
        BOOST_TEST(!script->getSourceCode().empty());
        BOOST_TEST(script->getParameterCount() == 1u);
        BOOST_TEST(script->getParameterType(0) == runtime.getIntegerType());
        BOOST_TEST(script->getVariableId("value").isInvalid());
        BOOST_TEST(script->getParameterId("this") == NOMAD_INVALID_ID);
        BOOST_TEST(script->getVariableId("this").isInvalid());
        script->setHub("model");
        script->setSourceLineRange(3, 9);
        script->setFunctionStart(100);
        script->setFunctionEnd(120);
        BOOST_TEST(script->getHub() == "model");
        BOOST_TEST(script->getSourceStartLine() == 3u);
        BOOST_TEST(script->getSourceEndLine() == 9u);
        BOOST_TEST(script->getFunctionStart() == 100u);
        BOOST_TEST(script->getFunctionEnd() == 120u);
        BOOST_TEST(runtime.getFunction(id) == nullptr);
        BOOST_TEST(runtime.getScriptFunction(id) == nullptr);
    }

    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    auto* script = runtime.getScriptCallable(methodId);
    Tokenizer tokens(&context, "point.nomad", "return value");
    BOOST_REQUIRE(tokens.nextLine());
    auto statement = parser::parseLine(&context, script, &tokens);
    BOOST_REQUIRE(statement != nullptr);
    BOOST_REQUIRE(statement->resolve(&context, script));
    BOOST_TEST(!context.hasError());
    BOOST_TEST(script->getReturnType() == runtime.getIntegerType());
    BOOST_TEST(method->getReturnType() == runtime.getIntegerType());
    const ParserBlockScope scope(context, script, nullptr, true, true);
    BOOST_TEST(context.isFunctionRoot(script));
    {
        const auto* constructorScript = runtime.getScriptCallable(constructorId);
        const ParserBlockScope constructorScope(context, constructorScript);
        BOOST_TEST(!context.isFunctionRoot(constructorScript));
        BOOST_TEST(context.isFunctionRoot(script));
    }
    BOOST_TEST(context.isFunctionRoot(script));
}

BOOST_AUTO_TEST_CASE(unknown_name_resolves_to_invalid_callable)
{
    const Runtime runtime;

    const auto callable = runtime.getCallableId("missing");

    BOOST_TEST(callable.isInvalid());
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

    BOOST_REQUIRE(nativeFunctionId.isValid());

    const auto callable = runtime.getCallableId("test.greet");

    BOOST_REQUIRE(callable.isValid());
    BOOST_TEST((runtime.getFunction(callable)->asNativeFunction() != nullptr));
    BOOST_TEST(callable == nativeFunctionId);
    BOOST_TEST(runtime.getCallable(callable)->getName() == "test.greet");
    BOOST_TEST(runtime.getCallable(callable)->getParameterCount() == NomadIndex{1});
    BOOST_TEST(runtime.getCallable(callable)->getParameters()[0].name == "name");
    BOOST_TEST(runtime.getCallable(callable)->getParameters()[0].type == runtime.getStringType());
    BOOST_TEST(runtime.getCallable(callable)->getReturnType() == runtime.getVoidType());
}

BOOST_AUTO_TEST_CASE(function_is_registered_in_catalog)
{
    Runtime runtime;

    const auto functionId = runtime.registerScriptFunction("entities.player", "entities/player.nomad", "return 1");

    BOOST_REQUIRE(functionId.isValid());

    const auto callable = runtime.getCallableId("entities.player");

    BOOST_REQUIRE(callable.isValid());
    BOOST_TEST((runtime.getFunction(callable)->asScriptFunction() != nullptr));
    BOOST_TEST(callable == functionId);
    BOOST_TEST(runtime.getCallable(callable)->getName() == "entities.player");
    BOOST_TEST(runtime.getCallable(callable)->getParameterCount() == NomadIndex{0});
}

// Signature changes are visible through every view of the same runtime-owned callable.
BOOST_AUTO_TEST_CASE(function_signature_is_read_live)
{
    Runtime runtime;

    const auto functionId = runtime.registerScriptFunction("compute", "compute.nomad", "return 1");
    const auto callable = runtime.getCallableId("compute");

    BOOST_REQUIRE(callable.isValid());
    BOOST_TEST(runtime.getCallable(callable)->getReturnType() == nullptr);

    auto* function = runtime.getScriptFunction(functionId);
    BOOST_REQUIRE(function != nullptr);

    function->addParameter("value", runtime.getIntegerType(), "Value to compute");
    function->setReturnType(runtime.getFloatType());

    BOOST_TEST(runtime.getCallable(callable)->getParameterCount() == NomadIndex{1});
    BOOST_TEST(runtime.getCallable(callable)->getParameters()[0].name == "value");
    BOOST_TEST(runtime.getCallable(callable)->getParameters()[0].type == runtime.getIntegerType());
    BOOST_TEST(runtime.getCallable(callable)->getParameters()[0].doc == "Value to compute");
    BOOST_TEST(runtime.getCallable(callable)->getReturnType() == runtime.getFloatType());
}

BOOST_AUTO_TEST_CASE(script_function_joins_function_table)
{
    Runtime runtime;

    const auto nativeId = registerVoidNativeFunction(runtime, "test.native");
    const auto scriptId = runtime.registerScriptFunction("scripted", "scripted.nomad", "");

    BOOST_REQUIRE(scriptId.isValid());
    BOOST_TEST(scriptId != nativeId);

    const auto* function = runtime.getScriptFunction(scriptId);
    BOOST_REQUIRE(function != nullptr);
    BOOST_TEST(function->getId() == scriptId);
    BOOST_TEST(function->getName() == "scripted");
    BOOST_TEST((function->getSource() == CallableSource::Script));
    BOOST_TEST((function->getType() == CallableType::Function));
    BOOST_TEST(runtime.getCallable(scriptId) == static_cast<const Callable*>(function));
    BOOST_TEST(runtime.getFunction(scriptId) == static_cast<const Function*>(function));
    BOOST_TEST(runtime.getScriptFunction(nativeId) == nullptr);
    BOOST_TEST(runtime.getNativeFunction(scriptId) == nullptr);
    BOOST_TEST(runtime.getFunction(nativeId)->asNativeFunction() == runtime.getNativeFunction(nativeId));
    BOOST_TEST(runtime.getFunction(nativeId)->asScriptFunction() == nullptr);
    BOOST_TEST(runtime.getFunction(scriptId)->asNativeFunction() == nullptr);
    BOOST_TEST(runtime.getFunction(scriptId)->asScriptFunction() == std::as_const(runtime).getScriptFunction(scriptId));
    BOOST_TEST(runtime.getScriptFunction(scriptId)->asScriptFunction() == runtime.getScriptFunction(scriptId));

    runtime.getScriptFunction(scriptId)->addParameter("value", runtime.getIntegerType(), "");

    BOOST_REQUIRE(function->getParameters().size() == NomadIndex{1});
    BOOST_TEST(function->getParameters()[0].name == "value");
    BOOST_TEST(function->getParameters()[0].type == runtime.getIntegerType());
    BOOST_TEST(function->getReturnType() == nullptr);

    BOOST_TEST(runtime.setScriptFunctionReturnType(scriptId, runtime.getFloatType()));
    BOOST_TEST(function->getReturnType() == runtime.getFloatType());
    BOOST_TEST(!runtime.setScriptFunctionReturnType(nativeId, runtime.getFloatType()));

    const auto resolved = runtime.resolveFunction("scripted", {runtime.getIntegerType()});
    BOOST_TEST((resolved.status == ResolveStatus::Resolved));
    BOOST_TEST(resolved.id == scriptId);
}

BOOST_AUTO_TEST_CASE(catalog_separates_native_functions_from_functions)
{
    Runtime runtime;

    registerVoidNativeFunction(runtime, "native.only");
    runtime.registerScriptFunction("function.only", "function.nomad", "return 1");

    BOOST_TEST(runtime.getNativeFunctionId("native.only").isValid());
    BOOST_TEST(runtime.getFunctionId("native.only").isInvalid());

    BOOST_TEST(runtime.getFunctionId("function.only").isValid());
    BOOST_TEST(runtime.getNativeFunctionId("function.only").isInvalid());
}

BOOST_AUTO_TEST_CASE(typed_function_views_reject_invalid_ids_and_wrong_sources)
{
    Runtime runtime;
    const auto nativeId = registerVoidNativeFunction(runtime, "native");
    const auto scriptId = runtime.registerScriptFunction("script", "script.nomad", "return 1");
    const auto& readOnlyRuntime = std::as_const(runtime);

    BOOST_TEST(runtime.getFunction(nativeId) == static_cast<const Function*>(runtime.getNativeFunction(nativeId)));
    BOOST_TEST(runtime.getFunction(scriptId) == static_cast<const Function*>(runtime.getScriptFunction(scriptId)));
    BOOST_TEST(readOnlyRuntime.getScriptFunction(scriptId) == runtime.getScriptFunction(scriptId));
    BOOST_TEST(readOnlyRuntime.getScriptFunction(nativeId) == nullptr);
    BOOST_TEST(runtime.getScriptFunction(nativeId) == nullptr);
    BOOST_TEST(runtime.getNativeFunction(scriptId) == nullptr);

    for (const auto id : {NOMAD_INVALID_ID, NomadId{-2}, toNomadId(runtime.getFunctionTableSize())}) {
        BOOST_TEST(runtime.getFunction(CallableId{id}) == nullptr);
        BOOST_TEST(runtime.getScriptFunction(CallableId{id}) == nullptr);
        BOOST_TEST(readOnlyRuntime.getScriptFunction(CallableId{id}) == nullptr);
        BOOST_TEST(runtime.getNativeFunction(CallableId{id}) == nullptr);
        BOOST_TEST(runtime.getMethod(CallableId{id}) == nullptr);
        BOOST_TEST(runtime.getNativeMethod(CallableId{id}) == nullptr);
        BOOST_TEST(runtime.getConstructor(CallableId{id}) == nullptr);
        BOOST_TEST(runtime.getNativeConstructor(CallableId{id}) == nullptr);
    }
}

BOOST_AUTO_TEST_CASE(script_function_views_and_state_survive_table_growth)
{
    Runtime runtime;
    const auto scriptId = runtime.registerScriptFunction("script", "source.nomad", "return value");
    auto* script = runtime.getScriptFunction(scriptId);
    BOOST_REQUIRE(script != nullptr);
    const auto* function = runtime.getFunction(scriptId);
    const auto* callable = runtime.getCallable(scriptId);

    script->addParameter("value", runtime.getIntegerType(), "");
    script->setReturnType(runtime.getIntegerType());
    script->registerVariable("local", runtime.getFloatType());
    script->setSourceLineRange(2, 4);
    script->setFunctionStart(10);
    script->setFunctionEnd(20);
    const auto initialTableSize = runtime.getFunctionTableSize();

    for (NomadIndex index = 0; index < 64; ++index) {
        BOOST_REQUIRE(registerVoidNativeFunction(runtime, "native" + std::to_string(index)).isValid());
    }

    BOOST_TEST(runtime.getFunctionTableSize() == initialTableSize + 64);
    BOOST_TEST(runtime.getScriptFunction(scriptId) == script);
    BOOST_TEST(runtime.getFunction(scriptId) == function);
    BOOST_TEST(runtime.getCallable(scriptId) == callable);
    BOOST_TEST(function->asScriptFunction() == script);
    BOOST_TEST(function->asNativeFunction() == nullptr);
    BOOST_TEST(&callable->getParameters() == &script->getParameters());
    BOOST_TEST(function->getReturnType() == runtime.getIntegerType());
    BOOST_TEST(script->getPath() == "source.nomad");
    BOOST_TEST(script->getSourceCode() == "return value");
    BOOST_TEST(script->getVariableCount() == 1U);
    BOOST_TEST(script->getVariableType(script->getVariableId("local")) == runtime.getFloatType());
    BOOST_TEST(script->getVariableId("value").isInvalid());
    BOOST_TEST(script->getParameterType(script->getParameterId("value")) == runtime.getIntegerType());
    BOOST_TEST(runtime.getFunctionCount() == 1U);
    BOOST_TEST(runtime.getFunctionSize() == 10U);
    BOOST_TEST(runtime.getFunctionAtLine("source.nomad", 3) == script);
    BOOST_TEST(runtime.getFunctionByStartAddress(10) == script);
    BOOST_TEST(runtime.getFunctionAtInstructionIndex(19) == script);
    BOOST_TEST(runtime.getFunctionAtInstructionIndex(20) == nullptr);

    std::vector<ScriptFunction*> scripts;
    runtime.getFunctions(scripts);
    BOOST_REQUIRE(scripts.size() == 1U);
    BOOST_TEST(scripts.front() == script);
    TempVector<ScriptFunction*> temporaryScripts;
    runtime.getFunctions(temporaryScripts);
    BOOST_REQUIRE(temporaryScripts.size() == 1U);
    BOOST_TEST(temporaryScripts.front() == script);
}

BOOST_AUTO_TEST_CASE(duplicate_native_function_name_is_rejected)
{
    Runtime runtime;

    BOOST_REQUIRE(registerVoidNativeFunction(runtime, "duplicate").isValid());

    BOOST_TEST(registerVoidNativeFunction(runtime, "duplicate").isInvalid());
    BOOST_TEST(runtime.getCallableOverloadCount("duplicate") == NomadIndex{1});
}

BOOST_AUTO_TEST_CASE(duplicate_function_name_is_rejected)
{
    Runtime runtime;

    BOOST_REQUIRE(runtime.registerScriptFunction("duplicate", "duplicate.nomad", "return 1").isValid());

    BOOST_TEST(runtime.registerScriptFunction("duplicate", "other.nomad", "return 2").isInvalid());
    BOOST_TEST(runtime.getCallableOverloadCount("duplicate") == NomadIndex{1});
}

// A function may not shadow a native function, and vice versa: one name means one callable.
BOOST_AUTO_TEST_CASE(function_cannot_shadow_native_function)
{
    Runtime runtime;
    registerBuildInNativeFunctions(&runtime);

    BOOST_REQUIRE(runtime.getNativeFunctionId("log.info").isValid());

    BOOST_TEST(runtime.registerScriptFunction("log.info", "log/info.nomad", "return 1").isInvalid());

    const auto callable = runtime.getCallableId("log.info");

    BOOST_REQUIRE(callable.isValid());
    BOOST_TEST((runtime.getFunction(callable)->asNativeFunction() != nullptr));
}

BOOST_AUTO_TEST_CASE(native_function_cannot_shadow_function)
{
    Runtime runtime;

    BOOST_REQUIRE(runtime.registerScriptFunction("entities.player", "entities/player.nomad", "return 1").isValid());

    BOOST_TEST(registerVoidNativeFunction(runtime, "entities.player").isInvalid());

    const auto callable = runtime.getCallableId("entities.player");

    BOOST_REQUIRE(callable.isValid());
    BOOST_TEST((runtime.getFunction(callable)->asScriptFunction() != nullptr));
}

BOOST_AUTO_TEST_CASE(overloads_are_reported_for_a_registered_name)
{
    Runtime runtime;

    const auto nativeFunctionId = registerVoidNativeFunction(runtime, "test.single");

    std::vector<CallableId> overloads;
    runtime.getCallableOverloads("test.single", overloads);

    BOOST_REQUIRE(overloads.size() == 1);
    BOOST_TEST(overloads[0] == nativeFunctionId);
    BOOST_TEST((runtime.getFunction(overloads[0])->asNativeFunction() != nullptr));

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

    BOOST_REQUIRE(fromFloat.isValid());
    BOOST_REQUIRE(fromString.isValid());
    BOOST_TEST(runtime.getCallableOverloadCount("test.convert") == NomadIndex{2});


    const auto selectedFloat = runtime.resolveFunction("test.convert", {runtime.getFloatType()});
    BOOST_REQUIRE(selectedFloat.isResolved());
    BOOST_TEST(selectedFloat.id == fromFloat);

    const auto selectedString = runtime.resolveFunction("test.convert", {runtime.getStringType()});
    BOOST_REQUIRE(selectedString.isResolved());
    BOOST_TEST(selectedString.id == fromString);
}

// A `string` overload and an overload of another type are both candidates; the exact one has to win.
BOOST_AUTO_TEST_CASE(overload_selection_prefers_an_exact_match)
{
    Runtime runtime;

    registerNativeFunction(
        runtime,
        "test.pick",
        {defParameter("text", runtime.getIntegerType(), NomadDoc("Text"))},
        runtime.getIntegerType()
    );
    const auto takesString = registerNativeFunction(
        runtime,
        "test.pick",
        {defParameter("text", runtime.getStringType(), NomadDoc("Text"))},
        runtime.getIntegerType()
    );

    BOOST_REQUIRE(takesString.isValid());

    const auto selected = runtime.resolveFunction("test.pick", {runtime.getStringType()});

    BOOST_REQUIRE(selected.isResolved());
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

    const auto selected = runtime.resolveFunction("test.convert", {runtime.getBooleanType()});

    BOOST_TEST(!selected.isResolved());
    BOOST_TEST(!runtime.describeResolveFailure("test.convert", selected).empty());
}

BOOST_AUTO_TEST_CASE(overloads_can_have_different_parameter_counts)
{
    Runtime runtime;

    BOOST_REQUIRE(
        registerNativeFunction(
            runtime,
            "test.convert",
            {defParameter("value", runtime.getFloatType(), NomadDoc("Value"))},
            runtime.getIntegerType()
        ).isValid()
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
        ).isValid()
    );

    BOOST_TEST(runtime.getCallableOverloadCount("test.convert") == NomadIndex{2});
}

BOOST_AUTO_TEST_CASE(different_arity_overloads_reject_source_parameters_in_either_registration_order)
{
    for (const auto sourceFirst : {false, true}) {
        Runtime runtime;
        const std::vector<Parameter> sourceParameters{
            defParameter("$file", runtime.getFileNameType(), ""),
            defParameter("value", runtime.getIntegerType(), "")
        };
        const std::vector<Parameter> valueParameters{
            defParameter("value", runtime.getIntegerType(), "")
        };
        const auto& first = sourceFirst ? sourceParameters : valueParameters;
        const auto& second = sourceFirst ? valueParameters : sourceParameters;
        BOOST_REQUIRE(registerNativeFunction(runtime, "test.source", first, runtime.getIntegerType()).isValid());
        BOOST_TEST(registerNativeFunction(runtime, "test.source", second, runtime.getIntegerType()).isInvalid());
        BOOST_TEST(runtime.getCallableOverloadCount("test.source") == NomadIndex{1});
    }
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
        ).isValid()
    );

    BOOST_TEST(
        registerVoidNativeFunction(
            runtime,
            "test.convert",
            {defParameter("value", runtime.getStringType(), NomadDoc("Value"))}
        ).isInvalid()
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
        ).isValid()
    );

    BOOST_TEST(
        registerVoidNativeFunction(
            runtime,
            "test.run",
            {defParameter("condition", runtime.getBooleanType(), NomadDoc("Condition"))}
        ).isInvalid()
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

BOOST_AUTO_TEST_CASE(native_and_script_calls_share_expression_and_statement_dispatch)
{
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    runtime.registerNativeFunction("test.pick", [](VirtualMachine* machine) {
        machine->setIntegerResult(10);
    }, {defParameter("value", runtime.getIntegerType(), "")}, runtime.getIntegerType(), "");
    runtime.registerNativeFunction("test.pick", [](VirtualMachine* machine) {
        machine->setIntegerResult(20);
    }, {defParameter("value", runtime.getStringType(), "")}, runtime.getIntegerType(), "");
    fixture.addFunction("answer", "return test.pick \"text\"");
    fixture.addFunction("main", "test.pick 1\nanswer\nreturn (test.pick 1) + answer");

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto outcome = fixture.execute("main");
    BOOST_TEST(!outcome.fault.has_value());
    BOOST_TEST(outcome.integerValue == 30);
    BOOST_TEST(outcome.leakedStrings == 0);
}

BOOST_AUTO_TEST_CASE(different_arity_calls_use_the_longest_signature_regardless_of_registration_order)
{
    for (const auto longestFirst : {false, true}) {
        LanguageTestFixture fixture;
        auto& runtime = fixture.getRuntime();
        for (const auto count : (longestFirst ? std::vector<int>{2, 1, 0} : std::vector<int>{0, 1, 2})) {
            std::vector<Parameter> parameters;
            for (auto index = 0; index < count; ++index) {
                parameters.push_back(defParameter("value" + std::to_string(index), runtime.getIntegerType(), ""));
            }
            BOOST_REQUIRE(runtime.registerNativeFunction(
                "test.arity",
                [count](VirtualMachine* machine) { machine->setIntegerResult(count); },
                parameters, runtime.getIntegerType(), ""
            ).isValid());
        }
        fixture.addFunction("main",
            "test.arity\n"
            "test.arity 1\n"
            "test.arity 1 2\n"
            "return (test.arity) + (test.arity 1) + (test.arity 1 2)");
        BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
        const auto result = fixture.execute("main");
        BOOST_TEST(!result.fault.has_value());
        BOOST_TEST(result.integerValue == 3);
    }
}

BOOST_AUTO_TEST_SUITE_END()

///////////////////////////////////////////////////////////////////////////////
/// Function table and resolve*
///////////////////////////////////////////////////////////////////////////////
BOOST_AUTO_TEST_SUITE(function_resolution)

namespace {

class ResolutionObject final : public NomadObject {
};

const ObjectType* registerResolutionType(Runtime& runtime, const NomadString& name) {
    const auto descriptorId = runtime.createObjectType<ObjectType>(name, ObjectTypeMode::Closed, std::vector<const Type*>{}, "A test object."
    );
    BOOST_REQUIRE(descriptorId.isValid());
    const auto* type = runtime.getType(descriptorId).value()->asObjectType();
    return type;
}

CallableId registerResolutionConstructor(
    Runtime& runtime,
    const ObjectType* type,
    const std::vector<ParameterDefinition>& parameters
) {
    return runtime.addConstructor(type->getId(), {
        .parameters = parameters,
        .function = [](VirtualMachine* machine) { machine->getResult().setObjectValue(new ResolutionObject); },
        .documentation = ""
    });
}

} // namespace

BOOST_AUTO_TEST_CASE(registered_callables_share_one_id_space)
{
    Runtime runtime;
    const auto* type = registerResolutionType(runtime, "Thing");

    const auto function = registerNativeFunction(runtime, "test.one", {}, runtime.getIntegerType());
    const auto method = runtime.addMethod(type->getId(), {
        .name = "size",
        .parameters = {},
        .returnTypeId = runtime.getTypeId("int"),
        .function = [](VirtualMachine*) {},
        .documentation = ""
    });
    const auto constructor = registerResolutionConstructor(runtime, type, {});

    BOOST_REQUIRE(function.isValid() && method.isValid() && constructor.isValid());
    BOOST_REQUIRE(runtime.getFunctionTableSize() >= NomadIndex{3});

    const auto functionResult = runtime.resolveFunction("test.one", {});
    const auto methodResult = runtime.resolveMethod(type, "size", {});
    const auto constructorResult = runtime.resolveConstructor(type, {});

    BOOST_REQUIRE(functionResult.isResolved() && methodResult.isResolved() && constructorResult.isResolved());
    BOOST_TEST(functionResult.id != methodResult.id);
    BOOST_TEST(methodResult.id != constructorResult.id);
    BOOST_TEST(functionResult.id != constructorResult.id);

    BOOST_TEST(functionResult.id == function);
    BOOST_TEST(constructorResult.id == constructor);
    BOOST_TEST(runtime.getCallable(methodResult.id)->getObjectType() == type);
    BOOST_TEST(runtime.getNativeFunction(functionResult.id) != nullptr);
    BOOST_TEST(runtime.getNativeFunction(methodResult.id) == nullptr);
    BOOST_TEST(runtime.getNativeMemberMethod(method)->getId() == methodResult.id);
    BOOST_TEST(runtime.getNativeFunction(constructorResult.id) == nullptr);
    BOOST_TEST(runtime.getNativeConstructor(constructorResult.id) != nullptr);
    BOOST_TEST(runtime.getNativeConstructor(functionResult.id) == nullptr);
    BOOST_TEST(runtime.getFunction(functionResult.id) != nullptr);
    BOOST_TEST(runtime.getFunction(methodResult.id) == nullptr);
    BOOST_TEST(runtime.getFunction(constructorResult.id) == nullptr);

    BOOST_TEST(runtime.getCallable(functionResult.id)->getType() == CallableType::Function);
    BOOST_TEST(runtime.getCallable(methodResult.id)->getType() == CallableType::Method);
    BOOST_TEST(runtime.getCallable(constructorResult.id)->getType() == CallableType::Constructor);
    BOOST_TEST(runtime.getCallable(functionResult.id) == runtime.getNativeFunction(functionResult.id));
    BOOST_TEST(runtime.getCallable(methodResult.id) == runtime.getNativeMethod(methodResult.id));
    BOOST_TEST(runtime.getCallable(CallableId::INVALID) == nullptr);
}

BOOST_AUTO_TEST_CASE(constructors_belong_to_the_declaring_type)
{
    Runtime runtime;
    const auto* declaringType = registerResolutionType(runtime, "ConstructorOwner");
    const auto* createdType = registerResolutionType(runtime, "ConstructorResult");
    const auto id = runtime.addConstructor(declaringType->getId(), createdType->getId(), {
        .parameters = {},
        .function = [](VirtualMachine* machine) { machine->getResult().setObjectValue(new ResolutionObject); },
        .documentation = ""
    });
    BOOST_REQUIRE(id.isValid());
    const auto constructors = declaringType->getConstructors();
    BOOST_REQUIRE(constructors.size() == 1);
    BOOST_TEST(constructors.front() == runtime.getConstructor(id));
    BOOST_TEST(constructors.front()->getReturnType() == createdType);
    BOOST_TEST(createdType->getConstructors().empty());

    const auto duplicate = registerResolutionConstructor(runtime, declaringType, {});
    BOOST_TEST(duplicate.isInvalid());
    BOOST_TEST(declaringType->getConstructors().size() == 1);

    Runtime otherRuntime;
    const auto otherCallableCount = otherRuntime.getFunctionTableSize();
    BOOST_TEST(otherRuntime.addConstructor(otherRuntime.getTypeId(declaringType->getTypeName()), {
        .parameters = {},
        .function = [](VirtualMachine* machine) { machine->getResult().setObjectValue(new ResolutionObject); },
        .documentation = ""
    }).isInvalid());
    BOOST_TEST(otherRuntime.getFunctionTableSize() == otherCallableCount);
    BOOST_TEST(declaringType->getConstructors().size() == 1);

    for (auto index = 0; index < 32; ++index) {
        BOOST_REQUIRE(registerVoidNativeFunction(runtime, "extra" + std::to_string(index)).isValid());
    }
    BOOST_TEST(runtime.getConstructor(id) == constructors.front());
    BOOST_TEST(runtime.resolveConstructor(declaringType, {}).id == id);
}

BOOST_AUTO_TEST_CASE(type_owned_constructor_factory_is_released_once)
{
    auto destructions = 0;
    auto aliveAtTypeDestruction = false;
    {
        class ConstructorOwner final : public ObjectType {
        public:
            ConstructorOwner(
                Runtime* runtime,
                TypeId id,
                NomadString name,
                ObjectTypeMode mode,
                int& destructions,
                bool& alive
            ):
                ObjectType(runtime, id, std::move(name), mode),
                m_destructions(destructions),
                m_alive(alive)
            {
            }

            ~ConstructorOwner() override {
                m_alive = m_destructions == 0 && getConstructors().size() == 1;
            }

        private:
            int& m_destructions;
            bool& m_alive;
        };

        Runtime runtime;
        const auto ownerId = runtime.createObjectType<ConstructorOwner>(
            "LifetimeOwner", ObjectTypeMode::Closed, destructions, aliveAtTypeDestruction
        );
        BOOST_REQUIRE(ownerId.isValid());
        const auto* type = runtime.getType(ownerId).value()->asObjectType();
        auto lifetime = std::shared_ptr<int>(new int{0}, [&destructions](int* value) {
            ++destructions;
            delete value;
        });
        const auto id = runtime.addConstructor(type->getId(), {
            .parameters = {},
            .function = [lifetime = std::move(lifetime)](VirtualMachine* machine) {
                machine->getResult().setObjectValue(new ResolutionObject);
            },
            .documentation = ""
        });
        BOOST_REQUIRE(id.isValid());
        BOOST_TEST(destructions == 0);
    }
    BOOST_TEST(aliveAtTypeDestruction);
    BOOST_TEST(destructions == 1);
}

BOOST_AUTO_TEST_CASE(methods_belong_to_the_receiver_type)
{
    Runtime runtime;
    const auto* type = registerResolutionType(runtime, "MethodOwner");
    const auto* otherType = registerResolutionType(runtime, "OtherMethodOwner");
    const auto memberId = runtime.addMethod(type->getId(), {
        .name = "update",
        .parameters = {{runtime.getTypeId("int"), "value", ""}},
        .returnTypeId = runtime.getTypeId("void"),
        .function = [](VirtualMachine*) {},
        .documentation = ""
    });
    BOOST_REQUIRE(memberId.isValid());
    const auto methods = type->getMethods();
    BOOST_REQUIRE(methods.size() == 1);
    BOOST_TEST(methods.front() == runtime.getNativeMemberMethod(memberId));
    const auto callableId = methods.front()->getId();
    BOOST_TEST(methods.front() == runtime.getMethod(callableId));
    BOOST_TEST(otherType->getMethods().empty());

    const auto overloadId = runtime.addMethod(otherType->getId(), {
        .name = "update",
        .parameters = {{runtime.getTypeId("float"), "value", ""}},
        .returnTypeId = runtime.getTypeId("void"),
        .function = [](VirtualMachine*) {},
        .documentation = ""
    });
    BOOST_REQUIRE(overloadId.isValid());
    BOOST_TEST(type->getMethods().size() == 1);
    BOOST_TEST(otherType->getMethods().size() == 1);
    BOOST_TEST(runtime.resolveMethod(type, "update", {runtime.getIntegerType()}).id == callableId);
    BOOST_TEST(runtime.resolveMethod(otherType, "update", {runtime.getFloatType()}).id ==
        runtime.getNativeMemberMethod(overloadId)->getId());
    BOOST_TEST(runtime.getCallableId("update").isInvalid());

    BOOST_TEST(runtime.addMethod(type->getId(), {
        .name = "update",
        .parameters = {{runtime.getTypeId("int"), "value", ""}},
        .returnTypeId = runtime.getTypeId("void"),
        .function = [](VirtualMachine*) {},
        .documentation = ""
    }).isInvalid());
    BOOST_TEST(type->getMethods().size() == 1);

    Runtime otherRuntime;
    const auto callableCount = otherRuntime.getFunctionTableSize();
    BOOST_TEST(otherRuntime.addMethod(otherRuntime.getTypeId(type->getTypeName()), {
        .name = "update",
        .parameters = {},
        .returnTypeId = otherRuntime.getTypeId("void"),
        .function = [](VirtualMachine*) {},
        .documentation = ""
    }).isInvalid());
    BOOST_TEST(otherRuntime.getFunctionTableSize() == callableCount);
    BOOST_TEST(type->getMethods().size() == 1);

    for (auto index = 0; index < 32; ++index) {
        BOOST_REQUIRE(runtime.addMethod(type->getId(), {
            .name = "extra" + std::to_string(index),
            .parameters = {},
            .returnTypeId = runtime.getTypeId("void"),
            .function = [](VirtualMachine*) {},
            .documentation = ""
        }).isValid());
    }
    BOOST_TEST(runtime.getMethod(callableId) == methods.front());
    BOOST_TEST(runtime.getNativeMemberMethod(memberId) == methods.front());
}

BOOST_AUTO_TEST_CASE(type_owned_method_callback_is_released_once)
{
    auto destructions = 0;
    auto aliveAtTypeDestruction = false;
    {
        class MethodOwner final : public ObjectType {
        public:
            MethodOwner(
                Runtime* runtime,
                TypeId id,
                NomadString name,
                ObjectTypeMode mode,
                int& destructions,
                bool& alive
            ):
                ObjectType(runtime, id, std::move(name), mode),
                m_destructions(destructions),
                m_alive(alive)
            {
            }

            ~MethodOwner() override {
                m_alive = m_destructions == 0 && getMethods().size() == 1;
            }

        private:
            int& m_destructions;
            bool& m_alive;
        };

        Runtime runtime;
        const auto ownerId = runtime.createObjectType<MethodOwner>(
            "MethodLifetimeOwner", ObjectTypeMode::Closed, destructions, aliveAtTypeDestruction
        );
        BOOST_REQUIRE(ownerId.isValid());
        const auto* type = runtime.getType(ownerId).value()->asObjectType();
        auto lifetime = std::shared_ptr<int>(new int{0}, [&destructions](int* value) {
            ++destructions;
            delete value;
        });
        const auto memberId = runtime.addMethod(type->getId(), {
            .name = "update",
            .parameters = {},
            .returnTypeId = runtime.getTypeId("void"),
            .function = [lifetime = std::move(lifetime)](VirtualMachine*) {},
            .documentation = ""
        });
        BOOST_REQUIRE(memberId.isValid());
        BOOST_TEST(destructions == 0);
    }
    BOOST_TEST(aliveAtTypeDestruction);
    BOOST_TEST(destructions == 1);
}

BOOST_AUTO_TEST_CASE(category_getters_and_conversions_use_resolved_callable_ids)
{
    Runtime runtime;
    const auto* type = registerResolutionType(runtime, "Typed");
    const auto nativeId = registerNativeFunction(runtime, "native", {}, runtime.getIntegerType());
    const auto scriptId = runtime.registerScriptFunction("script", "script.nomad", "");
    const auto memberId = runtime.addMethod(type->getId(), {
        .name = "size",
        .parameters = {},
        .returnTypeId = runtime.getTypeId("int"),
        .function = [](VirtualMachine*) {},
        .documentation = ""
    });
    const auto constructorId = registerResolutionConstructor(runtime, type, {});
    const auto methodResult = runtime.resolveMethod(type, "size", {});
    const auto constructorResult = runtime.resolveConstructor(type, {});
    BOOST_REQUIRE(methodResult.isResolved());
    BOOST_REQUIRE(constructorResult.isResolved());
    BOOST_TEST(constructorResult.id == constructorId);

    const auto* method = runtime.getMethod(methodResult.id);
    const auto* constructor = runtime.getConstructor(constructorResult.id);
    BOOST_REQUIRE(method != nullptr);
    BOOST_REQUIRE(constructor != nullptr);
    BOOST_TEST(method->asNativeMethod() == runtime.getNativeMemberMethod(memberId));
    BOOST_TEST(method->asNativeMethod() == runtime.getNativeMethod(methodResult.id));
    BOOST_TEST(method->asScriptMethod() == nullptr);
    BOOST_TEST(constructor->asNativeConstructor() == runtime.getNativeConstructor(constructorId));
    BOOST_TEST(constructor->asScriptConstructor() == nullptr);

    for (const auto id : {nativeId, scriptId}) {
        BOOST_TEST(runtime.getMethod(id) == nullptr);
        BOOST_TEST(runtime.getNativeMethod(id) == nullptr);
        BOOST_TEST(runtime.getConstructor(id) == nullptr);
        BOOST_TEST(runtime.getNativeConstructor(id) == nullptr);
    }
    BOOST_TEST(runtime.getMethod(constructorId) == nullptr);
    BOOST_TEST(runtime.getNativeMethod(constructorId) == nullptr);
    BOOST_TEST(runtime.getConstructor(methodResult.id) == nullptr);
    BOOST_TEST(runtime.getNativeConstructor(methodResult.id) == nullptr);
    BOOST_TEST(runtime.getScriptFunction(methodResult.id) == nullptr);
    BOOST_TEST(runtime.getScriptFunction(constructorId) == nullptr);
    BOOST_TEST(std::as_const(runtime).getScriptFunction(methodResult.id) == nullptr);
    BOOST_TEST(std::as_const(runtime).getScriptFunction(constructorId) == nullptr);
}

BOOST_AUTO_TEST_CASE(member_method_lookup_distinguishes_member_and_callable_ids)
{
    Runtime runtime;
    const auto* type = registerResolutionType(runtime, "MemberIds");
    // Additional free functions shift callable IDs without adding native member entries.
    for (NomadIndex index = 0; index < 32; ++index) {
        BOOST_REQUIRE(registerVoidNativeFunction(runtime, "padding" + std::to_string(index)).isValid());
    }
    const auto propertyId = runtime.addProperty(type->getId(), {
        .valueTypeId = runtime.getTypeId("int"),
        .name = "value",
        .getter = [](VirtualMachine*, RuntimeValue&) {},
        .setter = {},
        .documentation = ""
    });
    const auto memberId = runtime.addMethod(type->getId(), {
        .name = "size",
        .parameters = {},
        .returnTypeId = runtime.getTypeId("int"),
        .function = [](VirtualMachine*) {},
        .documentation = ""
    });
    BOOST_REQUIRE(propertyId.isValid());
    BOOST_REQUIRE(memberId.isValid());
    const auto* method = runtime.getNativeMemberMethod(memberId);
    BOOST_REQUIRE(method != nullptr);
    BOOST_TEST(method->getId() == memberId);
    BOOST_TEST(runtime.getNativeMethod(method->getId()) == method);
    BOOST_TEST(runtime.getMethod(method->getId()) == static_cast<const Method*>(method));
    BOOST_TEST(runtime.getNativeMemberMethod(propertyId) == nullptr);
    BOOST_TEST(runtime.getNativeMemberMethod(CallableId::INVALID) == nullptr);
    BOOST_TEST(runtime.getNativeMemberMethod(CallableId{-2}) == nullptr);
    BOOST_TEST(runtime.getNativeMemberMethod(CallableId{toNomadId(runtime.getFunctionTableSize())}) == nullptr);
}

BOOST_AUTO_TEST_CASE(resolve_function_reports_each_failure_status)
{
    Runtime runtime;

    registerNativeFunction(
        runtime, "test.convert", {defParameter("value", runtime.getFloatType(), "")}, runtime.getIntegerType()
    );

    BOOST_TEST(runtime.resolveFunction("test.missing", {}).status == ResolveStatus::UnknownName);
    BOOST_TEST(runtime.resolveFunction("test.convert", {}).status == ResolveStatus::NoMatchingArity);
    BOOST_TEST(
        runtime.resolveFunction("test.convert", {runtime.getBooleanType()}).status == ResolveStatus::NoMatchingTypes
    );

    const auto resolved = runtime.resolveFunction("test.convert", {runtime.getFloatType()});
    BOOST_TEST(resolved.status == ResolveStatus::Resolved);
    BOOST_TEST(resolved.candidates.size() == 1u);
    BOOST_TEST(runtime.describeResolveFailure("test.convert", resolved).empty());
}

BOOST_AUTO_TEST_CASE(resolve_function_picks_between_overloads)
{
    Runtime runtime;

    const auto fromFloat = registerNativeFunction(
        runtime, "test.convert", {defParameter("value", runtime.getFloatType(), "")}, runtime.getIntegerType()
    );
    const auto fromString = registerNativeFunction(
        runtime, "test.convert", {defParameter("value", runtime.getStringType(), "")}, runtime.getIntegerType()
    );

    const auto floatResult = runtime.resolveFunction("test.convert", {runtime.getFloatType()});
    const auto stringResult = runtime.resolveFunction("test.convert", {runtime.getStringType()});

    BOOST_REQUIRE(floatResult.isResolved() && stringResult.isResolved());
    BOOST_TEST(floatResult.id == fromFloat);
    BOOST_TEST(stringResult.id == fromString);

    // A compiler-supplied argument cannot tell the overloads apart.
    const auto wildcard = runtime.resolveFunction("test.convert", {nullptr});
    BOOST_TEST(wildcard.status == ResolveStatus::Ambiguous);
    BOOST_TEST(wildcard.candidates.size() == 2u);
    BOOST_TEST(runtime.describeResolveFailure("test.convert", wildcard).find("Ambiguous") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(resolve_method_is_scoped_to_the_object_type)
{
    Runtime runtime;
    const auto* first = registerResolutionType(runtime, "First");
    const auto* second = registerResolutionType(runtime, "Second");

    BOOST_REQUIRE(
        runtime.addMethod(first->getId(), {
            .name = "size",
            .parameters = {},
            .returnTypeId = runtime.getTypeId("int"),
            .function = [](VirtualMachine*) {},
            .documentation = ""
        }).isValid()
    );

    BOOST_TEST(runtime.resolveMethod(first, "size", {}).isResolved());
    BOOST_TEST(runtime.resolveMethod(second, "size", {}).status == ResolveStatus::UnknownName);
    BOOST_TEST(runtime.resolveMethod(first, "size", {runtime.getIntegerType()}).status == ResolveStatus::NoMatchingArity);
    // A method is not a free function.
    BOOST_TEST(runtime.resolveFunction("size", {}).status == ResolveStatus::UnknownName);
}

BOOST_AUTO_TEST_CASE(resolve_constructor_selects_by_argument_types)
{
    Runtime runtime;
    const auto* type = registerResolutionType(runtime, "Thing");

    const auto empty = registerResolutionConstructor(runtime, type, {});
    const auto fromInteger = registerResolutionConstructor(
        runtime, type, {{runtime.getIntegerType()->getId(), "value", ""}}
    );
    const auto fromPair = registerResolutionConstructor(
        runtime, type,
        {{runtime.getIntegerType()->getId(), "x", ""}, {runtime.getIntegerType()->getId(), "y", ""}}
    );

    BOOST_REQUIRE(empty.isValid() && fromInteger.isValid() && fromPair.isValid());

    const auto emptyResult = runtime.resolveConstructor(type, {});
    const auto integerResult = runtime.resolveConstructor(type, {runtime.getIntegerType()});
    const auto pairResult = runtime.resolveConstructor(type, {runtime.getIntegerType(), runtime.getIntegerType()});

    BOOST_REQUIRE(emptyResult.isResolved() && integerResult.isResolved() && pairResult.isResolved());
    BOOST_TEST(emptyResult.id == empty);
    BOOST_TEST(integerResult.id == fromInteger);
    BOOST_TEST(pairResult.id == fromPair);

    BOOST_TEST(runtime.resolveConstructor(type, {runtime.getStringType()}).status == ResolveStatus::NoMatchingTypes);
    BOOST_TEST(
        runtime.resolveConstructor(type, {nullptr, nullptr, nullptr}).status == ResolveStatus::NoMatchingArity
    );
    BOOST_TEST(
        runtime.resolveConstructor(registerResolutionType(runtime, "Other"), {}).status == ResolveStatus::UnknownName
    );
}

BOOST_AUTO_TEST_SUITE_END()
