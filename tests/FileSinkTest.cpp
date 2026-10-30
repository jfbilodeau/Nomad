// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/log/FileSink.hpp>
#include <nomad/log/Logger.hpp>
#include <nomad/log/MemorySink.hpp>

#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

#include <fstream>
#include <iterator>
#include <limits>
#include <type_traits>
#include <utility>

using namespace nomad;

namespace {

struct FileSinkFixture {
    test::TestDirectory directory{"nomad-file-sink"};
    FileSinkConfiguration configuration;

    FileSinkFixture() {
        configuration.directory = directory.getPath();
    }

    [[nodiscard]] NomadString read(const NomadString& filename = "application.log") const {
        std::ifstream input(directory.getPath() / filename, std::ios::binary);
        if (!input) {
            throw NomadException("Failed to read test log: " + filename);
        }
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    [[nodiscard]] bool exists(const NomadString& filename) const {
        return std::filesystem::exists(directory.getPath() / filename);
    }
};

void writeRecord(FileSink& sink, const NomadString& message) {
    const LogEntry entry{{}, LogLevel::Info, message};
    sink.log(&entry);
    sink.end();
}

static_assert(std::is_base_of_v<SinkConfiguration, FileSinkConfiguration>);
static_assert(std::is_same_v<decltype(FileSinkConfiguration::maxSizeBytes), NomadInteger>);
static_assert(std::is_same_v<decltype(FileSinkConfiguration::maxFiles), NomadInteger>);
static_assert(std::is_same_v<decltype(std::declval<FileSink>().getMaxSizeBytes()), NomadInteger>);
static_assert(std::is_same_v<decltype(std::declval<FileSink>().getMaxFiles()), NomadInteger>);
static_assert(std::is_same_v<std::underlying_type_t<FileRotationMode>, NomadInteger>);

} // namespace

BOOST_FIXTURE_TEST_SUITE(file_sink, FileSinkFixture)

BOOST_AUTO_TEST_CASE(defaults_and_factory_lookup) {
    BOOST_TEST((configuration.level == LogLevel::Info));
    BOOST_TEST(configuration.name == "application");
    BOOST_TEST((configuration.rotationMode == FileRotationMode::Size));
    BOOST_TEST(configuration.rotateAtStartup);
    BOOST_TEST(configuration.maxSizeBytes == 10U * 1024U * 1024U);
    BOOST_TEST(configuration.maxFiles == 10U);

    directory.write("application.log", "previous run\n");
    Logger logger;
    logger.clear();
    const auto id = logger.createFileSink(configuration);
    auto* sink = dynamic_cast<FileSink*>(logger.getSink(id));
    BOOST_REQUIRE(sink != nullptr);
    BOOST_TEST(sink->getRotateAtStartup());
    BOOST_TEST(read().empty());
    BOOST_TEST(read("application.1.log") == "previous run\n");
    BOOST_TEST((sink->getPath() == directory.getPath() / "application.log"));
    logger.replaceSinks({id});
    logger.info("factory record");
    logger.flush();
    BOOST_TEST(read().find("[INFO] factory record") != NomadString::npos);
    logger.removeSink(id);
    BOOST_TEST(logger.getSink(id) == nullptr);
}

BOOST_AUTO_TEST_CASE(global_factory_supports_inactive_sinks) {
    configuration.active = false;
    configuration.directory /= "not-created";
    const auto id = log::createFileSink(configuration);
    BOOST_TEST(dynamic_cast<FileSink*>(log::getSink(id)) != nullptr);
    BOOST_TEST(!std::filesystem::exists(configuration.directory));
    log::removeSink(id);
    BOOST_TEST(log::getSink(id) == nullptr);
}

BOOST_AUTO_TEST_CASE(creates_directories_and_uses_configurable_name) {
    configuration.directory /= "nested";
    configuration.name = "server.session";
    configuration.maxSizeBytes = 1;
    FileSink sink(configuration);
    writeRecord(sink, "record");
    writeRecord(sink, "next");
    BOOST_TEST(std::filesystem::exists(configuration.directory / "server.session.log"));
    BOOST_TEST(std::filesystem::exists(configuration.directory / "server.session.1.log"));
    BOOST_TEST(!std::filesystem::exists(configuration.directory / "server.session.log.1"));
    BOOST_TEST(!exists("application.log"));
    BOOST_TEST((sink.getPath() == configuration.directory / "server.session.log"));
}

BOOST_AUTO_TEST_CASE(default_size_limit_rotates_after_exactly_ten_mebibytes) {
    FileSink sink(configuration);
    writeRecord(sink, NomadString(10U * 1024U * 1024U - 1, 'x'));
    BOOST_TEST(std::filesystem::file_size(sink.getPath()) == 10U * 1024U * 1024U);
    BOOST_TEST(!exists("application.1.log"));
    writeRecord(sink, "next");
    BOOST_TEST(std::filesystem::file_size(directory.getPath() / "application.1.log") == 10U * 1024U * 1024U);
    BOOST_TEST(read() == "next\n");
}

BOOST_AUTO_TEST_CASE(appends_existing_bytes_without_newline_translation) {
    directory.write("application.log", "previous\n");
    configuration.rotateAtStartup = false;
    FileSink sink(configuration);
    writeRecord(sink, "next");
    writeRecord(sink, "");
    BOOST_TEST(read() == "previous\nnext\n\n");
}

BOOST_AUTO_TEST_CASE(preserves_embedded_null_bytes) {
    FileSink sink(configuration);
    const NomadString message("a\0b", 3);
    writeRecord(sink, message);
    BOOST_TEST(read() == message + "\n");
    BOOST_TEST(std::filesystem::file_size(sink.getPath()) == 4U);
}

BOOST_AUTO_TEST_CASE(removal_flushes_pending_records_and_releases_the_file) {
    Logger logger;
    logger.clear();
    const auto id = logger.createFileSink(configuration);
    logger.replaceSinks({id});
    logger.info("pending");
    logger.removeSink(id);
    BOOST_TEST(read().find("pending") != NomadString::npos);
    std::filesystem::rename(directory.getPath() / "application.log", directory.getPath() / "closed.log");
    BOOST_TEST(exists("closed.log"));
}

BOOST_AUTO_TEST_CASE(logger_shutdown_flushes_to_owned_file_sink) {
    {
        Logger logger;
        logger.clear();
        const auto id = logger.createFileSink(configuration);
        logger.replaceSinks({id});
        logger.info("before shutdown");
    }
    BOOST_TEST(read().find("before shutdown") != NomadString::npos);
    BOOST_TEST(read().find("Logger shutting down.") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(sink_level_and_activation_control_file_delivery) {
    configuration.level = LogLevel::Warning;
    Logger logger;
    logger.clear();
    const auto id = logger.createFileSink(configuration);
    logger.replaceSinks({id});
    logger.info("filtered");
    logger.warning("included");
    logger.flush();
    auto* sink = logger.getSink(id);
    sink->setActive(false);
    logger.warning("inactive");
    logger.flush();
    sink->setActive(true);
    sink->setLogLevel(LogLevel::Info);
    logger.info("enabled");
    logger.flush();
    const auto content = read();
    BOOST_TEST(content.find("filtered") == NomadString::npos);
    BOOST_TEST(content.find("inactive") == NomadString::npos);
    BOOST_TEST(content.find("included") != NomadString::npos);
    BOOST_TEST(content.find("enabled") != NomadString::npos);
    logger.clearAllSinks();
}

BOOST_AUTO_TEST_CASE(exact_boundary_and_whole_record_rotation) {
    configuration.maxSizeBytes = 6;
    FileSink sink(configuration);
    writeRecord(sink, "ab");
    writeRecord(sink, "cd");
    BOOST_TEST(read() == "ab\ncd\n");
    BOOST_TEST(!exists("application.1.log"));
    writeRecord(sink, "e");
    BOOST_TEST(read("application.1.log") == "ab\ncd\n");
    BOOST_TEST(read() == "e\n");
}

BOOST_AUTO_TEST_CASE(rotation_counts_utf8_bytes_and_newline) {
    configuration.maxSizeBytes = 4;
    FileSink sink(configuration);
    const NomadString utf8 = "\xC3\xA9";
    writeRecord(sink, utf8);
    writeRecord(sink, "x");
    BOOST_TEST(read("application.1.log") == utf8 + "\n");
    BOOST_TEST(read() == "x\n");
}

BOOST_AUTO_TEST_CASE(oversized_record_is_kept_whole_in_an_empty_file) {
    configuration.maxSizeBytes = 3;
    FileSink sink(configuration);
    writeRecord(sink, "oversized");
    BOOST_TEST(!exists("application.1.log"));
    BOOST_TEST(read() == "oversized\n");
    writeRecord(sink, "x");
    BOOST_TEST(read("application.1.log") == "oversized\n");
    BOOST_TEST(read() == "x\n");
}

BOOST_AUTO_TEST_CASE(existing_file_size_is_used_on_first_write) {
    directory.write("application.log", "old\n");
    configuration.rotateAtStartup = false;
    configuration.maxSizeBytes = 5;
    FileSink sink(configuration);
    BOOST_TEST(!exists("application.1.log"));
    writeRecord(sink, "new");
    BOOST_TEST(read("application.1.log") == "old\n");
    BOOST_TEST(read() == "new\n");
}

BOOST_AUTO_TEST_CASE(retains_ten_archives_by_default_newest_first) {
    configuration.maxSizeBytes = 1;
    FileSink sink(configuration);
    for (auto index = 0; index < 12; ++index) {
        writeRecord(sink, std::to_string(index));
    }
    BOOST_TEST(read() == "11\n");
    for (auto index = 1; index <= 10; ++index) {
        BOOST_TEST(read("application." + std::to_string(index) + ".log") == std::to_string(11 - index) + "\n");
    }
    BOOST_TEST(!exists("application.11.log"));
}

BOOST_AUTO_TEST_CASE(reduced_retention_removes_stale_archives_only_when_rotating) {
    directory.write("application.1.log", "first");
    directory.write("application.3.log", "third");
    directory.write("application.99.log", "old");
    directory.write("application.01.log", "unrelated");
    directory.write("application.backup.log", "backup");
    directory.write("other.99.log", "other");
    configuration.maxSizeBytes = 2;
    FileSink sink(configuration);
    sink.setRotationLimits(2, 2);
    BOOST_TEST(exists("application.99.log"));
    writeRecord(sink, "a");
    writeRecord(sink, "b");
    BOOST_TEST(read("application.1.log") == "a\n");
    BOOST_TEST(read("application.2.log") == "first");
    BOOST_TEST(!exists("application.3.log"));
    BOOST_TEST(!exists("application.99.log"));
    BOOST_TEST(read("application.01.log") == "unrelated");
    BOOST_TEST(read("application.backup.log") == "backup");
    BOOST_TEST(read("other.99.log") == "other");
}

BOOST_AUTO_TEST_CASE(zero_archives_discards_previous_active_contents) {
    configuration.maxSizeBytes = 2;
    configuration.maxFiles = 0;
    directory.write("application.1.log", "old archive");
    FileSink sink(configuration);
    writeRecord(sink, "a");
    writeRecord(sink, "b");
    writeRecord(sink, "c");
    BOOST_TEST(read() == "c\n");
    BOOST_TEST(!exists("application.1.log"));
}

BOOST_AUTO_TEST_CASE(none_appends_without_rotation_or_retention_cleanup) {
    configuration.rotationMode = FileRotationMode::None;
    configuration.rotateAtStartup = false;
    configuration.maxSizeBytes = 1;
    configuration.maxFiles = 0;
    directory.write("application.log", "old\n");
    directory.write("application.1.log", "archive");
    FileSink sink(configuration);
    writeRecord(sink, "large message");
    BOOST_TEST(read() == "old\nlarge message\n");
    BOOST_TEST(read("application.1.log") == "archive");
}

BOOST_AUTO_TEST_CASE(startup_rotates_once_per_sink_lifetime) {
    directory.write("application.log", "previous\n");
    directory.write("application.1.log", "older\n");
    configuration.rotationMode = FileRotationMode::None;
    configuration.maxSizeBytes = 1;
    FileSink sink(configuration);
    BOOST_TEST(read("application.1.log") == "previous\n");
    BOOST_TEST(read("application.2.log") == "older\n");
    writeRecord(sink, "first");
    sink.setActive(false);
    sink.setActive(true);
    sink.setRotateAtStartup(false);
    sink.setRotateAtStartup(true);
    sink.begin();
    writeRecord(sink, "second");
    BOOST_TEST(read() == "first\nsecond\n");
    BOOST_TEST(read("application.1.log") == "previous\n");
}

BOOST_AUTO_TEST_CASE(startup_does_not_archive_empty_or_missing_active_file) {
    {
        FileSink sink(configuration);
        BOOST_TEST(!exists("application.1.log"));
    }
    FileSink sink(configuration);
    BOOST_TEST(!exists("application.1.log"));
    writeRecord(sink, "first");
    BOOST_TEST(read() == "first\n");
}

BOOST_AUTO_TEST_CASE(inactive_staging_defers_filesystem_effects_and_startup_rotation) {
    directory.write("application.log", "old\n");
    configuration.active = false;
    Logger logger;
    logger.clear();
    const auto id = logger.createFileSink(configuration);
    logger.flush();
    BOOST_TEST(read() == "old\n");
    BOOST_TEST(!exists("application.1.log"));
    logger.replaceSinks({id});
    BOOST_TEST(read() == "old\n");
    logger.info("new");
    logger.flush();
    BOOST_TEST(read("application.1.log") == "old\n");
    BOOST_TEST(read().find("new") != NomadString::npos);
    logger.clearAllSinks();
}

BOOST_AUTO_TEST_CASE(startup_and_size_rotation_both_apply) {
    directory.write("application.log", "previous run\n");
    directory.write("application.1.log", "older run\n");
    configuration.maxSizeBytes = 2;
    FileSink sink(configuration);
    BOOST_TEST(read().empty());
    BOOST_TEST(read("application.1.log") == "previous run\n");
    BOOST_TEST(read("application.2.log") == "older run\n");
    writeRecord(sink, "a");
    BOOST_TEST(read() == "a\n");
    writeRecord(sink, "b");
    BOOST_TEST(read() == "b\n");
    BOOST_TEST(read("application.1.log") == "a\n");
    BOOST_TEST(read("application.2.log") == "previous run\n");
    BOOST_TEST(read("application.3.log") == "older run\n");
}

BOOST_AUTO_TEST_CASE(startup_with_zero_archives_discards_only_the_previous_run) {
    directory.write("application.log", "previous run\n");
    directory.write("application.1.log", "older run\n");
    configuration.rotationMode = FileRotationMode::None;
    configuration.maxFiles = 0;
    configuration.maxSizeBytes = 1;
    FileSink sink(configuration);
    BOOST_TEST(read().empty());
    BOOST_TEST(!exists("application.1.log"));
    writeRecord(sink, "first");
    writeRecord(sink, "second");
    BOOST_TEST(read() == "first\nsecond\n");
}

BOOST_AUTO_TEST_CASE(startup_rotation_occurs_again_for_a_new_sink_lifetime) {
    {
        FileSink sink(configuration);
        writeRecord(sink, "first run");
    }
    {
        FileSink sink(configuration);
        writeRecord(sink, "second run");
    }
    BOOST_TEST(read() == "second run\n");
    BOOST_TEST(read("application.1.log") == "first run\n");
    BOOST_TEST(!exists("application.2.log"));
}

BOOST_AUTO_TEST_CASE(startup_setting_can_be_changed_before_starting_a_staged_sink) {
    directory.write("application.log", "previous run\n");
    configuration.active = false;
    configuration.rotateAtStartup = false;
    FileSink enabled(configuration);
    BOOST_TEST(!enabled.getRotateAtStartup());
    enabled.setRotateAtStartup(true);
    enabled.begin();
    BOOST_TEST(read().empty());
    BOOST_TEST(read("application.1.log") == "previous run\n");

    configuration.name = "disabled";
    configuration.rotateAtStartup = true;
    directory.write("disabled.log", "previous run\n");
    FileSink disabled(configuration);
    disabled.setRotateAtStartup(false);
    BOOST_TEST(!disabled.getRotateAtStartup());
    writeRecord(disabled, "next");
    BOOST_TEST(read("disabled.log") == "previous run\nnext\n");
    BOOST_TEST(!exists("disabled.1.log"));
}

BOOST_AUTO_TEST_CASE(startup_failure_preserves_active_contents_and_can_be_retried) {
    directory.write("application.log", "previous run\n");
    const auto blocker = directory.getPath() / "application.1.log";
    std::filesystem::create_directory(blocker);
    configuration.active = false;
    FileSink sink(configuration);
    BOOST_CHECK_THROW(sink.begin(), NomadException);
    BOOST_TEST(read() == "previous run\n");
    std::filesystem::remove(blocker);
    sink.begin();
    BOOST_TEST(read().empty());
    BOOST_TEST(read("application.1.log") == "previous run\n");
    sink.begin();
    BOOST_TEST(!exists("application.2.log"));
}

BOOST_AUTO_TEST_CASE(inactive_creation_does_not_create_directories) {
    configuration.directory /= "missing";
    configuration.active = false;
    FileSink sink(configuration);
    BOOST_TEST(!std::filesystem::exists(configuration.directory));
}

BOOST_AUTO_TEST_CASE(mutable_rotation_configuration_applies_to_subsequent_writes) {
    configuration.rotationMode = FileRotationMode::None;
    FileSink sink(configuration);
    writeRecord(sink, "first");
    sink.setRotationLimits(2, 1);
    sink.setRotationMode(FileRotationMode::Size);
    BOOST_TEST((sink.getRotationMode() == FileRotationMode::Size));
    BOOST_TEST(sink.getMaxSizeBytes() == 2U);
    BOOST_TEST(sink.getMaxFiles() == 1U);
    BOOST_TEST(!exists("application.1.log"));
    writeRecord(sink, "next");
    BOOST_TEST(read("application.1.log") == "first\n");
    BOOST_TEST(read() == "next\n");
    BOOST_CHECK_THROW(sink.setRotationMode(static_cast<FileRotationMode>(0)), NomadException);
    BOOST_CHECK_THROW(sink.setRotationLimits(0, 7), NomadException);
    BOOST_TEST((sink.getRotationMode() == FileRotationMode::Size));
    BOOST_TEST(sink.getMaxSizeBytes() == 2U);
    BOOST_TEST(sink.getMaxFiles() == 1U);
}

BOOST_AUTO_TEST_CASE(invalid_configuration_is_rejected_without_filesystem_effects) {
    configuration.active = false;
    configuration.directory /= "missing";
    for (const auto& name : {"", "..", "nested/name", "nested\\name", "name:", "name.", "name "}) {
        configuration.name = name;
        BOOST_CHECK_THROW((void)FileSink(configuration), NomadException);
    }
    configuration.name = "application";
    configuration.rotationMode = static_cast<FileRotationMode>(0);
    BOOST_CHECK_THROW((void)FileSink(configuration), NomadException);
    configuration.rotationMode = FileRotationMode::Size;
    configuration.maxSizeBytes = 0;
    BOOST_CHECK_THROW((void)FileSink(configuration), NomadException);
    BOOST_TEST(!std::filesystem::exists(configuration.directory));
    configuration.directory.clear();
    BOOST_CHECK_THROW((void)FileSink(configuration), NomadException);
}

BOOST_AUTO_TEST_CASE(negative_rotation_settings_are_rejected_before_starting_any_sink) {
    configuration.directory /= "missing";
    for (const auto active : {false, true}) {
        configuration.active = active;
        for (const auto size : {std::numeric_limits<NomadInteger>::min(), NomadInteger{-1}, NomadInteger{0}}) {
            configuration.maxSizeBytes = size;
            BOOST_CHECK_THROW((void)FileSink(configuration), NomadException);
        }
        configuration.maxSizeBytes = 64;
        for (const auto files : {std::numeric_limits<NomadInteger>::min(), NomadInteger{-1}}) {
            configuration.maxFiles = files;
            BOOST_CHECK_THROW((void)FileSink(configuration), NomadException);
        }
        configuration.maxFiles = 10;
        BOOST_TEST(!std::filesystem::exists(configuration.directory));
    }
}

BOOST_AUTO_TEST_CASE(rotation_limit_updates_validate_both_values_before_changing_either) {
    configuration.active = false;
    configuration.maxSizeBytes = 64;
    configuration.maxFiles = 3;
    FileSink sink(configuration);
    for (const auto& [size, files] : {
        std::pair{NomadInteger{0}, NomadInteger{7}},
        std::pair{NomadInteger{-1}, NomadInteger{7}},
        std::pair{NomadInteger{128}, NomadInteger{-1}},
        std::pair{std::numeric_limits<NomadInteger>::min(), NomadInteger{7}},
        std::pair{NomadInteger{128}, std::numeric_limits<NomadInteger>::min()}
    }) {
        BOOST_CHECK_THROW(sink.setRotationLimits(size, files), NomadException);
        BOOST_TEST(sink.getMaxSizeBytes() == 64);
        BOOST_TEST(sink.getMaxFiles() == 3);
    }
    auto largestLimit = std::numeric_limits<NomadInteger>::max();
    if (!std::in_range<NomadIndex>(largestLimit)) {
        BOOST_CHECK_THROW(sink.setRotationLimits(largestLimit, 3), NomadException);
        BOOST_CHECK_THROW(sink.setRotationLimits(64, largestLimit), NomadException);
        BOOST_TEST(sink.getMaxSizeBytes() == 64);
        BOOST_TEST(sink.getMaxFiles() == 3);
        largestLimit = static_cast<NomadInteger>(std::numeric_limits<NomadIndex>::max());
    }
    sink.setRotationLimits(largestLimit, largestLimit);
    BOOST_TEST(sink.getMaxSizeBytes() == largestLimit);
    BOOST_TEST(sink.getMaxFiles() == largestLimit);
    configuration.maxSizeBytes = largestLimit;
    configuration.maxFiles = largestLimit;
    const FileSink largest(configuration);
    BOOST_TEST(largest.getMaxSizeBytes() == largestLimit);
    BOOST_TEST(largest.getMaxFiles() == largestLimit);
    BOOST_TEST(!exists("application.log"));
}

BOOST_AUTO_TEST_CASE(rotation_mode_validation_preserves_full_width_enum_values) {
    configuration.active = false;
    FileSink sink(configuration);
    for (const auto value : {
        std::numeric_limits<NomadInteger>::min(), NomadInteger{0}, (NomadInteger{1} << 32) + 2,
        std::numeric_limits<NomadInteger>::max()
    }) {
        BOOST_CHECK_THROW(sink.setRotationMode(static_cast<FileRotationMode>(value)), NomadException);
        BOOST_CHECK(sink.getRotationMode() == FileRotationMode::Size);
    }
}

BOOST_AUTO_TEST_CASE(directory_and_open_failures_are_explicit) {
    configuration.directory = directory.write("blocked", "not a directory");
    BOOST_CHECK_THROW((void)FileSink(configuration), std::filesystem::filesystem_error);
    configuration.directory = directory.getPath();
    std::filesystem::create_directory(configuration.directory / "application.log");
    BOOST_CHECK_THROW((void)FileSink(configuration), NomadException);
}

BOOST_AUTO_TEST_CASE(rotation_failure_preserves_active_file_and_can_recover) {
    configuration.maxSizeBytes = 2;
    FileSink sink(configuration);
    writeRecord(sink, "a");
    const auto blocker = directory.getPath() / "application.1.log";
    std::filesystem::create_directory(blocker);
    BOOST_CHECK_THROW(writeRecord(sink, "b"), NomadException);
    BOOST_TEST(read() == "a\n");
    std::filesystem::remove(blocker);
    writeRecord(sink, "c");
    BOOST_TEST(read("application.1.log") == "a\n");
    BOOST_TEST(read() == "c\n");
}

BOOST_AUTO_TEST_CASE(retention_failure_does_not_silently_remove_a_directory) {
    configuration.maxSizeBytes = 2;
    configuration.maxFiles = 0;
    FileSink sink(configuration);
    writeRecord(sink, "a");
    const auto blocker = directory.getPath() / "application.50.log";
    std::filesystem::create_directory(blocker);
    BOOST_CHECK_THROW(writeRecord(sink, "b"), NomadException);
    BOOST_TEST(std::filesystem::is_directory(blocker));
    BOOST_TEST(read() == "a\n");
}

BOOST_AUTO_TEST_CASE(failed_factory_does_not_remove_existing_sinks) {
    Logger logger;
    const auto memoryId = logger.createMemorySink();
    logger.replaceSinks({memoryId});
    auto* memory = static_cast<MemorySink*>(logger.getSink(memoryId));
    memory->clear();
    configuration.directory = directory.write("blocked", "not a directory");
    BOOST_CHECK_THROW((void)logger.createFileSink(configuration), std::filesystem::filesystem_error);
    logger.info("still available");
    logger.flush();
    BOOST_REQUIRE_EQUAL(memory->getEntries().size(), 1U);
    BOOST_TEST(memory->getEntries().front().message.find("still available") != NomadString::npos);
}

BOOST_AUTO_TEST_SUITE_END()
