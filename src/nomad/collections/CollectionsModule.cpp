// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/ListObject.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>

#include <nomad/system/Module.hpp>

NOMAD_MODULE_EXPORT void registerModule(nomad::ModuleContext* context, nomad::ModuleCallbacks*) noexcept(false) {
    if (context->mode == nomad::ModuleMode::Packaging) {
        return;
    }
    if (!context->runtime->registerParameterizedType(
        "List",
        1,
        [](const std::vector<const nomad::Type*>& arguments) {
            return std::make_unique<nomad::ObjectType>(
                "List<" + arguments[0]->getTypeName() + ">",
                arguments,
                [] { return new nomad::ListObject; }
            );
        }
    )) {
        throw nomad::NomadException("Failed to register List");
    }
}
