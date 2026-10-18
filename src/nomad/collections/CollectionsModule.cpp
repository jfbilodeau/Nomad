// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/Array1Type.hpp>
#include <nomad/collections/Array1dObject.hpp>
#include <nomad/collections/Array2Type.hpp>
#include <nomad/collections/Array2dObject.hpp>
#include <nomad/collections/Array3Type.hpp>
#include <nomad/collections/Array3dObject.hpp>
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
        nomad::Runtime::ParameterizedTypeFactory factory,
        nomad::Runtime::ParameterizedTypeInitializer registerConstructor
    ) {
        if (!runtime->registerParameterizedType(
            name,
            1,
            std::move(factory),
            [runtime, registerConstructor = std::move(registerConstructor)](const nomad::Type* type) {
                registerConstructor(type);
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
        [](const std::vector<const nomad::Type*>& arguments) {
            return std::make_unique<nomad::Array1Type>(arguments[0]);
        },
        [runtime = context->runtime](const nomad::Type* type) {
            const auto* arrayType = static_cast<const nomad::Array1Type*>(type);
            if (runtime->registerConstructor(
                type->getTypeName(),
                {nomad::defParameter("size", runtime->getIntegerType(), "")},
                arrayType,
                [arrayType](const std::span<const nomad::RuntimeValue> arguments) {
                    return arrayType->createObject(arguments[0].getIntegerValue());
                }, ""
            ) == nomad::NOMAD_INVALID_ID) {
                throw nomad::NomadException("Failed to register Array1 constructor");
            }
        }
    );
    registerArrayType(
        "Array2",
        [](const std::vector<const nomad::Type*>& arguments) {
            return std::make_unique<nomad::Array2Type>(arguments[0]);
        },
        [runtime = context->runtime](const nomad::Type* type) {
            const auto* arrayType = static_cast<const nomad::Array2Type*>(type);
            if (runtime->registerConstructor(
                type->getTypeName(),
                {
                    nomad::defParameter("size1", runtime->getIntegerType(), ""),
                    nomad::defParameter("size2", runtime->getIntegerType(), "")
                },
                arrayType,
                [arrayType](const std::span<const nomad::RuntimeValue> arguments) {
                    return arrayType->createObject(arguments[0].getIntegerValue(), arguments[1].getIntegerValue());
                }, ""
            ) == nomad::NOMAD_INVALID_ID) {
                throw nomad::NomadException("Failed to register Array2 constructor");
            }
        }
    );
    registerArrayType(
        "Array3",
        [](const std::vector<const nomad::Type*>& arguments) {
            return std::make_unique<nomad::Array3Type>(arguments[0]);
        },
        [runtime = context->runtime](const nomad::Type* type) {
            const auto* arrayType = static_cast<const nomad::Array3Type*>(type);
            if (runtime->registerConstructor(
                type->getTypeName(),
                {
                    nomad::defParameter("size1", runtime->getIntegerType(), ""),
                    nomad::defParameter("size2", runtime->getIntegerType(), ""),
                    nomad::defParameter("size3", runtime->getIntegerType(), "")
                },
                arrayType,
                [arrayType](const std::span<const nomad::RuntimeValue> arguments) {
                    return arrayType->createObject(
                        arguments[0].getIntegerValue(), arguments[1].getIntegerValue(), arguments[2].getIntegerValue()
                    );
                }, ""
            ) == nomad::NOMAD_INVALID_ID) {
                throw nomad::NomadException("Failed to register Array3 constructor");
            }
        }
    );
    if (!context->runtime->registerParameterizedConstructor(
        "Array", 1,
        [runtime = context->runtime](const NomadString& name, const std::vector<const nomad::Type*>& arguments) {
            for (const auto* family : {"Array1", "Array2", "Array3"}) {
                const auto* type = runtime->getParameterizedType(family, arguments);
                if (type == nullptr) {
                    throw nomad::NomadException("Failed to resolve Array constructor return type");
                }
                nomad::ConstructorDefinition rankConstructor;
                for (const auto* candidate : runtime->getConstructors()) {
                    if (candidate->name == type->getTypeName()) {
                        rankConstructor = *candidate;
                        break;
                    }
                }
                if (!rankConstructor.factory || runtime->registerConstructor(
                    name, rankConstructor.parameters, static_cast<const nomad::ObjectTypeBase*>(rankConstructor.returnType),
                    rankConstructor.factory, ""
                ) == nomad::NOMAD_INVALID_ID) {
                    throw nomad::NomadException("Failed to register Array constructor");
                }
            }
        }
    )) {
        throw nomad::NomadException("Failed to register Array constructors");
    }
    if (!context->runtime->registerParameterizedType(
        "List",
        1,
        [](const std::vector<const nomad::Type*>& arguments) {
            return std::make_unique<nomad::ListType>(arguments[0]);
        },
        [runtime = context->runtime](const nomad::Type* type) {
            const auto* objectType = static_cast<const nomad::ObjectType*>(type);
            const auto* elementType = objectType->getTypeArguments()[0];
            const auto constructorId = runtime->registerConstructor(
                type->getTypeName(), {}, objectType,
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
            if (constructorId == nomad::NOMAD_INVALID_ID || countId == nomad::NOMAD_INVALID_ID || pushId == nomad::NOMAD_INVALID_ID ||
                indexerId == nomad::NOMAD_INVALID_ID) {
                throw nomad::NomadException("Failed to register List members");
            }
        }
    )) {
        throw nomad::NomadException("Failed to register List");
    }
}
