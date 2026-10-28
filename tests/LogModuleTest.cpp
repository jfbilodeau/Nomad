// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/log/ConsoleSink.hpp>
#include <nomad/log/Logger.hpp>
#include <nomad/log/MemorySink.hpp>

#include <nomad/script/Hub.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/ModuleHost.hpp>
#include <nomad/system/Path.hpp>
#include <nomad/system/ProjectSession.hpp>

#include <TestDirectory.hpp>

#include <boost/io/ios_state.hpp>
#include <boost/test/unit_test.hpp>

#include <array>
#include <iostream>
#include <sstream>
#include <tuple>

using namespace nomad;
using namespace nomad::test;

namespace {

class RegisteredMemorySink final {
public:
    RegisteredMemorySink() : m_previousLevel(log::getLogLevel()) {
        log::flush();
        MemorySinkConfiguration configuration;
        configuration.level = LogLevel::Debug;
        m_sinkId = log::createMemorySink(configuration);
    }

    ~RegisteredMemorySink() {
        if (log::getSink(m_sinkId) != nullptr) {
            log::removeSink(m_sinkId);
        }
        log::setLogLevel(m_previousLevel);
    }

    [[nodiscard]] const std::vector<LogEntry>& getEntries() const {
        const auto* sink = dynamic_cast<MemorySink*>(log::getSink(m_sinkId));
        if (sink == nullptr) {
            throw NomadException("Captured log sink is no longer available");
        }
        return sink->getEntries();
    }

private:
    LogLevel m_previousLevel;
    LogSinkId m_sinkId;
};

} // namespace

BOOST_AUTO_TEST_SUITE(core_print)

