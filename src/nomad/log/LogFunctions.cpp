// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/log/LogFunctions.hpp>

#include <nomad/log/Logger.hpp>
#include <nomad/log/LogSinkObject.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptObject.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/Module.hpp>
#include <nomad/system/Path.hpp>

#include <memory>
#include <type_traits>
#include <utility>

namespace nomad {

namespace {

template<typename Value>
Value readSetting(const RuntimeValue& value) {
    if constexpr (std::is_same_v<Value, NomadBoolean>) {
        return value.getBooleanValue();
    } else {
        const auto integer = value.getIntegerValue();
        if constexpr (std::is_enum_v<Value>) {
            if (!std::in_range<std::underlying_type_t<Value>>(integer)) {
                throw NomadException("Log enumeration value is out of range");
            }
        } else if (!std::in_range<Value>(integer)) {
            throw NomadException("Log size or archive count is out of range");
        }
        return static_cast<Value>(integer);
    }
}

template<typename Value>
RuntimeValue writeSetting(const Value value) {
    if constexpr (std::is_same_v<Value, NomadBoolean>) {
        return RuntimeValue(value);
    } else if constexpr (std::is_enum_v<Value>) {
        return RuntimeValue(static_cast<NomadInteger>(value));
    } else {
        if (!std::in_range<NomadInteger>(value)) {
            throw NomadException("Log size or archive count cannot be represented as a Nomad integer");
        }
        return RuntimeValue(static_cast<NomadInteger>(value));
    }
}

void registerFunction(
    ModuleContext* context,
    const NomadString& name,
    NativeFunctionFn function,
    const std::vector<Parameter>& parameters,
    const Type* returnType,
    const NomadString& documentation,
    const bool applicationOnly = true
) {
    const auto id = context->runtime->registerNativeFunction(
        name,
        [function = std::move(function), mode = context->mode, applicationOnly](VirtualMachine* machine) {
            if (applicationOnly && mode != ModuleMode::Application) {
                machine->fault("Logging configuration requires application mode");
            }
            try {
                function(machine);
            } catch (const NomadBug&) {
                throw;
            } catch (const NomadException& error) {
                machine->fault(error.what());
            } catch (const std::filesystem::filesystem_error& error) {
                machine->fault(error.what());
            }
        },
        parameters,
        returnType,
        NomadDoc(documentation)
    );
    if (id.isInvalid()) {
        throw NomadException("Failed to register " + name);
    }
}

const RuntimeValue& getField(const ScriptObject& object, const NomadString& name) {
    const auto* type = object.getClassType();
    const auto* property = type->getProperty(name);
    if (property == nullptr) {
        throw NomadBug("Missing log configuration field: " + name);
    }
    return object.getField(type->getFieldIndex(property->getId()));
}

void setField(ScriptObject& object, const NomadString& name, const RuntimeValue& value) {
    const auto* type = object.getClassType();
    const auto* property = type->getProperty(name);
    if (property == nullptr) {
        throw NomadBug("Missing log configuration field: " + name);
    }
    object.setField(type->getFieldIndex(property->getId()), value);
}

void setStringField(ScriptObject& object, const NomadString& name, const NomadString& text) {
    RuntimeValue value(text);
    try {
        setField(object, name, value);
    } catch (...) {
        value.freeObjectValue();
        throw;
    }
    value.freeObjectValue();
}

const ClassType* registerConfiguration(
    Runtime* runtime,
    const HubId hubId,
    const Type* levelType,
    const Type* rotationType,
    const bool file
) {
    const NomadString name = file ? "FileSinkConfiguration" : "SinkConfiguration";
    const auto id = runtime->registerHubClass(hubId, name, ClassMode::Closed,
        NomadDoc("Mutable creation settings; factories copy these values into a logger-owned sink."));
    if (id.isInvalid()) {
        throw NomadException("Failed to register " + name);
    }
    const auto* type = runtime->getType(id).value()->asClassType();
    const auto addField = [runtime, id](const NomadString& field, const Type* valueType) {
        const auto propertyId = runtime->registerProperty(id, field, valueType);
        if (propertyId.isInvalid()) {
            throw NomadException("Failed to register log configuration field: " + field);
        }
        runtime->setPropertyHasExplicitInitializer(propertyId, true);
    };
    addField("active", runtime->getBooleanType());
    addField("level", levelType);
    if (file) {
        addField("directory", runtime->getStringType());
        addField("name", runtime->getStringType());
        addField("rotationMode", rotationType);
        addField("rotateAtStartup", runtime->getBooleanType());
        addField("maxSizeBytes", runtime->getIntegerType());
        addField("maxFiles", runtime->getIntegerType());
    }
    if (runtime->addConstructor(id, {
        .parameters = {},
        .function = [type, file](VirtualMachine* machine) {
            auto object = std::make_unique<ScriptObject>(type);
            const SinkConfiguration defaults;
            setField(*object, "active", RuntimeValue(defaults.active));
            setField(*object, "level", writeSetting(defaults.level));
            if (file) {
                const FileSinkConfiguration fileDefaults;
                setStringField(*object, "directory", "logs");
                setStringField(*object, "name", fileDefaults.name);
                setField(*object, "rotationMode", writeSetting(fileDefaults.rotationMode));
                setField(*object, "rotateAtStartup", RuntimeValue(fileDefaults.rotateAtStartup));
                setField(*object, "maxSizeBytes", writeSetting(fileDefaults.maxSizeBytes));
                setField(*object, "maxFiles", writeSetting(fileDefaults.maxFiles));
            }
            machine->getResult().setObjectValue(object.release());
        },
        .documentation = NomadDoc("Creates independent logging configuration with the default settings.")
    }).isInvalid()) {
        throw NomadException("Failed to register constructor for " + name);
    }
    return type;
}

const ScriptObject& getConfiguration(const VirtualMachine* machine) {
    const auto* object = dynamic_cast<ScriptObject*>(machine->getParameter(0).getObjectValue());
    if (object == nullptr) {
        throw NomadException("Log sink configuration must not be null");
    }
    return *object;
}

LogSinkObject& getSinkObject(Logger* logger, const VirtualMachine* machine) {
    auto* object = dynamic_cast<LogSinkObject*>(machine->getParameter(0).getObjectValue());
    if (object == nullptr) {
        throw NomadException("Log sink must not be null");
    }
    if (&object->getLogger() != logger) {
        throw NomadException("Log sink belongs to a different logger");
    }
    return *object;
}

template<typename Sink = LogSink>
Sink& getSink(Logger* logger, const VirtualMachine* machine) {
    auto* concrete = dynamic_cast<Sink*>(&getSinkObject(logger, machine).getSink());
    if (concrete == nullptr) {
        throw NomadException("Log sink is not a file sink");
    }
    return *concrete;
}

template<typename Owner, typename Value, typename Resolve>
void registerSetting(
    ModuleContext* context,
    const NomadString& name,
    const Type* valueType,
    const std::vector<Parameter>& parameters,
    Resolve resolve,
    Value (Owner::*getter)() const,
    void (Owner::*setter)(Value)
) {
    registerFunction(context, "log.get" + name,
        [resolve, getter](VirtualMachine* machine) {
            machine->setResult(writeSetting((resolve(machine).*getter)()));
        },
        parameters, valueType, NomadDoc("Returns the current " + name + " setting."), false);
    if (setter != nullptr) {
        auto setterParameters = parameters;
        setterParameters.push_back(defParameter("value", valueType, NomadParamDoc("The new setting value.")));
        registerFunction(context, "log.set" + name,
            [resolve, setter, index = parameters.size()](VirtualMachine* machine) {
                (resolve(machine).*setter)(readSetting<Value>(machine->getParameter(index)));
            },
            setterParameters, context->runtime->getVoidType(), NomadDoc("Changes the " + name + " setting."));
    }
}

} // namespace

void registerLogConfigurationFunctions(ModuleContext* context, const HubId hubId) {
    auto* runtime = context->runtime;
    auto* logger = context->logger;
    const auto levelId = runtime->registerHubEnum(hubId, "Level", {
        {"Debug", static_cast<NomadInteger>(LogLevel::Debug)},
        {"Info", static_cast<NomadInteger>(LogLevel::Info)},
        {"Warning", static_cast<NomadInteger>(LogLevel::Warning)},
        {"Error", static_cast<NomadInteger>(LogLevel::Error)},
        {"Critical", static_cast<NomadInteger>(LogLevel::Critical)}
    }, NomadDoc("Minimum severity accepted by a logger or sink."));
    const auto rotationId = runtime->registerHubEnum(hubId, "FileRotationMode", {
        {"None", static_cast<NomadInteger>(FileRotationMode::None)},
        {"Size", static_cast<NomadInteger>(FileRotationMode::Size)}
    }, NomadDoc("Ongoing file rotation; startup rotation is configured independently."));
    const auto deliveryId = runtime->registerHubEnum(hubId, "DeliveryMode", {
        {"Buffered", static_cast<NomadInteger>(LogDeliveryMode::Buffered)},
        {"Synchronous", static_cast<NomadInteger>(LogDeliveryMode::Synchronous)}
    }, NomadDoc("Buffered host delivery or delivery before each logging call returns."));
    const auto* sinkType = runtime->getTypeByName("log.Sink");
    if (levelId.isInvalid() || rotationId.isInvalid() || deliveryId.isInvalid() || sinkType == nullptr) {
        throw NomadException("Failed to register log configuration types");
    }
    const auto* levelType = runtime->getType(levelId).value();
    const auto* rotationType = runtime->getType(rotationId).value();
    const auto* deliveryType = runtime->getType(deliveryId).value();
    const auto* configurationType = registerConfiguration(runtime, hubId, levelType, rotationType, false);
    const auto* fileConfigurationType = registerConfiguration(runtime, hubId, levelType, rotationType, true);
    const std::vector<Parameter> sinkParameters{
        defParameter("sink", sinkType, NomadParamDoc("A reference to a logger-owned sink."))
    };
    const auto resolveLogger = [logger](VirtualMachine*) -> Logger& { return *logger; };
    const auto resolveSink = [logger](VirtualMachine* machine) -> LogSink& { return getSink(logger, machine); };
    const auto resolveFile = [logger](VirtualMachine* machine) -> FileSink& { return getSink<FileSink>(logger, machine); };
    registerSetting(context, "LogLevel", levelType, {}, resolveLogger, &Logger::getLogLevel, &Logger::setLogLevel);
    registerSetting(context, "DeliveryMode", deliveryType, {}, resolveLogger,
        &Logger::getDeliveryMode, &Logger::setDeliveryMode);
    registerSetting(context, "SinkLevel", levelType, sinkParameters, resolveSink,
        &LogSink::getLogLevel, &LogSink::setLogLevel);
    registerSetting(context, "SinkActive", runtime->getBooleanType(), sinkParameters, resolveSink,
        &LogSink::getActive, &LogSink::setActive);
    registerSetting(context, "FileRotationMode", rotationType, sinkParameters, resolveFile,
        &FileSink::getRotationMode, &FileSink::setRotationMode);
    registerSetting(context, "FileRotateAtStartup", runtime->getBooleanType(), sinkParameters, resolveFile,
        &FileSink::getRotateAtStartup, &FileSink::setRotateAtStartup);
    registerSetting(context, "FileMaxSizeBytes", runtime->getIntegerType(), sinkParameters, resolveFile,
        &FileSink::getMaxSizeBytes, static_cast<void (FileSink::*)(NomadIndex)>(nullptr));
    registerSetting(context, "FileMaxFiles", runtime->getIntegerType(), sinkParameters, resolveFile,
        &FileSink::getMaxFiles, static_cast<void (FileSink::*)(NomadIndex)>(nullptr));
    registerFunction(context, "log.createConsoleSink",
        [logger](VirtualMachine* machine) {
            const auto& object = getConfiguration(machine);
            ConsoleSinkConfiguration configuration;
            configuration.active = getField(object, "active").getBooleanValue();
            configuration.level = readSetting<LogLevel>(getField(object, "level"));
            machine->getResult().setObjectValue(new LogSinkObject(*logger, logger->createConsoleSink(configuration)));
        },
        {defParameter("configuration", configurationType, NomadParamDoc("The console sink creation settings."))},
        sinkType, NomadDoc("Creates a logger-owned console sink without removing existing sinks."));
    registerFunction(context, "log.createFileSink",
        [logger, projectDirectory = context->projectDirectory](VirtualMachine* machine) {
            const auto& object = getConfiguration(machine);
            FileSinkConfiguration configuration;
            configuration.active = getField(object, "active").getBooleanValue();
            configuration.level = readSetting<LogLevel>(getField(object, "level"));
            const auto* directory = getField(object, "directory").getStringObject();
            const auto* name = getField(object, "name").getStringObject();
            if (directory == nullptr || name == nullptr || directory->getString().empty()) {
                throw NomadException("Log file directory and name must not be null or empty");
            }
            configuration.directory = pathFromUtf8(directory->getString());
            if (configuration.directory.is_relative()) {
                configuration.directory = projectDirectory / configuration.directory;
            }
            configuration.name = name->getString();
            configuration.rotationMode = readSetting<FileRotationMode>(getField(object, "rotationMode"));
            configuration.rotateAtStartup = getField(object, "rotateAtStartup").getBooleanValue();
            configuration.maxSizeBytes = readSetting<NomadIndex>(getField(object, "maxSizeBytes"));
            configuration.maxFiles = readSetting<NomadIndex>(getField(object, "maxFiles"));
            machine->getResult().setObjectValue(new LogSinkObject(*logger, logger->createFileSink(configuration)));
        },
        {defParameter("configuration", fileConfigurationType, NomadParamDoc("The file sink creation settings."))},
        sinkType, NomadDoc("Creates a logger-owned file sink; relative directories use the project directory."));
    registerFunction(context, "log.removeSink",
        [logger](VirtualMachine* machine) { logger->removeSink(getSinkObject(logger, machine).getSinkId()); },
        sinkParameters, runtime->getVoidType(), NomadDoc("Flushes and removes a sink; unknown or removed handles fail."));
    registerFunction(context, "log.clearAllSinks",
        [logger](VirtualMachine*) { logger->clearAllSinks(); },
        {}, runtime->getVoidType(), NomadDoc("Flushes and removes all sinks without installing a replacement."));
    registerFunction(context, "log.flush",
        [logger](VirtualMachine*) { logger->flush(); },
        {}, runtime->getVoidType(), NomadDoc("Delivers pending messages and flushes active sinks."));
    registerFunction(context, "log.getFilePath",
        [logger](VirtualMachine* machine) {
            machine->setStringResult(pathToUtf8(getSink<FileSink>(logger, machine).getPath()));
        },
        sinkParameters, runtime->getStringType(), NomadDoc("Returns the resolved active file path."), false);
    auto rotationParameters = sinkParameters;
    rotationParameters.push_back(defParameter("maxSizeBytes", runtime->getIntegerType(),
        NomadParamDoc("The positive byte limit, including record newlines.")));
    rotationParameters.push_back(defParameter("maxFiles", runtime->getIntegerType(),
        NomadParamDoc("The nonnegative number of archives, excluding the active file.")));
    registerFunction(context, "log.setFileRotationLimits",
        [logger](VirtualMachine* machine) {
            const auto size = readSetting<NomadIndex>(machine->getParameter(1));
            const auto files = readSetting<NomadIndex>(machine->getParameter(2));
            getSink<FileSink>(logger, machine).setRotationLimits(size, files);
        },
        rotationParameters, runtime->getVoidType(), NomadDoc("Changes rotation limits for subsequent writes."));
}

} // namespace nomad
