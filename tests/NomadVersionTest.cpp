// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/NomadVersion.hpp>

#include <boost/test/unit_test.hpp>

using namespace nomad;

BOOST_AUTO_TEST_SUITE(nomad_version)

BOOST_AUTO_TEST_CASE(parses_and_formats_versions)
{
    const auto version = NomadVersion::parse("12.34.56");

    BOOST_TEST(version.getMajor() == 12);
    BOOST_TEST(version.getMinor() == 34);
    BOOST_TEST(version.getPatch() == 56);
    BOOST_TEST(version.toString() == "12.34.56");
}

BOOST_AUTO_TEST_CASE(compares_versions_by_components)
{
    BOOST_TEST(NomadVersion(1, 0, 0) > NomadVersion(0, 99, 99));
    BOOST_TEST(NomadVersion(1, 2, 0) > NomadVersion(1, 1, 99));
    BOOST_TEST(NomadVersion(1, 2, 4) > NomadVersion(1, 2, 3));
    BOOST_TEST(NomadVersion(1, 2, 3) == NomadVersion::parse("1.2.3"));
}

BOOST_AUTO_TEST_CASE(rejects_non_canonical_versions)
{
    BOOST_CHECK_THROW((void)NomadVersion::parse("1.2"), NomadVersionError);
    BOOST_CHECK_THROW((void)NomadVersion::parse("1.2.3.4"), NomadVersionError);
    BOOST_CHECK_THROW((void)NomadVersion::parse("1.02.3"), NomadVersionError);
    BOOST_CHECK_THROW((void)NomadVersion::parse("1.2.beta"), NomadVersionError);
    BOOST_CHECK_THROW((void)NomadVersion::parse("-1.2.3"), NomadVersionError);
}

BOOST_AUTO_TEST_SUITE_END()
