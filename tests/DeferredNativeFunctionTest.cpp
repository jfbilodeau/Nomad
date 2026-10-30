// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/generated/log/Bindings.hpp>

#include <nomad/compiler/Compilation.hpp>
#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/FunctionParameterTypeTarget.hpp>
#include <nomad/compiler/FunctionReturnTypeTarget.hpp>

#include <nomad/modules/ScriptBindings.hpp>

#include <nomad/script/InterpreterContext.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <BindingTestFixture.hpp>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <sstream>
#include <utility>

using namespace nomad;
using namespace nomad::test;
using namespace nomad::generated::log;

namespace {

class RecordingDeferredTypeTarget final : public DeferredTypeTarget {
public:
    RecordingDeferredTypeTarget(
        TypeReference type,
        const Type*& destination,
        int& resolutions,
        int& destructions
    ) : m_type(std::move(type)), m_destination(destination),
        m_resolutions(resolutions), m_destructions(destructions) {
    }

    ~RecordingDeferredTypeTarget() override {
        ++m_destructions;
    }

    [[nodiscard]] const TypeReference& getDeferredType() const override {
        return m_type;
    }

    [[nodiscard]] NomadString getDescription() const override {
        return "recording target";
    }

    void resolve(const Type* type) override {
        m_destination = type;
        ++m_resolutions;
    }

private:
    TypeReference m_type;
    const Type*& m_destination;
    int& m_resolutions;
    int& m_destructions;
};

} // namespace

BOOST_AUTO_TEST_SUITE(native_deferred_functions)

BOOST_AUTO_TEST_CASE(placeholders_are_compiler_owned_named_types_not_runtime_types_or_usable_values) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    const auto* type = compiler->deferType({"Settings"});
    BOOST_TEST(type == compiler->deferType({"Settings"}));
    BOOST_TEST(type->getRuntime() == &runtime);
    BOOST_TEST(type->getId().isInvalid());
    BOOST_TEST(type->getTypeName() == "Settings");
    BOOST_TEST(runtime.getTypeByName("Settings") == nullptr);
    RuntimeValue value;
    NomadString text;
    BOOST_CHECK_THROW((void)type->getStorage(), NomadException);
    BOOST_CHECK_THROW(type->initValue(value), NomadException);
    BOOST_CHECK_THROW(type->copyValue(value, value), NomadException);
    BOOST_CHECK_THROW(type->freeValue(value), NomadException);
    BOOST_CHECK_THROW(type->toString(value, text), NomadException);
    BOOST_CHECK_THROW((void)compiler->deferType({""}), NomadException);
}

BOOST_AUTO_TEST_CASE(unused_unresolved_placeholders_fail_compilation_including_analysis) {
    for (const auto target : {CompilationTarget::Executable, CompilationTarget::Analysis}) {
        Runtime runtime;
        auto compiler = runtime.createCompiler();
        (void)compiler->deferType({"Missing"});
        const auto result = compileSources({{{"main", {"main.nomad", "return 1"}}}, {}}, *compiler, target);
        BOOST_TEST(!result.succeeded());
        BOOST_TEST(std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
            return diagnostic.message.find("Missing") != NomadString::npos;
        }));
    }
}

BOOST_AUTO_TEST_CASE(compiler_owns_targets_and_delivers_one_canonical_type_to_multiple_destinations) {
    Runtime runtime;
    const Type* first = nullptr;
    const Type* second = nullptr;
    auto resolutions = 0;
    auto destructions = 0;
    auto compiler = runtime.createCompiler();
    compiler->registerDeferredTypeTarget(std::make_unique<RecordingDeferredTypeTarget>(
        TypeReference{"Settings"}, first, resolutions, destructions
    ));
    compiler->registerDeferredTypeTarget(std::make_unique<RecordingDeferredTypeTarget>(
        TypeReference{"Settings"}, second, resolutions, destructions
    ));
    BOOST_TEST(resolutions == 0);
    BOOST_TEST(destructions == 0);
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"main", {"main.nomad", "class Settings\nend\nreturn 1"}}}, {}});
    BOOST_REQUIRE(compiler->compileScripts(&context));
    BOOST_TEST(first == runtime.getTypeByName("Settings"));
    BOOST_TEST(second == first);
    BOOST_TEST(resolutions == 2);
    BOOST_TEST(destructions == 2);
    compiler->resolveDeferredTypes();
    BOOST_TEST(resolutions == 2);
    BOOST_TEST(destructions == 2);
}

