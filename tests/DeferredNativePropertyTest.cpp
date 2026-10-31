// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compilation.hpp>
#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/NativePropertyTypeTarget.hpp>

#include <nomad/script/NativeProperty.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <memory>
#include <string>

using namespace nomad;

namespace {

class PropertyOwner final : public NomadObject {};

TypeId registerPropertyOwner(Runtime& runtime) {
    const auto id = runtime.registerClass("PropertyOwner", ClassMode::Closed);
    BOOST_REQUIRE(id.isValid());
    BOOST_REQUIRE(runtime.addConstructor(id, {
        .parameters = {},
        .function = [](VirtualMachine* machine) {
            machine->getResult().setObjectValue(new PropertyOwner);
        },
        .documentation = ""
    }).isValid());
    return id;
}

} // namespace

BOOST_AUTO_TEST_SUITE(native_deferred_properties)

BOOST_AUTO_TEST_CASE(enum_properties_resolve_in_both_targets_without_running_callbacks_and_keep_stable_ids) {
    for (const auto target : {CompilationTarget::Analysis, CompilationTarget::Executable}) {
        Runtime runtime;
        auto compiler = runtime.createCompiler();
        const auto ownerId = registerPropertyOwner(runtime);
        const auto* placeholder = compiler->deferType({"example.Level"});
        auto getterCalls = 0;
        auto setterCalls = 0;
        NomadInteger stored = 1;
        const auto id = runtime.addProperty(ownerId, {
            .valueType = placeholder,
            .name = "level",
            .getter = [&](VirtualMachine*, RuntimeValue& result) {
                ++getterCalls;
                result.setIntegerValue(stored);
            },
            .setter = [&](VirtualMachine*, const RuntimeValue& value) {
                ++setterCalls;
                stored = value.getIntegerValue();
            },
            .documentation = "Deferred enum property."
        });
        BOOST_REQUIRE(id.isValid());
        const auto* property = runtime.getNativeProperty(id);
        BOOST_REQUIRE(property != nullptr);
        BOOST_TEST(!property->isTypeResolved());
        const auto info = property->getInfo();
        BOOST_TEST(info.valueType == placeholder);
        const auto* owner = runtime.getType(ownerId).value()->asClassType();
        BOOST_REQUIRE(owner != nullptr);
        BOOST_TEST(property->getOwnerType() == owner->asCompositeType());
        BOOST_TEST(property->getClassType() == owner);
        runtime.finalizeCompositeFieldLayout(owner->getId());
        BOOST_TEST(owner->isFieldLayoutFinalized());
        BOOST_TEST(owner->getFieldStorageSize() == 0U);
        BOOST_TEST(owner->getFieldIndex(id) == NOMAD_INVALID_INDEX);
        BOOST_CHECK_THROW((void)owner->getFieldOffset(id), NomadException);
        RuntimeValue value;
        BOOST_CHECK_THROW(info.getFn(nullptr, value), NomadException);
        BOOST_CHECK_THROW(info.setFn(nullptr, RuntimeValue(NomadInteger{2})), NomadException);
        {
            VirtualMachine machine(&runtime);
            RuntimeValue receiver;
            receiver.setObjectValue(new PropertyOwner);
            BOOST_CHECK_THROW(machine.getNativeProperty(id, receiver, value), NomadException);
            BOOST_CHECK_THROW(machine.setNativeProperty(id, receiver, RuntimeValue(NomadInteger{2})), NomadException);
            receiver.freeObjectValue();
        }
        compiler->registerDeferredTypeTarget(std::make_unique<NativePropertyTypeTarget>(
            runtime, id, TypeReference{"example.Level"}
        ));
        compiler->registerPhaseHook([&](CompilerContext*, const CompilationPhase phase) {
            if (phase == CompilationPhase::Parse) {
                for (auto index = 0; index < 64; ++index) {
                    BOOST_REQUIRE(runtime.addProperty(ownerId, {
                        .valueType = runtime.getIntegerType(),
                        .name = "extra" + std::to_string(index),
                        .getter = [](VirtualMachine*, RuntimeValue& result) { result.setIntegerValue(0); }
                    }).isValid());
                }
            }
        });
        const auto instructionCount = runtime.getInstructions().size();
        const auto result = compileSources({{{"main", {"property.nomad",
            "open hub example\nenum Level\nLow\nHigh\nend\nend\n"
            "owner = new PropertyOwner\nowner.level = example.Level.High\n"
            "return owner.level == example.Level.High"}}}, {}}, *compiler, target);
        BOOST_REQUIRE_MESSAGE(result.succeeded(),
            (result.diagnostics.empty() ? "" : result.diagnostics.front().message));
        BOOST_TEST(runtime.getNativeProperty(id) == property);
        BOOST_TEST(property->getId() == id);
        BOOST_TEST(property->isTypeResolved());
        BOOST_TEST(property->getValueType() == runtime.getTypeByName("example.Level"));
        BOOST_TEST(property->getOwnerType() == owner->asCompositeType());
        BOOST_TEST(owner->isFieldLayoutFinalized());
        BOOST_TEST(runtime.getType(ownerId).value()->asClassType()->getProperty("level") == property);
        BOOST_TEST(owner->getFieldStorageSize() == 0U);
        BOOST_CHECK_THROW((void)owner->getFieldOffset(id), NomadException);
        BOOST_TEST(getterCalls == 0);
        BOOST_TEST(setterCalls == 0);
        if (target == CompilationTarget::Analysis) {
            BOOST_TEST(runtime.getInstructions().size() == instructionCount);
        } else {
            RuntimeValue answer;
            runtime.executeFunction(runtime.getFunctionId("main"), {}, answer);
            BOOST_TEST(answer.getBooleanValue());
            BOOST_TEST(stored == 2);
            BOOST_TEST(getterCalls == 1);
            BOOST_TEST(setterCalls == 1);
            info.getFn(nullptr, value);
            BOOST_TEST(value.getIntegerValue() == 2);
        }
        compiler->resolveDeferredTypes();
        BOOST_TEST(property->isTypeResolved());
    }
}

