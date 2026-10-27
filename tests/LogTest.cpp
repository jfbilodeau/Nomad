// Copyright (c) 2025-2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/io/ios_state.hpp>
#include <boost/test/unit_test.hpp>

#include <nomad/log/ConsoleSink.hpp>
#include <nomad/log/Logger.hpp>
#include <nomad/log/MemorySink.hpp>

#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace nomad;

namespace {

struct MemoryLoggerFixture {
    Logger logger;
    LogSinkId sinkId = logger.createMemorySink();
    MemorySink& sink = *static_cast<MemorySink*>(logger.getSink(sinkId));

    MemoryLoggerFixture() {
        logger.replaceSinks({sinkId});
        sink.clear();
    }
};

struct SinkObservation {
    std::vector<LogEntry> entries;
    NomadString failure;
    std::function<void()> onLog;
    NomadIndex destructions = 0;
};

class ObservedSink final : public LogSink {
public:
    explicit ObservedSink(
        std::shared_ptr<SinkObservation> observation,
        const SinkConfiguration& configuration = {}
    ) :
        LogSink(configuration),
        m_observation(std::move(observation))
    {}

    ~ObservedSink() override {
        ++m_observation->destructions;
    }

    void begin() override {
        if (m_observation->failure == "begin") {
            throw NomadException("Sink begin failed");
        }
    }

    void log(const LogEntry* entry) override {
        if (m_observation->failure == "log") {
            throw NomadException("Sink log failed");
        }
        m_observation->entries.push_back(*entry);
        if (m_observation->onLog) {
            m_observation->onLog();
        }
    }

    void end() override {
        if (m_observation->failure == "end") {
            throw NomadException("Sink end failed");
        }
    }

private:
    std::shared_ptr<SinkObservation> m_observation;
};

static_assert(std::is_base_of_v<SinkConfiguration, ConsoleSinkConfiguration>);
static_assert(std::is_base_of_v<SinkConfiguration, MemorySinkConfiguration>);
static_assert(!std::is_convertible_v<LogSinkId, NomadId>);
static_assert(!std::is_constructible_v<Logger, LogSink*>);

} // namespace

BOOST_FIXTURE_TEST_SUITE(LogTests, MemoryLoggerFixture)

