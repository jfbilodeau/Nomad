// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/system/Module.hpp>

#include <nomad/script/Runtime.hpp>

#include <nomad/system/SystemFunctions.hpp>

NOMAD_MODULE_EXPORT void registerModule(
    nomad::ModuleContext* context,
    nomad::ModuleCallbacks* callbacks
) noexcept(false) {
    using nomad::NomadString;

    if (context->mode == nomad::ModuleMode::Packaging) {

        return;
    }

    nomad::registerSystemPrimitives(context->runtime);

    callbacks->parse = [context, registered = false](nomad::CompilerContext*) mutable {
        if (registered) {

            return;
        }

        const auto hubId = context->runtime->getHubId("system");
        if (hubId.isInvalid()) {
            throw nomad::NomadException("System module requires a script-declared system hub");
        }

        nomad::registerSystemHubFunctions(context->runtime);
        const auto id = context->runtime->registerHubNativeFunction(
            hubId,
            "exit",
            [requestExit = context->requestExit](nomad::VirtualMachine*) { requestExit(0); },
            {},
            context->runtime->getVoidType(),
            NomadDoc("Requests orderly application shutdown after the current script invocation.")
        );
        if (id.isInvalid()) {
            throw nomad::NomadException("Failed to register system.exit");
        }

        registered = true;
    };
}
