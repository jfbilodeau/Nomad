// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/RegisterArrayType.hpp>
#include <nomad/collections/RegisterKeyValueType.hpp>
#include <nomad/collections/RegisterListType.hpp>
#include <nomad/collections/RegisterMapType.hpp>
#include <nomad/collections/RegisterRangeType.hpp>

#include <nomad/system/Module.hpp>

NOMAD_MODULE_EXPORT void registerModule(nomad::ModuleContext* context, nomad::ModuleCallbacks*) noexcept(false) {
    if (context->mode == nomad::ModuleMode::Packaging) {
        return;
    }
    auto* runtime = context->runtime;
    nomad::registerArrayType(runtime);
    nomad::registerKeyValueType(runtime);
    nomad::registerListType(runtime);
    nomad::registerMapType(runtime);
    nomad::registerRangeType(runtime);
}