BOOST_AUTO_TEST_CASE(script_class_properties_preserve_object_reference_ownership) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    const auto ownerId = registerPropertyOwner(runtime);
    RuntimeValue stored;
    const auto id = runtime.addProperty(ownerId, {
        .valueType = compiler->deferType({"Settings"}),
        .name = "settings",
        .getter = [&](VirtualMachine*, RuntimeValue& result) {
            runtime.getTypeByName("Settings")->copyValue(&stored, &result);
        },
        .setter = [&](VirtualMachine*, const RuntimeValue& value) {
            runtime.getTypeByName("Settings")->assignValue(value, stored);
        }
    });
    BOOST_REQUIRE(id.isValid());
    const auto result = compileSources({{{"main", {"settings.nomad",
        "class Settings\ncount = 3\nend\nowner = new PropertyOwner\n"
        "original = new Settings\nowner.settings = original\n"
        "alias = owner.settings\nalias.count = 42\n"
        "owner.settings = new Settings\n"
        "assert original.count == 42\nreturn owner.settings.count"}}}, {}}, *compiler);
    BOOST_REQUIRE_MESSAGE(result.succeeded(),
        (result.diagnostics.empty() ? "" : result.diagnostics.front().message));
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    RuntimeValue answer;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, answer);
    BOOST_TEST(answer.getIntegerValue() == 3);
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore + 1U);
    stored.freeObjectValue();
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    BOOST_TEST(runtime.getNativeProperty(id)->getValueType() == runtime.getTypeByName("Settings"));
}

BOOST_AUTO_TEST_CASE(missing_types_fail_with_property_context_before_applying_any_targets) {
    for (const auto target : {CompilationTarget::Analysis, CompilationTarget::Executable}) {
        Runtime runtime;
        auto compiler = runtime.createCompiler();
        const auto ownerId = registerPropertyOwner(runtime);
        const auto* known = compiler->deferType({"Known"});
        const auto firstId = runtime.addProperty(ownerId, {
            .valueType = known,
            .name = "known",
            .getter = [](VirtualMachine*, RuntimeValue&) {}
        });
        const auto missingId = runtime.addProperty(ownerId, {
            .valueType = compiler->deferType({"Missing"}),
            .name = "missing",
            .getter = [](VirtualMachine*, RuntimeValue&) {}
        });
        BOOST_REQUIRE(firstId.isValid());
        BOOST_REQUIRE(missingId.isValid());
        const auto result = compileSources({{{"main", {"missing.nomad",
            "class Known\nend\nreturn 1"}}}, {}}, *compiler, target);
        BOOST_TEST(!result.succeeded());
        BOOST_TEST(std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
            return diagnostic.message.find("native property 'PropertyOwner.missing'") != NomadString::npos &&
                diagnostic.message.find("Missing") != NomadString::npos;
        }));
        BOOST_TEST(runtime.getNativeProperty(firstId)->getValueType() == known);
        BOOST_TEST(!runtime.getNativeProperty(firstId)->isTypeResolved());
        BOOST_TEST(!runtime.getNativeProperty(missingId)->isTypeResolved());
        RuntimeValue value;
        BOOST_CHECK_THROW(runtime.getNativeProperty(missingId)->getInfo().getFn(nullptr, value), NomadException);
    }
}

