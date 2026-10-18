// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/Array1dObject.hpp>
#include <nomad/collections/Array2dObject.hpp>
#include <nomad/collections/Array3dObject.hpp>
#include <nomad/collections/ListObject.hpp>
#include <nomad/collections/MapObject.hpp>

#include <nomad/script/NomadObject.hpp>
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

class FailingCopyType final : public ObjectTypeBase {
public:
    NomadString getTypeName() const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
    void copyValue(const RuntimeValue& source, RuntimeValue& destination) const override;
};

class UnmanagedType final : public Type {
public:
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
    ObjectTypeBase::copyValue(source, destination);
    throw NomadException("Copy failed");
}

} // namespace

BOOST_AUTO_TEST_SUITE(NomadObjectTests)

BOOST_AUTO_TEST_CASE(ReferenceHelpersHandleNullSelfAndReplacement) {
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
    UnmanagedType elementType;
    BOOST_TEST(!elementType.needsFree());
    auto* list = new ListObject(&elementType);
    for (auto index = 0; index < 100; ++index) {
        list->push(RuntimeValue(NomadInteger{index}));
    }
    list->releaseRef();
    BOOST_TEST(elementType.freeCalls == 0u);
}

BOOST_AUTO_TEST_CASE(ArraySkipsCleanupForUnmanagedElements) {
    UnmanagedType elementType;
    {
        Array1dObject array(&elementType, 100);
        BOOST_TEST(array.getCount() == 100u);
    }
    BOOST_TEST(elementType.freeCalls == 0u);
}

BOOST_AUTO_TEST_CASE(ListReleasesPartialCopyWhenPushFails) {
    FailingCopyType elementType;
    NomadIndex destroyed = 0;
    auto* list = new ListObject(&elementType);
    RuntimeValue value;
    value.setObjectValue(new CountingObject(destroyed));
    BOOST_CHECK_THROW(list->push(value), NomadException);
    BOOST_TEST(list->getCount() == 0u);
    BOOST_TEST(value.getObjectValue()->getReferenceCount() == 1u);
    list->releaseRef();
    elementType.freeValue(value);
    BOOST_TEST(destroyed == 1u);
}

BOOST_AUTO_TEST_CASE(ListRetainsAndReleasesStoredObjects) {
    ObjectType elementType("Element");
    NomadIndex destroyed = 0;
    auto* list = new ListObject(&elementType);
    BOOST_TEST(list->getElementType() == &elementType);
    BOOST_TEST(list->getCount() == 0u);
    RuntimeValue value;
    value.setObjectValue(new CountingObject(destroyed));
    for (auto index = 0; index < 100; ++index) {
        list->push(value);
    }
    BOOST_TEST(list->getCount() == 100u);
    BOOST_TEST(value.getObjectValue()->getReferenceCount() == 101u);
    elementType.freeValue(value);
    BOOST_TEST(destroyed == 0u);
    list->releaseRef();
    BOOST_TEST(destroyed == 1u);
}

BOOST_AUTO_TEST_CASE(ObjectDescriptorsInitializeNullReferences) {
    ObjectType type("Example");
    RuntimeValue value;
    type.initValue(value);
    BOOST_TEST(value.getObjectValue() == nullptr);
    type.freeValue(value);
}

BOOST_AUTO_TEST_CASE(ObjectTypeSharesAndReleasesReferences) {
    IntegerType elementType;
    ObjectType type("Example");
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
    IntegerType elementType;
    const auto before = NomadObject::getLiveObjectCount();
    for (auto* object : {
        static_cast<NomadObject*>(new Array1dObject(&elementType)),
        static_cast<NomadObject*>(new Array2dObject(&elementType)),
        static_cast<NomadObject*>(new Array3dObject(&elementType)),
        static_cast<NomadObject*>(new ListObject(&elementType)),
        static_cast<NomadObject*>(new MapObject)
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
    NomadIndex destroyed = 0;
    auto* object = new CountingObject(destroyed);

    BOOST_TEST(object->getReferenceCount() == 1U);

    object->releaseRef();
}

BOOST_AUTO_TEST_CASE(ObjectDeletesItselfWhenLastReferenceIsReleased) {
    NomadIndex destroyed = 0;
    auto* object = new CountingObject(destroyed);

    object->addRef();
    object->releaseRef();
    BOOST_TEST(destroyed == 0U);

    object->releaseRef();
    BOOST_TEST(destroyed == 1U);
}

BOOST_AUTO_TEST_SUITE_END()
