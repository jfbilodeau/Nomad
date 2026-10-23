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
        [runtime, registerMembers, writable, name](const Type* type) {
            registerMembers(type, writable);
            const auto* objectType = static_cast<const ArrayType*>(type);
            const auto countId = runtime->registerNativeProperty(
                objectType, "count", runtime->getIntegerType(),
                [](VirtualMachine* machine, RuntimeValue& result) {
                    const auto* array = machine->getThisAs<ArrayObject>();
                    result.setIntegerValue(static_cast<NomadInteger>(array->getCount()));
                },
                {}, NomadDoc("The total number of elements in the fixed-size array.")
            );
            if (countId.isInvalid()) {
                throw NomadException("Failed to register Array members");
            }
            if (writable) {
                const auto* readOnlyType = runtime->getParameterizedType(
                    "ReadOnly" + name, {objectType->getElementType()}
                );
                if (readOnlyType == nullptr || runtime->registerNativeMethod(
                    objectType, "toReadOnly",
                    [](VirtualMachine* machine) {
                        auto snapshot = machine->getThisAs<ArrayObject>()->createCopy();
                        machine->getResult().setObjectValue(snapshot.release());
                    },
                    {}, readOnlyType,
                    NomadDoc("Creates an independent read-only snapshot with the same dimensions. Object elements share references.")
                ).isInvalid()) {
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
        registerArrayFamily(runtime, name, "ReadOnly" + name, [dimensionCount](const std::vector<const Type*>& arguments) {
            return std::make_unique<ReadOnlyArrayType>(arguments[0], dimensionCount);
        }, registerMembers, false);
        registerArrayFamily(runtime, name, name, std::move(factory), registerMembers, true);
    };
    registerArrayType(
        "Array1", 1,
        [](const std::vector<const nomad::Type*>& arguments) {
            const auto* elementType = arguments[0];
            return std::make_unique<nomad::Array1Type>(elementType);
        },
        [runtime](const nomad::Type* type, const bool writable) {
            const auto* arrayType = static_cast<const nomad::ArrayType*>(type);
            if (writable && runtime->registerNativeConstructor(
                arrayType,
                {nomad::defParameter("size", runtime->getIntegerType(), "")},
                [arrayType](const std::span<const nomad::RuntimeValue> arguments) {
                    const auto size = arguments[0].getIntegerValue();
                    return new Array1dObject(arrayType->getElementType(), size);
                }, ""
            ).isInvalid()) {
                throw nomad::NomadException("Failed to register Array1 constructor");
            }
            const auto* elementType = arrayType->getElementType();
            const auto iteratorId = runtime->registerNativeMethod(
                arrayType, "iterator",
                [](VirtualMachine* machine) {
                    machine->getResult().setObjectValue(new ArrayIterator(machine->getThisAs<Array1dObject>()));
                },
                {}, runtime->getIteratorType(elementType),
                NomadDoc("Creates an independent live iterator over the array.")
            );
            runtime->registerIterableType(arrayType, elementType, runtime->getNativeMemberMethod(iteratorId));
            if (runtime->registerNativeIndexer(
                arrayType,
                {runtime->getIntegerType()},
                elementType,
                [](nomad::VirtualMachine* machine) {
                    const auto* array = machine->getThisAs<nomad::Array1dObject>();
                    const auto index = machine->getIntegerParameter(0);
                    auto& result = machine->getResult();
                    array->getValue(index, result);
                },
                writable ? NativeFunctionFn{[](nomad::VirtualMachine* machine) {
                    auto* array = machine->getThisAs<nomad::Array1dObject>();
                    const auto index = machine->getIntegerParameter(0);
                    const auto& value = machine->getParameter(1);
                    array->setValue(index, value);
                }} : NativeFunctionFn{},
                writable
                    ? NomadDoc("Reads or replaces an element at a zero-based index. Invalid indices log an error; reads return a default and writes do nothing.")
                    : NomadDoc("Reads an element at a zero-based index. Invalid indices log an error and return a default.")
            ) == nomad::NOMAD_INVALID_ID) {
                throw nomad::NomadException("Failed to register Array1 indexer");
            }
        }
    );
    registerArrayType(
        "Array2", 2,
        [](const std::vector<const nomad::Type*>& arguments) {
            const auto* elementType = arguments[0];
            return std::make_unique<nomad::Array2Type>(elementType);
        },
        [runtime](const nomad::Type* type, const bool writable) {
            const auto* arrayType = static_cast<const nomad::ArrayType*>(type);
            if (writable && runtime->registerNativeConstructor(
                arrayType,
                {
                    nomad::defParameter("size1", runtime->getIntegerType(), ""),
                    nomad::defParameter("size2", runtime->getIntegerType(), "")
                },
                [arrayType](const std::span<const nomad::RuntimeValue> arguments) {
                    const auto size1 = arguments[0].getIntegerValue();
                    const auto size2 = arguments[1].getIntegerValue();
                    return new Array2dObject(arrayType->getElementType(), size1, size2);
                }, ""
            ).isInvalid()) {
                throw nomad::NomadException("Failed to register Array2 constructor");
            }
            if (runtime->registerNativeIndexer(
                arrayType,
                {runtime->getIntegerType(), runtime->getIntegerType()},
                arrayType->getElementType(),
                [](nomad::VirtualMachine* machine) {
                    const auto* array = machine->getThisAs<nomad::Array2dObject>();
                    const auto index1 = machine->getIntegerParameter(0);
                    const auto index2 = machine->getIntegerParameter(1);
                    auto& result = machine->getResult();
                    array->getValue(index1, index2, result);
                },
                writable ? NativeFunctionFn{[](nomad::VirtualMachine* machine) {
                    auto* array = machine->getThisAs<nomad::Array2dObject>();
                    const auto index1 = machine->getIntegerParameter(0);
                    const auto index2 = machine->getIntegerParameter(1);
                    const auto& value = machine->getParameter(2);
                    array->setValue(index1, index2, value);
                }} : NativeFunctionFn{},
                writable
                    ? NomadDoc("Reads or replaces an element at two zero-based indices. Invalid indices log an error; reads return a default and writes do nothing.")
                    : NomadDoc("Reads an element at two zero-based indices. Invalid indices log an error and return a default.")
            ) == nomad::NOMAD_INVALID_ID) {
                throw nomad::NomadException("Failed to register Array2 indexer");
            }
        }
    );
    registerArrayType(
        "Array3", 3,
        [](const std::vector<const nomad::Type*>& arguments) {
            const auto* elementType = arguments[0];
            return std::make_unique<nomad::Array3Type>(elementType);
        },
        [runtime](const nomad::Type* type, const bool writable) {
            const auto* arrayType = static_cast<const nomad::ArrayType*>(type);
            if (writable && runtime->registerNativeConstructor(
                arrayType,
                {
                    nomad::defParameter("size1", runtime->getIntegerType(), ""),
                    nomad::defParameter("size2", runtime->getIntegerType(), ""),
                    nomad::defParameter("size3", runtime->getIntegerType(), "")
                },
                [arrayType](const std::span<const nomad::RuntimeValue> arguments) {
                    const auto size1 = arguments[0].getIntegerValue();
                    const auto size2 = arguments[1].getIntegerValue();
                    const auto size3 = arguments[2].getIntegerValue();
                    return new Array3dObject(arrayType->getElementType(), size1, size2, size3);
                }, ""
            ).isInvalid()) {
                throw nomad::NomadException("Failed to register Array3 constructor");
            }
            if (runtime->registerNativeIndexer(
                arrayType,
                {runtime->getIntegerType(), runtime->getIntegerType(), runtime->getIntegerType()},
                arrayType->getElementType(),
                [](nomad::VirtualMachine* machine) {
                    const auto* array = machine->getThisAs<nomad::Array3dObject>();
                    const auto index1 = machine->getIntegerParameter(0);
                    const auto index2 = machine->getIntegerParameter(1);
                    const auto index3 = machine->getIntegerParameter(2);
                    auto& result = machine->getResult();
                    array->getValue(index1, index2, index3, result);
                },
                writable ? NativeFunctionFn{[](nomad::VirtualMachine* machine) {
                    auto* array = machine->getThisAs<nomad::Array3dObject>();
                    const auto index1 = machine->getIntegerParameter(0);
                    const auto index2 = machine->getIntegerParameter(1);
                    const auto index3 = machine->getIntegerParameter(2);
                    const auto& value = machine->getParameter(3);
                    array->setValue(index1, index2, index3, value);
                }} : NativeFunctionFn{},
                writable
                    ? NomadDoc("Reads or replaces an element at three zero-based indices. Invalid indices log an error; reads return a default and writes do nothing.")
                    : NomadDoc("Reads an element at three zero-based indices. Invalid indices log an error and return a default.")
            ) == nomad::NOMAD_INVALID_ID) {
                throw nomad::NomadException("Failed to register Array3 indexer");
            }
        }
    );
    if (!runtime->registerNativeParameterizedConstructor(
        "Array", 1,
        [runtime](const NomadString& name, const std::vector<const nomad::Type*>& arguments) {
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
                    static_cast<const nomad::ObjectType*>(constructor->getReturnType()),
                    constructor->getFactory(),
                    ""
                ).isInvalid()) {
                    throw nomad::NomadException("Failed to register Array constructor");
                }
            }
        }
    )) {
        throw nomad::NomadException("Failed to register Array constructors");
    }
}

} // namespace nomad
