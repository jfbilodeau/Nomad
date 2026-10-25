// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptObject.hpp>
#include <nomad/script/Type.hpp>

#include <boost/test/unit_test.hpp>

#include <string>
#include <type_traits>
#include <utility>

using namespace nomad;

static_assert(!std::is_copy_constructible_v<ScriptObject>);
static_assert(!std::is_copy_assignable_v<ScriptObject>);

namespace {

const ObjectType* createTestType(Runtime& runtime, const NomadString& name) {
    const auto id = runtime.createObjectType(name, ObjectTypeMode::Opened);
    BOOST_REQUIRE(id.isValid());
    return runtime.getType(id).value()->asObjectType();
}

class CountingObject final : public NomadObject {
public:
    explicit CountingObject(NomadIndex& destroyed): m_destroyed(destroyed) {}
    ~CountingObject() override { ++m_destroyed; }

private:
    NomadIndex& m_destroyed;
};

class InitializingType final : public ObjectType {
public:
    InitializingType(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        ObjectTypeMode mode,
        NomadIndex& initialized,
        NomadIndex& destroyed,
        bool fail
    ):
        ObjectType(runtime, id, std::move(name), mode),
        m_initialized(initialized),
        m_destroyed(destroyed),
        m_fail(fail)
    {}

    void initValue(RuntimeValue& value) const override {
        ++m_initialized;
        value.setObjectValue(new CountingObject(m_destroyed));
        if (m_fail) {
            throw NomadException("Field initialization failed");
        }
    }

private:
    NomadIndex& m_initialized;
    NomadIndex& m_destroyed;
    bool m_fail;
};

} // namespace

BOOST_AUTO_TEST_SUITE(language_script_object)

BOOST_AUTO_TEST_CASE(fields_use_owner_local_slots_and_type_defaults) {
    Runtime runtime;
    const auto* unrelated = createTestType(runtime, "Unrelated");
    BOOST_REQUIRE(runtime.registerProperty(unrelated, "other", runtime.getIntegerType()).isValid());
    const auto* type = createTestType(runtime, "Fields");
    const auto nativeId = runtime.addProperty(type->getId(), {
        .valueTypeId = runtime.getTypeId("int"),
        .name = "native",
        .getter = [](VirtualMachine*, RuntimeValue& result) { result.setIntegerValue(42); }
    });
    BOOST_REQUIRE(nativeId.isValid());
    const auto integerId = runtime.registerProperty(type, "integer", runtime.getIntegerType());
    const auto floatId = runtime.registerProperty(type, "number", runtime.getFloatType());
    const auto booleanId = runtime.registerProperty(type, "flag", runtime.getBooleanType());
    const auto stringId = runtime.registerProperty(type, "text", runtime.getStringType());
    const auto referenceId = runtime.registerProperty(type, "reference", type);
    for (const auto id : {integerId, floatId, booleanId, stringId, referenceId}) {
        BOOST_REQUIRE(id.isValid());
    }
    ScriptObject object(type);
    BOOST_TEST(object.getObjectType() == type);
    BOOST_TEST(object.getFieldCount() == 5u);
    BOOST_TEST(type->getFieldIndex(nativeId) == NOMAD_INVALID_INDEX);
    BOOST_TEST(type->getFieldIndex(integerId) == 0u);
    BOOST_TEST(type->getFieldIndex(referenceId) == 4u);
    object.initializeFields();
    BOOST_TEST(object.getField(type->getFieldIndex(integerId)).getIntegerValue() == 0);
    BOOST_TEST(object.getField(type->getFieldIndex(floatId)).getFloatValue() == 0.0);
    BOOST_TEST(!object.getField(type->getFieldIndex(booleanId)).getBooleanValue());
    BOOST_TEST(NomadString(object.getField(type->getFieldIndex(stringId)).getStringValue()).empty());
    BOOST_TEST(object.getField(type->getFieldIndex(referenceId)).getObjectValue() == nullptr);
}

BOOST_AUTO_TEST_CASE(allocation_does_not_initialize_and_defaults_run_once) {
    Runtime runtime;
    auto initialized = NomadIndex{0};
    auto destroyed = NomadIndex{0};
    const auto valueId = runtime.createObjectType<InitializingType>(
        "Counted", ObjectTypeMode::Closed, initialized, destroyed, false
    );
    BOOST_REQUIRE(valueId.isValid());
    const auto* type = createTestType(runtime, "Owner");
    BOOST_REQUIRE(runtime.registerProperty(type, "value", runtime.getType(valueId).value()).isValid());
    {
        ScriptObject uninitialized(type);
        BOOST_TEST(initialized == 0u);
        BOOST_CHECK_THROW((void)uninitialized.getField(0), NomadException);
        BOOST_CHECK_THROW(uninitialized.setField(0, RuntimeValue{}), NomadException);
    }
    BOOST_TEST(destroyed == 0u);
    {
        ScriptObject first(type);
        ScriptObject second(type);
        first.initializeFields();
        second.initializeFields();
        BOOST_TEST(initialized == 2u);
        BOOST_TEST(first.getField(0).getObjectValue() != second.getField(0).getObjectValue());
        BOOST_CHECK_THROW(first.initializeFields(), NomadException);
        BOOST_TEST(initialized == 2u);
    }
    BOOST_TEST(destroyed == 2u);
}