BOOST_AUTO_TEST_CASE(missing_target_types_fail_before_any_targets_are_applied_and_are_owned_until_teardown) {
    Runtime runtime;
    const Type* destination = nullptr;
    auto resolutions = 0;
    auto destructions = 0;
    {
        auto compiler = runtime.createCompiler();
        compiler->registerDeferredTypeTarget(std::make_unique<RecordingDeferredTypeTarget>(
            TypeReference{"int"}, destination, resolutions, destructions
        ));
        compiler->registerDeferredTypeTarget(std::make_unique<RecordingDeferredTypeTarget>(
            TypeReference{"Missing"}, destination, resolutions, destructions
        ));
        CompilerContext context(compiler.get());
        compiler->registerSources(&context, {{{"main", {"main.nomad", "return 1"}}}, {}});
        BOOST_TEST(!compiler->compileScripts(&context));
        BOOST_TEST(context.hasError());
        BOOST_TEST(resolutions == 0);
        BOOST_TEST(destination == nullptr);
        BOOST_TEST(destructions == 0);
    }
    BOOST_TEST(destructions == 2);
}

BOOST_AUTO_TEST_CASE(stable_parameter_and_return_targets_survive_callable_growth_and_check_destination_types) {
    Runtime runtime;
    Runtime foreign;
    auto compiler = runtime.createCompiler();
    const auto id = runtime.registerNativeFunction("echo", [](VirtualMachine*) {},
        {defParameter("value", compiler->deferType({"int"}), "")}, compiler->deferType({"int"}), "");
    BOOST_REQUIRE(id.isValid());
    auto parameter = std::make_unique<FunctionParameterTypeTarget>(runtime, id, 0, TypeReference{"int"});
    auto result = std::make_unique<FunctionReturnTypeTarget>(runtime, id, TypeReference{"int"});
    for (auto index = 0; index < 64; ++index) {
        BOOST_REQUIRE(runtime.registerNativeFunction(
            "additional" + std::to_string(index), [](VirtualMachine*) {}, {}, runtime.getVoidType(), ""
        ).isValid());
    }
    BOOST_CHECK_THROW(parameter->resolve(foreign.getIntegerType()), NomadException);
    BOOST_CHECK_THROW(result->resolve(foreign.getIntegerType()), NomadException);
    BOOST_CHECK_THROW(parameter->resolve(runtime.getFloatType()), NomadException);
    BOOST_CHECK_THROW(result->resolve(runtime.getFloatType()), NomadException);
    BOOST_CHECK_THROW((void)FunctionParameterTypeTarget(runtime, id, 1, TypeReference{"int"}), NomadException);
    BOOST_CHECK_THROW((void)FunctionReturnTypeTarget(runtime, CallableId{}, TypeReference{"int"}), NomadException);
    compiler->registerDeferredTypeTarget(std::move(parameter));
    compiler->registerDeferredTypeTarget(std::move(result));
    compiler->resolveDeferredTypes();
    const auto* function = runtime.getNativeFunction(id);
    BOOST_TEST(function->isSignatureResolved());
    BOOST_TEST(function->getParameters()[0].type == runtime.getIntegerType());
    BOOST_TEST(function->getReturnType() == runtime.getIntegerType());
    BOOST_TEST(runtime.getNativeFunctionId("echo") == id);
    BOOST_CHECK_THROW(compiler->registerDeferredTypeTarget(nullptr), NomadException);
}

