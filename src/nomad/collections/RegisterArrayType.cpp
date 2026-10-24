// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/RegisterArrayType.hpp>

#include <nomad/collections/Array1Type.hpp>
#include <nomad/collections/Array1dObject.hpp>
#include <nomad/collections/Array2Type.hpp>
#include <nomad/collections/Array2dObject.hpp>
#include <nomad/collections/Array3Type.hpp>
#include <nomad/collections/Array3dObject.hpp>
#include <nomad/collections/ArrayIterator.hpp>
#include <nomad/collections/ArrayObject.hpp>
#include <nomad/collections/ArrayType.hpp>
#include <nomad/collections/ReadOnlyArrayType.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <utility>

namespace nomad {

namespace {

void registerArrayFamily(
    Runtime* runtime,
    const NomadString& name,
    const NomadString& family,
    Runtime::ParameterizedTypeFactory factory,
    const std::function<void(const Type*, bool)>& registerMembers,
    const bool writable
) {
    if (!runtime->registerParameterizedType(
        family,
        1,
        std::move(factory),
        [registerMembers, writable, name](Runtime* runtime, const TypeId id) {
            const auto* type = runtime->getType(id).value();
            registerMembers(type, writable);
            const auto* objectType = static_cast<const ArrayType*>(type);
            const auto countId = runtime->addProperty(id, {
                .valueTypeId = runtime->getTypeId("int"),
                .name = "count",
                .getter = [](VirtualMachine* machine, RuntimeValue& result) {
                    const auto* array = machine->getThisAs<ArrayObject>();
                    result.setIntegerValue(static_cast<NomadInteger>(array->getCount()));
                },
                .setter = {},
                .documentation = NomadDoc("The total number of elements in the fixed-size array.")
            });
            if (countId.isInvalid()) {
                throw NomadException("Failed to register Array members");
            }
            if (writable) {
                const auto* readOnlyType = runtime->getParameterizedType(
                    "ReadOnly" + name, {objectType->getElementType()}
                );
                if (readOnlyType == nullptr || runtime->addMethod(id, {
                    .name = "toReadOnly",
                    .parameters = {},
                    .returnTypeId = readOnlyType->getId(),
                    .function = [](VirtualMachine* machine) {
                        auto snapshot = machine->getThisAs<ArrayObject>()->createCopy();
                        machine->getResult().setObjectValue(snapshot.release());
                    },
                    .documentation = NomadDoc("Creates an independent read-only snapshot with the same dimensions. Object elements share references.")
                }).isInvalid()) {
                    throw NomadException("Failed to register Array.toReadOnly");
                }
            }
        }
    )) {
        throw NomadException("Failed to register " + family);
    }
}

} // namespace

void registerArrayType(Runtime* runtime) {
    const auto registerArrayType = [runtime](
        const NomadString& name,
        const NomadIndex dimensionCount,
        nomad::Runtime::ParameterizedTypeFactory factory,
        std::function<void(const Type*, bool)> registerMembers
    ) {
        registerArrayFamily(runtime, name, "ReadOnly" + name, [dimensionCount, name](
            Runtime* owningRuntime,
            TypeId id,
            const std::vector<const Type*>& arguments
        ) {
            return std::make_unique<ReadOnlyArrayType>(
                owningRuntime, id, "ReadOnly" + name + "<" + arguments[0]->getTypeName() + ">",
                ObjectTypeMode::Closed, arguments[0], dimensionCount
            );
        }, registerMembers, false);
        registerArrayFamily(runtime, name, name, std::move(factory), registerMembers, true);
    };
    registerArrayType(
        "Array1", 1,
        [](Runtime* owningRuntime, TypeId id, const std::vector<const nomad::Type*>& arguments) {
            const auto* elementType = arguments[0];
            return std::make_unique<nomad::Array1Type>(
                owningRuntime, id, "Array1<" + elementType->getTypeName() + ">", ObjectTypeMode::Closed, elementType
            );
        },
        [runtime](const nomad::Type* type, const bool writable) {
            const auto* arrayType = static_cast<const nomad::ArrayType*>(type);
            if (writable) {
                const auto constructorId = runtime->addConstructor(arrayType->getId(), {
                    .parameters = {{runtime->getIntegerType()->getId(), "size", ""}},
                    .factory = [arrayType](const std::span<const nomad::RuntimeValue> arguments) {
                        const auto size = arguments[0].getIntegerValue();
                        return new Array1dObject(arrayType->getElementType(), size);
                    },
                    .documentation = ""
                });
                if (constructorId.isInvalid()) {
                    throw nomad::NomadException("Failed to register Array1 constructor");
                }
            }
            const auto* elementType = arrayType->getElementType();
            runtime->addIterable(arrayType->getId(), {
                .valueTypeId = elementType->getId(),
                .factory = {
                    .name = "iterator",
                    .parameters = {},
                    .returnTypeId = runtime->getIteratorType(elementType)->getId(),
                    .function = [](VirtualMachine* machine) {
                        machine->getResult().setObjectValue(new ArrayIterator(machine->getThisAs<Array1dObject>()));
                    },
                    .documentation = NomadDoc("Creates an independent live iterator over the array.")
                }
            });
            if (runtime->addIndexer(arrayType->getId(), {
                .valueTypeId = elementType->getId(),
                .indexTypes = {runtime->getIntegerType()->getId()},
                .getter = [](nomad::VirtualMachine* machine) {
                    const auto* array = machine->getThisAs<nomad::Array1dObject>();
                    const auto index = machine->getIntegerParameter(0);
                    auto& result = machine->getResult();
                    array->getValue(index, result);
                },
                .setter = writable ? NativeFunctionFn{[](nomad::VirtualMachine* machine) {
                    auto* array = machine->getThisAs<nomad::Array1dObject>();
                    const auto index = machine->getIntegerParameter(0);
                    const auto& value = machine->getParameter(1);
                    array->setValue(index, value);
                }} : NativeFunctionFn{},
                .documentation = writable
                    ? NomadDoc("Reads or replaces an element at a zero-based index. Invalid indices log an error; reads return a default and writes do nothing.")
                    : NomadDoc("Reads an element at a zero-based index. Invalid indices log an error and return a default.")
            }).isInvalid()) {
                throw nomad::NomadException("Failed to register Array1 indexer");
            }
        }
    );
    registerArrayType(
        "Array2", 2,
        [](Runtime* owningRuntime, TypeId id, const std::vector<const nomad::Type*>& arguments) {
            const auto* elementType = arguments[0];
            return std::make_unique<nomad::Array2Type>(
                owningRuntime, id, "Array2<" + elementType->getTypeName() + ">", ObjectTypeMode::Closed, elementType
            );
        },
        [runtime](const nomad::Type* type, const bool writable) {
            const auto* arrayType = static_cast<const nomad::ArrayType*>(type);
            if (writable) {
                const auto constructorId = runtime->addConstructor(arrayType->getId(), {
                    .parameters = {
                        {runtime->getIntegerType()->getId(), "size1", ""},
                        {runtime->getIntegerType()->getId(), "size2", ""}
                    },
                    .factory = [arrayType](const std::span<const nomad::RuntimeValue> arguments) {
                        const auto size1 = arguments[0].getIntegerValue();
                        const auto size2 = arguments[1].getIntegerValue();
                        return new Array2dObject(arrayType->getElementType(), size1, size2);
                    },
                    .documentation = ""
                });
                if (constructorId.isInvalid()) {
                    throw nomad::NomadException("Failed to register Array2 constructor");
                }
            }
            if (runtime->addIndexer(arrayType->getId(), {
                .valueTypeId = arrayType->getElementType()->getId(),
                .indexTypes = {runtime->getIntegerType()->getId(), runtime->getIntegerType()->getId()},
                .getter = [](nomad::VirtualMachine* machine) {
                    const auto* array = machine->getThisAs<nomad::Array2dObject>();
                    const auto index1 = machine->getIntegerParameter(0);
                    const auto index2 = machine->getIntegerParameter(1);
                    auto& result = machine->getResult();
                    array->getValue(index1, index2, result);
                },
                .setter = writable ? NativeFunctionFn{[](nomad::VirtualMachine* machine) {
                    auto* array = machine->getThisAs<nomad::Array2dObject>();
                    const auto index1 = machine->getIntegerParameter(0);
                    const auto index2 = machine->getIntegerParameter(1);
                    const auto& value = machine->getParameter(2);
                    array->setValue(index1, index2, value);
                }} : NativeFunctionFn{},
                .documentation = writable
                    ? NomadDoc("Reads or replaces an element at two zero-based indices. Invalid indices log an error; reads return a default and writes do nothing.")
                    : NomadDoc("Reads an element at two zero-based indices. Invalid indices log an error and return a default.")
            }).isInvalid()) {
                throw nomad::NomadException("Failed to register Array2 indexer");
            }
        }
    );
    registerArrayType(
        "Array3", 3,
        [](Runtime* owningRuntime, TypeId id, const std::vector<const nomad::Type*>& arguments) {
            const auto* elementType = arguments[0];
            return std::make_unique<nomad::Array3Type>(
                owningRuntime, id, "Array3<" + elementType->getTypeName() + ">", ObjectTypeMode::Closed, elementType
            );
        },
        [runtime](const nomad::Type* type, const bool writable) {
            const auto* arrayType = static_cast<const nomad::ArrayType*>(type);
            if (writable) {
                const auto constructorId = runtime->addConstructor(arrayType->getId(), {
                    .parameters = {
                        {runtime->getIntegerType()->getId(), "size1", ""},
                        {runtime->getIntegerType()->getId(), "size2", ""},
                        {runtime->getIntegerType()->getId(), "size3", ""}
                    },
                    .factory = [arrayType](const std::span<const nomad::RuntimeValue> arguments) {
                        const auto size1 = arguments[0].getIntegerValue();
                        const auto size2 = arguments[1].getIntegerValue();
                        const auto size3 = arguments[2].getIntegerValue();
                        return new Array3dObject(arrayType->getElementType(), size1, size2, size3);
                    },
                    .documentation = ""
                });
                if (constructorId.isInvalid()) {
                    throw nomad::NomadException("Failed to register Array3 constructor");
                }
            }
            if (runtime->addIndexer(arrayType->getId(), {
                .valueTypeId = arrayType->getElementType()->getId(),
                .indexTypes = {
                    runtime->getIntegerType()->getId(),
                    runtime->getIntegerType()->getId(),
                    runtime->getIntegerType()->getId()
                },
                .getter = [](nomad::VirtualMachine* machine) {
                    const auto* array = machine->getThisAs<nomad::Array3dObject>();
                    const auto index1 = machine->getIntegerParameter(0);
                    const auto index2 = machine->getIntegerParameter(1);
                    const auto index3 = machine->getIntegerParameter(2);
                    auto& result = machine->getResult();
                    array->getValue(index1, index2, index3, result);
                },
                .setter = writable ? NativeFunctionFn{[](nomad::VirtualMachine* machine) {
                    auto* array = machine->getThisAs<nomad::Array3dObject>();
                    const auto index1 = machine->getIntegerParameter(0);
                    const auto index2 = machine->getIntegerParameter(1);
                    const auto index3 = machine->getIntegerParameter(2);
                    const auto& value = machine->getParameter(3);
                    array->setValue(index1, index2, index3, value);
                }} : NativeFunctionFn{},
                .documentation = writable
                    ? NomadDoc("Reads or replaces an element at three zero-based indices. Invalid indices log an error; reads return a default and writes do nothing.")
                    : NomadDoc("Reads an element at three zero-based indices. Invalid indices log an error and return a default.")
            }).isInvalid()) {
                throw nomad::NomadException("Failed to register Array3 indexer");
            }
        }
    );
    if (!runtime->registerNativeParameterizedConstructor(
        "Array", 1,
        [runtime](const NomadString& name, const std::vector<const nomad::Type*>& arguments) {
            // One class per specialization (e.g. `Array<int>`); each rank constructor builds a different array type.
            const auto id = runtime->createObjectType(name, nomad::ObjectTypeMode::Closed);
            if (id.isInvalid()) {
                throw nomad::NomadException("Failed to create Array constructor type");
            }
            for (const auto* family : {"Array1", "Array2", "Array3"}) {
                const auto* type = runtime->getParameterizedType(family, arguments);
                if (type == nullptr) {
                    throw nomad::NomadException("Failed to resolve Array constructor return type");
                }
                const auto rankConstructors = runtime->getConstructors(type);
                const auto* constructor = rankConstructors.empty() ? nullptr : rankConstructors.front();
                if (constructor == nullptr) {
                    throw nomad::NomadException("Failed to resolve Array rank constructor");
                }
                std::vector<ParameterDefinition> parameters;
                parameters.reserve(constructor->getParameters().size());
                for (const auto& parameter : constructor->getParameters()) {
                    parameters.push_back({parameter.type->getId(), parameter.name, parameter.doc});
                }
                if (runtime->addConstructor(id, constructor->getReturnType()->getId(), {
                    .parameters = std::move(parameters),
                    .factory = constructor->getFactory(),
                    .documentation = ""
                }).isInvalid()) {
                    throw nomad::NomadException("Failed to register Array constructor");
                }
            }
        }
    )) {
        throw nomad::NomadException("Failed to register Array constructors");
    }
}

} // namespace nomad
