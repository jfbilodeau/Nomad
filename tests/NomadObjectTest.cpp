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
    explicit CountingObject(NomadIndex& destroyed):
        m_destroyed(destroyed)
    {
    }

protected:
    ~CountingObject() override {
        ++m_destroyed;
    }

private:
    NomadIndex& m_destroyed;
};

} // namespace

BOOST_AUTO_TEST_SUITE(NomadObjectTests)

BOOST_AUTO_TEST_CASE(ObjectFactoriesAreOptionalAndMustReturnObjects) {
    ObjectType abstractType("Abstract");
    BOOST_TEST(!abstractType.isConstructible());
    BOOST_CHECK_THROW(static_cast<void>(abstractType.createObject()), NomadException);
    ObjectType brokenType("Broken", {}, [] { return nullptr; });
    BOOST_TEST(brokenType.isConstructible());
    BOOST_CHECK_THROW(static_cast<void>(brokenType.createObject()), NomadException);

    NomadIndex destroyed = 0;
    ObjectType type("Constructible", {}, [&] { return new CountingObject(destroyed); });
    BOOST_TEST(type.isConstructible());
    auto* object = type.createObject();
    BOOST_REQUIRE(object != nullptr);
    BOOST_TEST(object->getReferenceCount() == 1u);
    object->releaseRef();
    BOOST_TEST(destroyed == 1u);
}

BOOST_AUTO_TEST_CASE(ObjectTypeSharesAndReleasesReferences) {
    ObjectType type("Example");
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue source;
    source.setObjectValue(new ListObject);
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
    const auto before = NomadObject::getLiveObjectCount();
    for (auto* object : {
        static_cast<NomadObject*>(new Array1dObject),
        static_cast<NomadObject*>(new Array2dObject),
        static_cast<NomadObject*>(new Array3dObject),
        static_cast<NomadObject*>(new ListObject),
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
