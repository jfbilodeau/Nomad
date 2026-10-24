// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/Array1dObject.hpp>
#include <nomad/collections/Array2dObject.hpp>
#include <nomad/collections/Array3dObject.hpp>
#include <nomad/collections/KeyValueObject.hpp>
#include <nomad/collections/KeyValueType.hpp>
#include <nomad/collections/ListObject.hpp>
#include <nomad/collections/MapObject.hpp>

#include <nomad/script/NomadObject.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>

#include <boost/test/unit_test.hpp>

using namespace nomad;

namespace {

class CountingObject final : public NomadObject {
public:
    explicit CountingObject(NomadIndex& destroyed);

protected:
    ~CountingObject() override;

private:
    NomadIndex& m_destroyed;
};

CountingObject::CountingObject(NomadIndex& destroyed):
    m_destroyed(destroyed)
{}

CountingObject::~CountingObject() {
    ++m_destroyed;
}

class FailingCopyType final : public ObjectType {
public:
    explicit FailingCopyType(Runtime* runtime): ObjectType(runtime, "FailingCopy", ObjectTypeMode::Closed) {}

    NomadString getTypeName() const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
    void copyValue(const RuntimeValue& source, RuntimeValue& destination) const override;
};

class UnmanagedType final : public ValueType {
public:
    using ValueType::ValueType;
    void initValue(RuntimeValue& value) const override;
    NomadString getTypeName() const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
    void copyValue(const RuntimeValue& source, RuntimeValue& destination) const override;
    void freeValue(RuntimeValue& value) const override;

    mutable NomadIndex freeCalls = 0;
};

void UnmanagedType::initValue(RuntimeValue& value) const {
    value = RuntimeValue{};
}

NomadString UnmanagedType::getTypeName() const {
    return "Unmanaged";
}

void UnmanagedType::toString(const RuntimeValue&, NomadString& string) const {
    string = getTypeName();
}

void UnmanagedType::copyValue(const RuntimeValue& source, RuntimeValue& destination) const {
    destination = source;
}

void UnmanagedType::freeValue(RuntimeValue&) const {
    ++freeCalls;
}

NomadString FailingCopyType::getTypeName() const {
    return "FailingCopy";
}

void FailingCopyType::toString(const RuntimeValue&, NomadString& string) const {
    string = getTypeName();
}

void FailingCopyType::copyValue(const RuntimeValue& source, RuntimeValue& destination) const {
    ObjectType::copyValue(source, destination);
    throw NomadException("Copy failed");
}

} // namespace

BOOST_AUTO_TEST_SUITE(NomadObjectTests)

BOOST_AUTO_TEST_CASE(KeyValueRetainsBothReferencesAndReleasesThemOnDestruction) {
    Runtime runtime;
    ObjectType type(&runtime, "Element", ObjectTypeMode::Closed);
    NomadIndex destroyed = 0;
    RuntimeValue value;
    value.setObjectValue(new CountingObject(destroyed));
    {
        KeyValueObject pair(&type, &type, value, value);
        BOOST_TEST(pair.getKey().getObjectValue() == value.getObjectValue());
        BOOST_TEST(pair.getValue().getObjectValue() == value.getObjectValue());
        BOOST_TEST(value.getObjectValue()->getReferenceCount() == 3u);
        type.freeValue(value);
        BOOST_TEST(pair.getKey().getObjectValue()->getReferenceCount() == 2u);
        BOOST_TEST(destroyed == 0u);
    }
    BOOST_TEST(destroyed == 1u);
}

BOOST_AUTO_TEST_CASE(KeyValueReleasesPartialCopiesWhenEitherArgumentFails) {
    Runtime runtime;
    ObjectType type(&runtime, "Element", ObjectTypeMode::Closed);
    FailingCopyType failingType(&runtime);
    NomadIndex destroyed = 0;
    RuntimeValue key;
    RuntimeValue value;
    key.setObjectValue(new CountingObject(destroyed));
    value.setObjectValue(new CountingObject(destroyed));
    BOOST_CHECK_THROW(KeyValueObject(&failingType, &type, key, value), NomadException);
    BOOST_TEST(key.getObjectValue()->getReferenceCount() == 1u);
    BOOST_TEST(value.getObjectValue()->getReferenceCount() == 1u);
    BOOST_CHECK_THROW(KeyValueObject(&type, &failingType, key, value), NomadException);
    BOOST_TEST(key.getObjectValue()->getReferenceCount() == 1u);
    BOOST_TEST(value.getObjectValue()->getReferenceCount() == 1u);
    type.freeValue(key);
    type.freeValue(value);
    BOOST_TEST(destroyed == 2u);
}

