// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/system/Module.hpp>

#include <nomad/script/Runtime.hpp>

#include <nomad/system/NativeFunctions.hpp>

NOMAD_MODULE_EXPORT void registerModule(nomad::ModuleContext* context, nomad::ModuleCallbacks*) noexcept(false) {
    using nomad::NomadString;
    if (context->mode == nomad::ModuleMode::Packaging) {
        return;
    }
    nomad::registerBuildInNativeFunctions(context->runtime);
    const auto id = context->runtime->registerNativeFunction(
        "system.exit",
        [requestExit = context->requestExit](nomad::VirtualMachine*) { requestExit(0); },
        {},
        context->runtime->getVoidType(),
        NomadDoc("Requests orderly application shutdown after the current script invocation.")
    );
    if (id == nomad::NOMAD_INVALID_ID) {
        throw nomad::NomadException("Failed to register system.exit");
    }
}
