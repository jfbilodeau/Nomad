// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/log/Logger.hpp>
#include <nomad/log/LoggerObject.hpp>
#include <nomad/log/LogSinkObject.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptObject.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/ModuleHost.hpp>
#include <nomad/system/Path.hpp>
#include <nomad/system/ProjectSession.hpp>

#include <TestDirectory.hpp>

#include <boost/dll/shared_library.hpp>
#include <boost/io/ios_state.hpp>
#include <boost/test/unit_test.hpp>

#include <fstream>
#include <iterator>
#include <sstream>

using namespace nomad;

namespace {

struct LogConfigurationFixture {
    test::TestDirectory directory{"nomad-script-logging"};
    boost::dll::shared_library library;
    Logger logger;
    Runtime runtime;
    LogSinkId memoryId;
    RuntimeValue savedSink;
    NomadIndex sourceIndex = 0;

    explicit LogConfigurationFixture(const ModuleMode mode = ModuleMode::Application) {
        const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-log"});
        BOOST_REQUIRE_EQUAL(modules.size(), 1U);
        const auto path = modules.front().directory / getModuleLibraryName("nomad-log");
        library.load(path.native(), boost::dll::load_mode::load_with_altered_search_path);
        ModuleContext context{
            &runtime, nullptr, &logger, mode, directory.getPath(), modules.front().directory,
            {}, {}, {}, {}, nullptr
        };
        ModuleCallbacks callbacks;
        library.get<RegisterModuleFn>("registerModule")(&context, &callbacks);
        MemorySinkConfiguration memory;
        memory.level = LogLevel::Debug;
        memoryId = logger.createMemorySink(memory);
        logger.replaceSinks({memoryId});
        getMemory().clear();
    }

    [[nodiscard]] MemorySink& getMemory() {
        return *static_cast<MemorySink*>(logger.getSink(memoryId));
    }

    ~LogConfigurationFixture() {
        savedSink.freeObjectValue();
    }

    [[nodiscard]] CallableId compile(const NomadString& source) {
        const auto name = "logging" + std::to_string(sourceIndex++);
        auto compiler = runtime.createCompiler();
        CompilerContext context(compiler.get());
        const auto text = "fun " + name + " savedSink:log.Sink\n" + source + "\nend\n";
        compiler->registerSources(&context, {{{name + "Source", {name + ".nomad", text}}}, {}});
        const auto success = compiler->compileFunctions(&context);
        const auto* error = context.getFirstError();
        const auto message = error == nullptr ? NomadString("Unknown compilation error") : error->message;
        BOOST_REQUIRE_MESSAGE(success, message);
        return runtime.getFunctionId(name);
    }

    RuntimeValue execute(const NomadString& source) {
        RuntimeValue result;
        runtime.executeFunction(compile(source), {savedSink}, result);
        return result;
    }

