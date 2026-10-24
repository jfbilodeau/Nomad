// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include "nomad/game/Entity.hpp"
#include "nomad/game/EntityVariableContext.hpp"
#include "nomad/game/Scene.hpp"
#include "nomad/script/Documentation.hpp"
#include <nomad/script/Hub.hpp>
#include "nomad/script/Runtime.hpp"
#include "nomad/script/Type.hpp"
#include "nomad/script/Variable.hpp"

#include <sstream>

using namespace nomad;

BOOST_AUTO_TEST_CASE(runtime_type_descriptors_report_their_owning_runtime) {
    Runtime runtime;
    BOOST_REQUIRE(runtime.createObjectType("AssociatedObject", ObjectTypeMode::Closed).isValid());
    BOOST_REQUIRE(runtime.createEnum("AssociatedEnum", {{"Ready", 1}}).isValid());
    BOOST_REQUIRE(runtime.registerOpaqueIdType("AssociatedHandle", "") != nullptr);
    BOOST_REQUIRE(runtime.registerEvent("AssociatedEvent", {}).isValid());
    BOOST_REQUIRE(runtime.getCallbackType({runtime.getIntegerType()}, runtime.getBooleanType()) != nullptr);
    BOOST_REQUIRE(runtime.getIteratorType(runtime.getIntegerType()) != nullptr);

    std::vector<const Type*> types;
    runtime.getTypes(types);
    for (const auto* type : types) {
        BOOST_TEST_CONTEXT(type->getTypeName()) {
            BOOST_TEST(type->getRuntime() == &runtime);
            BOOST_REQUIRE(type->getId().isValid());
            BOOST_TEST(runtime.getType(type->getId()).value() == type);
        }
    }
    BOOST_CHECK_THROW(static_cast<void>(IntegerType(nullptr, TypeId::INVALID)), NomadException);
    BOOST_CHECK_THROW(static_cast<void>(ObjectType(nullptr, TypeId::INVALID, "Invalid", ObjectTypeMode::Closed)), NomadException);

    Runtime other;
    auto descriptor = std::make_unique<ObjectType>(&runtime, TypeId::INVALID, "Unregistered", ObjectTypeMode::Closed);
    BOOST_TEST(descriptor->getRuntime() == &runtime);
    BOOST_TEST(runtime.getTypeId("Unregistered").isInvalid());
    BOOST_TEST(descriptor->getId().isInvalid());
    BOOST_TEST(other.getTypeId("Unregistered").isInvalid());
    BOOST_TEST(runtime.getTypeId("Unregistered").isInvalid());
}

BOOST_AUTO_TEST_CASE(runtime_specialized_creation_forwards_arguments_and_publishes_after_construction) {
    class CreationProbe final : public ObjectType {
    public:
        CreationProbe(
            Runtime* runtime,
            TypeId id,
            NomadString name,
            ObjectTypeMode mode,
            std::unique_ptr<int> value,
            int& constructions,
            bool& unpublished
        ):
            ObjectType(runtime, id, std::move(name), mode),
            m_value(std::move(value))
        {
            ++constructions;
            unpublished = runtime->getTypeId(getTypeName()).isInvalid() &&
                !runtime->getType(id).has_value();
        }

        [[nodiscard]] int getValue() const { return *m_value; }

    private:
        std::unique_ptr<int> m_value;
    };

    Runtime runtime;
    int constructions = 0;
    bool unpublished = false;
    const auto id = runtime.createObjectType<CreationProbe>(
        "CreationProbe", ObjectTypeMode::Opened, std::make_unique<int>(42), constructions, unpublished
    );
    BOOST_REQUIRE(id.isValid());
    const auto* type = dynamic_cast<const CreationProbe*>(runtime.getType(id).value());
    BOOST_REQUIRE(type != nullptr);
    BOOST_TEST(type->getRuntime() == &runtime);
    BOOST_TEST(type->getId() == id);
    BOOST_TEST(type->getValue() == 42);
    BOOST_TEST(type->isOpen());
    BOOST_TEST(unpublished);
    BOOST_TEST(constructions == 1);

    for (const auto& name : {"CreationProbe", ""}) {
        BOOST_TEST(runtime.createObjectType<CreationProbe>(
            name, ObjectTypeMode::Closed, std::make_unique<int>(1), constructions, unpublished
        ).isInvalid());
    }
    BOOST_TEST(runtime.createObjectType<CreationProbe>(
        "InvalidMode", static_cast<ObjectTypeMode>(0), std::make_unique<int>(1), constructions, unpublished
    ).isInvalid());
    BOOST_TEST(constructions == 1);
    BOOST_TEST(runtime.getType(id).value() == type);
}