BOOST_AUTO_TEST_CASE(instances_have_independent_values_and_aliases_share_the_same_instance) {
    Runtime runtime;
    const auto* type = createTestType(runtime, "Point");
    BOOST_REQUIRE(runtime.registerProperty(type, "value", runtime.getIntegerType()).isValid());
    RuntimeValue owner;
    owner.setObjectValue(new ScriptObject(type));
    auto* first = owner.asObject<ScriptObject>();
    first->initializeFields();
    ScriptObject second(type);
    second.initializeFields();
    RuntimeValue alias;
    type->copyValue(owner, alias);
    BOOST_TEST(first->getReferenceCount() == 2u);
    alias.asObject<ScriptObject>()->setField(0, RuntimeValue(NomadInteger{17}));
    BOOST_TEST(first->getField(0).getIntegerValue() == 17);
    BOOST_TEST(second.getField(0).getIntegerValue() == 0);
    type->freeValue(owner);
    BOOST_TEST(alias.asObject<ScriptObject>()->getField(0).getIntegerValue() == 17);
    type->freeValue(alias);
}

BOOST_AUTO_TEST_CASE(field_replacement_retains_references_and_handles_self_assignment) {
    Runtime runtime;
    const auto* type = createTestType(runtime, "Owner");
    BOOST_REQUIRE(runtime.registerProperty(type, "first", type).isValid());
    BOOST_REQUIRE(runtime.registerProperty(type, "second", type).isValid());
    auto destroyed = NomadIndex{0};
    {
        ScriptObject object(type);
        object.initializeFields();
        RuntimeValue value;
        value.setObjectValue(new CountingObject(destroyed));
        object.setField(0, value);
        object.setField(1, object.getField(0));
        object.setField(0, object.getField(0));
        BOOST_TEST(value.getObjectValue()->getReferenceCount() == 3u);
        type->freeValue(value);
        object.setField(0, RuntimeValue{});
        BOOST_TEST(destroyed == 0u);
        BOOST_TEST(object.getField(1).getObjectValue()->getReferenceCount() == 1u);
        object.setField(1, RuntimeValue{});
        BOOST_TEST(destroyed == 1u);
        RuntimeValue remaining;
        remaining.setObjectValue(new CountingObject(destroyed));
        object.setField(0, remaining);
        type->freeValue(remaining);
    }
    BOOST_TEST(destroyed == 2u);
}

BOOST_AUTO_TEST_CASE(partial_initialization_releases_completed_and_throwing_fields) {
    Runtime runtime;
    auto initialized = NomadIndex{0};
    auto destroyed = NomadIndex{0};
    const auto countedId = runtime.createObjectType<InitializingType>(
        "Counted", ObjectTypeMode::Closed, initialized, destroyed, false
    );
    const auto failingId = runtime.createObjectType<InitializingType>(
        "Failing", ObjectTypeMode::Closed, initialized, destroyed, true
    );
    BOOST_REQUIRE(countedId.isValid());
    BOOST_REQUIRE(failingId.isValid());
    for (auto failingIndex = NomadIndex{0}; failingIndex < 3; ++failingIndex) {
        const auto* type = createTestType(runtime, "Failure" + std::to_string(failingIndex));
        for (auto index = NomadIndex{0}; index < 3; ++index) {
            const auto* fieldType = runtime.getType(index == failingIndex ? failingId : countedId).value();
            BOOST_REQUIRE(runtime.registerProperty(type, "field" + std::to_string(index), fieldType).isValid());
        }
        const auto before = initialized;
        {
            ScriptObject object(type);
            BOOST_CHECK_THROW(object.initializeFields(), NomadException);
            BOOST_TEST(initialized == before + failingIndex + 1);
            BOOST_CHECK_THROW((void)object.getField(0), NomadException);
            BOOST_CHECK_THROW(object.setField(0, RuntimeValue{}), NomadException);
            BOOST_CHECK_THROW(object.initializeFields(), NomadException);
        }
        BOOST_TEST(destroyed == initialized);
    }
}

BOOST_AUTO_TEST_CASE(layout_does_not_resize_and_invalid_access_is_rejected) {
    Runtime runtime;
    BOOST_CHECK_THROW(ScriptObject(nullptr), NomadException);
    const auto* type = createTestType(runtime, "Layout");
    const auto firstId = runtime.registerProperty(type, "first", runtime.getIntegerType());
    BOOST_REQUIRE(firstId.isValid());
    ScriptObject object(type);
    object.initializeFields();
    const auto laterId = runtime.registerProperty(type, "later", runtime.getFloatType());
    BOOST_REQUIRE(laterId.isValid());
    BOOST_TEST(object.getFieldCount() == 1u);
    BOOST_TEST(object.getField(0).getIntegerValue() == 0);
    BOOST_CHECK_THROW((void)object.getField(type->getFieldIndex(laterId)), NomadException);
    BOOST_CHECK_THROW(object.setField(1, RuntimeValue{}), NomadException);
    BOOST_CHECK_THROW((void)object.getField(NOMAD_INVALID_INDEX), NomadException);
    const auto* emptyType = createTestType(runtime, "Empty");
    ScriptObject empty(emptyType);
    BOOST_TEST(empty.getFieldCount() == 0u);
    empty.initializeFields();
    BOOST_CHECK_THROW(empty.initializeFields(), NomadException);
    BOOST_CHECK_THROW((void)empty.getField(0), NomadException);
}

BOOST_AUTO_TEST_SUITE_END()