BOOST_AUTO_TEST_CASE(LoggingToMemorySink)
{
    logger.info("Test message from unit test");
    logger.flush();

    const auto& entries = sink.getEntries();
    BOOST_REQUIRE_EQUAL(entries.size(), 1U);

    // The last entry should contain our test message text.
    auto last = entries.back();
    BOOST_TEST(last.message.find("Test message from unit test") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(LogLevelFiltering)
{
    // Set logger level to Warning so Debug and Info messages are filtered out.
    logger.setLogLevel(LogLevel::Warning);

    logger.debug("debug-msg");
    logger.info("info-msg");
    logger.warning("warning-msg");

    logger.flush();

    const auto& entries = sink.getEntries();
    BOOST_REQUIRE_EQUAL(entries.size(), 1U);

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

BOOST_AUTO_TEST_CASE(DefaultSinkLevelFiltersDebugUntilExplicitlyLowered)
{
    BOOST_TEST((sink.getLogLevel() == LogLevel::Info));
    logger.setLogLevel(LogLevel::Debug);
    logger.debug("sink-filtered");
    logger.flush();
    BOOST_TEST(sink.getEntries().empty());
    sink.setLogLevel(LogLevel::Debug);
    logger.debug("included-debug");
    logger.flush();
    BOOST_REQUIRE_EQUAL(sink.getEntries().size(), 1U);
    BOOST_TEST(sink.getEntries().front().message.find("included-debug") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(LoggingStringViewDoesNotRequireNullTermination)
{
    const NomadString backing = "xxview messageyy";
    const NomadStringView message(backing.data() + 2, 12);

    logger.info(message);
    logger.flush();

    const auto& entries = sink.getEntries();
    BOOST_REQUIRE(!entries.empty());
    BOOST_TEST(entries.back().message.find("view message") != NomadString::npos);
    BOOST_TEST(entries.back().message.find("yy") == NomadString::npos);
}

BOOST_AUTO_TEST_CASE(SuppressingAndRestoringLogLevel)
{
    // The debug console raises the log level to Fatal while it re-evaluates
    // dynamic variables so per-frame getter warnings do not flood the log,
    // then restores the previous level.
    logger.setLogLevel(LogLevel::Warning);
    const auto previousLevel = logger.getLogLevel();
    BOOST_TEST((previousLevel == LogLevel::Warning));

    logger.setLogLevel(LogLevel::Fatal);
    logger.warning("suppressed-warning");
    logger.flush();

    for (const auto& entry : sink.getEntries()) {
        BOOST_TEST(entry.message.find("suppressed-warning") == std::string::npos);
    }

    logger.setLogLevel(previousLevel);
    BOOST_TEST((logger.getLogLevel() == LogLevel::Warning));

    logger.warning("restored-warning");
    logger.flush();

    bool foundRestored = false;
    for (const auto& entry : sink.getEntries()) {
        if (entry.message.find("restored-warning") != std::string::npos) {
            foundRestored = true;
            break;
        }
    }

    BOOST_TEST(foundRestored);
}

BOOST_AUTO_TEST_CASE(ClearingPendingEntries)
{
    logger.info("discarded");
    logger.clear();
    logger.info("retained");
    logger.flush();

    const auto& entries = sink.getEntries();
    BOOST_REQUIRE(entries.size() == 1U);
    BOOST_TEST(entries.front().message.find("retained") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(ConcurrentSubmissionAndDelivery)
{
    logger.clear();
    std::vector<std::jthread> writers;
    for (auto index = 0; index < 4; ++index) {
        writers.emplace_back([this] {
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

BOOST_AUTO_TEST_CASE(OwnedSinkLookupRemovalAndIds)
{
    const auto observation = std::make_shared<SinkObservation>();
    const auto ownedId = logger.createSink(std::make_unique<ObservedSink>(observation));
    BOOST_REQUIRE(logger.getSink(ownedId) != nullptr);
    BOOST_TEST(std::as_const(logger).getSink(ownedId) == logger.getSink(ownedId));
    BOOST_TEST(logger.getSink(LogSinkId::INVALID) == nullptr);
    BOOST_TEST(logger.getSink(LogSinkId{NOMAD_ID_MAX}) == nullptr);
    logger.info("before-removal");
    logger.removeSink(ownedId);
    BOOST_REQUIRE_EQUAL(observation->entries.size(), 1U);
    BOOST_TEST(observation->entries.front().message.find("before-removal") != NomadString::npos);
    BOOST_TEST(observation->destructions == 1U);
    BOOST_TEST(logger.getSink(ownedId) == nullptr);
    BOOST_CHECK_THROW(logger.removeSink(ownedId), NomadException);
    const auto nextId = logger.createMemorySink();
    BOOST_TEST(nextId.id > ownedId.id);
}

BOOST_AUTO_TEST_CASE(InactiveFactoryConfigurationAndMutableSettings)
{
    ConsoleSinkConfiguration console;
    console.active = false;
    console.level = LogLevel::Error;
    const auto consoleId = logger.createConsoleSink(console);
    BOOST_REQUIRE(dynamic_cast<ConsoleSink*>(logger.getSink(consoleId)) != nullptr);
    BOOST_TEST(!logger.getSink(consoleId)->getActive());
    BOOST_TEST((logger.getSink(consoleId)->getLogLevel() == LogLevel::Error));
    MemorySinkConfiguration configuration;
    configuration.active = false;
    configuration.level = LogLevel::Warning;
    const auto inactiveId = logger.createMemorySink(configuration);
    auto* inactiveSink = dynamic_cast<MemorySink*>(logger.getSink(inactiveId));
    BOOST_REQUIRE(inactiveSink != nullptr);
    BOOST_TEST(!inactiveSink->getActive());
    BOOST_TEST((inactiveSink->getLogLevel() == LogLevel::Warning));
    logger.warning("while-inactive");
    logger.flush();
    BOOST_TEST(inactiveSink->getEntries().empty());
    inactiveSink->setActive(true);
    logger.info("below-sink-level");
    logger.warning("accepted-warning");
    logger.flush();
    BOOST_REQUIRE_EQUAL(inactiveSink->getEntries().size(), 1U);
    BOOST_TEST(inactiveSink->getEntries().front().message.find("accepted-warning") != NomadString::npos);
    inactiveSink->setLogLevel(LogLevel::Info);
    logger.info("updated-level");
    logger.flush();
    BOOST_TEST(inactiveSink->getEntries().size() == 2U);
    inactiveSink->setActive(false);
    logger.critical("disabled-critical");
    BOOST_TEST(inactiveSink->getEntries().size() == 2U);
}

BOOST_AUTO_TEST_CASE(GlobalLevelRemainsTheFirstGate)
{
    MemorySinkConfiguration configuration;
    configuration.level = LogLevel::Warning;
    const auto warningId = logger.createMemorySink(configuration);
    const auto* warningSink = static_cast<MemorySink*>(logger.getSink(warningId));
    logger.debug("globally-filtered");
    logger.info("info");
    logger.warning("warning");
    logger.flush();
    BOOST_TEST(sink.getEntries().size() == 2U);
    BOOST_REQUIRE_EQUAL(warningSink->getEntries().size(), 1U);
    BOOST_TEST((warningSink->getEntries().front().level == LogLevel::Warning));
}

BOOST_AUTO_TEST_CASE(ReplacementFlushesOldSinksBeforeActivatingStagedSinks)
{
    const auto observation = std::make_shared<SinkObservation>();
    const auto oldId = logger.createSink(std::make_unique<ObservedSink>(observation));
    logger.replaceSinks({oldId});
    logger.info("old-message");
    MemorySinkConfiguration staged;
    staged.active = false;
    const auto stagedId = logger.createMemorySink(staged);
    logger.replaceSinks({stagedId});
    const auto* replacement = static_cast<MemorySink*>(logger.getSink(stagedId));
    BOOST_REQUIRE(replacement != nullptr);
    BOOST_TEST(replacement->getActive());
    BOOST_TEST(replacement->getEntries().empty());
    BOOST_TEST(logger.getSink(sinkId) == nullptr);
    BOOST_TEST(logger.getSink(oldId) == nullptr);
    BOOST_TEST(observation->destructions == 1U);
    BOOST_REQUIRE_EQUAL(observation->entries.size(), 1U);
    BOOST_TEST(observation->entries.front().message.find("old-message") != NomadString::npos);
    logger.info("new-message");
    logger.flush();
    BOOST_REQUIRE_EQUAL(replacement->getEntries().size(), 1U);
    BOOST_TEST(replacement->getEntries().front().message.find("new-message") != NomadString::npos);
    logger.flush();
    BOOST_TEST(replacement->getEntries().size() == 1U);
}

BOOST_AUTO_TEST_CASE(ClearAllSinksFlushesDestroysAndDoesNotRecycleIds)
{
    const auto observation = std::make_shared<SinkObservation>();
    const auto ownedId = logger.createSink(std::make_unique<ObservedSink>(observation));
    logger.info("before-clear");
    logger.clearAllSinks();
    BOOST_REQUIRE_EQUAL(observation->entries.size(), 1U);
    BOOST_TEST(observation->destructions == 1U);
    BOOST_TEST(logger.getSink(ownedId) == nullptr);
    BOOST_TEST(logger.getSink(sinkId) == nullptr);
    logger.setLogLevel(LogLevel::Fatal);
    BOOST_CHECK_EXCEPTION(logger.fatal("fatal-without-sinks"), NomadException,
        [](const auto& error) { return NomadString(error.what()) == "fatal-without-sinks"; });
    const auto recreatedId = logger.createMemorySink();
    BOOST_TEST(recreatedId.id > ownedId.id);
    BOOST_TEST(logger.getSink(ownedId) == nullptr);
}

BOOST_AUTO_TEST_CASE(BufferedAndSynchronousDeliveryAreExplicit)
{
    BOOST_TEST((logger.getDeliveryMode() == LogDeliveryMode::Buffered));
    logger.info("buffered");
    BOOST_TEST(sink.getEntries().empty());
    logger.setDeliveryMode(LogDeliveryMode::Synchronous);
    BOOST_REQUIRE_EQUAL(sink.getEntries().size(), 1U);
    logger.info("synchronous");
    BOOST_TEST(sink.getEntries().size() == 2U);
    logger.setDeliveryMode(LogDeliveryMode::Buffered);
    logger.info("buffered-again");
    BOOST_TEST(sink.getEntries().size() == 2U);
    logger.flush();
    BOOST_TEST(sink.getEntries().size() == 3U);
}

BOOST_AUTO_TEST_CASE(InvalidConfigurationPreservesExistingSinksAndPendingMessages)
{
    logger.info("still-pending");
    BOOST_CHECK_THROW(logger.replaceSinks({sinkId, sinkId}), NomadException);
    BOOST_CHECK_THROW(logger.replaceSinks({LogSinkId::INVALID}), NomadException);
    BOOST_CHECK_THROW(logger.removeSink(LogSinkId::INVALID), NomadException);
    BOOST_CHECK_THROW(static_cast<void>(logger.createSink(nullptr)), NomadException);
    BOOST_CHECK_THROW(logger.setLogLevel(static_cast<LogLevel>(0)), NomadException);
    BOOST_CHECK_THROW(sink.setLogLevel(static_cast<LogLevel>(100)), NomadException);
    BOOST_CHECK_THROW(logger.setDeliveryMode(static_cast<LogDeliveryMode>(0)), NomadException);
    MemorySinkConfiguration invalid;
    invalid.level = static_cast<LogLevel>(0);
    BOOST_CHECK_THROW(static_cast<void>(logger.createMemorySink(invalid)), NomadException);
    BOOST_TEST(logger.getSink(sinkId) == &sink);
    BOOST_TEST(sink.getEntries().empty());
    BOOST_TEST((logger.getLogLevel() == LogLevel::Info));
    BOOST_TEST((logger.getDeliveryMode() == LogDeliveryMode::Buffered));
    BOOST_TEST((sink.getLogLevel() == LogLevel::Info));
    logger.flush();
    BOOST_REQUIRE_EQUAL(sink.getEntries().size(), 1U);
}

BOOST_AUTO_TEST_CASE(DeliveryFailureDoesNotCommitReplacementOrReplayTheFailedBatch)
{
    for (const auto* failure : {"begin", "log", "end"}) {
        const auto observation = std::make_shared<SinkObservation>();
        const auto oldId = logger.createSink(std::make_unique<ObservedSink>(observation));
        logger.replaceSinks({oldId});
        MemorySinkConfiguration staged;
        staged.active = false;
        const auto stagedId = logger.createMemorySink(staged);
        observation->failure = failure;
        logger.info("failed-batch");
        BOOST_CHECK_THROW(logger.replaceSinks({stagedId}), NomadException);
        BOOST_TEST(logger.getSink(oldId) != nullptr);
        BOOST_TEST(logger.getSink(oldId)->getActive());
        BOOST_TEST(!logger.getSink(stagedId)->getActive());
        BOOST_TEST(observation->destructions == 0U);
        observation->failure.clear();
        const auto deliveredBeforeRecovery = observation->entries.size();
        logger.flush();
        BOOST_TEST(observation->entries.size() == deliveredBeforeRecovery);
        logger.info("after-failure");
        logger.flush();
        BOOST_TEST(observation->entries.size() == deliveredBeforeRecovery + 1U);
        logger.replaceSinks({stagedId});
        BOOST_TEST(observation->destructions == 1U);
        BOOST_TEST(logger.getSink(stagedId)->getActive());
        BOOST_TEST(static_cast<MemorySink*>(logger.getSink(stagedId))->getEntries().empty());
    }
}

BOOST_AUTO_TEST_CASE(RemovalAndClearFailuresKeepOwnership)
{
    const auto observation = std::make_shared<SinkObservation>();
    const auto ownedId = logger.createSink(std::make_unique<ObservedSink>(observation));
    observation->failure = "end";
    BOOST_CHECK_THROW(logger.removeSink(ownedId), NomadException);
    BOOST_CHECK_THROW(logger.clearAllSinks(), NomadException);
    BOOST_TEST(logger.getSink(ownedId) != nullptr);
    BOOST_TEST(logger.getSink(sinkId) == &sink);
    BOOST_TEST(observation->destructions == 0U);
    observation->failure.clear();
    logger.removeSink(ownedId);
    BOOST_TEST(observation->destructions == 1U);
}

BOOST_AUTO_TEST_CASE(FailedSynchronousTransitionPreservesBufferedMode)
{
    const auto observation = std::make_shared<SinkObservation>();
    const auto ownedId = logger.createSink(std::make_unique<ObservedSink>(observation));
    observation->failure = "end";
    BOOST_CHECK_THROW(logger.setDeliveryMode(LogDeliveryMode::Synchronous), NomadException);
    BOOST_TEST((logger.getDeliveryMode() == LogDeliveryMode::Buffered));
    observation->failure.clear();
    logger.removeSink(ownedId);
}

BOOST_AUTO_TEST_CASE(ReentrantDeliveryAndRemovalFailExplicitlyAndRemainRecoverable)
{
    const auto observation = std::make_shared<SinkObservation>();
    const auto ownedId = logger.createSink(std::make_unique<ObservedSink>(observation));
    observation->onLog = [this] { logger.clearAllSinks(); };
    logger.info("reentrant-clear");
    BOOST_CHECK_THROW(logger.flush(), NomadException);
    BOOST_TEST(logger.getSink(ownedId) != nullptr);
    observation->onLog = [this] { logger.info("recursive-message"); };
    logger.info("reentrant-log");
    BOOST_CHECK_THROW(logger.flush(), NomadException);
    observation->onLog = {};
    logger.info("recovered");
    logger.flush();
    BOOST_TEST(observation->entries.back().message.find("recovered") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(LoggerDestructionFlushesBeforeDestroyingOwnedSinks)
{
    const auto observation = std::make_shared<SinkObservation>();
    {
        Logger ownedLogger;
        const auto ownedId = ownedLogger.createSink(std::make_unique<ObservedSink>(observation));
        ownedLogger.replaceSinks({ownedId});
        observation->entries.clear();
        ownedLogger.info("pending-at-shutdown");
        BOOST_TEST(observation->destructions == 0U);
    }
    BOOST_TEST(observation->destructions == 1U);
    BOOST_REQUIRE_EQUAL(observation->entries.size(), 2U);
    BOOST_TEST(observation->entries.front().message.find("pending-at-shutdown") != NomadString::npos);
    BOOST_TEST(observation->entries.back().message.find("Logger shutting down.") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(ConcurrentSubmissionAndReplacementDeliverEachMessageOnce)
{
    const auto observation = std::make_shared<SinkObservation>();
    const auto firstId = logger.createSink(std::make_unique<ObservedSink>(observation));
    logger.replaceSinks({firstId});
    std::vector<std::jthread> writers;
    for (auto index = 0; index < 4; ++index) {
        writers.emplace_back([this, index] {
            for (auto message = 0; message < 100; ++message) {
                logger.info("concurrent-replacement-message-" + std::to_string(index) + "-" + std::to_string(message));
            }
        });
    }
    SinkConfiguration staged;
    staged.active = false;
    for (auto index = 0; index < 20; ++index) {
        const auto nextId = logger.createSink(std::make_unique<ObservedSink>(observation, staged));
        logger.replaceSinks({nextId});
    }
    writers.clear();
    logger.flush();
    BOOST_TEST(observation->entries.size() == 400U);
    std::unordered_set<NomadString> deliveredMessages;
    for (const auto& entry : observation->entries) {
        const auto start = entry.message.find("concurrent-replacement-message-");
        BOOST_REQUIRE(start != NomadString::npos);
        const auto end = entry.message.find(" [", start);
        BOOST_REQUIRE(end != NomadString::npos);
        BOOST_TEST(deliveredMessages.insert(entry.message.substr(start, end - start)).second);
    }
    for (auto writer = 0; writer < 4; ++writer) {
        for (auto message = 0; message < 100; ++message) {
            BOOST_TEST(deliveredMessages.contains(
                "concurrent-replacement-message-" + std::to_string(writer) + "-" + std::to_string(message)
            ));
        }
    }
    BOOST_TEST(observation->destructions == 20U);
}

BOOST_AUTO_TEST_CASE(ConcurrentSynchronousSubmissionIsSerialized)
{
    logger.setDeliveryMode(LogDeliveryMode::Synchronous);
    std::vector<std::jthread> writers;
    for (auto index = 0; index < 4; ++index) {
        writers.emplace_back([this] {
            for (auto message = 0; message < 100; ++message) {
                logger.info("synchronous-message");
            }
        });
    }
    writers.clear();
    BOOST_TEST(sink.getEntries().size() == 400U);
}

BOOST_AUTO_TEST_CASE(ConsoleWriteAndFlushFailuresAreExplicit)
{
    ConsoleSink console;
    const LogEntry entry{std::chrono::system_clock::now(), LogLevel::Info, "failed-output"};
    {
        const boost::io::ios_iostate_saver restore(std::cout);
        std::cout.setstate(std::ios::badbit);
        BOOST_CHECK_THROW(console.log(&entry), NomadException);
        BOOST_CHECK_THROW(console.end(), NomadException);
    }
    {
        const boost::io::ios_iostate_saver restore(std::cerr);
        std::cerr.setstate(std::ios::badbit);
        const LogEntry error{std::chrono::system_clock::now(), LogLevel::Error, "failed-error"};
        BOOST_CHECK_THROW(console.log(&error), NomadException);
        BOOST_CHECK_THROW(console.end(), NomadException);
    }
}

BOOST_AUTO_TEST_SUITE_END()
