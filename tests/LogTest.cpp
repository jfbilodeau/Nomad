// Copyright (c) 2025-2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include "nomad/log/Logger.hpp"
#include "nomad/log/MemorySink.hpp"

#include <string>

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

BOOST_AUTO_TEST_SUITE_END()
