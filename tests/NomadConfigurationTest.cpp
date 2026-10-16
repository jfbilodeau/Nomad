// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/system/NomadConfiguration.hpp>

#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

#include <utility>

using namespace nomad;
using namespace nomad::test;

BOOST_AUTO_TEST_SUITE(nomad_configuration)

BOOST_AUTO_TEST_CASE(reads_strict_values_through_a_const_document)
{
    const auto configuration = NomadConfiguration::parse(R"(
[module.settings]
name = "example"
count = 42
enabled = false
directories = ["audio", "mod/audio"]
empty = []
)");

    BOOST_TEST(configuration.contains("module"));
    BOOST_TEST(configuration.contains("module.settings"));
    BOOST_TEST(configuration.requireString("module.settings.name") == "example");
    BOOST_TEST(configuration.requireInteger("module.settings.count") == 42);
    BOOST_TEST(!configuration.requireBoolean("module.settings.enabled"));
    const auto directories = configuration.requireStringArray("module.settings.directories");
    BOOST_REQUIRE(directories.size() == 2U);
    BOOST_TEST(directories[0] == "audio");
    BOOST_TEST(directories[1] == "mod/audio");
    BOOST_TEST(configuration.requireStringArray("module.settings.empty").empty());
}

BOOST_AUTO_TEST_CASE(distinguishes_missing_fields_from_invalid_types)
{
    const auto configuration = NomadConfiguration::parse(R"(
string = ""
integer = 1
floating = 1.0
boolean = true
array = ["valid", 2]
table = {}
)");

    BOOST_TEST(!configuration.contains("absent"));
    BOOST_TEST(!configuration.getString("absent").has_value());
    BOOST_TEST(!configuration.getInteger("absent.nested").has_value());
    BOOST_TEST(!configuration.getBoolean("absent").has_value());
    BOOST_TEST(!configuration.getStringArray("absent").has_value());
    BOOST_TEST(configuration.requireString("string").empty());
    BOOST_CHECK_EXCEPTION(
        (void)configuration.requireString("absent"),
        NomadConfigurationException,
        [](const auto& error) {
            return NomadString(error.what()).find("is required") != NomadString::npos;
        }
    );
    BOOST_CHECK_THROW((void)configuration.requireInteger("absent"), NomadConfigurationException);
    BOOST_CHECK_THROW((void)configuration.requireBoolean("absent"), NomadConfigurationException);
    BOOST_CHECK_THROW((void)configuration.requireStringArray("absent"), NomadConfigurationException);
    BOOST_CHECK_THROW((void)configuration.getString("integer"), NomadConfigurationException);
    BOOST_CHECK_THROW((void)configuration.getInteger("floating"), NomadConfigurationException);
    BOOST_CHECK_THROW((void)configuration.getInteger("boolean"), NomadConfigurationException);
    BOOST_CHECK_THROW((void)configuration.getBoolean("integer"), NomadConfigurationException);
    BOOST_CHECK_THROW((void)configuration.getStringArray("string"), NomadConfigurationException);
    BOOST_CHECK_THROW((void)configuration.getStringArray("array"), NomadConfigurationException);
    BOOST_CHECK_THROW((void)configuration.getString("table"), NomadConfigurationException);
    BOOST_CHECK_THROW((void)configuration.getString("integer.nested"), NomadConfigurationException);
}

BOOST_AUTO_TEST_CASE(rejects_wrong_type_intermediate_module_tables)
{
    const auto malformed = NomadConfiguration::parse("nomad = 1");
    BOOST_CHECK_THROW((void)malformed.getStringArray("nomad.modules"), NomadConfigurationException);
    BOOST_CHECK_THROW((void)malformed.requireStringArray("nomad.modules"), NomadConfigurationException);
    BOOST_CHECK_THROW((void)malformed.contains("nomad.modules"), NomadConfigurationException);

    const auto missing = NomadConfiguration::parse("[nomad]");
    BOOST_TEST(!missing.getStringArray("nomad.modules").has_value());
    BOOST_TEST(!missing.contains("nomad.modules"));
}