BOOST_AUTO_TEST_CASE(no_modules_prints_exact_text_independently_of_logging) {
    const TestDirectory directory("core-print");
    const auto host = ModuleHost::create({}, directory.getPath(), {}, {}, ModuleMode::Application);
    auto& runtime = *host->getRuntime();
    BOOST_REQUIRE(runtime.getNativeFunctionId("print").isValid());
    const auto compiled = host->compileSources({{{"main", {"print.nomad",
        "print \"plain text\"\nprint \"\"\nprint $\"value: {6 * 7}\"\n"
    }}}, {}});
    BOOST_REQUIRE(compiled.succeeded());
    RegisteredMemorySink sink;
    log::setLogLevel(LogLevel::Fatal);
    std::ostringstream output;
    const boost::io::ios_rdbuf_saver restoreOutput(std::cout);
    std::cout.rdbuf(output.rdbuf());
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(output.str() == "plain text\n\nvalue: 42\n");
    log::flush();
    BOOST_TEST(sink.getEntries().empty());
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(log_module)

BOOST_AUTO_TEST_CASE(all_logging_functions_are_registered_as_local_hub_members) {
    const TestDirectory directory("log-module-functions");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-log"});
    for (const auto mode : {ModuleMode::Application, ModuleMode::Analysis}) {
        const auto host = ModuleHost::create({}, directory.getPath(), modules, {}, mode);
        const auto& runtime = *host->getRuntime();
        const auto* hub = runtime.getHub(runtime.getHubId("log"));
        BOOST_REQUIRE(hub != nullptr);
        for (const auto* name : {
            "debug", "info", "warning", "error", "critical",
            "createConsoleSink", "createFileSink",
            "removeSink", "clearAllSinks", "flush", "getFilePath", "setFileRotationLimits"
        }) {
            BOOST_TEST_CONTEXT(name) {
                const auto* member = hub->getMember(name);
                BOOST_REQUIRE(member != nullptr);
                const auto* functions = std::get_if<HubFunctions>(member);
                BOOST_REQUIRE(functions != nullptr);
                BOOST_REQUIRE_EQUAL(functions->ids.size(), 1U);
                const auto id = runtime.getNativeFunctionId("log." + NomadString(name));
                BOOST_REQUIRE(id.isValid());
                BOOST_TEST(functions->ids.front() == id);
                BOOST_TEST(runtime.getNativeFunctionId(name).isInvalid());
            }
            for (const auto* name : {
                "getLogLevel", "setLogLevel", "getDeliveryMode", "setDeliveryMode",
                "getSinkLevel", "setSinkLevel", "getSinkActive", "setSinkActive",
                "getFileRotationMode", "setFileRotationMode", "getFileRotateAtStartup", "setFileRotateAtStartup",
                "getFileMaxSizeBytes", "getFileMaxFiles"
            }) {
                BOOST_TEST(hub->getMember(name) == nullptr);
                BOOST_TEST(runtime.getNativeFunctionId("log." + NomadString(name)).isInvalid());
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(module_registers_sink_type_and_active_property) {
    const TestDirectory directory("log-module-sink-type");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-log"});
    for (const auto mode : {ModuleMode::Application, ModuleMode::Analysis}) {
        const auto host = ModuleHost::create({}, directory.getPath(), modules, {}, mode);
        const auto& runtime = *host->getRuntime();
        const auto* hub = runtime.getHub(runtime.getHubId("log"));
        BOOST_REQUIRE(hub != nullptr);
        for (const auto* name : {"Logger", "Sink", "SinkConfiguration", "FileSinkConfiguration"}) {
            const auto* member = hub->getMember(name);
            BOOST_REQUIRE(member != nullptr);
            const auto* registeredType = std::get<const Type*>(*member);
            BOOST_TEST(registeredType == runtime.getTypeByName("log." + NomadString(name)));
            BOOST_TEST(runtime.getTypeByName(name) == nullptr);
            BOOST_REQUIRE(registeredType->asClassType() != nullptr);
            BOOST_TEST(!registeredType->asClassType()->isOpen());
        }
        const auto* type = host->getRuntime()->getTypeByName("log.Sink");
        BOOST_REQUIRE(type != nullptr);
        const auto* classType = type->asClassType();
        BOOST_REQUIRE(classType != nullptr);
        BOOST_TEST(classType->getConstructors().empty());
        BOOST_TEST(classType->getPropertyCount() == 6U);
        const auto* active = classType->getProperty("active");
        BOOST_REQUIRE(active != nullptr);
        BOOST_TEST(active->getValueType() == host->getRuntime()->getBooleanType());
        const auto* loggerType = runtime.getTypeByName("log.Logger");
        BOOST_REQUIRE(loggerType != nullptr);
        BOOST_TEST(loggerType->asClassType()->getConstructors().empty());
        BOOST_TEST(loggerType->asClassType()->getPropertyCount() == 2U);
        const auto* member = hub->getMember("logger");
        BOOST_REQUIRE(member != nullptr);
        const auto* loggerId = std::get_if<ConstantId>(member);
        BOOST_REQUIRE(loggerId != nullptr);
        BOOST_TEST(runtime.getConstantType(*loggerId) == loggerType);
    }
}

BOOST_AUTO_TEST_CASE(module_registers_logging_enums_and_values_in_the_log_hub) {
    const TestDirectory directory("log-module-enums");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-log"});
    for (const auto mode : {ModuleMode::Application, ModuleMode::Analysis}) {
        const auto host = ModuleHost::create({}, directory.getPath(), modules, {}, mode);
        const auto& runtime = *host->getRuntime();
        const auto* hub = runtime.getHub(runtime.getHubId("log"));
        BOOST_REQUIRE(hub != nullptr);
        for (const auto& [name, valueName, value] : {
            std::tuple{"Level", "Info", static_cast<NomadInteger>(LogLevel::Info)},
            std::tuple{"FileRotationMode", "Size", static_cast<NomadInteger>(FileRotationMode::Size)},
            std::tuple{"DeliveryMode", "Buffered", static_cast<NomadInteger>(LogDeliveryMode::Buffered)}
        }) {
            const auto* member = hub->getMember(name);
            BOOST_REQUIRE(member != nullptr);
            const auto* type = std::get<const Type*>(*member);
            BOOST_TEST(type == runtime.getTypeByName("log." + NomadString(name)));
            BOOST_TEST(runtime.getTypeByName(name) == nullptr);
            const auto* constant = hub->getMember(NomadString(name) + "." + valueName);
            BOOST_REQUIRE(constant != nullptr);
            const auto constantId = std::get<ConstantId>(*constant);
            BOOST_TEST(runtime.getConstantType(constantId) == type);
            RuntimeValue registeredValue;
            runtime.getConstantValue(constantId, registeredValue);
            BOOST_TEST(registeredValue.getIntegerValue() == value);
        }
    }
}

BOOST_AUTO_TEST_CASE(module_is_independent_and_script_functions_preserve_locations) {
    const TestDirectory directory("log-module");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-log"});
    BOOST_REQUIRE_EQUAL(modules.size(), 1U);
    BOOST_TEST(modules.front().name == "nomad-log");
    const auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Application);
    auto& runtime = *host->getRuntime();
    BOOST_TEST(runtime.isHub("log"));
    const auto* infoMember = runtime.getHub(runtime.getHubId("log"))->getMember("info");
    BOOST_REQUIRE(infoMember != nullptr);
    const auto* infoFunctions = std::get_if<HubFunctions>(infoMember);
    BOOST_REQUIRE(infoFunctions != nullptr);
    BOOST_REQUIRE_EQUAL(infoFunctions->ids.size(), 1U);
    BOOST_TEST(infoFunctions->ids.front() == runtime.getNativeFunctionId("log.info"));
    BOOST_TEST(runtime.getNativeFunctionId("info").isInvalid());
    BOOST_TEST(runtime.getNativeFunctionId("system.exit").isInvalid());
    BOOST_TEST(runtime.getNativeFunctionId("log.fatal").isInvalid());
    const auto compiled = host->compileSources({{{"main", {"logging-script.nomad",
        "log.debug \"script-debug\"\n"
        "log.info \"script-info\"\n"
        "log.warning \"script-warning\"\n"
        "log.error \"script-error\"\n"
        "log.critical \"script-critical\"\n"
        "return 42\n"
    }}}, {}});
    BOOST_REQUIRE(compiled.succeeded());
    RegisteredMemorySink sink;
    log::setLogLevel(LogLevel::Debug);
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(result.getIntegerValue() == 42);
    BOOST_REQUIRE_EQUAL(sink.getEntries().size(), 5U);
    const std::array levels{LogLevel::Debug, LogLevel::Info, LogLevel::Warning, LogLevel::Error, LogLevel::Critical};
    for (auto index = NomadIndex{0}; index < levels.size(); ++index) {
        const auto& entry = sink.getEntries()[index];
        BOOST_TEST((entry.level == levels[index]));
        BOOST_TEST(entry.message.find("[main]") != NomadString::npos);
        BOOST_TEST(entry.message.find("[logging-script.nomad:" + std::to_string(index + 1) + ":0]") != NomadString::npos);
    }
    BOOST_TEST(sink.getEntries().back().message.find("[CRITICAL] script-critical") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(open_hub_can_be_extended_and_fatal_is_not_callable) {
    const TestDirectory directory("log-module-open-hub");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-log"});
    const auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Application);
    const auto compiled = host->compileSources({{{"main", {"extension.nomad",
        "open hub log\nfun answer\nreturn 42\nend\nend\nreturn log.answer"
    }}}, {}});
    BOOST_REQUIRE(compiled.succeeded());
    RuntimeValue result;
    host->getRuntime()->executeFunction(host->getRuntime()->getFunctionId("main"), {}, result);
    BOOST_TEST(result.getIntegerValue() == 42);
    const auto rejected = host->compileSources({{{"invalid", {"fatal.nomad", "log.fatal \"not available\""}}}, {}});
    BOOST_TEST(!rejected.succeeded());
}

BOOST_AUTO_TEST_CASE(system_module_does_not_supply_logging) {
    const TestDirectory directory("system-without-log");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-system"});
    BOOST_REQUIRE_EQUAL(modules.size(), 1U);
    const auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    BOOST_TEST(!host->getRuntime()->isHub("log"));
    BOOST_TEST(host->getRuntime()->getNativeFunctionId("log.info").isInvalid());
    const auto compiled = host->compileSources({{{"main", {"system.nomad", "log.info \"not available\""}}}, {}},
        CompilationTarget::Analysis);
    BOOST_TEST(!compiled.succeeded());
}

BOOST_AUTO_TEST_CASE(analysis_exposes_logging_and_packaging_requires_no_runtime) {
    const TestDirectory directory("log-module-modes");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-log"});
    const auto host = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Analysis);
    for (const auto* name : {"log.debug", "log.info", "log.warning", "log.error", "log.critical"}) {
        BOOST_TEST_CONTEXT(name) {
            BOOST_TEST(host->getRuntime()->getNativeFunctionId(name).isValid());
        }
    }
    const auto compiled = host->compileSources({{{"main", {"analysis.nomad",
        "log.debug \"debug\"\nlog.info \"info\"\nlog.warning \"warning\"\n"
        "log.error \"error\"\nlog.critical \"critical\""
    }}}, {}}, CompilationTarget::Analysis);
    BOOST_TEST(compiled.succeeded());
    const auto packaging = ModuleHost::create({}, directory.getPath(), modules, {}, ModuleMode::Packaging);
    BOOST_TEST(packaging->getRuntime() == nullptr);
}

BOOST_AUTO_TEST_CASE(console_routes_critical_to_standard_error) {
    std::ostringstream output;
    std::ostringstream errors;
    const boost::io::ios_rdbuf_saver restoreOutput(std::cout);
    const boost::io::ios_rdbuf_saver restoreErrors(std::cerr);
    std::cout.rdbuf(output.rdbuf());
    std::cerr.rdbuf(errors.rdbuf());
    ConsoleSink sink;
    const LogEntry entry{std::chrono::system_clock::now(), LogLevel::Critical, "critical-output"};
    sink.log(&entry);
    BOOST_TEST(output.str().empty());
    BOOST_TEST(errors.str() == "critical-output\n");
}

BOOST_AUTO_TEST_SUITE_END()