BOOST_AUTO_TEST_CASE(public_resolution_apis_validate_slots_types_and_completion_without_friend_access) {
    Runtime runtime;
    Runtime foreign;
    auto compiler = runtime.createCompiler();
    const auto* placeholder = compiler->deferType({"int"});
    const auto id = runtime.registerNativeFunction("echoResolved",
        [](VirtualMachine* machine) { machine->setIntegerResult(machine->getIntegerParameter(0)); },
        {
            defParameter("value", placeholder, ""),
            defParameter("enabled", runtime.getBooleanType(), "")
        },
        placeholder, "");
    BOOST_REQUIRE(id.isValid());
    auto* function = runtime.getMutableNativeFunction(id);
    BOOST_REQUIRE(function != nullptr);
    BOOST_TEST(function->getReturnType() == placeholder);
    BOOST_TEST(function->getReturnTypeName() == "int");
    BOOST_TEST(runtime.getMutableNativeFunction(CallableId{}) == nullptr);
    const auto scriptId = runtime.registerScriptFunction("scriptOnly", "script.nomad", "");
    BOOST_REQUIRE(scriptId.isValid());
    BOOST_TEST(runtime.getMutableNativeFunction(scriptId) == nullptr);
    BOOST_CHECK_THROW(function->resolveParameterType(2, runtime.getIntegerType()), NomadException);
    BOOST_CHECK_THROW(function->resolveParameterType(1, runtime.getBooleanType()), NomadException);
    BOOST_CHECK_THROW(function->resolveParameterType(0, nullptr), NomadException);
    BOOST_CHECK_THROW(function->resolveParameterType(0, foreign.getIntegerType()), NomadException);
    BOOST_CHECK_THROW(function->resolveParameterType(0, runtime.getFloatType()), NomadException);
    BOOST_CHECK_THROW(function->resolveReturnType(nullptr), NomadException);
    BOOST_CHECK_THROW(function->resolveReturnType(foreign.getIntegerType()), NomadException);
    BOOST_CHECK_THROW(function->resolveReturnType(runtime.getFloatType()), NomadException);
    BOOST_CHECK_THROW(function->completeSignatureResolution(), NomadException);
    BOOST_CHECK_THROW(runtime.validateNativeFunctionSignatures(), NomadException);
    BOOST_TEST(function->getParameters()[0].type == placeholder);
    BOOST_TEST(function->getReturnType() == placeholder);
    function->resolveParameterType(0, runtime.getIntegerType());
    BOOST_CHECK_THROW(function->completeSignatureResolution(), NomadException);
    function->resolveReturnType(runtime.getIntegerType());
    BOOST_TEST(!function->isSignatureResolved());
    BOOST_CHECK_THROW((void)function->getFn(), NomadException);
    runtime.validateNativeFunctionSignatures();
    BOOST_TEST(function->isSignatureResolved());
    BOOST_TEST(function->getReturnType() == runtime.getIntegerType());
    BOOST_TEST(function->getParameters()[0].type == runtime.getIntegerType());
    BOOST_CHECK_THROW(function->resolveParameterType(0, runtime.getIntegerType()), NomadException);
    BOOST_CHECK_THROW(function->resolveReturnType(runtime.getIntegerType()), NomadException);
    VirtualMachine machine(&runtime);
    machine.runNativeFunction(id, {RuntimeValue(NomadInteger{42}), RuntimeValue(true)});
    BOOST_TEST(machine.getResult().getIntegerValue() == 42);
}