    [[nodiscard]] NomadString read(const NomadPath& path) const {
        std::ifstream input(directory.getPath() / path, std::ios::binary);
        if (!input) {
            throw NomadException("Failed to read script log: " + pathToUtf8(path));
        }
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    void saveHandle(const LogSinkId id) {
        savedSink.freeObjectValue();
        savedSink.setObjectValue(new LogSinkObject(logger, id));
    }

    LogSinkId saveHandle(RuntimeValue& value) {
        const auto* object = dynamic_cast<LogSinkObject*>(value.getObjectValue());
        BOOST_REQUIRE(object != nullptr);
        const auto id = object->getSinkId();
        savedSink.freeObjectValue();
        savedSink.copyObjectValue(value);
        value.freeObjectValue();
        return id;
    }
};

const RuntimeValue& field(const ScriptObject& object, const NomadString& name) {
    const auto* type = object.getClassType();
    return object.getField(type->getFieldIndex(type->getProperty(name)->getId()));
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(log_configuration, LogConfigurationFixture)

BOOST_AUTO_TEST_CASE(configuration_objects_are_script_objects_with_matching_defaults) {
    auto baseValue = execute("return new log.SinkConfiguration");
    const auto* base = dynamic_cast<ScriptObject*>(baseValue.getObjectValue());
    BOOST_REQUIRE(base != nullptr);
    BOOST_TEST(base->getFieldCount() == 2U);
    BOOST_TEST(field(*base, "active").getBooleanValue());
    BOOST_TEST(field(*base, "level").getIntegerValue() == static_cast<NomadInteger>(LogLevel::Info));
    runtime.getTypeByName("log.SinkConfiguration")->freeValue(baseValue);

    auto fileValue = execute("return new log.FileSinkConfiguration");
    const auto* file = dynamic_cast<ScriptObject*>(fileValue.getObjectValue());
    BOOST_REQUIRE(file != nullptr);
    BOOST_TEST(file->getFieldCount() == 8U);
    BOOST_TEST(field(*file, "active").getBooleanValue());
    BOOST_TEST(field(*file, "level").getIntegerValue() == static_cast<NomadInteger>(LogLevel::Info));
    BOOST_TEST(field(*file, "directory").getStringObject()->getString() == "logs");
    BOOST_TEST(field(*file, "name").getStringObject()->getString() == "application");
    BOOST_TEST(field(*file, "rotationMode").getIntegerValue() == static_cast<NomadInteger>(FileRotationMode::Size));
    BOOST_TEST(field(*file, "rotateAtStartup").getBooleanValue());
    BOOST_TEST(field(*file, "maxSizeBytes").getIntegerValue() == 10 * 1024 * 1024);
    BOOST_TEST(field(*file, "maxFiles").getIntegerValue() == 10);
    runtime.getTypeByName("log.FileSinkConfiguration")->freeValue(fileValue);
    BOOST_TEST(!std::filesystem::exists(directory.getPath() / "logs"));
    BOOST_TEST(runtime.getTypeByName("log.ConsoleSinkConfiguration") == nullptr);
    BOOST_TEST(runtime.getNativeFunctionId("log.createMemorySink").isInvalid());
    BOOST_TEST(runtime.getTypeByName("log.SinkId") == nullptr);
}

BOOST_AUTO_TEST_CASE(logger_aliases_expose_live_native_settings_without_leaking_references) {
    const auto before = NomadObject::getLiveObjectCount();
    logger.setLogLevel(LogLevel::Warning);
    BOOST_TEST(execute("return log.logger.level").getIntegerValue() == static_cast<NomadInteger>(LogLevel::Warning));
    (void)execute(
        "alias = log.logger\n"
        "alias.level = log.Level.Debug\n"
        "alias.deliveryMode = log.DeliveryMode.Synchronous\n"
    );
    BOOST_TEST((logger.getLogLevel() == LogLevel::Debug));
    BOOST_TEST((logger.getDeliveryMode() == LogDeliveryMode::Synchronous));
    BOOST_TEST(execute("return log.logger.level").getIntegerValue() == static_cast<NomadInteger>(LogLevel::Debug));
    BOOST_TEST(execute("return log.logger.deliveryMode").getIntegerValue() ==
        static_cast<NomadInteger>(LogDeliveryMode::Synchronous));
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(logger_properties_reject_null_and_foreign_receivers) {
    const auto* type = runtime.getTypeByName("log.Logger")->asClassType();
    const auto levelId = type->getProperty("level")->getId();
    auto machine = runtime.createVirtualMachine();
    RuntimeValue receiver;
    RuntimeValue result;
    BOOST_CHECK_THROW(machine->getProperty(levelId, receiver, result), VirtualMachineException);
    BOOST_CHECK_THROW(machine->setProperty(levelId, receiver,
        RuntimeValue(static_cast<NomadInteger>(LogLevel::Debug))), VirtualMachineException);
    Logger otherLogger;
    receiver.setObjectValue(new LoggerObject(otherLogger));
    BOOST_CHECK_THROW(machine->getProperty(levelId, receiver, result), VirtualMachineException);
    BOOST_CHECK_THROW(machine->setProperty(levelId, receiver,
        RuntimeValue(static_cast<NomadInteger>(LogLevel::Debug))), VirtualMachineException);
    BOOST_TEST((logger.getLogLevel() == LogLevel::Info));
    BOOST_TEST((otherLogger.getLogLevel() == LogLevel::Info));
    receiver.freeObjectValue();
}

BOOST_AUTO_TEST_CASE(script_file_creation_uses_project_directory_and_rotates_whole_records) {
    directory.write(NomadPath("logs") / "application.log", "previous run\n");
    auto result = execute(
        "options = new log.FileSinkConfiguration\n"
        "options.maxSizeBytes = 1\n"
        "sink = log.createFileSink options\n"
        "options.name = \"not-the-active-file\"\n"
        "log.logger.deliveryMode = log.DeliveryMode.Synchronous\n"
        "log.info \"first-record\"\n"
        "log.info \"second-record\"\n"
        "return sink\n"
    );
    const auto id = saveHandle(result);
    const auto* file = dynamic_cast<FileSink*>(logger.getSink(id));
    BOOST_REQUIRE(file != nullptr);
    BOOST_TEST((file->getPath() == directory.getPath() / "logs" / "application.log"));
    BOOST_TEST(read(NomadPath("logs") / "application.log").find("second-record") != NomadString::npos);
    BOOST_TEST(read(NomadPath("logs") / "application.1.log").find("first-record") != NomadString::npos);
    BOOST_TEST(read(NomadPath("logs") / "application.2.log") == "previous run\n");
    BOOST_REQUIRE_EQUAL(getMemory().getEntries().size(), 2U);
    BOOST_TEST((logger.getDeliveryMode() == LogDeliveryMode::Synchronous));
}

BOOST_AUTO_TEST_CASE(console_settings_control_output_without_replacing_cpp_sinks) {
    std::ostringstream output;
    std::ostringstream errors;
    const boost::io::ios_rdbuf_saver restoreOutput(std::cout);
    const boost::io::ios_rdbuf_saver restoreErrors(std::cerr);
    std::cout.rdbuf(output.rdbuf());
    std::cerr.rdbuf(errors.rdbuf());
    auto result = execute(
        "log.logger.level = log.Level.Debug\n"
        "options = new log.SinkConfiguration\n"
        "options.active = false\n"
        "options.level = log.Level.Warning\n"
        "sink = log.createConsoleSink options\n"
        "log.info \"inactive-record\"\nlog.flush\n"
        "sink.active = true\n"
        "log.info \"filtered-record\"\n"
        "log.warning \"warning-record\"\nlog.flush\n"
        "sink.level = log.Level.Debug\n"
        "log.debug \"debug-record\"\nlog.flush\n"
        "log.error \"error-record\"\n"
        "return sink\n"
    );
    BOOST_TEST(output.str().find("inactive-record") == NomadString::npos);
    BOOST_TEST(output.str().find("filtered-record") == NomadString::npos);
    BOOST_TEST(output.str().find("warning-record") != NomadString::npos);
    BOOST_TEST(output.str().find("debug-record") != NomadString::npos);
    BOOST_TEST(errors.str().find("error-record") != NomadString::npos);
    BOOST_REQUIRE_EQUAL(getMemory().getEntries().size(), 5U);
    const auto id = saveHandle(result);
    BOOST_TEST(execute("return savedSink.active").getBooleanValue());
    BOOST_TEST(execute("return savedSink.level").getIntegerValue() == static_cast<NomadInteger>(LogLevel::Debug));
    BOOST_TEST(execute("return log.logger.level").getIntegerValue() == static_cast<NomadInteger>(LogLevel::Debug));
    BOOST_TEST(execute("return log.logger.deliveryMode").getIntegerValue() == static_cast<NomadInteger>(LogDeliveryMode::Buffered));
    logger.removeSink(id);
}

BOOST_AUTO_TEST_CASE(staged_file_settings_remain_adjustable_before_activation) {
    directory.write(NomadPath("logs") / "application.log", "previous run\n");
    auto result = execute(
        "options = new log.FileSinkConfiguration\noptions.active = false\n"
        "return log.createFileSink options\n"
    );
    (void)saveHandle(result);
    BOOST_TEST(read(NomadPath("logs") / "application.log") == "previous run\n");
    BOOST_TEST(!std::filesystem::exists(directory.getPath() / "logs" / "application.1.log"));
    (void)execute(
        "savedSink.rotateAtStartup = false\n"
        "savedSink.rotationMode = log.FileRotationMode.None\n"
        "log.setFileRotationLimits savedSink 64 0\n"
        "savedSink.active = true\n"
        "log.info \"appended-record\"\nlog.flush\n"
    );
    const auto content = read(NomadPath("logs") / "application.log");
    BOOST_TEST(content.starts_with("previous run\n"));
    BOOST_TEST(content.find("appended-record") != NomadString::npos);
    BOOST_TEST(!execute("return savedSink.rotateAtStartup").getBooleanValue());
    BOOST_TEST(execute("return savedSink.rotationMode").getIntegerValue() ==
        static_cast<NomadInteger>(FileRotationMode::None));
    BOOST_TEST(execute("return savedSink.maxSizeBytes").getIntegerValue() == 64);
    BOOST_TEST(execute("return savedSink.maxFiles").getIntegerValue() == 0);
    auto path = execute("return log.getFilePath savedSink");
    BOOST_TEST(path.getStringObject()->getString() == pathToUtf8(directory.getPath() / "logs" / "application.log"));
    runtime.getStringType()->freeValue(path);
}

BOOST_AUTO_TEST_CASE(absolute_directories_are_supported_and_configuration_is_copied) {
    const auto absolute = pathToGenericUtf8(directory.getPath() / "absolute");
    auto result = execute(
        "options = new log.FileSinkConfiguration\n"
        "options.directory = \"" + absolute + "\"\n"
        "options.name = \"session\"\noptions.rotateAtStartup = false\n"
        "sink = log.createFileSink options\n"
        "options.active = false\noptions.level = log.Level.Critical\n"
        "log.info \"absolute-record\"\nlog.flush\nreturn sink\n"
    );
    const auto id = saveHandle(result);
    const auto* file = dynamic_cast<FileSink*>(logger.getSink(id));
    BOOST_REQUIRE(file != nullptr);
    BOOST_TEST(file->getActive());
    BOOST_TEST((file->getLogLevel() == LogLevel::Info));
    BOOST_TEST(read(NomadPath("absolute") / "session.log").find("absolute-record") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(removed_handles_cannot_reach_replacements) {
    auto result = execute(
        "options = new log.SinkConfiguration\noptions.active = false\n"
        "return log.createConsoleSink options\n"
    );
    const auto oldId = saveHandle(result);
    (void)execute("log.removeSink savedSink");
    const auto replacement = logger.createMemorySink();
    BOOST_TEST(replacement != oldId);
    BOOST_CHECK_THROW((void)execute("log.removeSink savedSink"), VirtualMachineException);
    BOOST_CHECK_THROW((void)execute("return savedSink.level"), VirtualMachineException);
    BOOST_CHECK_THROW((void)execute("savedSink.active = true"), VirtualMachineException);
    BOOST_TEST(logger.getSink(replacement) != nullptr);
    BOOST_CHECK_THROW((void)execute("return savedSink.active"), VirtualMachineException);
}

BOOST_AUTO_TEST_CASE(sink_objects_alias_and_release_without_removing_the_sink) {
    const auto before = NomadObject::getLiveObjectCount();
    auto result = execute(
        "options = new log.SinkConfiguration\noptions.active = false\n"
        "sink = log.createConsoleSink options\n"
        "alias = sink\nalias.active = true\n"
        "return sink\n"
    );
    const auto id = saveHandle(result);
    BOOST_TEST(execute("return savedSink.active").getBooleanValue());
    (void)execute("savedSink.active = false");
    BOOST_TEST(!logger.getSink(id)->getActive());
    savedSink.freeObjectValue();
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    BOOST_REQUIRE(logger.getSink(id) != nullptr);
    BOOST_TEST(!logger.getSink(id)->getActive());
    logger.removeSink(id);
}

BOOST_AUTO_TEST_CASE(sink_functions_reject_objects_from_another_logger) {
    Logger otherLogger;
    const auto otherId = otherLogger.createMemorySink();
    BOOST_REQUIRE(otherId == memoryId);
    savedSink.setObjectValue(new LogSinkObject(otherLogger, otherId));
    for (const auto& source : {
        "return savedSink.active",
        "savedSink.active = false",
        "return savedSink.level",
        "savedSink.level = log.Level.Debug",
        "log.removeSink savedSink"
    }) {
        BOOST_CHECK_THROW((void)execute(source), VirtualMachineException);
    }
    BOOST_REQUIRE(logger.getSink(memoryId) != nullptr);
    BOOST_REQUIRE(otherLogger.getSink(otherId) != nullptr);
    BOOST_TEST(logger.getSink(memoryId)->getActive());
    BOOST_TEST(otherLogger.getSink(otherId)->getActive());
    savedSink.freeObjectValue();
}

BOOST_AUTO_TEST_CASE(print_and_stale_handle_checks_survive_clearing_all_sinks) {
    saveHandle(memoryId);
    std::ostringstream output;
    const boost::io::ios_rdbuf_saver restoreOutput(std::cout);
    std::cout.rdbuf(output.rdbuf());
    (void)execute(
        "log.logger.level = log.Level.Critical\n"
        "log.clearAllSinks\nlog.error \"not-delivered\"\nlog.flush\n"
        "print \"plain output\"\n"
    );
    BOOST_TEST(output.str() == "plain output\n");
    BOOST_TEST(logger.getSink(memoryId) == nullptr);
    BOOST_CHECK_THROW((void)execute("savedSink.level = log.Level.Info"), VirtualMachineException);
    BOOST_CHECK_THROW((void)execute("return savedSink.active"), VirtualMachineException);
}

BOOST_AUTO_TEST_CASE(invalid_configuration_and_wrong_sink_kinds_fail_explicitly) {
    for (const auto& assignment : {
        "options.maxFiles = -1", "options.maxSizeBytes = -1", "options.maxSizeBytes = 0",
        "options.directory = \"\"", "options.name = \"bad/name\""
    }) {
        BOOST_CHECK_THROW((void)execute(
            "options = new log.FileSinkConfiguration\noptions.active = false\n" +
            NomadString(assignment) + "\nreturn log.createFileSink options\n"
        ), VirtualMachineException);
    }
    BOOST_TEST(!std::filesystem::exists(directory.getPath() / "logs"));
    saveHandle(memoryId);
    BOOST_CHECK_THROW((void)execute("return savedSink.maxFiles"), VirtualMachineException);
    BOOST_CHECK_THROW((void)execute("return savedSink.rotationMode"), VirtualMachineException);
    BOOST_CHECK_THROW((void)execute("savedSink.rotationMode = log.FileRotationMode.None"), VirtualMachineException);
    BOOST_CHECK_THROW((void)execute("return savedSink.rotateAtStartup"), VirtualMachineException);
    BOOST_CHECK_THROW((void)execute("savedSink.rotateAtStartup = false"), VirtualMachineException);
    BOOST_CHECK_THROW((void)execute("log.setFileRotationLimits savedSink 4 2"), VirtualMachineException);
    BOOST_TEST(logger.getSink(memoryId) != nullptr);
}

BOOST_AUTO_TEST_CASE(invalid_enum_values_are_rejected_without_narrowing_them) {
    const auto* levelType = runtime.getTypeByName("log.Level");
    saveHandle(memoryId);
    for (const auto value : {NomadInteger{0}, NomadInteger{100}, (NomadInteger{1} << 32) + 2}) {
        const auto name = "invalidLevel" + std::to_string(sourceIndex);
        BOOST_REQUIRE(runtime.registerConstant(name, RuntimeValue(value), levelType).isValid());
        BOOST_CHECK_THROW((void)execute("log.logger.level = " + name), VirtualMachineException);
        BOOST_CHECK_THROW((void)execute("savedSink.level = " + name), VirtualMachineException);
        BOOST_TEST((logger.getLogLevel() == LogLevel::Info));
        BOOST_TEST((getMemory().getLogLevel() == LogLevel::Debug));
        const auto deliveryName = "invalidDelivery" + std::to_string(sourceIndex);
        BOOST_REQUIRE(runtime.registerConstant(deliveryName, RuntimeValue(value),
            runtime.getTypeByName("log.DeliveryMode")).isValid());
        BOOST_CHECK_THROW((void)execute("log.logger.deliveryMode = " + deliveryName), VirtualMachineException);
        BOOST_TEST((logger.getDeliveryMode() == LogDeliveryMode::Buffered));
    }
}

BOOST_AUTO_TEST_CASE(invalid_handles_and_null_configuration_fail_explicitly) {
    BOOST_CHECK_THROW((void)execute("log.removeSink savedSink"), VirtualMachineException);
    BOOST_CHECK_THROW((void)execute("return savedSink.active"), VirtualMachineException);
    auto machine = runtime.createVirtualMachine();
    BOOST_CHECK_THROW(machine->runNativeFunction(
        runtime.getNativeFunctionId("log.createConsoleSink"), {RuntimeValue()}
    ), VirtualMachineException);
    BOOST_CHECK_THROW(machine->runNativeFunction(
        runtime.getNativeFunctionId("log.createFileSink"), {RuntimeValue()}
    ), VirtualMachineException);
}

BOOST_AUTO_TEST_CASE(logging_types_are_distinct_and_fatal_and_memory_apis_are_not_exposed) {
    for (const auto& source : {
        "log.logger.level = 1", "log.removeSink 1", "log.logger.level = log.FileRotationMode.Size",
        "sink = log.createFileSink new log.FileSinkConfiguration\nsink.maxFiles = 3",
        "sink = log.createFileSink new log.FileSinkConfiguration\nsink.maxSizeBytes = 64",
        "log.logger = log.logger",
        "return new log.Logger",
        "return log.Level.Fatal", "log.fatal \"not available\"", "return log.createMemorySink",
        "options = new log.FileSinkConfiguration\nreturn log.createConsoleSink options"
    }) {
        LogConfigurationFixture rejected;
        auto compiler = rejected.runtime.createCompiler();
        CompilerContext context(compiler.get());
        compiler->registerSources(&context, {{{"rejected", {"rejected.nomad", source}}}, {}});
        BOOST_TEST(!compiler->compileFunctions(&context));
    }
}

BOOST_AUTO_TEST_CASE(configuration_lifetimes_do_not_retain_objects_after_execution) {
    const auto id = compile(
        "first = new log.FileSinkConfiguration\n"
        "second = first\nsecond.name = \"session\"\nreturn first.maxFiles\n"
    );
    const auto before = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 20; ++index) {
        RuntimeValue result;
        runtime.executeFunction(id, {savedSink}, result);
        BOOST_TEST(result.getIntegerValue() == 10);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(analysis_exposes_types_without_applying_logging_configuration) {
    LogConfigurationFixture analysis(ModuleMode::Analysis);
    BOOST_REQUIRE(analysis.runtime.getTypeByName("log.Sink") != nullptr);
    analysis.saveHandle(analysis.memoryId);
    BOOST_TEST(analysis.execute("return savedSink.active").getBooleanValue());
    BOOST_CHECK_THROW((void)analysis.execute("savedSink.active = false"), VirtualMachineException);
    (void)analysis.execute("options = new log.FileSinkConfiguration\nreturn options.active");
    BOOST_TEST(analysis.execute("return log.logger.level").getIntegerValue() == static_cast<NomadInteger>(LogLevel::Info));
    BOOST_TEST(analysis.execute("return log.logger.deliveryMode").getIntegerValue() ==
        static_cast<NomadInteger>(LogDeliveryMode::Buffered));
    BOOST_CHECK_THROW((void)analysis.execute("log.logger.level = log.Level.Debug"), VirtualMachineException);
    BOOST_CHECK_THROW((void)analysis.execute("log.logger.deliveryMode = log.DeliveryMode.Synchronous"), VirtualMachineException);
    BOOST_CHECK_THROW((void)analysis.execute("savedSink.level = log.Level.Debug"), VirtualMachineException);
    FileSinkConfiguration configuration;
    configuration.directory = analysis.directory.getPath() / "logs";
    configuration.active = false;
    const auto fileId = analysis.logger.createFileSink(configuration);
    analysis.saveHandle(fileId);
    BOOST_TEST(analysis.execute("return savedSink.maxFiles").getIntegerValue() == 10);
    BOOST_CHECK_THROW((void)analysis.execute("savedSink.rotationMode = log.FileRotationMode.None"), VirtualMachineException);
    BOOST_CHECK_THROW((void)analysis.execute("savedSink.rotateAtStartup = false"), VirtualMachineException);
    BOOST_CHECK_THROW((void)analysis.execute(
        "options = new log.FileSinkConfiguration\nreturn log.createFileSink options\n"
    ), VirtualMachineException);
    BOOST_TEST((analysis.logger.getLogLevel() == LogLevel::Info));
    BOOST_TEST(analysis.logger.getSink(analysis.memoryId) != nullptr);
    BOOST_TEST(!std::filesystem::exists(analysis.directory.getPath() / "logs"));
}

BOOST_AUTO_TEST_SUITE_END()
