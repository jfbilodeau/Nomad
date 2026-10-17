// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/ListObject.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/Module.hpp>

using nomad::NomadString;

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
                [elementType = arguments[0]] { return new nomad::ListObject(elementType); }
            );
        },
        [runtime = context->runtime](const nomad::Type* type) {
            const auto* elementType = static_cast<const nomad::ObjectType*>(type)->getTypeArguments()[0];
            const auto countId = runtime->registerIntrinsicProperty(
                type,
                "count",
                runtime->getIntegerType(),
                [](nomad::VirtualMachine*, const nomad::RuntimeValue& receiver, nomad::RuntimeValue& result) {
                    const auto* list = static_cast<const nomad::ListObject*>(receiver.getObjectValue());
                    result.setIntegerValue(static_cast<nomad::NomadInteger>(list->getCount()));
                },
                {},
                NomadDoc("The number of elements in the list.")
            );
            const auto pushId = runtime->registerNativeMethod(
                static_cast<const nomad::ObjectType*>(type),
                "push",
                [](nomad::VirtualMachine* vm) {
                    auto* list = vm->getThisAs<nomad::ListObject>();
                    list->push(vm->getParameter(0));
                },
                {nomad::defParameter("value", elementType, NomadDoc("The value to append."))},
                runtime->getVoidType(),
                NomadDoc("Appends a value to the list.")
            );
            if (countId == nomad::NOMAD_INVALID_ID || pushId == nomad::NOMAD_INVALID_ID) {
                throw nomad::NomadException("Failed to register List members");
            }
        }
    )) {
        throw nomad::NomadException("Failed to register List");
    }
}