BOOST_AUTO_TEST_CASE(runtime_type_ids_survive_nested_creation_and_failed_construction) {
    class NestedCreation final : public ObjectType {
    public:
        NestedCreation(
            Runtime* runtime,
            TypeId id,
            NomadString name,
            ObjectTypeMode mode,
            TypeId& observedId,
            TypeId& nestedId,
            bool fail
        ):
            ObjectType(runtime, id, std::move(name), mode)
        {
            observedId = getId();
            BOOST_REQUIRE(id.isValid());
            BOOST_TEST(!runtime->getType(id).has_value());
            nestedId = runtime->createObjectType(getTypeName() + "Child", ObjectTypeMode::Closed);
            BOOST_REQUIRE(nestedId.isValid());
            BOOST_TEST(nestedId != id);
            std::vector<const Type*> types;
            runtime->getTypes(types);
            for (const auto* type : types) {
                BOOST_REQUIRE(type != nullptr);
                BOOST_TEST(type->getId() != id);
            }
            if (fail) {
                throw NomadException("Expected construction failure");
            }
        }
    };

    Runtime runtime;
    TypeId observedId;
    TypeId nestedId;
    const auto id = runtime.createObjectType<NestedCreation>(
        "Nested", ObjectTypeMode::Closed, observedId, nestedId, false
    );
    BOOST_REQUIRE(id.isValid());
    BOOST_TEST(id == observedId);
    BOOST_TEST(runtime.getType(id).value()->getId() == id);
    const auto* nested = runtime.getType(nestedId).value();
    BOOST_TEST(nested->getId() == nestedId);

    BOOST_CHECK_THROW(static_cast<void>(runtime.createObjectType<NestedCreation>(
        "Failed", ObjectTypeMode::Closed, observedId, nestedId, true
    )), NomadException);
    const auto failedId = observedId;
    BOOST_TEST(!runtime.getType(failedId).has_value());
    BOOST_TEST(runtime.getTypeId("Failed").isInvalid());
    BOOST_TEST(runtime.getType(nestedId).value()->getId() == nestedId);
    const auto subsequentId = runtime.createObjectType("Subsequent", ObjectTypeMode::Closed);
    BOOST_REQUIRE(subsequentId.isValid());
    BOOST_TEST(subsequentId != failedId);
    BOOST_TEST(runtime.getType(id).value()->getId() == id);
    BOOST_TEST(runtime.getType(nested->getId()).value() == nested);

    std::vector<const Type*> types;
    runtime.getTypes(types);
    for (const auto* type : types) {
        BOOST_REQUIRE(type != nullptr);
        BOOST_TEST(type->getId() != failedId);
        BOOST_TEST(runtime.getType(type->getId()).value() == type);
    }
    BOOST_REQUIRE(runtime.getCallbackType({}, runtime.getVoidType()) != nullptr);
    BOOST_REQUIRE(runtime.getIteratorType(runtime.getIntegerType()) != nullptr);
}

BOOST_AUTO_TEST_CASE(runtime_rejects_descriptors_with_forged_registered_ids) {
    Runtime runtime;
    Runtime other;
    const auto id = runtime.createObjectType("ActualOwner", ObjectTypeMode::Closed);
    BOOST_REQUIRE(id.isValid());
    const auto* actual = runtime.getType(id).value()->asObjectType();
    ObjectType impostor(&runtime, id, "ActualOwner", ObjectTypeMode::Closed);
    ObjectType foreign(&other, id, "ActualOwner", ObjectTypeMode::Closed);
    for (const auto* candidate : {&impostor, &foreign}) {
        BOOST_TEST(runtime.registerProperty(candidate, "value", runtime.getIntegerType()).isInvalid());
        BOOST_TEST(candidate->getProperties().empty());
    }
    BOOST_TEST(runtime.getType(id).value() == actual);
    BOOST_TEST(actual->getProperties().empty());
}

BOOST_AUTO_TEST_CASE(runtime_parameterized_factories_preserve_reservations_and_reject_wrong_ids) {
    Runtime runtime;
    TypeId reservedId;
    TypeId nestedId;
    auto validId = false;
    auto initializerCalls = 0;
    BOOST_REQUIRE(runtime.registerParameterizedType(
        "IdentityBox", 1,
        [&](Runtime* owner, TypeId id, const std::vector<const Type*>& arguments) {
            reservedId = id;
            BOOST_TEST(!owner->getType(id).has_value());
            if (nestedId.isInvalid()) {
                nestedId = owner->createObjectType("FactoryChild", ObjectTypeMode::Closed);
                BOOST_REQUIRE(nestedId.isValid());
            }
            return std::make_unique<ObjectType>(
                owner, validId ? id : nestedId, "IdentityBox<int>", ObjectTypeMode::Closed, arguments
            );
        },
        [&](Runtime* owner, TypeId id) {
            ++initializerCalls;
            BOOST_TEST(id == reservedId);
            BOOST_TEST(owner->getType(id).value()->getId() == id);
        }
    ));
    BOOST_TEST(runtime.getParameterizedType("IdentityBox", {runtime.getIntegerType()}) == nullptr);
    const auto rejectedId = reservedId;
    BOOST_TEST(!runtime.getType(rejectedId).has_value());
    BOOST_TEST(initializerCalls == 0);
    validId = true;
    const auto* type = runtime.getParameterizedType("IdentityBox", {runtime.getIntegerType()});
    BOOST_REQUIRE(type != nullptr);
    BOOST_TEST(type->getId() == reservedId);
    BOOST_TEST(type->getId() != rejectedId);
    BOOST_TEST(type->getId() != nestedId);
    BOOST_TEST(runtime.getParameterizedType("IdentityBox", {runtime.getIntegerType()}) == type);
    BOOST_TEST(initializerCalls == 1);
}

BOOST_AUTO_TEST_CASE(runtime_callback_signatures_have_distinct_stable_type_ids) {
    Runtime runtime;
    const auto* voidCallback = runtime.getCallbackType({}, runtime.getVoidType());
    const auto* booleanCallback = runtime.getCallbackType({}, runtime.getBooleanType());
    const auto* parameterCallback = runtime.getCallbackType(
        {runtime.getIntegerType()}, runtime.getVoidType()
    );
    BOOST_TEST(voidCallback->getTypeName() == booleanCallback->getTypeName());
    BOOST_TEST(voidCallback->getId() != booleanCallback->getId());
    BOOST_TEST(voidCallback->getId() != parameterCallback->getId());
    BOOST_TEST(booleanCallback->getId() != parameterCallback->getId());
    BOOST_TEST(runtime.getType(voidCallback->getId()).value() == voidCallback);
    BOOST_TEST(runtime.getType(booleanCallback->getId()).value() == booleanCallback);
    BOOST_TEST(runtime.getType(parameterCallback->getId()).value() == parameterCallback);
    BOOST_TEST(runtime.getCallbackType({}, runtime.getVoidType()) == voidCallback);
    BOOST_TEST(runtime.getCallbackType({}, runtime.getBooleanType()) == booleanCallback);
    BOOST_TEST(runtime.getCallbackType({runtime.getIntegerType()}, runtime.getVoidType()) == parameterCallback);
}

