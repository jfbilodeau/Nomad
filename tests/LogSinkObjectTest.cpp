// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/log/Logger.hpp>
#include <nomad/log/LogSinkObject.hpp>

#include <nomad/script/Hub.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/RuntimeValue.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/ModuleHost.hpp>
#include <nomad/system/ProjectSession.hpp>

#include <boost/dll/shared_library.hpp>
#include <boost/test/unit_test.hpp>

#include <memory>
#include <type_traits>

using namespace nomad;

namespace {

void loadLogModule(boost::dll::shared_library& library, Runtime& runtime, Logger& logger) {
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"log"});
    BOOST_REQUIRE_EQUAL(modules.size(), 1U);
    const auto path = modules.front().directory / getModuleLibraryName("log");
    library.load(path.native(), boost::dll::load_mode::load_with_altered_search_path);
    ModuleContext context{
        &runtime, 
        nullptr, 
        {},
        &logger, 
        ModuleMode::Application, 
        {}, 
        modules.front().directory,
        {},
        {}, 
        {}, 
        {}, 
        nullptr
    };
    ModuleCallbacks callbacks;
    library.get<RegisterModuleFn>("registerModule")(&context, &callbacks);
    const auto* type = runtime.getTypeByName("log.Sink");
    BOOST_REQUIRE(type != nullptr);
    BOOST_REQUIRE(type->asClassType() != nullptr);
}

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

BOOST_AUTO_TEST_CASE(registers_closed_sink_type_with_native_live_properties) {
    boost::dll::shared_library library;
    Runtime runtime;
    loadLogModule(library, runtime, logger);
    const auto hubId = runtime.getHubId("log");
    BOOST_REQUIRE(hubId.isValid());
    const auto* type = runtime.getTypeByName("log.Sink")->asClassType();
    BOOST_REQUIRE(type != nullptr);
    BOOST_TEST(type->getTypeName() == "log.Sink");
    BOOST_TEST(!type->isOpen());
    BOOST_TEST(type->getPropertyCount() == 6U);
    BOOST_TEST(type->getConstructors().empty());
    BOOST_TEST(type->getMethods().empty());
    const auto* active = type->getProperty("active");
    BOOST_REQUIRE(active != nullptr);
    BOOST_TEST(active->getValueType() == runtime.getBooleanType());
    const auto info = runtime.getNativePropertyInfo(active->getId());
    BOOST_REQUIRE(info.has_value());
    BOOST_TEST(static_cast<bool>(info->getFn));
    BOOST_TEST(static_cast<bool>(info->setFn));
    for (const auto* name : {"level", "rotationMode", "rotateAtStartup", "maxSizeBytes", "maxFiles"}) {
        BOOST_TEST_CONTEXT(name) {
            const auto* property = type->getProperty(name);
            BOOST_REQUIRE(property != nullptr);
            const auto native = runtime.getNativePropertyInfo(property->getId());
            BOOST_REQUIRE(native.has_value());
            BOOST_TEST(static_cast<bool>(native->getFn));
            const auto readOnly = NomadStringView(name) == "maxSizeBytes" || NomadStringView(name) == "maxFiles";
            BOOST_TEST(static_cast<bool>(native->setFn) == !readOnly);
        }
    }
    const auto* member = runtime.getHub(hubId)->getMember("Sink");
    BOOST_REQUIRE(member != nullptr);
    BOOST_REQUIRE(std::holds_alternative<const Type*>(*member));
    BOOST_TEST(std::get<const Type*>(*member) == type);
}

BOOST_AUTO_TEST_CASE(native_active_property_reads_current_state_and_controls_delivery) {
    boost::dll::shared_library library;
    Runtime runtime;
    loadLogModule(library, runtime, logger);
    const auto* type = runtime.getTypeByName("log.Sink")->asClassType();
    const auto activeId = type->getProperty("active")->getId();
    auto machine = runtime.createVirtualMachine();
    auto object = std::make_unique<LogSinkObject>(logger, sinkId);
    RuntimeValue receiver;
    receiver.setObjectValue(object.get());
    RuntimeValue result;
    machine->getProperty(activeId, receiver, result);
    BOOST_TEST(result.getBooleanValue());
    machine->setProperty(activeId, receiver, RuntimeValue(false));
    BOOST_TEST(!logger.getSink(sinkId)->getActive());
    logger.info("disabled record");
    logger.flush();
    BOOST_TEST(static_cast<MemorySink*>(logger.getSink(sinkId))->getEntries().empty());
    machine->getProperty(activeId, receiver, result);
    BOOST_TEST(!result.getBooleanValue());
    logger.getSink(sinkId)->setActive(true);
    machine->getProperty(activeId, receiver, result);
    BOOST_TEST(result.getBooleanValue());
    machine->setProperty(activeId, receiver, RuntimeValue(true));
    logger.info("enabled record");
    logger.flush();
    const auto& entries = static_cast<MemorySink*>(logger.getSink(sinkId))->getEntries();
    BOOST_REQUIRE_EQUAL(entries.size(), 1U);
    BOOST_TEST(entries.front().message.find("enabled record") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(native_active_property_rejects_removed_sink_without_touching_replacement) {
    boost::dll::shared_library library;
    Runtime runtime;
    loadLogModule(library, runtime, logger);
    const auto* type = runtime.getTypeByName("log.Sink")->asClassType();
    const auto activeId = type->getProperty("active")->getId();
    auto machine = runtime.createVirtualMachine();
    auto object = std::make_unique<LogSinkObject>(logger, sinkId);
    RuntimeValue receiver;
    receiver.setObjectValue(object.get());
    logger.removeSink(sinkId);
    const auto replacementId = logger.createMemorySink();
    RuntimeValue result;
    BOOST_CHECK_EXCEPTION(machine->getProperty(activeId, receiver, result), NomadException,
        [](const NomadException& error) {
            return NomadString(error.what()).find("Unknown or removed log sink ID:") != NomadString::npos;
        });
    BOOST_CHECK_THROW(machine->setProperty(activeId, receiver, RuntimeValue(false)), NomadException);
    BOOST_TEST(logger.getSink(replacementId)->getActive());
}

BOOST_AUTO_TEST_CASE(native_active_property_rejects_cleared_sink) {
    boost::dll::shared_library library;
    Runtime runtime;
    loadLogModule(library, runtime, logger);
    const auto* type = runtime.getTypeByName("log.Sink")->asClassType();
    const auto activeId = type->getProperty("active")->getId();
    auto machine = runtime.createVirtualMachine();
    auto object = std::make_unique<LogSinkObject>(logger, sinkId);
    RuntimeValue receiver;
    receiver.setObjectValue(object.get());
    logger.clearAllSinks();
    RuntimeValue result;
    BOOST_CHECK_THROW(machine->getProperty(activeId, receiver, result), NomadException);
    BOOST_CHECK_THROW(machine->setProperty(activeId, receiver, RuntimeValue(true)), NomadException);
}

BOOST_AUTO_TEST_SUITE_END()
