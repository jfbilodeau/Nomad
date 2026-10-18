// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/Array1Type.hpp>
#include <nomad/collections/Array2Type.hpp>
#include <nomad/collections/Array3Type.hpp>
#include <nomad/collections/ArrayObject.hpp>
#include <nomad/collections/ArrayType.hpp>
#include <nomad/collections/ListObject.hpp>
#include <nomad/collections/ListType.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/Module.hpp>

#include <utility>

using nomad::NomadString;

NOMAD_MODULE_EXPORT void registerModule(nomad::ModuleContext* context, nomad::ModuleCallbacks*) noexcept(false) {
    if (context->mode == nomad::ModuleMode::Packaging) {
        return;
    }
    const auto registerArrayType = [runtime = context->runtime](
        const NomadString& name,
        nomad::Runtime::ParameterizedTypeFactory factory
    ) {
        if (!runtime->registerParameterizedType(
            name,
            1,
            std::move(factory),
            [runtime](const nomad::Type* type) {
                const auto* objectType = static_cast<const nomad::ArrayType*>(type);
                const auto countId = runtime->registerNativeProperty(
                    objectType, "count", runtime->getIntegerType(),
                    [](nomad::VirtualMachine* machine, nomad::RuntimeValue& result) {
                        result.setIntegerValue(static_cast<nomad::NomadInteger>(
                            machine->getThisAs<nomad::ArrayObject>()->getCount()
                        ));
                    },
                    {}, NomadDoc("The total number of elements in the fixed-size array.")
                );
                if (countId == nomad::NOMAD_INVALID_ID) {
                    throw nomad::NomadException("Failed to register Array members");
                }
            }
        )) {
            throw nomad::NomadException("Failed to register " + name);
        }
    };
    registerArrayType(
        "Array1",
        [runtime = context->runtime](const std::vector<const nomad::Type*>& arguments) {
            return std::make_unique<nomad::Array1Type>(arguments[0], runtime->getIntegerType());
        }
    );
    registerArrayType(
        "Array2",
        [runtime = context->runtime](const std::vector<const nomad::Type*>& arguments) {
            return std::make_unique<nomad::Array2Type>(arguments[0], runtime->getIntegerType());
        }
    );
    registerArrayType(
        "Array3",
        [runtime = context->runtime](const std::vector<const nomad::Type*>& arguments) {
            return std::make_unique<nomad::Array3Type>(arguments[0], runtime->getIntegerType());
        }
    );
    if (!context->runtime->registerParameterizedType(
        "List",
        1,
        [](const std::vector<const nomad::Type*>& arguments) {
            return std::make_unique<nomad::ListType>(arguments[0]);
        },
        [runtime = context->runtime](const nomad::Type* type) {
            const auto* objectType = static_cast<const nomad::ObjectType*>(type);
            const auto* elementType = objectType->getTypeArguments()[0];
            const auto countId = runtime->registerNativeProperty(
                objectType,
                "count",
                runtime->getIntegerType(),
                [](nomad::VirtualMachine* machine, nomad::RuntimeValue& result) {
                    const auto* list = machine->getThisAs<nomad::ListObject>();
                    result.setIntegerValue(static_cast<nomad::NomadInteger>(list->getCount()));
                },
                {},
                NomadDoc("The number of elements in the list.")
            );
            const auto pushId = runtime->registerNativeMethod(
                objectType,
                "push",
                [](nomad::VirtualMachine* vm) {
                    auto* list = vm->getThisAs<nomad::ListObject>();
                    list->push(vm->getParameter(0));
                },
                {nomad::defParameter("value", elementType, NomadDoc("The value to append."))},
                runtime->getVoidType(),
                NomadDoc("Appends a value to the list.")
            );
            const auto indexerId = runtime->registerNativeIndexer1(
                objectType,
                runtime->getIntegerType(),
                elementType,
                [elementType](nomad::VirtualMachine* machine, const nomad::RuntimeValue& index, nomad::RuntimeValue& result) {
                    const auto* list = machine->getThisAs<nomad::ListObject>();
                    elementType->copyValue(list->getValue(index.getIntegerValue()), result);
                },
                [](nomad::VirtualMachine* machine, const nomad::RuntimeValue& index, const nomad::RuntimeValue& value) {
                    machine->getThisAs<nomad::ListObject>()->setValue(index.getIntegerValue(), value);
                },
                NomadDoc("Reads or replaces an element at a zero-based index. Out-of-range indices fault.")
            );
            if (countId == nomad::NOMAD_INVALID_ID || pushId == nomad::NOMAD_INVALID_ID ||
                indexerId == nomad::NOMAD_INVALID_ID) {
                throw nomad::NomadException("Failed to register List members");
            }
        }
    )) {
        throw nomad::NomadException("Failed to register List");
    }
}
