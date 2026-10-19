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
#include <nomad/collections/MapObject.hpp>
#include <nomad/collections/MapType.hpp>

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
            if (runtime->registerNativeConstructor(
                arrayType,
                {nomad::defParameter("size", runtime->getIntegerType(), "")},
                [arrayType](const std::span<const nomad::RuntimeValue> arguments) {
                    return arrayType->createObject(arguments[0].getIntegerValue());
                }, ""
            ) == nomad::NOMAD_INVALID_ID) {
                throw nomad::NomadException("Failed to register Array1 constructor");
            }
            const auto* elementType = arrayType->getElementType();
            if (runtime->registerNativeIndexer(
                arrayType,
                {runtime->getIntegerType()},
                elementType,
                [](nomad::VirtualMachine* machine) {
                    const auto* array = machine->getThisAs<nomad::Array1dObject>();
                    array->getValue(machine->getIntegerParameter(0), machine->getResult());
                },
                [](nomad::VirtualMachine* machine) {
                    machine->getThisAs<nomad::Array1dObject>()->setValue(machine->getIntegerParameter(0), machine->getParameter(1));
                },
                NomadDoc("Reads or replaces an element at a zero-based index. Invalid indices log an error; reads return a default and writes do nothing.")
            ) == nomad::NOMAD_INVALID_ID) {
                throw nomad::NomadException("Failed to register Array1 indexer");
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
            if (runtime->registerNativeConstructor(
                arrayType,
                {
                    nomad::defParameter("size1", runtime->getIntegerType(), ""),
                    nomad::defParameter("size2", runtime->getIntegerType(), "")
                },
                [arrayType](const std::span<const nomad::RuntimeValue> arguments) {
                    return arrayType->createObject(arguments[0].getIntegerValue(), arguments[1].getIntegerValue());
                }, ""
            ) == nomad::NOMAD_INVALID_ID) {
                throw nomad::NomadException("Failed to register Array2 constructor");
            }
            if (runtime->registerNativeIndexer(
                arrayType,
                {runtime->getIntegerType(), runtime->getIntegerType()},
                arrayType->getElementType(),
                [](nomad::VirtualMachine* machine) {
                    machine->getThisAs<nomad::Array2dObject>()->getValue(
                        machine->getIntegerParameter(0), machine->getIntegerParameter(1), machine->getResult()
                    );
                },
                [](nomad::VirtualMachine* machine) {
                    machine->getThisAs<nomad::Array2dObject>()->setValue(
                        machine->getIntegerParameter(0), machine->getIntegerParameter(1), machine->getParameter(2)
                    );
                },
                NomadDoc("Reads or replaces an element at two zero-based indices. Invalid indices log an error; reads return a default and writes do nothing.")
            ) == nomad::NOMAD_INVALID_ID) {
                throw nomad::NomadException("Failed to register Array2 indexer");
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
            if (runtime->registerNativeConstructor(
                arrayType,
                {
                    nomad::defParameter("size1", runtime->getIntegerType(), ""),
                    nomad::defParameter("size2", runtime->getIntegerType(), ""),
                    nomad::defParameter("size3", runtime->getIntegerType(), "")
                },
                [arrayType](const std::span<const nomad::RuntimeValue> arguments) {
                    return arrayType->createObject(
                        arguments[0].getIntegerValue(), arguments[1].getIntegerValue(), arguments[2].getIntegerValue()
                    );
                }, ""
            ) == nomad::NOMAD_INVALID_ID) {
                throw nomad::NomadException("Failed to register Array3 constructor");
            }
            if (runtime->registerNativeIndexer(
                arrayType,
                {runtime->getIntegerType(), runtime->getIntegerType(), runtime->getIntegerType()},
                arrayType->getElementType(),
                [](nomad::VirtualMachine* machine) {
                    machine->getThisAs<nomad::Array3dObject>()->getValue(
                        machine->getIntegerParameter(0), machine->getIntegerParameter(1),
                        machine->getIntegerParameter(2), machine->getResult()
                    );
                },
                [](nomad::VirtualMachine* machine) {
                    machine->getThisAs<nomad::Array3dObject>()->setValue(
                        machine->getIntegerParameter(0), machine->getIntegerParameter(1),
                        machine->getIntegerParameter(2), machine->getParameter(3)
                    );
                },
                NomadDoc("Reads or replaces an element at three zero-based indices. Invalid indices log an error; reads return a default and writes do nothing.")
            ) == nomad::NOMAD_INVALID_ID) {
                throw nomad::NomadException("Failed to register Array3 indexer");
            }
        }
    );
    if (!context->runtime->registerNativeParameterizedConstructor(
        "Array", 1,
        [runtime = context->runtime](const NomadString& name, const std::vector<const nomad::Type*>& arguments) {
            // One class per specialization (e.g. `Array<int>`); each rank constructor builds a different array type.
            auto classType = std::make_unique<nomad::ObjectType>(name);
            const auto* arrayClass = classType.get();
            runtime->registerType(std::move(classType));
            for (const auto* family : {"Array1", "Array2", "Array3"}) {
                const auto* type = runtime->getParameterizedType(family, arguments);
                if (type == nullptr) {
                    throw nomad::NomadException("Failed to resolve Array constructor return type");
                }
                const auto rankConstructors = runtime->getConstructors(type);
                const auto* constructor = rankConstructors.empty() ? nullptr : rankConstructors.front();
                if (constructor == nullptr || runtime->registerNativeConstructor(
                    arrayClass,
                    constructor->getParameters(),
                    static_cast<const nomad::ObjectTypeBase*>(constructor->getReturnType()),
                    constructor->getFactory(),
                    ""
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
            const auto indexerId = runtime->registerNativeIndexer(
                objectType,
                {runtime->getIntegerType()},
                elementType,
                [elementType](nomad::VirtualMachine* machine) {
                    const auto* list = machine->getThisAs<nomad::ListObject>();
                    elementType->copyValue(list->getValue(machine->getIntegerParameter(0)), machine->getResult());
                },
                [](nomad::VirtualMachine* machine) {
                    machine->getThisAs<nomad::ListObject>()->setValue(machine->getIntegerParameter(0), machine->getParameter(1));
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
    if (!context->runtime->registerParameterizedType(
        "Map",
        2,
        [](const std::vector<const nomad::Type*>& arguments) {
            return std::make_unique<nomad::MapType>(arguments[0], arguments[1]);
        },
        [runtime = context->runtime](const nomad::Type* type) {
            const auto* objectType = static_cast<const nomad::MapType*>(type);
            const auto* valueType = objectType->getTypeArguments()[1];
            const auto constructorId = runtime->registerNativeConstructor(
                objectType, {},
                [valueType](std::span<const nomad::RuntimeValue>) {
                    return new nomad::MapObject(valueType);
                },
                NomadDoc("Creates an empty map with string keys.")
            );
            const auto countId = runtime->registerNativeProperty(
                objectType,
                "count",
                runtime->getIntegerType(),
                [](nomad::VirtualMachine* machine, nomad::RuntimeValue& result) {
                    result.setIntegerValue(static_cast<nomad::NomadInteger>(
                        machine->getThisAs<nomad::MapObject>()->getCount()
                    ));
                },
                {},
                NomadDoc("The number of entries in the map.")
            );
            const auto indexerId = runtime->registerNativeIndexer(
                objectType,
                {runtime->getStringType()},
                valueType,
                [](nomad::VirtualMachine* machine) {
                    machine->getThisAs<nomad::MapObject>()->getValue(
                        machine->getStringParameter(0)->getString(), machine->getResult()
                    );
                },
                [](nomad::VirtualMachine* machine) {
                    machine->getThisAs<nomad::MapObject>()->setValue(
                        machine->getStringParameter(0)->getString(), machine->getParameter(1)
                    );
                },
                NomadDoc("Reads, inserts, or replaces an entry by string key. Missing keys log an error and return a default without inserting.")
            );
            const auto containsId = runtime->registerNativeMethod(
                objectType,
                "contains",
                [](nomad::VirtualMachine* machine) {
                    machine->setBooleanResult(machine->getThisAs<nomad::MapObject>()->contains(
                        machine->getStringParameter(0)->getString()
                    ));
                },
                {nomad::defParameter("key", runtime->getStringType(), NomadDoc("The case-sensitive key to check."))},
                runtime->getBooleanType(),
                NomadDoc("Returns whether the key exists, without logging or changing the map.")
            );
            if (constructorId == nomad::NOMAD_INVALID_ID || countId == nomad::NOMAD_INVALID_ID ||
                indexerId == nomad::NOMAD_INVALID_ID || containsId == nomad::NOMAD_INVALID_ID) {
                throw nomad::NomadException("Failed to register Map members");
            }
        }
    )) {
        throw nomad::NomadException("Failed to register Map");
    }
}
