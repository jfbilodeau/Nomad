// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compiler.hpp>

#include <nomad/log/LogConfiguration.hpp>

#include <nomad/script/Runtime.hpp>

#include <nomad/system/ModuleHost.hpp>
#include <nomad/system/Path.hpp>
#include <nomad/system/ProjectSession.hpp>

#include <TestDirectory.hpp>

#include <boost/dll/shared_library.hpp>
#include <boost/io/ios_state.hpp>
#include <boost/test/unit_test.hpp>

#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <utility>
#include <variant>

using namespace nomad;
using namespace nomad::test;

namespace {

struct SinkObservation {
    std::vector<NomadString> messages;
    bool failFlush = false;
    NomadIndex destructions = 0;
};

class ObservedStartupSink final : public LogSink {
public:
    explicit ObservedStartupSink(std::shared_ptr<SinkObservation> observation) :
        m_observation(std::move(observation))
    {}

    ~ObservedStartupSink() override {
        ++m_observation->destructions;
    }

    void log(const LogEntry* entry) override {
        m_observation->messages.push_back(entry->message);
    }

    void end() override {
        if (m_observation->failFlush) {
            throw NomadException("Startup flush failed");
        }
    }

private:
    std::shared_ptr<SinkObservation> m_observation;
};

struct LogActivationFixture {
    TestDirectory directory{"nomad-log-activation"};
    boost::dll::shared_library library;
    Logger logger;
    Runtime runtime;
    ModuleCallbacks callbacks;

    LogActivationFixture() {
        const auto modules = resolveModules(getSdkDirectory() / "modules", {"log"});
        const auto path = modules.front().directory / getModuleLibraryName("log");
        library.load(path.native(), boost::dll::load_mode::load_with_altered_search_path);
        logger.clearAllSinks();
    }

    void load(const NomadStringView text) {
        const auto configuration = NomadConfiguration::parse(text);
        auto compiler = runtime.createCompiler();
        ModuleContext context{
            &runtime, compiler.get(), &configuration, &logger, ModuleMode::Application,
            directory.getPath(), {}, {}, {}, {}, {}, nullptr
        };
        library.get<RegisterModuleFn>("registerModule")(&context, &callbacks);
    }

    [[nodiscard]] NomadString read(const NomadPath& path) const {
        std::ifstream input(directory.getPath() / path, std::ios::binary);
        if (!input) {
            throw NomadException("Failed to read configured log file");
        }
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }
};

} // namespace

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

