// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/Nomad.hpp>

#include <concepts>
#include <type_traits>

namespace {

DeclareId(TestTypeId);
DeclareId(TestCallableId);

static_assert(TestTypeId{}.id == nomad::NOMAD_INVALID_ID);
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
