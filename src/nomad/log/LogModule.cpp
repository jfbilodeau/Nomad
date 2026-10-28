// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/log/LogFunctions.hpp>
#include <nomad/log/Logger.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/Module.hpp>

#include <array>
#include <utility>

namespace nomad {

namespace {

void registerLogSinkType(Runtime& runtime, const HubId hubId) {
    const auto typeId = runtime.registerHubClass(hubId, "Sink", ClassMode::Closed,
        NomadDoc("A reference to a logger-owned sink. Releasing it does not remove the sink."));
    if (typeId.isInvalid()) {
        throw NomadException("Failed to register log.Sink");
    }
}

} // namespace

} // namespace nomad

NOMAD_MODULE_EXPORT void registerModule(nomad::ModuleContext* context, nomad::ModuleCallbacks*) noexcept(false) {
    using namespace nomad;
    if (context->mode == ModuleMode::Packaging) {
        return;
    }
    auto* runtime = context->runtime;
    const auto hubId = runtime->registerHub("log", HubMode::Opened);
    if (hubId.isInvalid()) {
        throw NomadException("Failed to register log hub");
    }
    registerLogSinkType(*runtime, hubId);
    registerLogConfigurationFunctions(context, hubId);
    const std::array functions{
        std::pair{"debug", &Logger::debug},
        std::pair{"info", &Logger::info},
        std::pair{"warning", &Logger::warning},
        std::pair{"error", &Logger::error},
        std::pair{"critical", &Logger::critical}
    };
    for (const auto& [name, function] : functions) {
        auto callback = [logger = context->logger, function](const VirtualMachine* machine) {
            const Location location(
                machine->getStringParameter(1)->getText(),
                machine->getStringParameter(0)->getText(),
                machine->getIntegerParameter(2),
                0
            );
            (logger->*function)(machine->getStringParameter(3)->getString(), location);
        };
        const std::vector<Parameter> parameters{
            defParameter("$file", runtime->getFileNameType(), NomadParamDoc("(hidden) The script file name")),
            defParameter("$function", runtime->getFunctionNameType(), NomadParamDoc("(hidden) The script function name")),
            defParameter("$line", runtime->getLineNumberType(), NomadParamDoc("(hidden) The script line number")),
            defParameter("message", runtime->getStringType(), NomadParamDoc("The text to log"))
        };
        const auto documentation = NomadDoc("Writes a message to the log at the " + NomadString(name) + " level.");
        const auto id = runtime->registerHubNativeFunction(hubId, name, std::move(callback),
            parameters, runtime->getVoidType(), documentation);
        if (id.isInvalid()) {
            throw NomadException("Failed to register " + NomadString(name));
        }
    }
}