BOOST_AUTO_TEST_CASE(generated_references_register_stable_ids_and_execute_the_script_native_cycle) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    const auto types = deferTypes(*compiler);
    ScriptBindings bindings(runtime);
    auto calls = 0;
    const auto id = runtime.registerNativeFunction("updateConfiguration",
        [&bindings, &calls](VirtualMachine* machine) {
            ++calls;
            auto settings = log_SinkConfiguration(bindings, machine->getParameter(1).getObjectValue());
            settings.setLevel(static_cast<log_Level>(machine->getIntegerParameter(0)));
            settings.setActive(machine->getBooleanParameter(2));
            auto* object = settings.getObject();
            object->addRef();
            machine->getResult().setObjectValue(object);
        },
        {
            defParameter("level", types.level, "The new level."),
            defParameter("configuration", types.sinkConfiguration, "The configuration."),
            defParameter("active", runtime.getBooleanType(), "The active state.")
        },
        types.sinkConfiguration, "Updates and returns the same script object.");
    BOOST_REQUIRE(id.isValid());
    const auto* declaration = runtime.getNativeFunction(id);
    BOOST_REQUIRE(declaration != nullptr);
    BOOST_TEST(!declaration->isSignatureResolved());
    BOOST_TEST(declaration->getParameters()[1].type == types.sinkConfiguration);
    BOOST_TEST(declaration->getParameters()[1].getTypeName() == "log.SinkConfiguration");
    BOOST_TEST(declaration->getReturnTypeName() == "log.SinkConfiguration");
    BOOST_CHECK_THROW((void)declaration->getFn(), NomadException);
    BOOST_CHECK_THROW((void)runtime.getNativeFunctionFn(id), NomadException);
    const auto unresolved = runtime.resolveFunction("updateConfiguration", {nullptr, nullptr, nullptr});
    BOOST_CHECK(unresolved.status == ResolveStatus::UnresolvedSignature);
    BOOST_TEST(runtime.describeResolveFailure("updateConfiguration", unresolved).find("unresolved") != NomadString::npos);
    VirtualMachine machine(&runtime);
    BOOST_CHECK_THROW(machine.runNativeFunction(id, {RuntimeValue{}, RuntimeValue{}, RuntimeValue{}}), NomadException);
    InterpreterContext interpreter(machine);
    runtime.executeNativeFunction(id, {RuntimeValue{}, RuntimeValue{}, RuntimeValue{}}, interpreter);
    BOOST_TEST(interpreter.hasError());
    BOOST_TEST(calls == 0);
    std::ostringstream documentation;
    runtime.dumpDocumentation(documentation);
    BOOST_TEST(documentation.str().find("configuration:log.SinkConfiguration") != NomadString::npos);

    const auto compilation = compileSources({{{"log.configuration", {"configuration.nomad", getLogBindingSource() +
        "\nfun exerciseDeferred\n"
        "original = new log.SinkConfiguration\n"
        "updated = updateConfiguration log.Level.Warning original false\n"
        "assert updated.level == log.Level.Warning\n"
        "assert original.active == false\n"
        "updated.active = true\n"
        "assert original.active\n"
        "return 42\n"
        "end\n"}}}, {}}, *compiler);
    BOOST_REQUIRE(compilation.succeeded());
    BOOST_TEST(runtime.getNativeFunctionId("updateConfiguration") == id);
    BOOST_TEST(runtime.getNativeFunction(id) == declaration);
    BOOST_TEST(declaration->isSignatureResolved());
    BOOST_TEST(declaration->getParameters()[1].type == runtime.getTypeByName("log.SinkConfiguration"));
    BOOST_TEST(declaration->getReturnType() == runtime.getTypeByName("log.SinkConfiguration"));
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("exerciseDeferred"), {}, result);
    BOOST_TEST(result.getIntegerValue() == 42);
    BOOST_TEST(calls == 1);
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);

    auto laterCompiler = runtime.createCompiler();
    const auto later = compileSources({{{"later", {"later.nomad",
        "configuration = new log.SinkConfiguration\n"
        "updated = updateConfiguration log.Level.Debug configuration true\n"
        "return updated.active\n"}}}, {}}, *laterCompiler);
    BOOST_REQUIRE(later.succeeded());
    BOOST_TEST(runtime.getCallableOverloadCount("updateConfiguration") == 1U);
    BOOST_TEST(runtime.getNativeFunctionId("updateConfiguration") == id);
    runtime.executeFunction(runtime.getFunctionId("later"), {}, result);
    BOOST_TEST(result.getBooleanValue());
    BOOST_TEST(calls == 2);
}

BOOST_AUTO_TEST_CASE(hub_enum_signatures_resolve_in_analysis_without_running_callbacks_or_emitting_code) {
    auto firstCalls = 0;
    auto secondCalls = 0;
    Runtime first;
    Runtime second;
    BOOST_REQUIRE(second.createEnum("Padding", {{"Value", 1}}).isValid());
    const auto resolve = [](Runtime& runtime, int& calls) {
        auto compiler = runtime.createCompiler();
        const auto hubId = runtime.registerHub("native", HubMode::Opened);
        BOOST_REQUIRE(hubId.isValid());
        const auto id = runtime.registerHubNativeFunction(hubId, "echoLevel",
            [&calls](VirtualMachine* machine) {
                ++calls;
                machine->setIntegerResult(machine->getIntegerParameter(0));
            },
            {defParameter("level", compiler->deferType(log_LevelBinding::typeReference()), "")},
            compiler->deferType(log_LevelBinding::typeReference()), "");
        BOOST_REQUIRE(id.isValid());
        CompilerContext context(compiler.get());
        compiler->registerSources(&context, {{{"contract", {"contract.nomad", getLogBindingSource() +
            "\nfun useNativeLevel\nreturn native.echoLevel log.Level.Info\nend\n"}}}, {}});
        const auto instructionCount = runtime.getInstructions().size();
        BOOST_REQUIRE(compiler->compileScripts(&context, CompilationTarget::Analysis));
        BOOST_TEST(calls == 0);
        BOOST_TEST(runtime.getInstructions().size() == instructionCount);
        const auto* function = runtime.getNativeFunction(id);
        BOOST_REQUIRE(function != nullptr);
        BOOST_TEST(function->isSignatureResolved());
        BOOST_TEST(function->getReturnType() == runtime.getTypeByName("log.Level"));
        BOOST_TEST(function->getParameters().front().type == function->getReturnType());
        return function->getReturnType();
    };
    const auto* firstType = resolve(first, firstCalls);
    const auto* secondType = resolve(second, secondCalls);
    BOOST_TEST(firstType != secondType);
    BOOST_CHECK(firstType->getId() != secondType->getId());
}

