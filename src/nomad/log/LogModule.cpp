// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/log/LogFunctions.hpp>
#include <nomad/log/Logger.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/Module.hpp>

#include <array>
#include <utility>

NOMAD_MODULE_EXPORT void registerModule(nomad::ModuleContext* context, nomad::ModuleCallbacks*) noexcept(false) {
    using namespace nomad;
    if (context->mode == ModuleMode::Packaging) {
        return;
    }
    auto* runtime = context->runtime;
    if (runtime->registerHub("log", HubMode::Opened).isInvalid()) {
        throw NomadException("Failed to register log hub");
    }
    registerLogConfigurationFunctions(context);
    const std::array functions{
        std::pair{"log.debug", &Logger::debug},
        std::pair{"log.info", &Logger::info},
        std::pair{"log.warning", &Logger::warning},
        std::pair{"log.error", &Logger::error},
        std::pair{"log.critical", &Logger::critical}
    };
    for (const auto& [name, function] : functions) {
        const auto id = runtime->registerNativeFunction(
            name,
            [logger = context->logger, function](const VirtualMachine* machine) {
                const Location location(
                    machine->getStringParameter(1)->getText(),
                    machine->getStringParameter(0)->getText(),
                    machine->getIntegerParameter(2),
                    0
                );
                (logger->*function)(machine->getStringParameter(3)->getString(), location);
            },
            {
                defParameter("$file", runtime->getFileNameType(), NomadParamDoc("(hidden) The script file name")),
                defParameter("$function", runtime->getFunctionNameType(), NomadParamDoc("(hidden) The script function name")),
                defParameter("$line", runtime->getLineNumberType(), NomadParamDoc("(hidden) The script line number")),
                defParameter("message", runtime->getStringType(), NomadParamDoc("The text to log"))
            },
            runtime->getVoidType(),
            NomadDoc("Writes a message to the log at the " + NomadString(name + 4) + " level.")
        );
        if (id.isInvalid()) {
            throw NomadException("Failed to register " + NomadString(name));
        }
    }
}