BOOST_AUTO_TEST_CASE(runtime_parameterized_callbacks_receive_the_owner_and_registered_specialization_id) {
    Runtime runtime;
    int factoryCalls = 0;
    int initializerCalls = 0;
    TypeId initializedId = TypeId::INVALID;
    BOOST_REQUIRE(runtime.registerParameterizedType(
        "AssociatedBox", 1,
        [&](Runtime* owningRuntime, TypeId id, const std::vector<const Type*>& arguments) {
            ++factoryCalls;
            BOOST_TEST(owningRuntime == &runtime);
            BOOST_REQUIRE(arguments.size() == 1u);
            BOOST_TEST(arguments[0] == runtime.getIntegerType());
            BOOST_TEST(owningRuntime->getTypeId("AssociatedBox<int>").isInvalid());
            return std::make_unique<ObjectType>(
                owningRuntime, id, "AssociatedBox<int>", ObjectTypeMode::Closed, arguments
            );
        },
        [&](Runtime* owningRuntime, const TypeId id) {
            ++initializerCalls;
            initializedId = id;
            BOOST_TEST(owningRuntime == &runtime);
            BOOST_REQUIRE(id.isValid());
            const auto type = owningRuntime->getType(id);
            BOOST_REQUIRE(type.has_value());
            BOOST_TEST((*type)->getRuntime() == owningRuntime);
            BOOST_TEST(owningRuntime->getTypeByName("AssociatedBox<int>") == *type);
            const auto* objectType = static_cast<const ObjectType*>(*type);
            BOOST_REQUIRE(owningRuntime->registerProperty(objectType, "value", runtime.getIntegerType()).isValid());
        }
    ));

    const auto* type = runtime.getParameterizedType("AssociatedBox", {runtime.getIntegerType()});
    BOOST_REQUIRE(type != nullptr);
    BOOST_TEST(type->getRuntime() == &runtime);
    BOOST_TEST(initializedId == runtime.getTypeId("AssociatedBox<int>"));
    BOOST_TEST(static_cast<const ObjectType*>(type)->getProperty("value") != nullptr);
    BOOST_TEST(runtime.getParameterizedType("AssociatedBox", {runtime.getIntegerType()}) == type);
    BOOST_TEST(factoryCalls == 1);
    BOOST_TEST(initializerCalls == 1);
}

