// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/log/Logger.hpp>
#include <nomad/log/LoggerObject.hpp>
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

template<typename Function>
void executeLoggingOperation(VirtualMachine* machine, Function&& function) {
    try {
        function();
    } catch (const NomadBug&) {
        throw;
    } catch (const NomadException& error) {
        machine->fault(error.what());
    } catch (const std::filesystem::filesystem_error& error) {
        machine->fault(error.what());
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

LogSinkObject& getSinkObject(Logger* logger, LogSinkObject* object) {
    if (object == nullptr) {
        throw NomadException("Log sink must not be null");
    }
    if (&object->getLogger() != logger) {
        throw NomadException("Log sink belongs to a different logger");
    }
    return *object;
}

FileSink& getFileSink(Logger* logger, LogSinkObject* object) {
    auto* sink = dynamic_cast<FileSink*>(&getSinkObject(logger, object).getSink());
    if (sink == nullptr) {
        throw NomadException("Log sink is not a file sink");
    }
    return *sink;
}

HubId registerLogHub(Runtime& runtime) {
    const auto hubId = runtime.registerHub("log", HubMode::Opened);
    if (hubId.isInvalid()) {
        throw NomadException("Failed to register log hub");
    }
    return hubId;
}

void registerLogEnums(Runtime& runtime, const HubId hubId) {
    const auto levelId = runtime.registerHubEnum(hubId, "Level", {
        {"Debug", static_cast<NomadInteger>(LogLevel::Debug)},
        {"Info", static_cast<NomadInteger>(LogLevel::Info)},
        {"Warning", static_cast<NomadInteger>(LogLevel::Warning)},
        {"Error", static_cast<NomadInteger>(LogLevel::Error)},
        {"Critical", static_cast<NomadInteger>(LogLevel::Critical)}
    }, NomadDoc("Minimum severity accepted by a logger or sink."));
    if (levelId.isInvalid()) {
        throw NomadException("Failed to register log.Level");
    }

    const auto rotationId = runtime.registerHubEnum(hubId, "FileRotationMode", {
        {"None", static_cast<NomadInteger>(FileRotationMode::None)},
        {"Size", static_cast<NomadInteger>(FileRotationMode::Size)}
    }, NomadDoc("Ongoing file rotation; startup rotation is configured independently."));
    if (rotationId.isInvalid()) {
        throw NomadException("Failed to register log.FileRotationMode");
    }

    const auto deliveryId = runtime.registerHubEnum(hubId, "DeliveryMode", {
        {"Buffered", static_cast<NomadInteger>(LogDeliveryMode::Buffered)},
        {"Synchronous", static_cast<NomadInteger>(LogDeliveryMode::Synchronous)}
    }, NomadDoc("Buffered host delivery or delivery before each logging call returns."));
    if (deliveryId.isInvalid()) {
        throw NomadException("Failed to register log.DeliveryMode");
    }
}

void registerSinkConfigurationClass(Runtime& runtime, const HubId hubId) {
    const auto typeId = runtime.registerHubClass(hubId, "SinkConfiguration", ClassMode::Closed,
        NomadDoc("Mutable creation settings; factories copy these values into a logger-owned sink."));
    if (typeId.isInvalid()) {
        throw NomadException("Failed to register log.SinkConfiguration");
    }
    const auto* type = runtime.getType(typeId).value()->asClassType();

    const auto activeId = runtime.registerProperty(typeId, "active", runtime.getBooleanType());
    if (activeId.isInvalid()) {
        throw NomadException("Failed to register log.SinkConfiguration.active");
    }
    runtime.setPropertyHasExplicitInitializer(activeId, true);

    const auto levelId = runtime.registerProperty(typeId, "level", runtime.getTypeByName("log.Level"));
    if (levelId.isInvalid()) {
        throw NomadException("Failed to register log.SinkConfiguration.level");
    }
    runtime.setPropertyHasExplicitInitializer(levelId, true);

    const auto constructorId = runtime.addConstructor(typeId, {
        .parameters = {},
        .function = [type](VirtualMachine* machine) {
            auto object = std::make_unique<ScriptObject>(type);
            const SinkConfiguration defaults;
            setField(*object, "active", RuntimeValue(defaults.active));
            setField(*object, "level", writeSetting(defaults.level));
            machine->getResult().setObjectValue(object.release());
        },
        .documentation = NomadDoc("Creates independent logging configuration with the default settings.")
    });
    if (constructorId.isInvalid()) {
        throw NomadException("Failed to register constructor for log.SinkConfiguration");
    }
}

void registerFileSinkConfigurationClass(Runtime& runtime, const HubId hubId) {
    const auto typeId = runtime.registerHubClass(hubId, "FileSinkConfiguration", ClassMode::Closed,
        NomadDoc("Mutable creation settings; factories copy these values into a logger-owned sink."));
    if (typeId.isInvalid()) {
        throw NomadException("Failed to register log.FileSinkConfiguration");
    }
    const auto* type = runtime.getType(typeId).value()->asClassType();

    const auto activeId = runtime.registerProperty(typeId, "active", runtime.getBooleanType());
    if (activeId.isInvalid()) {
        throw NomadException("Failed to register log.FileSinkConfiguration.active");
    }
    runtime.setPropertyHasExplicitInitializer(activeId, true);

    const auto levelId = runtime.registerProperty(typeId, "level", runtime.getTypeByName("log.Level"));
    if (levelId.isInvalid()) {
        throw NomadException("Failed to register log.FileSinkConfiguration.level");
    }
    runtime.setPropertyHasExplicitInitializer(levelId, true);

    const auto directoryId = runtime.registerProperty(typeId, "directory", runtime.getStringType());
    if (directoryId.isInvalid()) {
        throw NomadException("Failed to register log.FileSinkConfiguration.directory");
    }
    runtime.setPropertyHasExplicitInitializer(directoryId, true);

    const auto nameId = runtime.registerProperty(typeId, "name", runtime.getStringType());
    if (nameId.isInvalid()) {
        throw NomadException("Failed to register log.FileSinkConfiguration.name");
    }
    runtime.setPropertyHasExplicitInitializer(nameId, true);

    const auto rotationId = runtime.registerProperty(typeId, "rotationMode", runtime.getTypeByName("log.FileRotationMode"));
    if (rotationId.isInvalid()) {
        throw NomadException("Failed to register log.FileSinkConfiguration.rotationMode");
    }
    runtime.setPropertyHasExplicitInitializer(rotationId, true);

    const auto startupId = runtime.registerProperty(typeId, "rotateAtStartup", runtime.getBooleanType());
    if (startupId.isInvalid()) {
        throw NomadException("Failed to register log.FileSinkConfiguration.rotateAtStartup");
    }
    runtime.setPropertyHasExplicitInitializer(startupId, true);

    const auto sizeId = runtime.registerProperty(typeId, "maxSizeBytes", runtime.getIntegerType());
    if (sizeId.isInvalid()) {
        throw NomadException("Failed to register log.FileSinkConfiguration.maxSizeBytes");
    }
    runtime.setPropertyHasExplicitInitializer(sizeId, true);

    const auto filesId = runtime.registerProperty(typeId, "maxFiles", runtime.getIntegerType());
    if (filesId.isInvalid()) {
        throw NomadException("Failed to register log.FileSinkConfiguration.maxFiles");
    }
    runtime.setPropertyHasExplicitInitializer(filesId, true);

    const auto constructorId = runtime.addConstructor(typeId, {
        .parameters = {},
        .function = [type](VirtualMachine* machine) {
            auto object = std::make_unique<ScriptObject>(type);
            const FileSinkConfiguration defaults;
            setField(*object, "active", RuntimeValue(defaults.active));
            setField(*object, "level", writeSetting(defaults.level));
            setStringField(*object, "directory", "logs");
            setStringField(*object, "name", defaults.name);
            setField(*object, "rotationMode", writeSetting(defaults.rotationMode));
            setField(*object, "rotateAtStartup", RuntimeValue(defaults.rotateAtStartup));
            setField(*object, "maxSizeBytes", writeSetting(defaults.maxSizeBytes));
            setField(*object, "maxFiles", writeSetting(defaults.maxFiles));
            machine->getResult().setObjectValue(object.release());
        },
        .documentation = NomadDoc("Creates independent logging configuration with the default settings.")
    });
    if (constructorId.isInvalid()) {
        throw NomadException("Failed to register constructor for log.FileSinkConfiguration");
    }
}

void registerLoggerClass(ModuleContext* context, const HubId hubId) {
    auto* runtime = context->runtime;
    const auto typeId = runtime->registerHubClass(hubId, "Logger", ClassMode::Closed,
        NomadDoc("A reference to the host logger. The host must outlive all references."));
    if (typeId.isInvalid()) {
        throw NomadException("Failed to register log.Logger");
    }

    const auto levelId = runtime->addProperty(typeId, {
        .valueTypeId = runtime->getTypeByName("log.Level")->getId(),
        .name = "level",
        .getter = [](VirtualMachine* machine, RuntimeValue& result) {
            executeLoggingOperation(machine, [&] {
                result = writeSetting(machine->getThisAs<LoggerObject>()->getLogger().getLogLevel());
            });
        },
        .setter = [mode = context->mode](VirtualMachine* machine, const RuntimeValue& value) {
            if (mode != ModuleMode::Application) {
                machine->fault("Logging configuration requires application mode");
            }
            executeLoggingOperation(machine, [&] {
                machine->getThisAs<LoggerObject>()->getLogger().setLogLevel(readSetting<LogLevel>(value));
            });
        },
        .documentation = NomadDoc("The current level setting.")
    });
    if (levelId.isInvalid()) {
        throw NomadException("Failed to register log.Logger.level");
    }

    const auto deliveryId = runtime->addProperty(typeId, {
        .valueTypeId = runtime->getTypeByName("log.DeliveryMode")->getId(),
        .name = "deliveryMode",
        .getter = [](VirtualMachine* machine, RuntimeValue& result) {
            executeLoggingOperation(machine, [&] {
                result = writeSetting(machine->getThisAs<LoggerObject>()->getLogger().getDeliveryMode());
            });
        },
        .setter = [mode = context->mode](VirtualMachine* machine, const RuntimeValue& value) {
            if (mode != ModuleMode::Application) {
                machine->fault("Logging configuration requires application mode");
            }
            executeLoggingOperation(machine, [&] {
                machine->getThisAs<LoggerObject>()->getLogger().setDeliveryMode(readSetting<LogDeliveryMode>(value));
            });
        },
        .documentation = NomadDoc("The current deliveryMode setting.")
    });
    if (deliveryId.isInvalid()) {
        throw NomadException("Failed to register log.Logger.deliveryMode");
    }
}

void registerSinkClass(ModuleContext* context, const HubId hubId) {
    auto* runtime = context->runtime;
    auto* logger = context->logger;
    const auto typeId = runtime->registerHubClass(hubId, "Sink", ClassMode::Closed,
        NomadDoc("A reference to a logger-owned sink. Releasing it does not remove the sink."));
    if (typeId.isInvalid()) {
        throw NomadException("Failed to register log.Sink");
    }

    const auto levelId = runtime->addProperty(typeId, {
        .valueTypeId = runtime->getTypeByName("log.Level")->getId(),
        .name = "level",
        .getter = [logger](VirtualMachine* machine, RuntimeValue& result) {
            executeLoggingOperation(machine, [&] {
                result = writeSetting(getSinkObject(logger, machine->getThisAs<LogSinkObject>()).getSink().getLogLevel());
            });
        },
        .setter = [logger, mode = context->mode](VirtualMachine* machine, const RuntimeValue& value) {
            if (mode != ModuleMode::Application) {
                machine->fault("Logging configuration requires application mode");
            }
            executeLoggingOperation(machine, [&] {
                getSinkObject(logger, machine->getThisAs<LogSinkObject>()).getSink().setLogLevel(readSetting<LogLevel>(value));
            });
        },
        .documentation = NomadDoc("The current level setting.")
    });
    if (levelId.isInvalid()) {
        throw NomadException("Failed to register log.Sink.level");
    }

    const auto activeId = runtime->addProperty(typeId, {
        .valueTypeId = runtime->getBooleanType()->getId(),
        .name = "active",
        .getter = [logger](VirtualMachine* machine, RuntimeValue& result) {
            executeLoggingOperation(machine, [&] {
                result.setBooleanValue(getSinkObject(logger, machine->getThisAs<LogSinkObject>()).getSink().getActive());
            });
        },
        .setter = [logger, mode = context->mode](VirtualMachine* machine, const RuntimeValue& value) {
            if (mode != ModuleMode::Application) {
                machine->fault("Logging configuration requires application mode");
            }
            executeLoggingOperation(machine, [&] {
                getSinkObject(logger, machine->getThisAs<LogSinkObject>()).getSink().setActive(value.getBooleanValue());
            });
        },
        .documentation = NomadDoc("The current active setting.")
    });
    if (activeId.isInvalid()) {
        throw NomadException("Failed to register log.Sink.active");
    }

    const auto rotationId = runtime->addProperty(typeId, {
        .valueTypeId = runtime->getTypeByName("log.FileRotationMode")->getId(),
        .name = "rotationMode",
        .getter = [logger](VirtualMachine* machine, RuntimeValue& result) {
            executeLoggingOperation(machine, [&] {
                result = writeSetting(getFileSink(logger, machine->getThisAs<LogSinkObject>()).getRotationMode());
            });
        },
        .setter = [logger, mode = context->mode](VirtualMachine* machine, const RuntimeValue& value) {
            if (mode != ModuleMode::Application) {
                machine->fault("Logging configuration requires application mode");
            }
            executeLoggingOperation(machine, [&] {
                getFileSink(logger, machine->getThisAs<LogSinkObject>()).setRotationMode(readSetting<FileRotationMode>(value));
            });
        },
        .documentation = NomadDoc("The current rotationMode setting.")
    });
    if (rotationId.isInvalid()) {
        throw NomadException("Failed to register log.Sink.rotationMode");
    }

    const auto startupId = runtime->addProperty(typeId, {
        .valueTypeId = runtime->getBooleanType()->getId(),
        .name = "rotateAtStartup",
        .getter = [logger](VirtualMachine* machine, RuntimeValue& result) {
            executeLoggingOperation(machine, [&] {
                result.setBooleanValue(getFileSink(logger, machine->getThisAs<LogSinkObject>()).getRotateAtStartup());
            });
        },
        .setter = [logger, mode = context->mode](VirtualMachine* machine, const RuntimeValue& value) {
            if (mode != ModuleMode::Application) {
                machine->fault("Logging configuration requires application mode");
            }
            executeLoggingOperation(machine, [&] {
                getFileSink(logger, machine->getThisAs<LogSinkObject>()).setRotateAtStartup(value.getBooleanValue());
            });
        },
        .documentation = NomadDoc("The current rotateAtStartup setting.")
    });
    if (startupId.isInvalid()) {
        throw NomadException("Failed to register log.Sink.rotateAtStartup");
    }

    const auto sizeId = runtime->addProperty(typeId, {
        .valueTypeId = runtime->getIntegerType()->getId(),
        .name = "maxSizeBytes",
        .getter = [logger](VirtualMachine* machine, RuntimeValue& result) {
            executeLoggingOperation(machine, [&] {
                result = writeSetting(getFileSink(logger, machine->getThisAs<LogSinkObject>()).getMaxSizeBytes());
            });
        },
        .setter = {},
        .documentation = NomadDoc("The current maxSizeBytes setting.")
    });
    if (sizeId.isInvalid()) {
        throw NomadException("Failed to register log.Sink.maxSizeBytes");
    }

    const auto filesId = runtime->addProperty(typeId, {
        .valueTypeId = runtime->getIntegerType()->getId(),
        .name = "maxFiles",
        .getter = [logger](VirtualMachine* machine, RuntimeValue& result) {
            executeLoggingOperation(machine, [&] {
                result = writeSetting(getFileSink(logger, machine->getThisAs<LogSinkObject>()).getMaxFiles());
            });
        },
        .setter = {},
        .documentation = NomadDoc("The current maxFiles setting.")
    });
    if (filesId.isInvalid()) {
        throw NomadException("Failed to register log.Sink.maxFiles");
    }
}

void registerLogClasses(ModuleContext* context, const HubId hubId) {
    registerSinkConfigurationClass(*context->runtime, hubId);
    registerFileSinkConfigurationClass(*context->runtime, hubId);
    registerLoggerClass(context, hubId);
    registerSinkClass(context, hubId);
}

void registerLogConstants(ModuleContext* context, const HubId hubId) {
    auto* runtime = context->runtime;
    const auto* loggerType = runtime->getTypeByName("log.Logger");
    RuntimeValue loggerValue;
    loggerValue.setObjectValue(new LoggerObject(*context->logger));
    ConstantId loggerId;
    try {
        loggerId = runtime->registerHubConstant(hubId, "logger", loggerValue, loggerType);
    } catch (...) {
        loggerValue.freeObjectValue();
        throw;
    }
    loggerValue.freeObjectValue();
    if (loggerId.isInvalid()) {
        throw NomadException("Failed to register log.logger");
    }
}

void registerLogFunctions(ModuleContext* context, const HubId hubId) {
    auto* runtime = context->runtime;
    auto* logger = context->logger;
    const auto* sinkType = runtime->getTypeByName("log.Sink");

    const auto debugId = runtime->registerHubNativeFunction(hubId, "debug",
        [logger](const VirtualMachine* machine) {
            const Location location(
                machine->getStringParameter(1)->getText(),
                machine->getStringParameter(0)->getText(),
                machine->getIntegerParameter(2),
                0
            );
            logger->debug(machine->getStringParameter(3)->getString(), location);
        },
        {
            defParameter("$file", runtime->getFileNameType(), NomadParamDoc("(hidden) The script file name")),
            defParameter("$function", runtime->getFunctionNameType(), NomadParamDoc("(hidden) The script function name")),
            defParameter("$line", runtime->getLineNumberType(), NomadParamDoc("(hidden) The script line number")),
            defParameter("message", runtime->getStringType(), NomadParamDoc("The text to log"))
        },
        runtime->getVoidType(), NomadDoc("Writes a message to the log at the debug level."));
    if (debugId.isInvalid()) {
        throw NomadException("Failed to register log.debug");
    }

    const auto infoId = runtime->registerHubNativeFunction(hubId, "info",
        [logger](const VirtualMachine* machine) {
            const Location location(
                machine->getStringParameter(1)->getText(),
                machine->getStringParameter(0)->getText(),
                machine->getIntegerParameter(2),
                0
            );
            logger->info(machine->getStringParameter(3)->getString(), location);
        },
        {
            defParameter("$file", runtime->getFileNameType(), NomadParamDoc("(hidden) The script file name")),
            defParameter("$function", runtime->getFunctionNameType(), NomadParamDoc("(hidden) The script function name")),
            defParameter("$line", runtime->getLineNumberType(), NomadParamDoc("(hidden) The script line number")),
            defParameter("message", runtime->getStringType(), NomadParamDoc("The text to log"))
        },
        runtime->getVoidType(), NomadDoc("Writes a message to the log at the info level."));
    if (infoId.isInvalid()) {
        throw NomadException("Failed to register log.info");
    }

    const auto warningId = runtime->registerHubNativeFunction(hubId, "warning",
        [logger](const VirtualMachine* machine) {
            const Location location(
                machine->getStringParameter(1)->getText(),
                machine->getStringParameter(0)->getText(),
                machine->getIntegerParameter(2),
                0
            );
            logger->warning(machine->getStringParameter(3)->getString(), location);
        },
        {
            defParameter("$file", runtime->getFileNameType(), NomadParamDoc("(hidden) The script file name")),
            defParameter("$function", runtime->getFunctionNameType(), NomadParamDoc("(hidden) The script function name")),
            defParameter("$line", runtime->getLineNumberType(), NomadParamDoc("(hidden) The script line number")),
            defParameter("message", runtime->getStringType(), NomadParamDoc("The text to log"))
        },
        runtime->getVoidType(), NomadDoc("Writes a message to the log at the warning level."));
    if (warningId.isInvalid()) {
        throw NomadException("Failed to register log.warning");
    }

    const auto errorId = runtime->registerHubNativeFunction(hubId, "error",
        [logger](const VirtualMachine* machine) {
            const Location location(
                machine->getStringParameter(1)->getText(),
                machine->getStringParameter(0)->getText(),
                machine->getIntegerParameter(2),
                0
            );
            logger->error(machine->getStringParameter(3)->getString(), location);
        },
        {
            defParameter("$file", runtime->getFileNameType(), NomadParamDoc("(hidden) The script file name")),
            defParameter("$function", runtime->getFunctionNameType(), NomadParamDoc("(hidden) The script function name")),
            defParameter("$line", runtime->getLineNumberType(), NomadParamDoc("(hidden) The script line number")),
            defParameter("message", runtime->getStringType(), NomadParamDoc("The text to log"))
        },
        runtime->getVoidType(), NomadDoc("Writes a message to the log at the error level."));
    if (errorId.isInvalid()) {
        throw NomadException("Failed to register log.error");
    }

    const auto criticalId = runtime->registerHubNativeFunction(hubId, "critical",
        [logger](const VirtualMachine* machine) {
            const Location location(
                machine->getStringParameter(1)->getText(),
                machine->getStringParameter(0)->getText(),
                machine->getIntegerParameter(2),
                0
            );
            logger->critical(machine->getStringParameter(3)->getString(), location);
        },
        {
            defParameter("$file", runtime->getFileNameType(), NomadParamDoc("(hidden) The script file name")),
            defParameter("$function", runtime->getFunctionNameType(), NomadParamDoc("(hidden) The script function name")),
            defParameter("$line", runtime->getLineNumberType(), NomadParamDoc("(hidden) The script line number")),
            defParameter("message", runtime->getStringType(), NomadParamDoc("The text to log"))
        },
        runtime->getVoidType(), NomadDoc("Writes a message to the log at the critical level."));
    if (criticalId.isInvalid()) {
        throw NomadException("Failed to register log.critical");
    }

    const auto consoleId = runtime->registerHubNativeFunction(hubId, "createConsoleSink",
        [logger, mode = context->mode](VirtualMachine* machine) {
            if (mode != ModuleMode::Application) {
                machine->fault("Logging configuration requires application mode");
            }
            executeLoggingOperation(machine, [&] {
                const auto* object = machine->getParameter(0).asObject<ScriptObject>();
                if (object == nullptr) {
                    throw NomadException("Log sink configuration must not be null");
                }
                ConsoleSinkConfiguration configuration;
                configuration.active = getField(*object, "active").getBooleanValue();
                configuration.level = readSetting<LogLevel>(getField(*object, "level"));
                machine->getResult().setObjectValue(new LogSinkObject(*logger, logger->createConsoleSink(configuration)));
            });
        },
        {defParameter("configuration", runtime->getTypeByName("log.SinkConfiguration"),
            NomadParamDoc("The console sink creation settings."))},
        sinkType, NomadDoc("Creates a logger-owned console sink without removing existing sinks."));
    if (consoleId.isInvalid()) {
        throw NomadException("Failed to register log.createConsoleSink");
    }

    const auto fileId = runtime->registerHubNativeFunction(hubId, "createFileSink",
        [logger, mode = context->mode, projectDirectory = context->projectDirectory](VirtualMachine* machine) {
            if (mode != ModuleMode::Application) {
                machine->fault("Logging configuration requires application mode");
            }
            executeLoggingOperation(machine, [&] {
                const auto* object = machine->getParameter(0).asObject<ScriptObject>();
                if (object == nullptr) {
                    throw NomadException("Log sink configuration must not be null");
                }
                FileSinkConfiguration configuration;
                configuration.active = getField(*object, "active").getBooleanValue();
                configuration.level = readSetting<LogLevel>(getField(*object, "level"));
                const auto* directory = getField(*object, "directory").getStringObject();
                const auto* name = getField(*object, "name").getStringObject();
                if (directory == nullptr || name == nullptr || directory->getString().empty()) {
                    throw NomadException("Log file directory and name must not be null or empty");
                }
                configuration.directory = pathFromUtf8(directory->getString());
                if (configuration.directory.is_relative()) {
                    configuration.directory = projectDirectory / configuration.directory;
                }
                configuration.name = name->getString();
                configuration.rotationMode = readSetting<FileRotationMode>(getField(*object, "rotationMode"));
                configuration.rotateAtStartup = getField(*object, "rotateAtStartup").getBooleanValue();
                configuration.maxSizeBytes = readSetting<NomadIndex>(getField(*object, "maxSizeBytes"));
                configuration.maxFiles = readSetting<NomadIndex>(getField(*object, "maxFiles"));
                machine->getResult().setObjectValue(new LogSinkObject(*logger, logger->createFileSink(configuration)));
            });
        },
        {defParameter("configuration", runtime->getTypeByName("log.FileSinkConfiguration"),
            NomadParamDoc("The file sink creation settings."))},
        sinkType, NomadDoc("Creates a logger-owned file sink; relative directories use the project directory."));
    if (fileId.isInvalid()) {
        throw NomadException("Failed to register log.createFileSink");
    }

    const auto removeId = runtime->registerHubNativeFunction(hubId, "removeSink",
        [logger, mode = context->mode](VirtualMachine* machine) {
            if (mode != ModuleMode::Application) {
                machine->fault("Logging configuration requires application mode");
            }
            executeLoggingOperation(machine, [&] {
                logger->removeSink(getSinkObject(logger, machine->getParameter(0).asObject<LogSinkObject>()).getSinkId());
            });
        },
        {defParameter("sink", sinkType, NomadParamDoc("A reference to a logger-owned sink."))},
        runtime->getVoidType(), NomadDoc("Flushes and removes a sink; unknown or removed handles fail."));
    if (removeId.isInvalid()) {
        throw NomadException("Failed to register log.removeSink");
    }

    const auto clearId = runtime->registerHubNativeFunction(hubId, "clearAllSinks",
        [logger, mode = context->mode](VirtualMachine* machine) {
            if (mode != ModuleMode::Application) {
                machine->fault("Logging configuration requires application mode");
            }
            executeLoggingOperation(machine, [&] {
                logger->clearAllSinks();
            });
        },
        {}, runtime->getVoidType(), NomadDoc("Flushes and removes all sinks without installing a replacement."));
    if (clearId.isInvalid()) {
        throw NomadException("Failed to register log.clearAllSinks");
    }

    const auto flushId = runtime->registerHubNativeFunction(hubId, "flush",
        [logger, mode = context->mode](VirtualMachine* machine) {
            if (mode != ModuleMode::Application) {
                machine->fault("Logging configuration requires application mode");
            }
            executeLoggingOperation(machine, [&] {
                logger->flush();
            });
        },
        {}, runtime->getVoidType(), NomadDoc("Delivers pending messages and flushes active sinks."));
    if (flushId.isInvalid()) {
        throw NomadException("Failed to register log.flush");
    }

    const auto pathId = runtime->registerHubNativeFunction(hubId, "getFilePath",
        [logger](VirtualMachine* machine) {
            executeLoggingOperation(machine, [&] {
                machine->setStringResult(pathToUtf8(getFileSink(logger, machine->getParameter(0).asObject<LogSinkObject>()).getPath()));
            });
        },
        {defParameter("sink", sinkType, NomadParamDoc("A reference to a logger-owned sink."))},
        runtime->getStringType(), NomadDoc("Returns the resolved active file path."));
    if (pathId.isInvalid()) {
        throw NomadException("Failed to register log.getFilePath");
    }

    const auto limitsId = runtime->registerHubNativeFunction(hubId, "setFileRotationLimits",
        [logger, mode = context->mode](VirtualMachine* machine) {
            if (mode != ModuleMode::Application) {
                machine->fault("Logging configuration requires application mode");
            }
            executeLoggingOperation(machine, [&] {
                const auto size = readSetting<NomadIndex>(machine->getParameter(1));
                const auto files = readSetting<NomadIndex>(machine->getParameter(2));
                getFileSink(logger, machine->getParameter(0).asObject<LogSinkObject>()).setRotationLimits(size, files);
            });
        },
        {
            defParameter("sink", sinkType, NomadParamDoc("A reference to a logger-owned sink.")),
            defParameter("maxSizeBytes", runtime->getIntegerType(),
                NomadParamDoc("The positive byte limit, including record newlines.")),
            defParameter("maxFiles", runtime->getIntegerType(),
                NomadParamDoc("The nonnegative number of archives, excluding the active file."))
        },
        runtime->getVoidType(), NomadDoc("Changes rotation limits for subsequent writes."));
    if (limitsId.isInvalid()) {
        throw NomadException("Failed to register log.setFileRotationLimits");
    }
}

void registerLogModule(ModuleContext* context) {
    if (context->mode == ModuleMode::Packaging) {
        return;
    }
    const auto hubId = registerLogHub(*context->runtime);
    registerLogEnums(*context->runtime, hubId);
    registerLogClasses(context, hubId);
    registerLogConstants(context, hubId);
    registerLogFunctions(context, hubId);
}

} // namespace

} // namespace nomad

NOMAD_MODULE_EXPORT void registerModule(nomad::ModuleContext* context, nomad::ModuleCallbacks*) noexcept(false) {
    nomad::registerLogModule(context);
}