BOOST_AUTO_TEST_CASE(script_field_initializers_can_call_deferred_natives_registered_in_parse_callbacks) {
    Runtime runtime;
    ScriptBindings bindings(runtime);
    auto calls = 0;
    CallableId id;
    runtime.registerCompilerSetup([&](Compiler* compiler) {
        compiler->registerPhaseHook([&, compiler](CompilerContext*, const CompilationPhase phase) {
            if (phase != CompilationPhase::Parse || id.isValid()) {
                return;
            }
            id = runtime.registerNativeFunction("defaultActive",
                [&calls](VirtualMachine* machine) {
                    ++calls;
                    machine->setBooleanResult(
                        machine->getIntegerParameter(0) == static_cast<NomadInteger>(log_Level::Info)
                    );
                },
                {defParameter("level", compiler->deferType(log_LevelBinding::typeReference()), "")},
                runtime.getBooleanType(), "");
            BOOST_REQUIRE(id.isValid());
            BOOST_TEST(!runtime.getNativeFunction(id)->isSignatureResolved());
        });
    });
    auto source = getLogBindingSource();
    const auto position = source.find("active = true");
    BOOST_REQUIRE(position != NomadString::npos);
    source.replace(position, NomadString("active = true").size(), "active = defaultActive log.Level.Info");
    compileLogBindingSource(runtime, source);
    BOOST_TEST(calls == 0);
    BOOST_TEST(runtime.getNativeFunction(id)->isSignatureResolved());
    VirtualMachine machine(&runtime);
    auto settings = log_SinkConfiguration::create(bindings, machine);
    BOOST_TEST(settings.getActive());
    BOOST_TEST(calls == 1);
}

BOOST_AUTO_TEST_CASE(missing_or_void_parameter_types_and_missing_return_types_are_diagnostics) {
    for (const auto* name : {"Missing", "void"}) {
        Runtime runtime;
        auto compiler = runtime.createCompiler();
        const auto id = runtime.registerNativeFunction("unresolved",
            [](VirtualMachine*) {}, {defParameter("value", compiler->deferType({name}), "")}, runtime.getVoidType(), "");
        BOOST_REQUIRE(id.isValid());
        const auto result = compileSources({{{"main", {"main.nomad", "return 1"}}}, {}}, *compiler);
        BOOST_TEST(!result.succeeded());
        BOOST_TEST(std::ranges::any_of(result.diagnostics, [name](const auto& diagnostic) {
            return diagnostic.message.find("parameter 'value'") != NomadString::npos &&
                diagnostic.message.find(name) != NomadString::npos;
        }));
        BOOST_TEST(!runtime.getNativeFunction(id)->isSignatureResolved());
        BOOST_CHECK_THROW((void)runtime.getNativeFunctionFn(id), NomadException);
    }
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    const auto id = runtime.registerNativeFunction("missingReturn", [](VirtualMachine*) {}, {},
        compiler->deferType({"Missing"}), "");
    BOOST_REQUIRE(id.isValid());
    const auto failed = compileSources({{{"failed", {"failed.nomad", "return 1"}}}, {}}, *compiler);
    BOOST_TEST(!failed.succeeded());
    BOOST_TEST(std::ranges::any_of(failed.diagnostics, [](const auto& diagnostic) {
        return diagnostic.message.find("return type") != NomadString::npos &&
            diagnostic.message.find("Missing") != NomadString::npos;
    }));
    BOOST_TEST(!runtime.getNativeFunction(id)->isSignatureResolved());
    auto recoveryCompiler = runtime.createCompiler();
    const auto recovered = compileSources({{{"recovered", {"recovered.nomad",
        "class Missing\nend\nreturn 2"}}}, {}}, *recoveryCompiler);
    BOOST_REQUIRE(recovered.succeeded());
    BOOST_TEST(runtime.getNativeFunctionId("missingReturn") == id);
    BOOST_TEST(runtime.getNativeFunction(id)->isSignatureResolved());
}