BOOST_AUTO_TEST_CASE(invalid_resolved_types_and_foreign_placeholders_are_rejected) {
    for (const auto target : {CompilationTarget::Analysis, CompilationTarget::Executable}) {
        for (const auto internal : {false, true}) {
            Runtime runtime;
            auto compiler = runtime.createCompiler();
            const auto ownerId = registerPropertyOwner(runtime);
            const auto* invalid = internal ? runtime.getFileNameType() : runtime.getVoidType();
            const auto id = runtime.addProperty(ownerId, {
                .valueType = compiler->deferType({invalid->getTypeName()}),
                .name = "invalid",
                .getter = [](VirtualMachine*, RuntimeValue&) {}
            });
            BOOST_REQUIRE(id.isValid());
            const auto result = compileSources({}, *compiler, target);
            BOOST_TEST(!result.succeeded());
            BOOST_TEST(!runtime.getNativeProperty(id)->isTypeResolved());
            BOOST_TEST(std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
                return diagnostic.message.find("PropertyOwner.invalid") != NomadString::npos;
            }));
        }
    }
    Runtime runtime;
    Runtime other;
    auto compiler = runtime.createCompiler();
    auto otherCompiler = other.createCompiler();
    const auto ownerId = registerPropertyOwner(runtime);
    BOOST_TEST(runtime.registerProperty(ownerId, "storedDeferred", compiler->deferType({"Settings"})).isInvalid());
    BOOST_TEST(runtime.addProperty(ownerId, {
        .valueType = otherCompiler->deferType({"Settings"}),
        .name = "foreign",
        .getter = [](VirtualMachine*, RuntimeValue&) {}
    }).isInvalid());
    BOOST_TEST(runtime.getType(ownerId).value()->asClassType()->getProperty("foreign") == nullptr);
}

BOOST_AUTO_TEST_CASE(checked_resolution_preserves_readiness_until_central_completion) {
    Runtime runtime;
    Runtime other;
    auto compiler = runtime.createCompiler();
    const auto ownerId = registerPropertyOwner(runtime);
    const auto stateId = runtime.createEnum("State", {{"Ready", 1}});
    const auto otherStateId = other.createEnum("State", {{"Ready", 1}});
    BOOST_REQUIRE(stateId.isValid());
    BOOST_REQUIRE(otherStateId.isValid());
    auto calls = 0;
    const auto id = runtime.addProperty(ownerId, {
        .valueType = compiler->deferType({"State"}),
        .name = "state",
        .getter = [&](VirtualMachine*, RuntimeValue& result) { ++calls; result.setIntegerValue(1); }
    });
    BOOST_REQUIRE(id.isValid());
    const auto storedId = runtime.registerProperty(ownerId, "stored", runtime.getIntegerType());
    BOOST_REQUIRE(storedId.isValid());
    BOOST_TEST(runtime.getMutableNativeProperty(storedId) == nullptr);
    BOOST_TEST(runtime.getMutableNativeProperty(PropertyId{NOMAD_ID_MAX}) == nullptr);
    BOOST_CHECK_THROW((void)NativePropertyTypeTarget(runtime, storedId, {"State"}), NomadException);
    auto* property = runtime.getMutableNativeProperty(id);
    BOOST_REQUIRE(property != nullptr);
    RuntimeValue value;
    BOOST_CHECK_THROW(property->completeTypeResolution(), NomadException);
    BOOST_CHECK_THROW(property->resolveValueType(other.getTypeByName("State")), NomadException);
    BOOST_CHECK_THROW(property->resolveValueType(runtime.getIntegerType()), NomadException);
    NativePropertyTypeTarget destination(runtime, id, {"State"});
    BOOST_CHECK_THROW(destination.resolve(nullptr), NomadException);
    BOOST_CHECK_THROW(destination.resolve(runtime.getIntegerType()), NomadException);
    destination.resolve(runtime.getTypeByName("State"));
    BOOST_TEST(!property->isTypeResolved());
    BOOST_CHECK_THROW(property->getInfo().getFn(nullptr, value), NomadException);
    runtime.validateNativePropertyTypes();
    BOOST_TEST(property->isTypeResolved());
    property->getInfo().getFn(nullptr, value);
    BOOST_TEST(value.getIntegerValue() == 1);
    BOOST_TEST(calls == 1);
    BOOST_CHECK_THROW(property->resolveValueType(runtime.getTypeByName("State")), NomadException);
    BOOST_REQUIRE(compileSources({}, *compiler).succeeded());
}

BOOST_AUTO_TEST_SUITE_END()
