// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/generated/log/Bindings.hpp>

#include <nomad/log/Logger.hpp>
#include <nomad/log/LoggerObject.hpp>
#include <nomad/log/LogSinkObject.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/Module.hpp>
#include <nomad/system/Path.hpp>

#include <memory>

namespace nomad {

namespace {

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

void registerLoggerClass(
    ModuleContext* context,
    const HubId hubId,
    const generated::log::DeferredTypes& types
) {
    auto* runtime = context->runtime;
    const auto typeId = runtime->registerHubClass(hubId, "Logger", ClassMode::Closed,
        NomadDoc("A reference to the host logger. The host must outlive all references."));
    if (typeId.isInvalid()) {
        throw NomadException("Failed to register log.Logger");
    }

    const auto levelId = runtime->addProperty(typeId, {
        .valueType = types.level,
        .name = "level",
        .getter = [](VirtualMachine* machine, RuntimeValue& result) {
            const auto& logger = machine->getThisAs<LoggerObject>()->getLogger();

            auto logLevel = static_cast<NomadInteger>(logger.getLogLevel());

            result.setIntegerValue(logLevel);
        },
        .setter = [mode = context->mode](VirtualMachine* machine, const RuntimeValue& value) {
            if (mode != ModuleMode::Application) {
                machine->fault("Logging configuration requires application mode");
            }
            executeLoggingOperation(machine, [&] {
                machine->getThisAs<LoggerObject>()->getLogger().setLogLevel(
                    static_cast<LogLevel>(value.getIntegerValue()));
            });
        },
        .documentation = NomadDoc("The current level setting.")
    });
    if (levelId.isInvalid()) {
        throw NomadException("Failed to register log.Logger.level");
    }

    const auto deliveryId = runtime->addProperty(typeId, {
        .valueType = types.deliveryMode,
        .name = "deliveryMode",
        .getter = [](VirtualMachine* machine, RuntimeValue& result) {
            executeLoggingOperation(machine, [&] {
                result.setIntegerValue(static_cast<NomadInteger>(
                    machine->getThisAs<LoggerObject>()->getLogger().getDeliveryMode()));
            });
        },
        .setter = [mode = context->mode](VirtualMachine* machine, const RuntimeValue& value) {
            if (mode != ModuleMode::Application) {
                machine->fault("Logging configuration requires application mode");
            }
            executeLoggingOperation(machine, [&] {
                machine->getThisAs<LoggerObject>()->getLogger().setDeliveryMode(
                    static_cast<LogDeliveryMode>(value.getIntegerValue()));
            });
        },
        .documentation = NomadDoc("The current deliveryMode setting.")
    });
    if (deliveryId.isInvalid()) {
        throw NomadException("Failed to register log.Logger.deliveryMode");
    }
}

void registerSinkClass(
    ModuleContext* context,
    const HubId hubId,
    const generated::log::DeferredTypes& types
) {
    auto* runtime = context->runtime;
    auto* logger = context->logger;
    const auto typeId = runtime->registerHubClass(hubId, "Sink", ClassMode::Closed,
        NomadDoc("A reference to a logger-owned sink. Releasing it does not remove the sink."));
    if (typeId.isInvalid()) {
        throw NomadException("Failed to register log.Sink");
    }

    const auto levelId = runtime->addProperty(typeId, {
        .valueType = types.level,
        .name = "level",
        .getter = [logger](VirtualMachine* machine, RuntimeValue& result) {
            executeLoggingOperation(machine, [&] {
                const auto level = getSinkObject(logger, machine->getThisAs<LogSinkObject>()).getSink().getLogLevel();
                result.setIntegerValue(static_cast<NomadInteger>(level));
            });
        },
        .setter = [logger, mode = context->mode](VirtualMachine* machine, const RuntimeValue& value) {
            if (mode != ModuleMode::Application) {
                machine->fault("Logging configuration requires application mode");
            }
            executeLoggingOperation(machine, [&] {
                getSinkObject(logger, machine->getThisAs<LogSinkObject>()).getSink().setLogLevel(
                    static_cast<LogLevel>(value.getIntegerValue()));
            });
        },
        .documentation = NomadDoc("The current level setting.")
    });
    if (levelId.isInvalid()) {
        throw NomadException("Failed to register log.Sink.level");
    }

    const auto activeId = runtime->addProperty(typeId, {
        .valueType = runtime->getBooleanType(),
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
        .valueType = types.fileRotationMode,
        .name = "rotationMode",
        .getter = [logger](VirtualMachine* machine, RuntimeValue& result) {
            executeLoggingOperation(machine, [&] {
                const auto mode = getFileSink(logger, machine->getThisAs<LogSinkObject>()).getRotationMode();
                result.setIntegerValue(static_cast<NomadInteger>(mode));
            });
        },
        .setter = [logger, mode = context->mode](VirtualMachine* machine, const RuntimeValue& value) {
            if (mode != ModuleMode::Application) {
                machine->fault("Logging configuration requires application mode");
            }
            executeLoggingOperation(machine, [&] {
                getFileSink(logger, machine->getThisAs<LogSinkObject>()).setRotationMode(
                    static_cast<FileRotationMode>(value.getIntegerValue()));
            });
        },
        .documentation = NomadDoc("The current rotationMode setting.")
    });
    if (rotationId.isInvalid()) {
        throw NomadException("Failed to register log.Sink.rotationMode");
    }

    const auto startupId = runtime->addProperty(typeId, {
        .valueType = runtime->getBooleanType(),
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
        .valueType = runtime->getIntegerType(),
        .name = "maxSizeBytes",
        .getter = [logger](VirtualMachine* machine, RuntimeValue& result) {
            executeLoggingOperation(machine, [&] {
                result.setIntegerValue(getFileSink(logger, machine->getThisAs<LogSinkObject>()).getMaxSizeBytes());
            });
        },
        .setter = {},
        .documentation = NomadDoc("The current maxSizeBytes setting.")
    });
    if (sizeId.isInvalid()) {
        throw NomadException("Failed to register log.Sink.maxSizeBytes");
    }

    const auto filesId = runtime->addProperty(typeId, {
        .valueType = runtime->getIntegerType(),
        .name = "maxFiles",
        .getter = [logger](VirtualMachine* machine, RuntimeValue& result) {
            executeLoggingOperation(machine, [&] {
                result.setIntegerValue(getFileSink(logger, machine->getThisAs<LogSinkObject>()).getMaxFiles());
            });
        },
        .setter = {},
        .documentation = NomadDoc("The current maxFiles setting.")
    });
    if (filesId.isInvalid()) {
        throw NomadException("Failed to register log.Sink.maxFiles");
    }
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

void registerLogFunctions(
    ModuleContext* context,
    const HubId hubId,
    const generated::log::DeferredTypes& types,
    const std::shared_ptr<ScriptBindings>& bindings
) {
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
        [logger, mode = context->mode, bindings](VirtualMachine* machine) {
            if (mode != ModuleMode::Application) {
                machine->fault("Logging configuration requires application mode");
            }
            executeLoggingOperation(machine, [&] {
                auto* object = machine->getParameter(0).getObjectValue();
                if (object == nullptr) {
                    throw NomadException("Log sink configuration must not be null");
                }
                const generated::log::log_SinkConfiguration settings(*bindings, object);
                ConsoleSinkConfiguration configuration;
                configuration.active = settings.getActive();
                configuration.level = static_cast<LogLevel>(settings.getLevel());
                machine->getResult().setObjectValue(new LogSinkObject(*logger, logger->createConsoleSink(configuration)));
            });
        },
        {defParameter("configuration", types.sinkConfiguration,
            NomadParamDoc("The console sink creation settings."))},
        sinkType, NomadDoc("Creates a logger-owned console sink without removing existing sinks."));
    if (consoleId.isInvalid()) {
        throw NomadException("Failed to register log.createConsoleSink");
    }

    const auto fileId = runtime->registerHubNativeFunction(hubId, "createFileSink",
        [logger, mode = context->mode, projectDirectory = context->projectDirectory, bindings](VirtualMachine* machine) {
            if (mode != ModuleMode::Application) {
                machine->fault("Logging configuration requires application mode");
            }
            executeLoggingOperation(machine, [&] {
                auto* object = machine->getParameter(0).getObjectValue();
                if (object == nullptr) {
                    throw NomadException("Log sink configuration must not be null");
                }
                const generated::log::log_FileSinkConfiguration settings(*bindings, object);
                FileSinkConfiguration configuration;
                configuration.active = settings.getActive();
                configuration.level = static_cast<LogLevel>(settings.getLevel());
                configuration.directory = pathFromUtf8(settings.getDirectory());
                if (!configuration.directory.empty() && configuration.directory.is_relative()) {
                    configuration.directory = projectDirectory / configuration.directory;
                }
                configuration.name = settings.getName();
                configuration.rotationMode = static_cast<FileRotationMode>(settings.getRotationMode());
                configuration.rotateAtStartup = settings.getRotateAtStartup();
                configuration.maxSizeBytes = settings.getMaxSizeBytes();
                configuration.maxFiles = settings.getMaxFiles();
                machine->getResult().setObjectValue(new LogSinkObject(*logger, logger->createFileSink(configuration)));
            });
        },
        {defParameter("configuration", types.fileSinkConfiguration,
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
                const auto size = machine->getIntegerParameter(1);
                const auto files = machine->getIntegerParameter(2);
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

void registerLogModule(ModuleContext* context, ModuleCallbacks* callbacks) {
    if (context->mode == ModuleMode::Packaging) {
        return;
    }
    if (context->compiler == nullptr || context->runtime == nullptr || context->logger == nullptr) {
        throw NomadException("Log module requires a compiler, runtime, and logger");
    }
    const auto types = generated::log::deferTypes(*context->compiler);
    const auto bindings = std::make_shared<ScriptBindings>(*context->runtime);
    const auto hubId = registerLogHub(*context->runtime);
    registerLoggerClass(context, hubId, types);
    registerSinkClass(context, hubId, types);
    registerLogConstants(context, hubId);
    registerLogFunctions(context, hubId, types, bindings);
    // Native factory callbacks own the cache; compiler hooks run while the runtime is alive.
    callbacks->compilationCompleted = [bindings = bindings.get()](CompilerContext* compilerContext) {
        if (!compilerContext->hasError()) {
            (void)generated::log::log_SinkConfiguration::bind(*bindings);
            (void)generated::log::log_FileSinkConfiguration::bind(*bindings);
        }
    };
}

} // namespace

} // namespace nomad

NOMAD_MODULE_EXPORT void registerModule(
    nomad::ModuleContext* context,
    nomad::ModuleCallbacks* callbacks
) noexcept(false) {
    nomad::registerLogModule(context, callbacks);
}
