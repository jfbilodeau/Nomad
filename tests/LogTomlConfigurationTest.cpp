// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/log/LogConfiguration.hpp>

#include <nomad/system/ModuleHost.hpp>
#include <nomad/system/Path.hpp>
#include <nomad/system/ProjectSession.hpp>

#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

#include <limits>
#include <utility>
#include <variant>

using namespace nomad;
using namespace nomad::test;

BOOST_AUTO_TEST_SUITE(log_toml_configuration)

BOOST_AUTO_TEST_CASE(defaults_and_explicit_empty_sinks_are_distinct) {
    const TestDirectory directory("log-toml-defaults");
    for (const auto* text : {"", "[log]"}) {
        const auto settings = readLogConfiguration(NomadConfiguration::parse(text), directory.getPath());
        BOOST_TEST((settings.level == LogLevel::Info));
        BOOST_TEST((settings.deliveryMode == LogDeliveryMode::Synchronous));
        BOOST_REQUIRE(settings.sinks.size() == 1U);
        BOOST_REQUIRE(std::holds_alternative<ConsoleSinkConfiguration>(settings.sinks.front()));
        const auto& console = std::get<ConsoleSinkConfiguration>(settings.sinks.front());
        BOOST_TEST(console.active);
        BOOST_TEST((console.level == LogLevel::Info));
    }
    const auto settings = readLogConfiguration(
        NomadConfiguration::parse("[log]\nlevel = 'debug'\ndelivery-mode = 'buffered'\nsinks = []"),
        directory.getPath()
    );
    BOOST_TEST((settings.level == LogLevel::Debug));
    BOOST_TEST((settings.deliveryMode == LogDeliveryMode::Buffered));
    BOOST_TEST(settings.sinks.empty());
    BOOST_TEST(std::filesystem::is_empty(directory.getPath()));
}

BOOST_AUTO_TEST_CASE(parses_all_levels_and_preserves_independent_sink_defaults) {
    const TestDirectory directory("log-toml-levels");
    for (const auto& [text, level] : {
        std::pair{"debug", LogLevel::Debug}, std::pair{"info", LogLevel::Info},
        std::pair{"warning", LogLevel::Warning}, std::pair{"error", LogLevel::Error},
        std::pair{"critical", LogLevel::Critical}
    }) {
        const auto configuration = NomadConfiguration::parse(
            "[log]\nlevel = '" + NomadString(text) + "'\n"
            "[[log.sinks]]\ntype = 'console'\nlevel = '" + NomadString(text) + "'\n"
        );
        const auto settings = readLogConfiguration(configuration, directory.getPath());
        BOOST_TEST((settings.level == level));
        BOOST_TEST((std::get<ConsoleSinkConfiguration>(settings.sinks.front()).level == level));
    }
    const auto settings = readLogConfiguration(
        NomadConfiguration::parse("[log]\nlevel = 'debug'\n[[log.sinks]]\ntype = 'file'"),
        directory.getPath()
    );
    BOOST_REQUIRE(settings.sinks.size() == 1U);
    const auto& file = std::get<FileSinkConfiguration>(settings.sinks.front());
    BOOST_TEST(file.active);
    BOOST_TEST((file.level == LogLevel::Info));
    BOOST_TEST(file.directory == directory.getPath() / "logs");
    BOOST_TEST(file.name == "application");
    BOOST_TEST((file.rotationMode == FileRotationMode::Size));
    BOOST_TEST(file.rotateAtStartup);
    BOOST_TEST(file.maxSizeBytes == 10485760);
    BOOST_TEST(file.maxFiles == 10);
    BOOST_TEST(std::filesystem::is_empty(directory.getPath()));
}

BOOST_AUTO_TEST_CASE(parses_ordered_console_and_file_options_without_filesystem_effects) {
    const TestDirectory directory("log-toml-options");
    const auto configuration = NomadConfiguration::parse(
        "[[log.sinks]]\ntype = 'console'\nactive = false\nlevel = 'warning'\n"
        "[[log.sinks]]\ntype = 'file'\nactive = true\nlevel = 'error'\n"
        "directory = 'output'\nname = 'session'\nrotation-mode = 'none'\n"
        "rotate-at-startup = false\nmax-size-bytes = 64\nmax-files = 0\n"
    );
    const auto settings = readLogConfiguration(configuration, directory.getPath());
    BOOST_REQUIRE(settings.sinks.size() == 2U);
    const auto& console = std::get<ConsoleSinkConfiguration>(settings.sinks[0]);
    BOOST_TEST(!console.active);
    BOOST_TEST((console.level == LogLevel::Warning));
    const auto& file = std::get<FileSinkConfiguration>(settings.sinks[1]);
    BOOST_TEST(file.active);
    BOOST_TEST((file.level == LogLevel::Error));
    BOOST_TEST(file.directory == directory.getPath() / "output");
    BOOST_TEST(file.name == "session");
    BOOST_TEST((file.rotationMode == FileRotationMode::None));
    BOOST_TEST(!file.rotateAtStartup);
    BOOST_TEST(file.maxSizeBytes == 64);
    BOOST_TEST(file.maxFiles == 0);
    BOOST_TEST(std::filesystem::is_empty(directory.getPath()));
    const auto roundTrip = NomadConfiguration::parse(configuration.serialize());
    BOOST_TEST(roundTrip.serialize() == configuration.serialize());
    BOOST_TEST(readLogConfiguration(roundTrip, directory.getPath()).sinks.size() == 2U);
}

