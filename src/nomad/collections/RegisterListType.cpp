// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/RegisterListType.hpp>

#include <nomad/collections/ListObject.hpp>
#include <nomad/collections/ListType.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

namespace nomad {

void registerListType(Runtime* runtime) {
    if (!runtime->registerParameterizedType(
        "List",
        1,
        [](const std::vector<const nomad::Type*>& arguments) {
            const auto* elementType = arguments[0];
            return std::make_unique<nomad::ListType>(elementType);
        },
        [runtime](const nomad::Type* type) {
            const auto* objectType = static_cast<const nomad::ObjectType*>(type);
            const auto* elementType = objectType->getTypeArguments()[0];
            const auto constructorId = runtime->registerNativeConstructor(
                objectType, {},
                [elementType](std::span<const nomad::RuntimeValue>) {
                    return new nomad::ListObject(elementType);
                }, ""
            );
            const auto countId = runtime->registerNativeProperty(
                objectType,
                "count",
                runtime->getIntegerType(),
                [](nomad::VirtualMachine* machine, nomad::RuntimeValue& result) {
                    const auto* list = machine->getThisAs<nomad::ListObject>();
                    const auto count = static_cast<nomad::NomadInteger>(list->getCount());
                    result.setIntegerValue(count);
                },
                {},
                NomadDoc("The number of elements in the list.")
            );
            const auto pushId = runtime->registerNativeMethod(
                objectType,
                "push",
                [](nomad::VirtualMachine* vm) {
                    auto* list = vm->getThisAs<nomad::ListObject>();
                    const auto& value = vm->getParameter(0);
                    list->push(value);
                },
                {nomad::defParameter("value", elementType, NomadDoc("The value to append."))},
                runtime->getVoidType(),
                NomadDoc("Appends a value to the list.")
            );
            const auto indexerId = runtime->registerNativeIndexer(
                objectType,
                {runtime->getIntegerType()},
                elementType,
                [elementType](nomad::VirtualMachine* machine) {
                    const auto* list = machine->getThisAs<nomad::ListObject>();
                    const auto index = machine->getIntegerParameter(0);
                    auto& result = machine->getResult();
                    const auto& value = list->getValue(index);
                    elementType->copyValue(value, result);
                },
                [](nomad::VirtualMachine* machine) {
                    auto* list = machine->getThisAs<nomad::ListObject>();
                    const auto index = machine->getIntegerParameter(0);
                    const auto& value = machine->getParameter(1);
                    list->setValue(index, value);
                },
                NomadDoc("Reads or replaces an element at a zero-based index. Out-of-range indices fault.")
            );
            if (constructorId == nomad::NOMAD_INVALID_ID || countId == nomad::NOMAD_INVALID_ID || pushId == nomad::NOMAD_INVALID_ID ||
                indexerId == nomad::NOMAD_INVALID_ID) {
                throw nomad::NomadException("Failed to register List members");
            }
        }
    )) {
        throw nomad::NomadException("Failed to register List");
    }
}

} // namespace nomad
