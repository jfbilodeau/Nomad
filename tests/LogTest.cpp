// Copyright (c) 2025-2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include "nomad/log/Logger.hpp"
#include "nomad/log/MemorySink.hpp"

#include <string>
#include <thread>
#include <vector>

using namespace nomad;

BOOST_AUTO_TEST_SUITE(LogTests)

BOOST_AUTO_TEST_CASE(LoggingToMemorySink)
{
    MemorySink mem;
    Logger logger(&mem);

    // The Logger constructor logs an initialization message. Add our test message
    // and then flush; the MemorySink will contain the initialization entry + ours.
    logger.info("Test message from unit test");
    logger.flush();

    const auto& entries = mem.getEntries();

    // Expect at least two entries: the constructor's "Logger initialized." entry
    // and our "Test message from unit test" entry.
    BOOST_TEST(entries.size() >= 2);

    // The last entry should contain our test message text.
    auto last = entries.back();
    BOOST_TEST(last.message.find("Test message from unit test") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(LogLevelFiltering)
{
    MemorySink mem;
    Logger logger(&mem);

    // Set logger level to Warning so Debug and Info messages are filtered out.
    logger.setLogLevel(LogLevel::Warning);

    logger.debug("debug-msg");
    logger.info("info-msg");
    logger.warning("warning-msg");

    logger.flush();

    const auto& entries = mem.getEntries();

    // There will be the constructor init message plus only the warning entry.
    // So expect at least 2 entries (init + warning).
    BOOST_TEST(entries.size() >= 2);

    // Find that at least one entry contains our warning text.
    bool foundWarning = false;
    for (const auto& e : entries) {
        if (e.message.find("warning-msg") != std::string::npos) {
            foundWarning = true;
            break;
        }
    }

    BOOST_TEST(foundWarning);
}

BOOST_AUTO_TEST_CASE(LoggingStringViewDoesNotRequireNullTermination)
{
    MemorySink memorySink;
    Logger logger(&memorySink);
    const NomadString backing = "xxview messageyy";
    const NomadStringView message(backing.data() + 2, 12);

    logger.info(message);
    logger.flush();

    const auto& entries = memorySink.getEntries();
    BOOST_REQUIRE(!entries.empty());
    BOOST_TEST(entries.back().message.find("view message") != NomadString::npos);
    BOOST_TEST(entries.back().message.find("yy") == NomadString::npos);
}

BOOST_AUTO_TEST_CASE(SuppressingAndRestoringLogLevel)
{
    // The debug console raises the log level to Fatal while it re-evaluates
    // dynamic variables so per-frame getter warnings do not flood the log,
    // then restores the previous level.
    MemorySink mem;
    Logger logger(&mem);

    logger.setLogLevel(LogLevel::Warning);
    const auto previousLevel = logger.getLogLevel();
    BOOST_TEST((previousLevel == LogLevel::Warning));

    logger.setLogLevel(LogLevel::Fatal);
    logger.warning("suppressed-warning");
    logger.flush();

    for (const auto& entry : mem.getEntries()) {
        BOOST_TEST(entry.message.find("suppressed-warning") == std::string::npos);
    }

    logger.setLogLevel(previousLevel);
    BOOST_TEST((logger.getLogLevel() == LogLevel::Warning));

    logger.warning("restored-warning");
    logger.flush();

    bool foundRestored = false;
    for (const auto& entry : mem.getEntries()) {
        if (entry.message.find("restored-warning") != std::string::npos) {
            foundRestored = true;
            break;
        }
    }

    BOOST_TEST(foundRestored);
}

BOOST_AUTO_TEST_CASE(ClearingPendingEntries)
{
    MemorySink memorySink;
    Logger logger(&memorySink);

    logger.info("discarded");
    logger.clear();
    logger.info("retained");
    logger.flush();

    const auto& entries = memorySink.getEntries();
    BOOST_REQUIRE(entries.size() == 1U);
    BOOST_TEST(entries.front().message.find("retained") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(ConcurrentSubmissionAndDelivery)
{
    MemorySink sink;
    Logger logger(&sink);
    logger.clear();
    std::vector<std::jthread> writers;
    for (auto index = 0; index < 4; ++index) {
        writers.emplace_back([&logger] {
            for (auto message = 0; message < 100; ++message) {
                logger.info("concurrent-message");
                if (message % 10 == 0) {
                    logger.flush();
                }
            }
        });
    }
    writers.clear();
    logger.flush();
    BOOST_TEST(sink.getEntries().size() == 400U);
}

BOOST_AUTO_TEST_CASE(FatalReportsFailureWithoutTerminatingTheProcess)
{
    MemorySink sink;
    Logger logger(&sink);
    logger.clear();
    BOOST_CHECK_EXCEPTION(
        logger.fatal("fatal-message"),
        NomadException,
        [](const auto& error) { return NomadString(error.what()) == "fatal-message"; }
    );
    BOOST_REQUIRE_EQUAL(sink.getEntries().size(), 1U);
    BOOST_TEST(sink.getEntries().front().message.find("fatal-message") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(CriticalFiltersLowerLevelsFlushesAndContinues)
{
    MemorySink sink;
    Logger logger(&sink);
    logger.clear();
    logger.setLogLevel(LogLevel::Critical);
    logger.error("filtered-error");
    BOOST_TEST(sink.getEntries().empty());
    logger.critical("critical-message");
    BOOST_REQUIRE_EQUAL(sink.getEntries().size(), 1U);
    BOOST_TEST((sink.getEntries().front().level == LogLevel::Critical));
    BOOST_TEST(sink.getEntries().front().message.find("[CRITICAL] critical-message") != NomadString::npos);
    BOOST_CHECK_THROW(logger.fatal("fatal-after-critical"), NomadException);
    BOOST_REQUIRE_EQUAL(sink.getEntries().size(), 2U);
    BOOST_TEST((sink.getEntries().back().level == LogLevel::Fatal));
}

BOOST_AUTO_TEST_SUITE_END()