BOOST_AUTO_TEST_CASE(tooling_modes_validate_without_changing_the_logger) {
    const TestDirectory directory("log-toml-host");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"log"});
    auto& logger = log::getLogger();
    const auto level = logger.getLogLevel();
    const auto deliveryMode = logger.getDeliveryMode();
    const auto memoryId = logger.createMemorySink();
    for (const auto mode : {ModuleMode::Analysis, ModuleMode::Packaging}) {
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

BOOST_FIXTURE_TEST_CASE(application_startup_flushes_old_sinks_and_applies_file_settings, LogActivationFixture) {
    const auto observation = std::make_shared<SinkObservation>();
    const auto oldId = logger.createSink(std::make_unique<ObservedStartupSink>(observation));
    logger.info("old-pending-entry");
    load(
        "[log]\nlevel = 'debug'\ndelivery-mode = 'synchronous'\n"
        "[[log.sinks]]\ntype = 'file'\nlevel = 'debug'\n"
        "directory = 'logs'\nname = 'session'\nrotate-at-startup = false\n"
    );
    BOOST_TEST(logger.getSink(oldId) == nullptr);
    BOOST_TEST(observation->destructions == 1U);
    BOOST_REQUIRE(observation->messages.size() == 1U);
    BOOST_TEST(observation->messages.front().find("old-pending-entry") != NomadString::npos);
    BOOST_TEST((logger.getLogLevel() == LogLevel::Debug));
    BOOST_TEST((logger.getDeliveryMode() == LogDeliveryMode::Synchronous));
    BOOST_TEST(read("logs/session.log").empty());
    logger.debug("new-configured-entry");
    const auto text = read("logs/session.log");
    BOOST_TEST(text.find("new-configured-entry") != NomadString::npos);
    BOOST_TEST(text.find("old-pending-entry") == NomadString::npos);
    const auto laterId = logger.createMemorySink();
    logger.info("later-sink-entry");
    BOOST_REQUIRE(logger.getSink(laterId) != nullptr);
    BOOST_TEST(static_cast<MemorySink*>(logger.getSink(laterId))->getEntries().size() == 1U);
    callbacks.shutdown();
    BOOST_TEST(logger.getSink(laterId) == nullptr);
    BOOST_CHECK_NO_THROW(std::filesystem::rename(
        directory.getPath() / "logs/session.log", directory.getPath() / "logs/closed.log"
    ));
}

BOOST_FIXTURE_TEST_CASE(omitted_configuration_installs_one_console, LogActivationFixture) {
    const auto oldId = logger.createMemorySink();
    std::ostringstream output;
    const boost::io::ios_rdbuf_saver restoreOutput(std::cout);
    std::cout.rdbuf(output.rdbuf());
    load("");
    BOOST_TEST(logger.getSink(oldId) == nullptr);
    logger.info("default-console-entry");
    const auto text = output.str();
    const auto position = text.find("default-console-entry");
    BOOST_REQUIRE(position != NomadString::npos);
    BOOST_TEST(text.find("default-console-entry", position + 1) == NomadString::npos);
    BOOST_TEST((logger.getLogLevel() == LogLevel::Info));
    BOOST_TEST((logger.getDeliveryMode() == LogDeliveryMode::Synchronous));
    callbacks.shutdown();
}

BOOST_FIXTURE_TEST_CASE(empty_sink_list_disables_delivery_without_creating_files, LogActivationFixture) {
    const auto oldId = logger.createMemorySink();
    std::ostringstream output;
    const boost::io::ios_rdbuf_saver restoreOutput(std::cout);
    std::cout.rdbuf(output.rdbuf());
    load("[log]\nsinks = []\ndelivery-mode = 'buffered'");
    logger.info("disabled-entry");
    logger.flush();
    BOOST_TEST(output.str().find("disabled-entry") == NomadString::npos);
    BOOST_TEST(logger.getSink(oldId) == nullptr);
    BOOST_TEST((logger.getDeliveryMode() == LogDeliveryMode::Buffered));
    BOOST_TEST(std::filesystem::is_empty(directory.getPath()));
    callbacks.shutdown();
}

BOOST_FIXTURE_TEST_CASE(inactive_file_sinks_remain_inactive_and_rotation_is_applied, LogActivationFixture) {
    load(
        "[[log.sinks]]\ntype = 'file'\nactive = false\ndirectory = 'inactive'\n"
        "[[log.sinks]]\ntype = 'file'\ndirectory = 'active'\nmax-size-bytes = 1\nmax-files = 1\n"
    );
    logger.info("first-record");
    logger.info("second-record");
    BOOST_TEST(!std::filesystem::exists(directory.getPath() / "inactive"));
    BOOST_TEST(read("active/application.1.log").find("first-record") != NomadString::npos);
    BOOST_TEST(read("active/application.log").find("second-record") != NomadString::npos);
    callbacks.shutdown();
}

BOOST_FIXTURE_TEST_CASE(preparation_failure_leaves_old_sinks_and_settings_attached, LogActivationFixture) {
    const auto oldId = logger.createMemorySink();
    logger.setLogLevel(LogLevel::Warning);
    directory.write("blocked", "not a directory");
    BOOST_CHECK_THROW(load(
        "[log]\nlevel = 'debug'\n[[log.sinks]]\ntype = 'file'\ndirectory = 'blocked'\n"
    ), std::filesystem::filesystem_error);
    BOOST_TEST(logger.getSink(oldId) != nullptr);
    BOOST_TEST((logger.getLogLevel() == LogLevel::Warning));
    BOOST_TEST((logger.getDeliveryMode() == LogDeliveryMode::Buffered));
    BOOST_TEST(!callbacks.shutdown);
    logger.warning("old-sink-survives");
    logger.flush();
    BOOST_TEST(static_cast<MemorySink*>(logger.getSink(oldId))->getEntries().size() == 1U);
}

BOOST_FIXTURE_TEST_CASE(old_sink_flush_failure_does_not_detach_sinks_or_change_settings, LogActivationFixture) {
    const auto observation = std::make_shared<SinkObservation>();
    const auto oldId = logger.createSink(std::make_unique<ObservedStartupSink>(observation));
    observation->failFlush = true;
    BOOST_CHECK_EXCEPTION(load("[log]\nlevel = 'critical'\nsinks = []"), NomadException,
        [](const auto& error) { return NomadStringView(error.what()) == "Startup flush failed"; });
    BOOST_TEST(logger.getSink(oldId) != nullptr);
    BOOST_TEST(observation->destructions == 0U);
    BOOST_TEST((logger.getLogLevel() == LogLevel::Info));
    BOOST_TEST((logger.getDeliveryMode() == LogDeliveryMode::Buffered));
    BOOST_TEST(!callbacks.shutdown);
    observation->failFlush = false;
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