BOOST_AUTO_TEST_CASE(KeyValueReleasesPartiallyInitializedDefaults) {
    Runtime runtime;
    class InitializingType final : public ObjectType {
    public:
        InitializingType(Runtime* runtime, NomadIndex& destroyed):
            ObjectType(runtime, "Initializing", ObjectTypeMode::Closed),
            m_destroyed(destroyed)
        {}
        void initValue(RuntimeValue& value) const override {
            value.setObjectValue(new CountingObject(m_destroyed));
            if (failInitialization) {
                throw NomadException("Initialization failed");
            }
        }
        bool failInitialization = false;
    private:
        NomadIndex& m_destroyed;
    };
    NomadIndex destroyed = 0;
    InitializingType type(&runtime, destroyed);
    InitializingType failingType(&runtime, destroyed);
    failingType.failInitialization = true;
    BOOST_CHECK_THROW(KeyValueObject(&failingType, &type), NomadException);
    BOOST_TEST(destroyed == 1u);
    BOOST_CHECK_THROW(KeyValueObject(&type, &failingType), NomadException);
    BOOST_TEST(destroyed == 3u);
    {
        KeyValueObject pair(&type, &type);
        BOOST_TEST(pair.getKey().getObjectValue() != pair.getValue().getObjectValue());
    }
    BOOST_TEST(destroyed == 5u);
}

BOOST_AUTO_TEST_CASE(KeyValueDefaultsAndUnmanagedValuesNeedNoCleanup) {
    Runtime runtime;
    UnmanagedType type(&runtime);
    KeyValueType pairType(&runtime, "KeyValue<Unmanaged Unmanaged>", ObjectTypeMode::Closed, &type, &type);
    RuntimeValue first;
    RuntimeValue second;
    pairType.initValue(first);
    pairType.initValue(second);
    BOOST_TEST(first.getObjectValue() != second.getObjectValue());
    BOOST_TEST(first.asObject<KeyValueObject>()->getKey().getIntegerValue() == 0);
    BOOST_TEST(first.asObject<KeyValueObject>()->getValue().getIntegerValue() == 0);
    pairType.freeValue(first);
    pairType.freeValue(second);
    BOOST_TEST(type.freeCalls == 0u);
}

BOOST_AUTO_TEST_CASE(KeyValueRejectsInvalidTypes) {
    Runtime runtime;
    IntegerType integerType(&runtime);
    VoidType voidType(&runtime);
    BOOST_CHECK_THROW(KeyValueType(&runtime, "Invalid", ObjectTypeMode::Closed, nullptr, &integerType), NomadException);
    BOOST_CHECK_THROW(KeyValueType(&runtime, "Invalid", ObjectTypeMode::Closed, &integerType, nullptr), NomadException);
    BOOST_CHECK_THROW(KeyValueType(&runtime, "Invalid", ObjectTypeMode::Closed, &voidType, &integerType), NomadException);
    BOOST_CHECK_THROW(KeyValueType(&runtime, "Invalid", ObjectTypeMode::Closed, &integerType, &voidType), NomadException);
    BOOST_CHECK_THROW(KeyValueObject(nullptr, &integerType), NomadException);
    BOOST_CHECK_THROW(KeyValueObject(&integerType, nullptr), NomadException);
    BOOST_CHECK_THROW(KeyValueObject(&voidType, &integerType), NomadException);
    BOOST_CHECK_THROW(KeyValueObject(&integerType, &voidType), NomadException);
    RuntimeValue value;
    BOOST_CHECK_THROW(KeyValueObject(nullptr, &integerType, value, value), NomadException);
    BOOST_CHECK_THROW(KeyValueObject(&integerType, &voidType, value, value), NomadException);
}

BOOST_AUTO_TEST_CASE(ReferenceHelpersHandleNullSelfAndReplacement) {
    Runtime runtime;
    addRef(nullptr);
    releaseRef(nullptr);
    NomadIndex destroyed = 0;
    auto* first = new CountingObject(destroyed);
    auto* second = new CountingObject(destroyed);
    auto* current = replace(first, first);
    BOOST_TEST(current == first);
    BOOST_TEST(first->getReferenceCount() == 1U);
    current = replace(current, second);
    BOOST_TEST(current == second);
    BOOST_TEST(destroyed == 1U);
    BOOST_TEST(second->getReferenceCount() == 2U);
    releaseRef(second);
    current = replace(current, nullptr);
    BOOST_TEST(current == nullptr);
    BOOST_TEST(destroyed == 2U);
}

