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

LogSinkObject& getSinkObject(Logger* logger, LogSinkObject* object) {
    if (&object->getLogger() != logger) {
        throw NomadException("Log sink belongs to a different logger");
    }
    return *object;
}

FileSink& getFileSink(Logger* logger, LogSinkObject* object) {
    auto& sinkObject = getSinkObject(logger, object);
    auto& sink = sinkObject.getSink();
    return static_cast<FileSink&>(sink);
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
            auto& logger = machine->getThisAs<LoggerObject>()->getLogger();

            const auto logLevel = static_cast<LogLevel>(value.getIntegerValue());

            logger.setLogLevel(logLevel);
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
            const auto& logger = machine->getThisAs<LoggerObject>()->getLogger();

            const auto deliveryMode = static_cast<NomadInteger>(logger.getDeliveryMode());

            result.setIntegerValue(deliveryMode);
        },
        .setter = [mode = context->mode](VirtualMachine* machine, const RuntimeValue& value) {
            if (mode != ModuleMode::Application) {
                machine->fault("Logging configuration requires application mode");
            }
            auto& logger = machine->getThisAs<LoggerObject>()->getLogger();

            const auto deliveryMode = static_cast<LogDeliveryMode>(value.getIntegerValue());

            logger.setDeliveryMode(deliveryMode);
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
    for (const auto* name : {"ConsoleSink", "FileSink"}) {
        const auto typeId = runtime->registerHubClass(hubId, name, ClassMode::Closed,
            NomadDoc("A reference to a logger-owned sink. Releasing it does not remove the sink."));
        if (typeId.isInvalid()) {
            throw NomadException("Failed to register log." + NomadString(name));
        }

        const auto levelId = runtime->addProperty(typeId, {
            .valueType = types.level,
            .name = "level",
            .getter = [logger](VirtualMachine* machine, RuntimeValue& result) {
                auto* object = machine->getThisAs<LogSinkObject>();
                const auto& sink = getSinkObject(logger, object).getSink();

                const auto logLevel = static_cast<NomadInteger>(sink.getLogLevel());

                result.setIntegerValue(logLevel);
            },
            .setter = [logger, mode = context->mode](VirtualMachine* machine, const RuntimeValue& value) {
                if (mode != ModuleMode::Application) {
                    machine->fault("Logging configuration requires application mode");
                }
                auto* object = machine->getThisAs<LogSinkObject>();
                auto& sink = getSinkObject(logger, object).getSink();

                const auto logLevel = static_cast<LogLevel>(value.getIntegerValue());

                sink.setLogLevel(logLevel);
            },
            .documentation = NomadDoc("The current level setting.")
        });
        if (levelId.isInvalid()) {
            throw NomadException("Failed to register log." + NomadString(name) + ".level");
        }

        const auto activeId = runtime->addProperty(typeId, {
            .valueType = runtime->getBooleanType(),
            .name = "active",
            .getter = [logger](VirtualMachine* machine, RuntimeValue& result) {
                auto* object = machine->getThisAs<LogSinkObject>();
                const auto& sink = getSinkObject(logger, object).getSink();

                const auto active = sink.getActive();

                result.setBooleanValue(active);
            },
            .setter = [logger, mode = context->mode](VirtualMachine* machine, const RuntimeValue& value) {
                if (mode != ModuleMode::Application) {
                    machine->fault("Logging configuration requires application mode");
                }
                auto* object = machine->getThisAs<LogSinkObject>();
                auto& sink = getSinkObject(logger, object).getSink();

                const auto active = value.getBooleanValue();

                sink.setActive(active);
            },
            .documentation = NomadDoc("The current active setting.")
        });
        if (activeId.isInvalid()) {
            throw NomadException("Failed to register log." + NomadString(name) + ".active");
        }
    }

    const auto typeId = runtime->getTypeByName("log.FileSink")->getId();
    const auto rotationId = runtime->addProperty(typeId, {
        .valueType = types.fileRotationMode,
        .name = "rotationMode",
        .getter = [logger](VirtualMachine* machine, RuntimeValue& result) {
            auto* object = machine->getThisAs<LogSinkObject>();
            const auto& sink = getFileSink(logger, object);

            const auto rotationMode = static_cast<NomadInteger>(sink.getRotationMode());

            result.setIntegerValue(rotationMode);
        },
        .setter = [logger, mode = context->mode](VirtualMachine* machine, const RuntimeValue& value) {
            if (mode != ModuleMode::Application) {
                machine->fault("Logging configuration requires application mode");
            }
            auto* object = machine->getThisAs<LogSinkObject>();
            auto& sink = getFileSink(logger, object);

            const auto rotationMode = static_cast<FileRotationMode>(value.getIntegerValue());

            sink.setRotationMode(rotationMode);
        },
        .documentation = NomadDoc("The current rotationMode setting.")
    });
    if (rotationId.isInvalid()) {
        throw NomadException("Failed to register log.FileSink.rotationMode");
    }

    const auto startupId = runtime->addProperty(typeId, {
        .valueType = runtime->getBooleanType(),
        .name = "rotateAtStartup",
        .getter = [logger](VirtualMachine* machine, RuntimeValue& result) {
            auto* object = machine->getThisAs<LogSinkObject>();
            const auto& sink = getFileSink(logger, object);

            const auto rotateAtStartup = sink.getRotateAtStartup();

            result.setBooleanValue(rotateAtStartup);
        },
        .setter = [logger, mode = context->mode](VirtualMachine* machine, const RuntimeValue& value) {
            if (mode != ModuleMode::Application) {
                machine->fault("Logging configuration requires application mode");
            }
            auto* object = machine->getThisAs<LogSinkObject>();
            auto& sink = getFileSink(logger, object);

            const auto rotateAtStartup = value.getBooleanValue();

            sink.setRotateAtStartup(rotateAtStartup);
        },
        .documentation = NomadDoc("The current rotateAtStartup setting.")
    });
    if (startupId.isInvalid()) {
        throw NomadException("Failed to register log.FileSink.rotateAtStartup");
    }

    const auto sizeId = runtime->addProperty(typeId, {
        .valueType = runtime->getIntegerType(),
        .name = "maxSizeBytes",
        .getter = [logger](VirtualMachine* machine, RuntimeValue& result) {
            auto* object = machine->getThisAs<LogSinkObject>();
            const auto& sink = getFileSink(logger, object);

            const auto maxSizeBytes = sink.getMaxSizeBytes();

            result.setIntegerValue(maxSizeBytes);
        },
        .setter = {},
        .documentation = NomadDoc("The current maxSizeBytes setting.")
    });
    if (sizeId.isInvalid()) {
        throw NomadException("Failed to register log.FileSink.maxSizeBytes");
    }

    const auto filesId = runtime->addProperty(typeId, {
        .valueType = runtime->getIntegerType(),
        .name = "maxFiles",
        .getter = [logger](VirtualMachine* machine, RuntimeValue& result) {
            auto* object = machine->getThisAs<LogSinkObject>();
            const auto& sink = getFileSink(logger, object);

            const auto maxFiles = sink.getMaxFiles();

            result.setIntegerValue(maxFiles);
        },
        .setter = {},
        .documentation = NomadDoc("The current maxFiles setting.")
    });
    if (filesId.isInvalid()) {
        throw NomadException("Failed to register log.FileSink.maxFiles");
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
    const auto* consoleSinkType = runtime->getTypeByName("log.ConsoleSink");
    const auto* fileSinkType = runtime->getTypeByName("log.FileSink");

    const auto debugId = runtime->registerHubNativeFunction(hubId, "debug",
        [logger](const VirtualMachine* machine) {
            const Location location(
                machine->getStringParameter(1)->getText(),
                machine->getStringParameter(0)->getText(),
                machine->getIntegerParameter(2),
                0
            );
            const auto& message = machine->getStringParameter(3)->getString();

            logger->debug(message, location);
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
            const auto& message = machine->getStringParameter(3)->getString();

            logger->info(message, location);
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
            const auto& message = machine->getStringParameter(3)->getString();

            logger->warning(message, location);
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
            const auto& message = machine->getStringParameter(3)->getString();

            logger->error(message, location);
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
            const auto& message = machine->getStringParameter(3)->getString();

            logger->critical(message, location);
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
            auto* object = machine->getParameter(0).getObjectValue();
            const generated::log::log_SinkConfiguration settings(*bindings, object);

            ConsoleSinkConfiguration configuration;
            configuration.active = settings.getActive();
            configuration.level = static_cast<LogLevel>(settings.getLevel());

            const auto sinkId = logger->createConsoleSink(configuration);
            auto* sinkObject = new LogSinkObject(*logger, sinkId);

            machine->getResult().setObjectValue(sinkObject);
        },
        {defParameter("configuration", types.sinkConfiguration,
            NomadParamDoc("The console sink creation settings."))},
        consoleSinkType, NomadDoc("Creates a logger-owned console sink without removing existing sinks."));
    if (consoleId.isInvalid()) {
        throw NomadException("Failed to register log.createConsoleSink");
    }

    const auto fileId = runtime->registerHubNativeFunction(hubId, "createFileSink",
        [logger, mode = context->mode, projectDirectory = context->projectDirectory, bindings](VirtualMachine* machine) {
            if (mode != ModuleMode::Application) {
                machine->fault("Logging configuration requires application mode");
            }
            auto* object = machine->getParameter(0).getObjectValue();
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

            const auto sinkId = logger->createFileSink(configuration);
            auto* sinkObject = new LogSinkObject(*logger, sinkId);

            machine->getResult().setObjectValue(sinkObject);
        },
        {defParameter("configuration", types.fileSinkConfiguration,
            NomadParamDoc("The file sink creation settings."))},
        fileSinkType, NomadDoc("Creates a logger-owned file sink; relative directories use the project directory."));
    if (fileId.isInvalid()) {
        throw NomadException("Failed to register log.createFileSink");
    }

    for (const auto* sinkType : {consoleSinkType, fileSinkType}) {
        const auto removeId = runtime->registerHubNativeFunction(hubId, "removeSink",
            [logger, mode = context->mode](VirtualMachine* machine) {
                if (mode != ModuleMode::Application) {
                    machine->fault("Logging configuration requires application mode");
                }
                auto* object = machine->getParameter(0).asObject<LogSinkObject>();
                const auto& sinkObject = getSinkObject(logger, object);
                const auto sinkId = sinkObject.getSinkId();

                logger->removeSink(sinkId);
            },
            {defParameter("sink", sinkType, NomadParamDoc("A reference to a logger-owned sink."))},
            runtime->getVoidType(), NomadDoc("Flushes and removes a sink; unknown or removed handles fail."));
        if (removeId.isInvalid()) {
            throw NomadException("Failed to register log.removeSink");
        }
    }

    const auto clearId = runtime->registerHubNativeFunction(hubId, "clearAllSinks",
        [logger, mode = context->mode](VirtualMachine* machine) {
            if (mode != ModuleMode::Application) {
                machine->fault("Logging configuration requires application mode");
            }
            logger->clearAllSinks();
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
            logger->flush();
        },
        {}, runtime->getVoidType(), NomadDoc("Delivers pending messages and flushes active sinks."));
    if (flushId.isInvalid()) {
        throw NomadException("Failed to register log.flush");
    }

    const auto pathId = runtime->registerHubNativeFunction(hubId, "getFilePath",
        [logger](VirtualMachine* machine) {
            auto* object = machine->getParameter(0).asObject<LogSinkObject>();
            const auto& sink = getFileSink(logger, object);

            const auto& path = sink.getPath();
            const auto text = pathToUtf8(path);

            machine->setStringResult(text);
        },
        {defParameter("sink", fileSinkType, NomadParamDoc("A reference to a logger-owned file sink."))},
        runtime->getStringType(), NomadDoc("Returns the resolved active file path."));
    if (pathId.isInvalid()) {
        throw NomadException("Failed to register log.getFilePath");
    }

    const auto limitsId = runtime->registerHubNativeFunction(hubId, "setFileRotationLimits",
        [logger, mode = context->mode](VirtualMachine* machine) {
            if (mode != ModuleMode::Application) {
                machine->fault("Logging configuration requires application mode");
            }
            auto* object = machine->getParameter(0).asObject<LogSinkObject>();
            auto& sink = getFileSink(logger, object);

            const auto maxSizeBytes = machine->getIntegerParameter(1);
            const auto maxFiles = machine->getIntegerParameter(2);

            sink.setRotationLimits(maxSizeBytes, maxFiles);
        },
        {
            defParameter("sink", fileSinkType, NomadParamDoc("A reference to a logger-owned file sink.")),
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
    const auto hubId = context->runtime->registerHub("log", HubMode::Opened);
    if (hubId.isInvalid()) {
        throw NomadException("Failed to register log hub");
    }
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
