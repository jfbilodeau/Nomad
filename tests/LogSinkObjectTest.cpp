// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/log/Logger.hpp>
#include <nomad/log/LogSinkObject.hpp>

#include <nomad/script/RuntimeValue.hpp>

#include <boost/test/unit_test.hpp>

#include <type_traits>

using namespace nomad;

namespace {

struct LogSinkObjectFixture {
    Logger logger;
    LogSinkId sinkId = logger.createMemorySink();

    LogSinkObjectFixture() {
        logger.replaceSinks({sinkId});
        static_cast<MemorySink*>(logger.getSink(sinkId))->clear();
    }
};

static_assert(std::is_base_of_v<NomadObject, LogSinkObject>);
static_assert(!std::is_copy_constructible_v<LogSinkObject>);
static_assert(!std::is_copy_assignable_v<LogSinkObject>);

} // namespace

BOOST_FIXTURE_TEST_SUITE(log_sink_object, LogSinkObjectFixture)

BOOST_AUTO_TEST_CASE(resolves_id_through_the_owning_logger) {
    const LogSinkObject object(logger, sinkId);
    BOOST_TEST(object.getSinkId() == sinkId);
    BOOST_TEST(&object.getSink() == logger.getSink(sinkId));
    object.getSink().setLogLevel(LogLevel::Warning);
    BOOST_TEST((logger.getSink(sinkId)->getLogLevel() == LogLevel::Warning));
}

BOOST_AUTO_TEST_CASE(rejects_invalid_unknown_and_already_removed_ids) {
    BOOST_CHECK_THROW((void)LogSinkObject(logger, LogSinkId::INVALID), NomadException);
    BOOST_CHECK_THROW((void)LogSinkObject(logger, LogSinkId{NOMAD_ID_MAX - 1}), NomadException);
    logger.removeSink(sinkId);
    BOOST_CHECK_THROW((void)LogSinkObject(logger, sinkId), NomadException);
}

BOOST_AUTO_TEST_CASE(removed_id_does_not_resolve_to_a_new_sink) {
    const LogSinkObject object(logger, sinkId);
    logger.removeSink(sinkId);
    const auto replacementId = logger.createMemorySink();
    BOOST_TEST(replacementId != sinkId);
    BOOST_TEST(object.getSinkId() == sinkId);
    BOOST_CHECK_THROW((void)object.getSink(), NomadException);
    BOOST_TEST(logger.getSink(replacementId) != nullptr);
}

BOOST_AUTO_TEST_CASE(clear_and_replacement_invalidate_only_removed_sinks) {
    const LogSinkObject retained(logger, sinkId);
    const auto removedId = logger.createMemorySink();
    const LogSinkObject removed(logger, removedId);
    logger.replaceSinks({sinkId});
    BOOST_TEST(&retained.getSink() == logger.getSink(sinkId));
    BOOST_CHECK_THROW((void)removed.getSink(), NomadException);
    logger.clearAllSinks();
    BOOST_CHECK_THROW((void)retained.getSink(), NomadException);
}

BOOST_AUTO_TEST_CASE(wrapper_destruction_does_not_remove_or_deactivate_sink) {
    {
        const LogSinkObject object(logger, sinkId);
        BOOST_TEST(object.getSink().getActive());
    }
    BOOST_REQUIRE(logger.getSink(sinkId) != nullptr);
    BOOST_TEST(logger.getSink(sinkId)->getActive());
    logger.info("sink remains owned by logger");
    logger.flush();
    const auto* memory = static_cast<MemorySink*>(logger.getSink(sinkId));
    BOOST_REQUIRE_EQUAL(memory->getEntries().size(), 1U);
    BOOST_TEST(memory->getEntries().front().message.find("sink remains owned by logger") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(runtime_value_owns_the_wrapper_not_the_sink) {
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue value;
    value.setObjectValue(new LogSinkObject(logger, sinkId));
    BOOST_TEST(value.asObject<LogSinkObject>()->getSinkId() == sinkId);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before + 1);
    RuntimeValue alias;
    alias.copyObjectValue(value);
    value.freeObjectValue();
    BOOST_TEST(&alias.asObject<LogSinkObject>()->getSink() == logger.getSink(sinkId));
    alias.freeObjectValue();
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    BOOST_TEST(logger.getSink(sinkId) != nullptr);
}

BOOST_AUTO_TEST_CASE(logger_local_ids_resolve_against_the_stored_owner) {
    Logger otherLogger;
    const auto otherId = otherLogger.createMemorySink();
    otherLogger.replaceSinks({otherId});
    BOOST_REQUIRE(otherId == sinkId);
    const LogSinkObject object(logger, sinkId);
    BOOST_TEST(&object.getSink() != otherLogger.getSink(otherId));
    logger.removeSink(sinkId);
    BOOST_CHECK_THROW((void)object.getSink(), NomadException);
    BOOST_TEST(otherLogger.getSink(otherId) != nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