BOOST_AUTO_TEST_CASE(RuntimeValueProvidesBorrowedTypedObjectAccess) {
    Runtime runtime;
    RuntimeValue value;
    BOOST_TEST(value.asObject<CountingObject>() == nullptr);
    NomadIndex destroyed = 0;
    auto* object = new CountingObject(destroyed);
    value.setObjectValue(object);
    BOOST_TEST(value.asObject<CountingObject>() == object);
    BOOST_TEST(value.asObject<const CountingObject>() == object);
    BOOST_TEST(object->getReferenceCount() == 1U);
    value.freeObjectValue();
    BOOST_TEST(destroyed == 1U);
}

BOOST_AUTO_TEST_CASE(ListSkipsCleanupForUnmanagedElements) {
    Runtime runtime;
    UnmanagedType elementType(&runtime);
    BOOST_TEST(!elementType.needsFree());
    auto* list = new ListObject(&elementType);
    for (auto index = 0; index < 100; ++index) {
        list->append(RuntimeValue(NomadInteger{index}));
    }
    BOOST_TEST(list->removeFirst());
    BOOST_TEST(list->removeLast());
    BOOST_TEST(list->remove(10));
    list->clear();
    BOOST_TEST(list->getCount() == 0u);
    list->clear();
    BOOST_TEST(list->append(RuntimeValue(NomadInteger{9})) == 0);
    list->releaseRef();
    BOOST_TEST(elementType.freeCalls == 0u);
}

BOOST_AUTO_TEST_CASE(ArraySkipsCleanupForUnmanagedElements) {
    Runtime runtime;
    UnmanagedType elementType(&runtime);
    {
        Array1dObject array(&elementType, 100);
        BOOST_TEST(array.getCount() == 100u);
    }
    BOOST_TEST(elementType.freeCalls == 0u);
}

BOOST_AUTO_TEST_CASE(ListReleasesPartialCopyWhenAppendFails) {
    Runtime runtime;
    FailingCopyType elementType(&runtime);
    NomadIndex destroyed = 0;
    auto* list = new ListObject(&elementType);
    RuntimeValue value;
    value.setObjectValue(new CountingObject(destroyed));
    BOOST_CHECK_THROW(list->append(value), NomadException);
    BOOST_CHECK_THROW(list->prepend(value), NomadException);
    BOOST_CHECK_THROW(list->insert(0, value), NomadException);
    BOOST_TEST(list->getCount() == 0u);
    BOOST_TEST(value.getObjectValue()->getReferenceCount() == 1u);
    list->releaseRef();
    elementType.freeValue(value);
    BOOST_TEST(destroyed == 1u);
}

BOOST_AUTO_TEST_CASE(ListRetainsAndReleasesStoredObjects) {
    Runtime runtime;
    ObjectType elementType(&runtime, "Element", ObjectTypeMode::Closed);
    NomadIndex destroyed = 0;
    auto* list = new ListObject(&elementType);
    BOOST_TEST(list->getElementType() == &elementType);
    BOOST_TEST(list->getCount() == 0u);
    RuntimeValue value;
    value.setObjectValue(new CountingObject(destroyed));
    for (auto index = 0; index < 100; ++index) {
        list->append(value);
    }
    BOOST_TEST(list->getCount() == 100u);
    BOOST_TEST(value.getObjectValue()->getReferenceCount() == 101u);
    elementType.freeValue(value);
    BOOST_TEST(destroyed == 0u);
    list->releaseRef();
    BOOST_TEST(destroyed == 1u);
}

BOOST_AUTO_TEST_CASE(ListInsertionCopiesAliasedValuesBeforeReallocation) {
    Runtime runtime;
    ObjectType elementType(&runtime, "Element", ObjectTypeMode::Closed);
    NomadIndex destroyed = 0;
    ListObject list(&elementType);
    RuntimeValue value;
    value.setObjectValue(new CountingObject(destroyed));
    BOOST_TEST(list.append(value) == 0);
    for (auto index = 0; index < 100; ++index) {
        list.insert(index / 2, list.getValue(index));
    }
    list.prepend(list.getValue(100));
    BOOST_TEST(list.append(list.getValue(0)) == 102);
    BOOST_TEST(value.getObjectValue()->getReferenceCount() == 104u);
    BOOST_TEST(!list.remove(-1));
    BOOST_TEST(!list.remove(103));
    BOOST_CHECK_THROW(list.insert(-1, value), NomadException);
    BOOST_CHECK_THROW(list.insert(104, value), NomadException);
    BOOST_TEST(list.getCount() == 103u);
    BOOST_TEST(value.getObjectValue()->getReferenceCount() == 104u);
    BOOST_TEST(list.remove(50));
    BOOST_TEST(list.removeFirst());
    BOOST_TEST(list.removeLast());
    BOOST_TEST(value.getObjectValue()->getReferenceCount() == 101u);
    list.clear();
    BOOST_TEST(list.getCount() == 0u);
    BOOST_TEST(value.getObjectValue()->getReferenceCount() == 1u);
    BOOST_TEST(destroyed == 0u);
    BOOST_TEST(!list.removeFirst());
    BOOST_TEST(!list.removeLast());
    list.append(value);
    elementType.freeValue(value);
    BOOST_TEST(destroyed == 0u);
    BOOST_TEST(list.removeFirst());
    BOOST_TEST(destroyed == 1u);
}