BOOST_AUTO_TEST_CASE(runtime_rejects_foreign_parameterized_descriptors_without_initializing_or_caching_them) {
    Runtime runtime;
    Runtime other;
    int factoryCalls = 0;
    int initializerCalls = 0;
    BOOST_REQUIRE(runtime.registerParameterizedType(
        "ForeignBox", 1,
        [&](Runtime* owningRuntime, TypeId id, const std::vector<const Type*>&) {
            ++factoryCalls;
            BOOST_TEST(owningRuntime == &runtime);
            return std::make_unique<ObjectType>(&other, TypeId::INVALID, "ForeignBox<int>", ObjectTypeMode::Closed);
        },
        [&](Runtime*, TypeId) { ++initializerCalls; }
    ));

    std::vector<const Type*> originalTypes;
    runtime.getTypes(originalTypes);
    BOOST_TEST(runtime.getParameterizedType("ForeignBox", {runtime.getIntegerType()}) == nullptr);
    BOOST_TEST(runtime.getParameterizedType("ForeignBox", {runtime.getIntegerType()}) == nullptr);
    BOOST_TEST(factoryCalls == 2);
    BOOST_TEST(initializerCalls == 0);
    BOOST_TEST(runtime.getTypeId("ForeignBox<int>").isInvalid());
    BOOST_TEST(other.getTypeId("ForeignBox<int>").isInvalid());
    std::vector<const Type*> typesAfterRejection;
    runtime.getTypes(typesAfterRejection);
    BOOST_TEST(typesAfterRejection == originalTypes, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(bare_runtime_has_core_string_natives_without_standard_native_functions)
{
    Runtime runtime;

    for (const auto* name : {"toFloat", "toInt", "log.info", "math.floor"}) {
        BOOST_TEST_CONTEXT(name) {
            BOOST_TEST(runtime.getNativeFunctionId(name).isInvalid());
            BOOST_TEST(runtime.getCallableId(name).isInvalid());
        }
    }
    BOOST_TEST((runtime.getMemberType(runtime.getNativeMemberId(runtime.getStringType(), "length")) == MemberType::Property));
    BOOST_TEST((runtime.getMemberType(runtime.getNativeMemberId(runtime.getStringType(), "toUpper")) == MemberType::Method));
    BOOST_TEST((runtime.getMemberType(runtime.getNativeMemberId(runtime.getStringType(), "contains")) == MemberType::Method));
}

BOOST_AUTO_TEST_CASE(runtime_owns_registered_functions_and_exposes_observer_views)
{
    {
        Runtime runtime;

        const auto firstFunctionId = runtime.registerScriptFunction("first", "first.nomad", "");
        const auto secondFunctionId = runtime.registerScriptFunction("second", "second.nomad", "");

        BOOST_TEST(firstFunctionId.isValid());
        BOOST_TEST(secondFunctionId.id == firstFunctionId.id + 1);
        BOOST_TEST(runtime.getFunctionCount() == 2U);
        BOOST_TEST(runtime.getScriptFunction(firstFunctionId) != nullptr);
        BOOST_TEST(runtime.getScriptFunction(secondFunctionId) != nullptr);
        BOOST_TEST((runtime.getFunctionId("first") == firstFunctionId));
        BOOST_TEST(runtime.getFunctionName(secondFunctionId).value() == "second");

        std::vector<ScriptFunction*> functions;
        runtime.getFunctions(functions);

        BOOST_TEST(functions.size() == 2U);
        BOOST_TEST(functions[0] == runtime.getScriptFunction(firstFunctionId));
        BOOST_TEST(functions[1] == runtime.getScriptFunction(secondFunctionId));
    }
}

BOOST_AUTO_TEST_CASE(runtime_rejects_invalid_and_duplicate_function_ids)
{
    Runtime runtime;

    const auto functionId = runtime.registerScriptFunction("function", "function.nomad", "");

    BOOST_TEST(functionId.isValid());
    BOOST_TEST(runtime.registerScriptFunction("function", "duplicate.nomad", "").isInvalid());
    BOOST_TEST(runtime.getFunctionCount() == 1U);
    BOOST_TEST(runtime.getScriptFunction(CallableId::INVALID) == nullptr);
    BOOST_TEST(runtime.getScriptFunction(CallableId{-2}) == nullptr);
    BOOST_TEST(runtime.getScriptFunction(CallableId{1}) == nullptr);
    BOOST_TEST(!runtime.getFunctionName(CallableId::INVALID).has_value());
    BOOST_TEST(!runtime.getFunctionName(CallableId{-2}).has_value());
    BOOST_TEST(!runtime.getFunctionName(CallableId{1}).has_value());
}

BOOST_AUTO_TEST_CASE(runtime_source_lookup_ignores_unknown_ranges_and_prefers_inner_functions)
{
    Runtime runtime;
    const auto outerId = runtime.registerScriptFunction("outer", "source.nomad", "");
    const auto innerId = runtime.registerScriptFunction("inner", "source.nomad", "");
    const auto unparsedId = runtime.registerScriptFunction("unparsed", "source.nomad", "");
    const auto* unparsed = runtime.getScriptFunction(unparsedId);

    BOOST_TEST(unparsed->getSourceStartLine() == NOMAD_INVALID_INDEX);
    BOOST_TEST(unparsed->getSourceEndLine() == NOMAD_INVALID_INDEX);
    BOOST_TEST(runtime.getFunctionAtLine("source.nomad", 1) == nullptr);
    BOOST_TEST(runtime.getFunctionAtLine("source.nomad", NOMAD_INVALID_INDEX) == nullptr);

    auto* outer = runtime.getScriptFunction(outerId);
    auto* inner = runtime.getScriptFunction(innerId);
    outer->setSourceLineRange(1, 4);
    inner->setSourceLineRange(2, 3);
    inner->setParentId(outerId);
    BOOST_TEST(runtime.getFunctionAtLine("source.nomad", 1) == outer);
    BOOST_TEST(runtime.getFunctionAtLine("source.nomad", 2) == inner);
    BOOST_TEST(runtime.getFunctionAtLine("source.nomad", 3) == inner);
    BOOST_TEST(runtime.getFunctionAtLine("source.nomad", 4) == outer);
    BOOST_TEST(runtime.getFunctionAtLine("source.nomad", 5) == nullptr);
    BOOST_TEST(runtime.getFunctionAtLine("other.nomad", 2) == nullptr);

    inner->setSourceLineRange(1, 4);
    BOOST_TEST(runtime.getFunctionAtLine("source.nomad", 2) == inner);
}

BOOST_AUTO_TEST_CASE(runtime_creates_object_types_with_explicit_modes_and_owned_documentation) {
    Runtime runtime;
    NomadString name = "OpenedObject";
    NomadString documentation = "An opened object.";
    const auto openedId = runtime.createObjectType(name, ObjectTypeMode::Opened, documentation);
    const auto closedId = runtime.createObjectType("ClosedObject", ObjectTypeMode::Closed);
    BOOST_REQUIRE(openedId.isValid());
    BOOST_REQUIRE(closedId.isValid());
    BOOST_TEST(openedId != closedId);
    const auto openedType = runtime.getType(openedId);
    const auto closedType = runtime.getType(closedId);
    BOOST_REQUIRE(openedType.has_value());
    BOOST_REQUIRE(closedType.has_value());
    const auto* opened = static_cast<const ObjectType*>(*openedType);
    const auto* closed = static_cast<const ObjectType*>(*closedType);
    name = "Changed";
    documentation.clear();
    BOOST_TEST(opened->getTypeName() == "OpenedObject");
    BOOST_TEST(opened->getDocumentation() == "An opened object.");
    BOOST_TEST(opened->isOpen());
    BOOST_TEST(!closed->isOpen());
    BOOST_TEST(closed->getDocumentation().empty());
    BOOST_TEST(opened->getTypeArguments().empty());
    BOOST_TEST((opened->getStorage() == TypeStorage::Object));
    BOOST_TEST(runtime.getTypeId("OpenedObject") == openedId);
    BOOST_TEST(runtime.getTypeByName("ClosedObject") == closed);
    BOOST_TEST(opened->toString(RuntimeValue{}) == "OpenedObject");

    for (auto index = 0; index < 64; ++index) {
        BOOST_REQUIRE(runtime.createObjectType("AdditionalObject" + std::to_string(index), ObjectTypeMode::Closed).isValid());
    }
    BOOST_TEST(runtime.getType(openedId).value() == opened);
    BOOST_TEST(runtime.getType(closedId).value() == closed);
    BOOST_TEST(runtime.getTypeId("OpenedObject") == openedId);
    BOOST_TEST(opened->isOpen());
}

BOOST_AUTO_TEST_CASE(runtime_rejects_invalid_and_duplicate_object_creation_without_replacing_types) {
    Runtime runtime;
    const auto id = runtime.createObjectType("Object", ObjectTypeMode::Opened, "Original documentation.");
    const auto enumId = runtime.createEnum("State", {{"Ready", 1}});
    BOOST_REQUIRE(id.isValid());
    BOOST_REQUIRE(enumId.isValid());
    const auto* original = static_cast<const ObjectType*>(runtime.getType(id).value());
    std::vector<const Type*> originalTypes;
    runtime.getTypes(originalTypes);

    BOOST_TEST(runtime.createObjectType("", ObjectTypeMode::Closed).isInvalid());
    BOOST_TEST(runtime.createObjectType("InvalidMode", static_cast<ObjectTypeMode>(0)).isInvalid());
    BOOST_TEST(runtime.createObjectType("InvalidMode", static_cast<ObjectTypeMode>(99)).isInvalid());
    BOOST_TEST(runtime.createObjectType("int", ObjectTypeMode::Closed).isInvalid());
    BOOST_TEST(runtime.createObjectType("State", ObjectTypeMode::Opened).isInvalid());
    BOOST_TEST(runtime.createObjectType("Object", ObjectTypeMode::Opened, "Original documentation.").isInvalid());
    BOOST_TEST(runtime.createObjectType("Object", ObjectTypeMode::Closed, "Replacement documentation.").isInvalid());
    BOOST_TEST(runtime.createEnum("Object", {{"Ready", 1}}).isInvalid());

    std::vector<const Type*> typesAfterRejection;
    runtime.getTypes(typesAfterRejection);
    BOOST_TEST(typesAfterRejection == originalTypes, boost::test_tools::per_element());
    BOOST_TEST(runtime.getTypeId("InvalidMode").isInvalid());
    BOOST_TEST(runtime.getTypeId("Object") == id);
    BOOST_TEST(runtime.getTypeId("State") == enumId);
    BOOST_TEST(runtime.getType(id).value() == original);
    BOOST_TEST(original->getDocumentation() == "Original documentation.");
    BOOST_TEST(original->isOpen());
    BOOST_TEST(runtime.getConstantId("Object.Ready").isInvalid());
}

BOOST_AUTO_TEST_CASE(runtime_creates_object_hub_members_and_rejects_conflicting_names) {
    Runtime runtime;
    const auto hubId = runtime.registerHub("example");
    BOOST_REQUIRE(hubId.isValid());
    const auto variableId = runtime.registerHubVariable("example.current");
    const auto constantId = runtime.registerConstant(
        "example.answer", RuntimeValue{NomadInteger{42}}, runtime.getIntegerType()
    );
    const auto functionId = runtime.registerNativeFunction(
        "example.read", [](VirtualMachine*) {}, {}, runtime.getVoidType(), ""
    );
    BOOST_REQUIRE(variableId.isValid());
    BOOST_REQUIRE(constantId.isValid());
    BOOST_REQUIRE(functionId.isValid());
    const auto id = runtime.createObjectType("example.Object", ObjectTypeMode::Closed);
    BOOST_REQUIRE(id.isValid());
    const auto* type = runtime.getType(id).value();
    const auto* hub = runtime.getHub(hubId);
    const auto* member = hub->getMember("Object");
    BOOST_REQUIRE(member != nullptr);
    const auto* memberType = std::get_if<const Type*>(member);
    BOOST_REQUIRE(memberType != nullptr);
    BOOST_TEST(*memberType == type);
    std::vector<const Type*> originalTypes;
    runtime.getTypes(originalTypes);

    for (const auto* name : {"example", "example.current", "example.answer", "example.read"}) {
        BOOST_TEST_CONTEXT(name) {
            BOOST_TEST(runtime.createObjectType(name, ObjectTypeMode::Opened).isInvalid());
            BOOST_TEST(runtime.getTypeId(name).isInvalid());
        }
    }
    std::vector<const Type*> typesAfterRejection;
    runtime.getTypes(typesAfterRejection);
    BOOST_TEST(typesAfterRejection == originalTypes, boost::test_tools::per_element());
    BOOST_TEST(runtime.getHubId("example") == hubId);
    BOOST_TEST(runtime.isHubVariable("example.current"));
    BOOST_TEST(runtime.getConstantId("example.answer") == constantId);
    BOOST_TEST(runtime.getCallableId("example.read") == functionId);
}

BOOST_AUTO_TEST_CASE(runtime_creates_strongly_typed_enums_with_explicit_values)
{
    Runtime runtime;

    NomadString enumName = "body";
    std::vector<EnumValueDefinition> values{
        {"static", 3},
        {"dynamic", 7},
        {"kinematic", 11},
    };
    NomadString documentation = "Physics body modes.";
    const auto bodyId = runtime.createEnum(enumName, values, documentation);
    BOOST_REQUIRE(bodyId.isValid());
    const auto registeredType = runtime.getType(bodyId);
    BOOST_REQUIRE(registeredType.has_value());
    const auto* bodyType = (*registeredType)->asEnum();
    enumName = "changed";
    values[0].name = "changed";
    values[0].value = 99;
    documentation.clear();

    BOOST_REQUIRE(bodyType != nullptr);
    BOOST_TEST(runtime.getTypeId("body") == bodyId);
    BOOST_TEST(runtime.getTypeByName("body") == bodyType);
    BOOST_TEST(bodyType->getDocumentation() == "Physics body modes.");
    BOOST_TEST(bodyType->getValueCount() == 3U);
    BOOST_TEST(bodyType->getValue(0).name == "static");
    BOOST_TEST(bodyType->getValue(0).value == 3);
    BOOST_TEST(bodyType->toString(RuntimeValue{NomadInteger{7}}) == "dynamic");
    BOOST_TEST(bodyType->toString(RuntimeValue{NomadInteger{99}}) == "<invalid body value>");

    NomadInteger value = 0;
    BOOST_TEST(bodyType->getValueByName("kinematic", value));
    BOOST_TEST(value == 11);
    BOOST_TEST(!bodyType->getValueByName("missing", value));

    NomadString name;
    BOOST_TEST(bodyType->getNameByValue(3, name));
    BOOST_TEST(name == "static");
    BOOST_TEST(!bodyType->getNameByValue(99, name));

    const auto staticId = runtime.getConstantId("body.static");
    BOOST_REQUIRE(staticId.isValid());
    BOOST_TEST(runtime.getConstantType(staticId) == bodyType);

    RuntimeValue staticValue;
    runtime.getConstantValue(staticId, staticValue);
    BOOST_TEST(staticValue.getIntegerValue() == 3);
}

BOOST_AUTO_TEST_CASE(runtime_enum_types_only_support_same_type_equality)
{
    Runtime runtime;
    const auto bodyId = runtime.createEnum("body", {{"static", 1}, {"dynamic", 2}});
    const auto cardinalId = runtime.createEnum("cardinal", {{"north", 1}, {"south", 2}});
    BOOST_REQUIRE(bodyId.isValid());
    BOOST_REQUIRE(cardinalId.isValid());
    const auto* bodyType = runtime.getType(bodyId).value()->asEnum();
    const auto* cardinalType = runtime.getType(cardinalId).value()->asEnum();

    BOOST_REQUIRE(bodyType != nullptr);
    BOOST_REQUIRE(cardinalType != nullptr);
    BOOST_TEST(bodyType->getDocumentation().empty());
    BOOST_TEST(!bodyType->sameType(cardinalType));
    BOOST_TEST(!bodyType->sameType(runtime.getIntegerType()));
    BOOST_TEST(
        runtime.getBinaryOperatorResultType(BinaryOperator::EqualEqual, bodyType, bodyType) ==
        runtime.getBooleanType()
    );
    BOOST_TEST(
        runtime.getBinaryOperatorResultType(BinaryOperator::BangEqual, bodyType, bodyType) ==
        runtime.getBooleanType()
    );
    BOOST_TEST(runtime.getBinaryOperatorResultType(BinaryOperator::EqualEqual, bodyType, cardinalType) == nullptr);
    BOOST_TEST(
        runtime.getBinaryOperatorResultType(BinaryOperator::EqualEqual, bodyType, runtime.getIntegerType()) == nullptr
    );
    BOOST_TEST(runtime.getBinaryOperatorResultType(BinaryOperator::Plus, bodyType, bodyType) == nullptr);

    RuntimeValue result;
    BOOST_TEST(
        runtime.foldBinary(
            BinaryOperator::EqualEqual,
            bodyType,
            RuntimeValue{NomadInteger{2}},
            bodyType,
            RuntimeValue{NomadInteger{2}},
            result
        )
    );
    BOOST_TEST(result.getBooleanValue());
}

BOOST_AUTO_TEST_CASE(runtime_rejects_invalid_enum_definitions_without_partial_registration)
{
    Runtime runtime;

    std::vector<const Type*> originalTypes;
    runtime.getTypes(originalTypes);
    const auto integerId = runtime.getTypeId("int");
    BOOST_TEST(runtime.createEnum("", {{"value", 1}}).isInvalid());
    BOOST_TEST(runtime.createEnum("empty", {}).isInvalid());
    BOOST_TEST(runtime.createEnum("unnamed", {{"", 1}}).isInvalid());
    BOOST_TEST(runtime.createEnum("duplicateName", {{"value", 1}, {"value", 2}}).isInvalid());
    BOOST_TEST(runtime.createEnum("duplicateValue", {{"first", 1}, {"second", 1}}).isInvalid());
    BOOST_TEST(runtime.createEnum("int", {{"value", 1}}).isInvalid());
    std::vector<const Type*> typesAfterRejection;
    runtime.getTypes(typesAfterRejection);
    BOOST_TEST(typesAfterRejection == originalTypes, boost::test_tools::per_element());
    BOOST_TEST(runtime.getTypeId("int") == integerId);
    BOOST_TEST(runtime.getConstantId("int.value").isInvalid());
    BOOST_TEST(runtime.getConstantId("duplicateValue.first").isInvalid());

    const auto validId = runtime.createEnum("valid", {{"first", 1}}, "Original documentation.");
    BOOST_REQUIRE(validId.isValid());
    const auto* originalType = runtime.getType(validId).value();
    const auto firstId = runtime.getConstantId("valid.first");
    BOOST_REQUIRE(firstId.isValid());
    BOOST_TEST(runtime.createEnum("valid", {{"first", 1}}, "Original documentation.").isInvalid());
    BOOST_TEST(runtime.createEnum("valid", {{"second", 2}}, "Replacement documentation.").isInvalid());
    BOOST_TEST(runtime.getTypeId("valid") == validId);
    BOOST_TEST(runtime.getType(validId).value() == originalType);
    BOOST_TEST(originalType->getDocumentation() == "Original documentation.");
    BOOST_TEST(runtime.getConstantId("valid.first") == firstId);
    BOOST_TEST(runtime.getConstantType(firstId) == originalType);
    RuntimeValue firstValue;
    runtime.getConstantValue(firstId, firstValue);
    BOOST_TEST(firstValue.getIntegerValue() == 1);
    BOOST_TEST(runtime.getConstantId("valid.second").isInvalid());
    BOOST_TEST(runtime.getTypeByName("duplicateName") == nullptr);
    BOOST_TEST(runtime.getConstantId("duplicateName.value").isInvalid());
}

BOOST_AUTO_TEST_CASE(runtime_enum_creation_rejects_hub_conflicts_before_publishing_values) {
    Runtime runtime;
    BOOST_REQUIRE(runtime.registerHub("example").isValid());
    const auto constantId = runtime.registerConstant(
        "example.State", RuntimeValue{NomadInteger{42}}, runtime.getIntegerType()
    );
    BOOST_REQUIRE(constantId.isValid());
    const auto valueId = runtime.registerConstant(
        "example.ConstantConflict.Reserved", RuntimeValue{NomadInteger{7}}, runtime.getIntegerType()
    );
    BOOST_REQUIRE(valueId.isValid());
    BOOST_REQUIRE(runtime.registerHub("example.HubConflict.Reserved").isValid());
    const auto functionId = runtime.registerNativeFunction(
        "example.FunctionConflict.Reserved", [](VirtualMachine*) {}, {}, runtime.getVoidType(), ""
    );
    BOOST_REQUIRE(functionId.isValid());
    std::vector<const Type*> originalTypes;
    runtime.getTypes(originalTypes);

    BOOST_TEST(runtime.createEnum("example", {{"Ready", 1}}).isInvalid());
    BOOST_TEST(runtime.createEnum("example.State", {{"Ready", 1}}).isInvalid());
    for (const auto* name : {"example.ConstantConflict", "example.HubConflict", "example.FunctionConflict"}) {
        BOOST_TEST_CONTEXT(name) {
            BOOST_TEST(runtime.createEnum(name, {{"First", 1}, {"Reserved", 2}}).isInvalid());
            BOOST_TEST(runtime.getTypeId(name).isInvalid());
            BOOST_TEST(runtime.getConstantId(NomadString{name} + ".First").isInvalid());
        }
    }
    std::vector<const Type*> typesAfterRejection;
    runtime.getTypes(typesAfterRejection);
    BOOST_TEST(typesAfterRejection == originalTypes, boost::test_tools::per_element());
    BOOST_TEST(runtime.getTypeId("example.State").isInvalid());
    BOOST_TEST(runtime.getConstantId("example.State.Ready").isInvalid());
    BOOST_TEST(runtime.getConstantId("example.State") == constantId);
    BOOST_TEST(runtime.getConstantId("example.ConstantConflict.Reserved") == valueId);
    BOOST_TEST(runtime.getCallableId("example.FunctionConflict.Reserved") == functionId);
    BOOST_TEST(runtime.getHubId("example.HubConflict.Reserved").isValid());
}

BOOST_AUTO_TEST_CASE(runtime_registers_nominal_opaque_id_types)
{
    Runtime runtime;

    const auto* soundType = runtime.registerOpaqueIdType("sound", NomadDoc("A loaded sound effect."));
    const auto* musicType = runtime.registerOpaqueIdType("music", NomadDoc("A music stream."));

    BOOST_REQUIRE(soundType != nullptr);
    BOOST_REQUIRE(musicType != nullptr);
    BOOST_TEST(runtime.getTypeByName("sound") == soundType);
    BOOST_TEST(runtime.getTypeByName("music") == musicType);
    BOOST_TEST(soundType->getDocumentation() == "A loaded sound effect.");
    BOOST_TEST(!soundType->sameType(musicType));
    BOOST_TEST(!soundType->sameType(runtime.getTypeByName("id")));

    RuntimeValue source;
    source.setIdValue(42);
    RuntimeValue destination;
    soundType->copyValue(source, destination);

    BOOST_TEST(destination.getIdValue() == 42);
    BOOST_TEST(soundType->toString(destination) == "sound(42)");
}

BOOST_AUTO_TEST_CASE(runtime_rejects_invalid_opaque_id_types)
{
    Runtime runtime;

    BOOST_TEST(runtime.registerOpaqueIdType("", NomadDoc("Invalid.")) == nullptr);
    BOOST_TEST(runtime.registerOpaqueIdType("int", NomadDoc("Duplicate.")) == nullptr);
    BOOST_REQUIRE(runtime.registerOpaqueIdType("resource", NomadDoc("A resource.")) != nullptr);
    BOOST_TEST(runtime.registerOpaqueIdType("resource", NomadDoc("Duplicate.")) == nullptr);
}

BOOST_AUTO_TEST_CASE(documentation_includes_registered_type_descriptions)
{
    Runtime runtime;
    BOOST_REQUIRE(
        runtime.registerOpaqueIdType("resource", NomadDoc("An opaque resource handle.")) != nullptr
    );
    BOOST_REQUIRE(runtime.registerOpaqueIdType("undocumented", NomadDoc("")) != nullptr);

    std::ostringstream output;
    generateTypeDocumentation(&runtime, output);

    const auto documentation = output.str();
    BOOST_TEST(documentation.find("## Types") != NomadString::npos);
    BOOST_TEST(documentation.find("### `resource`") != NomadString::npos);
    BOOST_TEST(documentation.find("An opaque resource handle.") != NomadString::npos);
    BOOST_TEST(documentation.find("undocumented") == NomadString::npos);
}

BOOST_AUTO_TEST_CASE(documentation_renders_function_parameters_as_a_markdown_list)
{
    Runtime runtime;
    const auto* fontType = runtime.registerOpaqueIdType(
        "font",
        NomadDoc("A loaded font at a specific point size.")
    );
    BOOST_REQUIRE(fontType != nullptr);

    runtime.registerNativeFunction(
        "game.loadFont",
        [](VirtualMachine*) {},
        {
            defParameter("fontName", runtime.getStringType(), NomadParamDoc("Name of the font to load.")),
            defParameter("fontSize", runtime.getIntegerType(), NomadParamDoc("Size of the font in point to load.")),
        },
        fontType,
        NomadDoc("Loads a font from a file and returns a font handle.")
    );

    std::ostringstream output;
    generateDocumentation(&runtime, output);
    std::ostringstream grammar;
    generateTextMateGrammar(&runtime, grammar);

    const auto documentation = output.str();
    BOOST_TEST(
        documentation.find(
            "parameters:\n\n"
            "- `fontName:string` - Name of the font to load.\n"
            "- `fontSize:int` - Size of the font in point to load.\n"
        ) != NomadString::npos
    );
    BOOST_TEST(documentation.find("`returns font`") != NomadString::npos);

    const auto grammarText = grammar.str();
    const auto typeScopePosition = grammarText.find("\"storage.type.nomad\"");
    const auto typePatternEndPosition = grammarText.find(")\\\\b", typeScopePosition);
    const auto fontTypePosition = grammarText.find("font", typeScopePosition);
    BOOST_REQUIRE(typeScopePosition != NomadString::npos);
    BOOST_REQUIRE(typePatternEndPosition != NomadString::npos);
    BOOST_TEST(fontTypePosition < typePatternEndPosition);
}

BOOST_AUTO_TEST_CASE(id_validation_helpers_distinguish_values_from_ranges)
{
    BOOST_TEST(isValidId(NOMAD_ID_MIN));
    BOOST_TEST(isValidId(NOMAD_ID_MAX - 1));
    BOOST_TEST(isInvalidId(NOMAD_INVALID_ID));
    BOOST_TEST(isInvalidId(-2));
    BOOST_TEST(isInvalidId(NOMAD_ID_MAX));

    BOOST_TEST(isIdInRange(0, 1U));
    BOOST_TEST(!isIdInRange(1, 1U));
    BOOST_TEST(!isIdInRange(NOMAD_INVALID_ID, 1U));
    BOOST_TEST(!isIdInRange(NOMAD_ID_MAX, static_cast<NomadIndex>(NOMAD_ID_MAX + 1)));
    BOOST_TEST(!isIdOutOfRange(0, 1U));
    BOOST_TEST(isIdOutOfRange(1, 1U));
    BOOST_TEST(isIdOutOfRange(NOMAD_INVALID_ID, 1U));
}

BOOST_AUTO_TEST_CASE(runtime_context_variable_ids_are_global_and_stable)
{
    Runtime runtime;

    auto firstContext = std::make_unique<SimpleVariableContext>();
    firstContext->registerVariable("first.value", runtime.getIntegerType());
    const auto firstContextId = runtime.registerVariableContext("first", "first.", std::move(firstContext));

    auto secondContext = std::make_unique<SimpleVariableContext>();
    secondContext->registerVariable("second.value", runtime.getIntegerType());
    const auto secondContextId = runtime.registerVariableContext("second", "second.", std::move(secondContext));

    BOOST_REQUIRE(firstContextId.isValid());
    BOOST_REQUIRE(secondContextId.isValid());

    const auto firstVariableId = runtime.registerContextVariable(
        firstContextId,
        "first.value",
        runtime.getIntegerType()
    );
    const auto secondVariableId = runtime.registerContextVariable(
        secondContextId,
        "second.value",
        runtime.getIntegerType()
    );

    BOOST_REQUIRE(firstVariableId.isValid());
    BOOST_REQUIRE(secondVariableId.isValid());
    BOOST_TEST(firstVariableId != secondVariableId);
    BOOST_TEST(runtime.getContextVariableId("first.value") == firstVariableId);
    BOOST_TEST(runtime.getContextVariableName(firstVariableId) == "first.value");
    BOOST_TEST(runtime.getContextVariableName(secondVariableId) == "second.value");
    BOOST_TEST(runtime.getContextVariableType(firstVariableId) == runtime.getIntegerType());
    BOOST_TEST(runtime.getContextVariableType(secondVariableId) == runtime.getIntegerType());

    runtime.setContextVariableValue(firstVariableId, RuntimeValue{NomadInteger{17}});
    runtime.setContextVariableValue(secondVariableId, RuntimeValue{NomadInteger{29}});

    RuntimeValue firstValue;
    RuntimeValue secondValue;
    runtime.getContextVariableValue(firstVariableId, firstValue);
    runtime.getContextVariableValue(secondVariableId, secondValue);

    BOOST_TEST(firstValue.getIntegerValue() == 17);
    BOOST_TEST(secondValue.getIntegerValue() == 29);
}

BOOST_AUTO_TEST_CASE(runtime_stores_character_pointer_as_string_context_variable)
{
    Runtime runtime;

    const auto contextId = runtime.registerVariableContext("inventory", "inventory.", std::make_unique<SimpleVariableContext>());
    const auto variableId = runtime.registerContextVariable(contextId, "inventory.name", runtime.getStringType());

    const NomadChar* name = "Nomad";
    runtime.setStringContextVariableValue(variableId, name);

    NomadString value;
    runtime.getStringContextVariableValue(variableId, value);

    BOOST_TEST(value == "Nomad");
}

BOOST_AUTO_TEST_CASE(runtime_assigns_distinct_global_ids_to_mirrored_entity_variables)
{
    Runtime runtime;

    auto thisContext = std::make_unique<ThisEntityVariableContext>(nullptr);
    auto* thisContextPointer = thisContext.get();
    auto otherContext = std::make_unique<OtherEntityVariableContext>(thisContextPointer);

    const auto thisContextId = runtime.registerVariableContext(
        THIS_ENTITY_VARIABLE_CONTEXT,
        THIS_ENTITY_VARIABLE_PREFIX,
        std::move(thisContext)
    );
    const auto otherContextId = runtime.registerVariableContext(
        OTHER_ENTITY_VARIABLE_CONTEXT,
        OTHER_ENTITY_VARIABLE_PREFIX,
        std::move(otherContext)
    );

    const auto thisVariableId = runtime.registerContextVariable(
        thisContextId,
        "this.health",
        runtime.getIntegerType()
    );
    const auto otherVariableId = runtime.registerContextVariable(
        otherContextId,
        "other.health",
        runtime.getIntegerType()
    );

    BOOST_REQUIRE(thisVariableId.isValid());
    BOOST_REQUIRE(otherVariableId.isValid());
    BOOST_TEST(thisVariableId != otherVariableId);
    BOOST_TEST(runtime.getContextVariableId("this.health") == thisVariableId);
    BOOST_TEST(runtime.getContextVariableId("other.health") == otherVariableId);
    BOOST_TEST(runtime.getContextVariableName(thisVariableId) == "this.health");
    BOOST_TEST(runtime.getContextVariableName(otherVariableId) == "other.health");
}

BOOST_AUTO_TEST_CASE(scene_owns_and_removes_entities_without_game_initialization)
{
    VariableMap variableMap;
    Scene scene(1, &variableMap);

    auto entity = std::make_unique<Entity>(&scene, &variableMap, 2, 0.0f, 0.0f, 0);
    const auto entityPtr = entity.get();

    scene.addEntity(std::move(entity));
    BOOST_TEST(scene.getEntityById(2) == entityPtr);

    scene.removeEntity(entityPtr);
    BOOST_TEST(scene.getEntityById(2) == nullptr);
}

BOOST_AUTO_TEST_CASE(scene_rejects_invalid_animated_tile_ranges)
{
    VariableMap variableMap;
    Scene scene(1, &variableMap);

    BOOST_TEST(!scene.addAnimatedTile("missing", 4, 1));
    BOOST_TEST(!scene.addAnimatedTile("", 4, 1));
    BOOST_TEST(!scene.addAnimatedTile("missing", 0, 1));
    BOOST_TEST(!scene.addAnimatedTile("missing", 4, 0));
}

BOOST_AUTO_TEST_CASE(animated_tile_resolves_frames_at_the_configured_update_interval)
{
    const AnimatedTileDefinition animation{1408, 4, 2, 10};

    BOOST_TEST(animation.resolveTileId(1408, 10) == 1408U);
    BOOST_TEST(animation.resolveTileId(1408, 11) == 1408U);
    BOOST_TEST(animation.resolveTileId(1408, 12) == 1409U);
    BOOST_TEST(animation.resolveTileId(1408, 14) == 1410U);
    BOOST_TEST(animation.resolveTileId(1408, 16) == 1411U);
    BOOST_TEST(animation.resolveTileId(1408, 18) == 1408U);
    BOOST_TEST(animation.resolveTileId(1409, 12) == 1409U);
}
