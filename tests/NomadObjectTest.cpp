// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/NomadObject.hpp>

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