BOOST_AUTO_TEST_CASE(ListFailedInsertionPreservesExistingElements) {
    Runtime runtime;
    class ConditionalCopyType final : public ObjectType {
    public:
        explicit ConditionalCopyType(Runtime* runtime): ObjectType(runtime, "ConditionalCopy", ObjectTypeMode::Closed) {}

        NomadString getTypeName() const override { return "ConditionalCopy"; }
        void toString(const RuntimeValue&, NomadString& string) const override { string = getTypeName(); }
        void copyValue(const RuntimeValue& source, RuntimeValue& destination) const override {
            ObjectType::copyValue(source, destination);
            if (failCopy) {
                throw NomadException("Copy failed");
            }
        }
        bool failCopy = false;
    } elementType(&runtime);
    NomadIndex destroyed = 0;
    ListObject list(&elementType);
    RuntimeValue value;
    value.setObjectValue(new CountingObject(destroyed));
    list.append(value);
    elementType.failCopy = true;
    BOOST_CHECK_THROW(list.insert(0, list.getValue(0)), NomadException);
    BOOST_CHECK_THROW(list.prepend(list.getValue(0)), NomadException);
    BOOST_CHECK_THROW(list.append(list.getValue(0)), NomadException);
    BOOST_TEST(list.getCount() == 1u);
    BOOST_TEST(list.getValue(0).getObjectValue() == value.getObjectValue());
    BOOST_TEST(value.getObjectValue()->getReferenceCount() == 2u);
    elementType.freeValue(value);
    list.clear();
    BOOST_TEST(destroyed == 1u);
}

BOOST_AUTO_TEST_CASE(ObjectDescriptorsInitializeNullReferences) {
    Runtime runtime;
    ObjectType type(&runtime, "Example", ObjectTypeMode::Closed);
    RuntimeValue value;
    type.initValue(value);
    BOOST_TEST(value.getObjectValue() == nullptr);
    type.freeValue(value);
}

BOOST_AUTO_TEST_CASE(ObjectTypeSharesAndReleasesReferences) {
    Runtime runtime;
    IntegerType elementType(&runtime);
    ObjectType type(&runtime, "Example", ObjectTypeMode::Closed);
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue source;
    source.setObjectValue(new ListObject(&elementType));
    RuntimeValue destination;
    type.initValue(destination);
    type.copyValue(source, destination);
    BOOST_TEST(source.getObjectValue() == destination.getObjectValue());
    BOOST_TEST(source.getObjectValue()->getReferenceCount() == 2U);
    type.copyValue(source, source);
    BOOST_TEST(source.getObjectValue()->getReferenceCount() == 2U);
    type.freeValue(source);
    BOOST_TEST(destination.getObjectValue()->getReferenceCount() == 1U);
    BOOST_TEST(type.needsFree());
    type.freeValue(destination);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(CollectionObjectsUseReferenceCounting) {
    Runtime runtime;
    IntegerType elementType(&runtime);
    const auto before = NomadObject::getLiveObjectCount();
    for (auto* object : {
        static_cast<NomadObject*>(new Array1dObject(&elementType)),
        static_cast<NomadObject*>(new Array2dObject(&elementType)),
        static_cast<NomadObject*>(new Array3dObject(&elementType)),
        static_cast<NomadObject*>(new ListObject(&elementType)),
        static_cast<NomadObject*>(new MapObject(&elementType))
    }) {
        BOOST_TEST(object->getReferenceCount() == 1U);
        object->addRef();
        BOOST_TEST(object->getReferenceCount() == 2U);
        object->releaseRef();
        object->releaseRef();
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(NewObjectHasOneReference) {
    Runtime runtime;
    NomadIndex destroyed = 0;
    auto* object = new CountingObject(destroyed);

    BOOST_TEST(object->getReferenceCount() == 1U);

    object->releaseRef();
}

BOOST_AUTO_TEST_CASE(ObjectDeletesItselfWhenLastReferenceIsReleased) {
    Runtime runtime;
    NomadIndex destroyed = 0;
    auto* object = new CountingObject(destroyed);

    object->addRef();
    object->releaseRef();
    BOOST_TEST(destroyed == 0U);

    object->releaseRef();
    BOOST_TEST(destroyed == 1U);
}

BOOST_AUTO_TEST_SUITE_END()
