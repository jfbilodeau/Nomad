// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/Nomad.hpp>

#include <boost/test/unit_test.hpp>

#include <concepts>
#include <type_traits>
#include <unordered_set>

namespace {

DeclareId(TestTypeId);
DeclareId(TestCallableId);

static_assert(TestTypeId{}.id == nomad::NOMAD_INVALID_ID);
static_assert(TestTypeId::INVALID == TestTypeId{});
static_assert(TestTypeId::INVALID.isInvalid());
static_assert(TestCallableId::INVALID.isInvalid());
static_assert(std::is_same_v<decltype(TestTypeId::INVALID), const TestTypeId>);
static_assert(TestTypeId{42}.id == 42);
static_assert(TestTypeId{42} == TestTypeId{42});
static_assert(TestTypeId{42} != TestTypeId{43});
static_assert(noexcept(TestTypeId{} == TestTypeId{}));
static_assert(std::is_aggregate_v<TestTypeId>);
static_assert(std::is_trivially_copyable_v<TestTypeId>);
static_assert(!std::is_convertible_v<nomad::NomadId, TestTypeId>);
static_assert(!std::is_convertible_v<TestTypeId, nomad::NomadId>);
static_assert(!std::is_convertible_v<TestTypeId, TestCallableId>);
static_assert(!std::equality_comparable_with<TestTypeId, TestCallableId>);
static_assert(TestTypeId{}.isInvalid());
static_assert(!TestTypeId{}.isValid());
static_assert(TestTypeId{0}.isValid());
static_assert(TestTypeId{nomad::NOMAD_ID_MAX - 1}.isValid());
static_assert(TestTypeId{-2}.isInvalid());
static_assert(TestTypeId{nomad::NOMAD_ID_MAX}.isInvalid());
static_assert(!std::is_constructible_v<bool, TestTypeId>);
static_assert(!std::is_convertible_v<TestTypeId, bool>);

} // namespace

BOOST_AUTO_TEST_CASE(declared_ids_support_shared_hashing)
{
    const auto hash = nomad::IdHash{};
    BOOST_TEST(hash(TestTypeId{42}) == std::hash<nomad::NomadId>{}(42));
    BOOST_TEST(hash(TestCallableId{42}) == hash(TestTypeId{42}));
    const std::unordered_set<TestTypeId, nomad::IdHash> ids{TestTypeId{}, TestTypeId{42}, TestTypeId{42}};
    BOOST_TEST(ids.size() == 2);
    BOOST_TEST(ids.contains(TestTypeId{}));
    BOOST_TEST(ids.contains(TestTypeId{42}));
    BOOST_TEST(!ids.contains(TestTypeId{43}));
}