BOOST_AUTO_TEST_CASE(concrete_and_deferred_overloads_share_ordinary_resolution) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    const auto objectId = runtime.registerNativeFunction("choose",
        [](VirtualMachine* machine) { machine->setIntegerResult(20); },
        {defParameter("value", compiler->deferType({"Number"}), "")}, runtime.getIntegerType(), "");
    const auto integerId = runtime.registerNativeFunction("choose",
        [](VirtualMachine* machine) { machine->setIntegerResult(machine->getIntegerParameter(0) + 1); },
        {defParameter("value", runtime.getIntegerType(), "")}, runtime.getIntegerType(), "");
    BOOST_REQUIRE(objectId.isValid());
    BOOST_REQUIRE(integerId.isValid());
    const auto result = compileSources({{{"main", {"main.nomad",
        "class Number\nend\nleft = choose 21\nright = choose new Number\nreturn left + right"}}}, {}}, *compiler);
    BOOST_REQUIRE(result.succeeded());
    BOOST_TEST(runtime.resolveFunction("choose", {runtime.getTypeByName("Number")}).id == objectId);
    BOOST_TEST(runtime.resolveFunction("choose", {runtime.getIntegerType()}).id == integerId);
    RuntimeValue value;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, value);
    BOOST_TEST(value.getIntegerValue() == 42);
}

BOOST_AUTO_TEST_CASE(overload_conflicts_are_reported_after_type_resolution) {
    for (const auto conflictingReturn : {false, true}) {
        Runtime runtime;
        auto compiler = runtime.createCompiler();
        const auto firstId = runtime.registerNativeFunction("conflict", [](VirtualMachine*) {},
            {defParameter("value", compiler->deferType({conflictingReturn ? "Number" : "int"}), "")},
            runtime.getVoidType(), "");
        const auto secondId = runtime.registerNativeFunction("conflict", [](VirtualMachine*) {},
            {defParameter("value", runtime.getIntegerType(), "")},
            conflictingReturn ? runtime.getIntegerType() : runtime.getVoidType(), "");
        BOOST_REQUIRE(firstId.isValid());
        BOOST_REQUIRE(secondId.isValid());
        const auto result = compileSources({{{"main", {"main.nomad", "class Number\nend\nreturn 1"}}}, {}}, *compiler);
        BOOST_TEST(!result.succeeded());
        const auto expected = conflictingReturn ? "all return" : "same parameter types";
        BOOST_TEST(std::ranges::any_of(result.diagnostics, [expected](const auto& diagnostic) {
            return diagnostic.message.find(expected) != NomadString::npos;
        }));
        BOOST_TEST(!runtime.getNativeFunction(firstId)->isSignatureResolved());
        BOOST_TEST(!runtime.getNativeFunction(secondId)->isSignatureResolved());
        BOOST_CHECK_THROW((void)runtime.getNativeFunctionFn(firstId), NomadException);
    }
}

BOOST_AUTO_TEST_CASE(invalid_declarations_are_rejected_without_publishing_a_callable) {
    Runtime runtime;
    Runtime foreign;
    auto compiler = runtime.createCompiler();
    auto foreignCompiler = foreign.createCompiler();
    const NativeFunctionFn callback = [](VirtualMachine*) {};
    BOOST_CHECK_THROW((void)compiler->deferType({""}), NomadException);
    BOOST_TEST(runtime.registerNativeFunction("nullParameter", callback,
        {defParameter("value", nullptr, "")}, runtime.getVoidType(), "").isInvalid());
    BOOST_TEST(runtime.registerNativeFunction("nullReturn", callback, {}, nullptr, "").isInvalid());
    BOOST_TEST(runtime.registerNativeFunction("missingCallback", {}, {},
        compiler->deferType({"Number"}), "").isInvalid());
    BOOST_TEST(runtime.registerNativeFunction("foreignParameter", callback,
        {defParameter("value", foreignCompiler->deferType({"Number"}), "")}, runtime.getVoidType(), "").isInvalid());
    BOOST_TEST(runtime.registerNativeFunction("foreignReturn", callback, {},
        foreignCompiler->deferType({"Number"}), "").isInvalid());
    for (const auto* name : {"nullParameter", "nullReturn", "missingCallback", "foreignParameter", "foreignReturn"}) {
        BOOST_TEST(runtime.getNativeFunctionId(name).isInvalid());
    }
}

BOOST_AUTO_TEST_SUITE_END()