BOOST_AUTO_TEST_CASE(absolute_paths_and_large_signed_limits_are_preserved) {
    const TestDirectory directory("log-toml-absolute");
    const auto absolute = directory.getPath() / "absolute";
    const auto configuration = NomadConfiguration::parse(
        "[[log.sinks]]\ntype = 'file'\ndirectory = '" + pathToGenericUtf8(absolute) + "'\n"
        "max-size-bytes = 9223372036854775807\nmax-files = 9223372036854775807\n"
    );
    if constexpr (sizeof(NomadIndex) >= sizeof(NomadInteger)) {
        const auto settings = readLogConfiguration(configuration, directory.getPath());
        const auto& file = std::get<FileSinkConfiguration>(settings.sinks.front());
        BOOST_TEST(file.directory == absolute);
        BOOST_TEST(file.maxSizeBytes == std::numeric_limits<NomadInteger>::max());
        BOOST_TEST(file.maxFiles == std::numeric_limits<NomadInteger>::max());
    } else {
        BOOST_CHECK_THROW((void)readLogConfiguration(configuration, directory.getPath()), NomadException);
    }
    BOOST_TEST(std::filesystem::is_empty(directory.getPath()));
}

BOOST_AUTO_TEST_CASE(rejects_invalid_shapes_unknown_fields_and_invalid_values) {
    const TestDirectory directory("log-toml-invalid");
    for (const auto* text : {
        "log = false", "log = []",
        "[log]\nlevel = 1", "[log]\nlevel = ''", "[log]\nlevel = 'Info'", "[log]\nlevel = 'fatal'",
        "[log]\ndelivery-mode = false", "[log]\ndelivery-mode = 'async'",
        "[log]\ndelivery = 'buffered'", "[log]\nrotation-mode = 'size'",
        "[log]\nsinks = false", "[log.sinks]\ntype = 'console'", "[log]\nsinks = [false]",
        "[log]\nsinks = [{type = 'console'}, 2]",
        "[[log.sinks]]", "[[log.sinks]]\ntype = true", "[[log.sinks]]\ntype = ''",
        "[[log.sinks]]\ntype = 'memory'", "[[log.sinks]]\ntype = 'console'\nactive = 1",
        "[[log.sinks]]\ntype = 'console'\nlevel = 'warn'",
        "[[log.sinks]]\ntype = 'console'\ndirectory = 'logs'",
        "[[log.sinks]]\ntype = 'console'\nrotation-mode = 'none'",
        "[[log.sinks]]\ntype = 'file'\nunknown = 1",
        "[[log.sinks]]\ntype = 'file'\ndirectory = ''",
        "[[log.sinks]]\ntype = 'file'\ndirectory = false",
        "[[log.sinks]]\ntype = 'file'\nname = ''",
        "[[log.sinks]]\ntype = 'file'\nname = 'bad/name'",
        "[[log.sinks]]\ntype = 'file'\nrotation-mode = 'daily'",
        "[[log.sinks]]\ntype = 'file'\nrotate-at-startup = 'true'",
        "[[log.sinks]]\ntype = 'file'\nmax-size-bytes = 0",
        "[[log.sinks]]\ntype = 'file'\nmax-size-bytes = -1",
        "[[log.sinks]]\ntype = 'file'\nmax-size-bytes = 1.0",
        "[[log.sinks]]\ntype = 'file'\nmax-files = -1",
        "[[log.sinks]]\ntype = 'file'\nmax-files = true"
    }) {
        BOOST_TEST_CONTEXT(text) {
            const auto configuration = NomadConfiguration::parse(text);
            BOOST_CHECK_THROW((void)readLogConfiguration(configuration, directory.getPath()), NomadException);
            BOOST_TEST(std::filesystem::is_empty(directory.getPath()));
        }
    }
}

BOOST_AUTO_TEST_CASE(module_load_validates_all_modes_without_changing_the_logger) {
    const TestDirectory directory("log-toml-host");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"log"});
    auto& logger = log::getLogger();
    const auto level = logger.getLogLevel();
    const auto deliveryMode = logger.getDeliveryMode();
    const auto memoryId = logger.createMemorySink();
    for (const auto mode : {ModuleMode::Application, ModuleMode::Analysis, ModuleMode::Packaging}) {
        const auto configuration = NomadConfiguration::parse(
            "[log]\nlevel = 'critical'\ndelivery-mode = 'synchronous'\n"
            "[[log.sinks]]\ntype = 'file'\n"
        );
        const auto host = ModuleHost::create(configuration, directory.getPath(), modules, {}, mode);
        BOOST_TEST((logger.getLogLevel() == level));
        BOOST_TEST((logger.getDeliveryMode() == deliveryMode));
        BOOST_TEST(logger.getSink(memoryId) != nullptr);
        BOOST_TEST(std::filesystem::is_empty(directory.getPath()));
        BOOST_CHECK_EXCEPTION((void)ModuleHost::create(
            NomadConfiguration::parse("[log]\nlevel = 'invalid'"), directory.getPath(), modules, {}, mode
        ), NomadException, [](const auto& error) {
            return NomadStringView(error.what()).find("log.level") != NomadStringView::npos;
        });
    }
    logger.removeSink(memoryId);
}

BOOST_AUTO_TEST_CASE(unselected_log_settings_are_not_interpreted) {
    const TestDirectory directory("log-toml-unselected");
    for (const auto mode : {ModuleMode::Application, ModuleMode::Analysis, ModuleMode::Packaging}) {
        BOOST_CHECK_NO_THROW((void)ModuleHost::create(
            NomadConfiguration::parse("[log]\nlevel = false\nsinks = 'uninterpreted'"),
            directory.getPath(), {}, {}, mode
        ));
    }
    BOOST_TEST(std::filesystem::is_empty(directory.getPath()));
}

BOOST_AUTO_TEST_SUITE_END()