BOOST_AUTO_TEST_CASE(owns_deep_copies_and_mutable_nested_values)
{
    auto original = NomadConfiguration::parse("[module]\nname = 'original'\nvalues = ['first']\n");
    auto copy = original;
    NomadConfiguration assigned;
    assigned = original;
    copy.setString("module.name", "copy");
    copy.setStringArray("module.values", {"second", "third"});
    assigned.setString("module.name", "assigned");
    assigned.setInteger("new.settings.count", 123);
    assigned.setBoolean("new.settings.enabled", true);
    assigned.setStringArray("new.settings.empty", {});

    BOOST_TEST(original.requireString("module.name") == "original");
    BOOST_TEST(original.requireStringArray("module.values").front() == "first");
    BOOST_TEST(copy.requireString("module.name") == "copy");
    BOOST_TEST(copy.requireStringArray("module.values").size() == 2U);
    BOOST_TEST(assigned.requireString("module.name") == "assigned");
    BOOST_TEST(assigned.requireInteger("new.settings.count") == 123);
    BOOST_TEST(assigned.requireBoolean("new.settings.enabled"));
    BOOST_TEST(assigned.requireStringArray("new.settings.empty").empty());
    BOOST_TEST(!original.contains("new"));
    BOOST_TEST(copy.erase("module.values"));
    BOOST_TEST(!copy.erase("module.values"));
    BOOST_TEST(original.contains("module.values"));
    BOOST_TEST(!copy.erase("missing.nested"));
    BOOST_TEST(copy.erase("module"));
    BOOST_TEST(!copy.contains("module"));
    BOOST_CHECK_THROW(copy.setInteger("new..count", 1), NomadConfigurationException);
    BOOST_CHECK_THROW(assigned.setString("module.name.child", "invalid"), NomadConfigurationException);
    BOOST_CHECK_THROW((void)assigned.erase("module.name.child"), NomadConfigurationException);
    BOOST_CHECK_THROW((void)assigned.contains(""), NomadConfigurationException);
}

BOOST_AUTO_TEST_CASE(preserves_unknown_tables_and_native_toml_values)
{
    auto configuration = NomadConfiguration::parse(R"(
[module]
name = "example"
ratio = 1.25
date = 2026-10-16
matrix = [[1, 2], [3, 4]]
inline = { count = 7, label = "preserved" }

[[module.assets]]
name = "first"
)");
    configuration.merge(NomadConfiguration::parse(
        "[module]\nname = 'changed'\ninline = { count = 8 }\n"
    ));
    const auto roundTrip = NomadConfiguration::parse(configuration.serialize());
    BOOST_TEST(roundTrip.requireString("module.name") == "changed");
    BOOST_TEST(roundTrip.requireInteger("module.inline.count") == 8);
    BOOST_TEST(roundTrip.requireString("module.inline.label") == "preserved");
    BOOST_TEST(roundTrip.contains("module.ratio"));
    BOOST_TEST(roundTrip.contains("module.date"));
    BOOST_TEST(roundTrip.contains("module.matrix"));
    BOOST_TEST(roundTrip.contains("module.assets"));
    BOOST_TEST(roundTrip.serialize().find("1.25") != NomadString::npos);
    BOOST_TEST(roundTrip.serialize().find("2026-10-16") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(reads_files_and_reports_io_and_parse_errors)
{
    TestDirectory directory("nomad_configuration_read");
    const auto path = directory.write("configuration.toml", "[settings]\nvalue = 9\n");
    const auto configuration = NomadConfiguration::readFile(path);
    BOOST_TEST(configuration.requireInteger("settings.value") == 9);
    BOOST_CHECK_THROW(
        (void)NomadConfiguration::readFile(directory.getPath() / "missing.toml"),
        NomadConfigurationException
    );
    BOOST_CHECK_EXCEPTION(
        (void)NomadConfiguration::parse("invalid = ["),
        NomadConfigurationException,
        [](const auto& error) {
            return NomadString(error.what()).find("at 1:") != NomadString::npos;
        }
    );
}

BOOST_AUTO_TEST_CASE(round_trips_escaped_and_multilingual_strings)
{
    NomadConfiguration configuration;
    const auto value = NomadString("日本語 \"quoted\"\n\t\\path");
    configuration.setString("settings.name", value);
    configuration.setStringArray("settings.values", {value, ""});
    const auto parsed = NomadConfiguration::parse(configuration.serialize());
    BOOST_TEST(parsed.requireString("settings.name") == value);
    BOOST_TEST(parsed.requireStringArray("settings.values").front() == value);
}

BOOST_AUTO_TEST_SUITE_END()
