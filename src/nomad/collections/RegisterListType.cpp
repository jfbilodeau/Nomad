// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/RegisterListType.hpp>

#include <nomad/collections/ListIterator.hpp>
#include <nomad/collections/ListObject.hpp>
#include <nomad/collections/ListType.hpp>
#include <nomad/collections/ReadOnlyListType.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <utility>

namespace nomad {

namespace {

void registerListReaders(Runtime* runtime, const ObjectType* objectType, const bool writable) {
    const auto* elementType = objectType->getTypeArguments()[0];
    const auto iteratorId = runtime->registerNativeMethod(
        objectType, "iterator",
        [](VirtualMachine* machine) {
            machine->getResult().setObjectValue(new ListIterator(machine->getThisAs<ListObject>()));
        },
        {}, runtime->getIteratorType(elementType),
        NomadDoc("Creates an independent live iterator. Structural changes invalidate its next advance.")
    );
    const auto countId = runtime->registerNativeProperty(
        objectType, "count", runtime->getIntegerType(),
        [](VirtualMachine* machine, RuntimeValue& result) {
            result.setIntegerValue(static_cast<NomadInteger>(machine->getThisAs<ListObject>()->getCount()));
        },
        {}, NomadDoc("The number of elements in the list.")
    );
    NativeFunctionFn setter;
    if (writable) {
        setter = [](VirtualMachine* machine) {
            machine->getThisAs<ListObject>()->setValue(machine->getIntegerParameter(0), machine->getParameter(1));
        };
    }
    const auto indexerId = runtime->registerNativeIndexer(
        objectType, {runtime->getIntegerType()}, elementType,
        [elementType](VirtualMachine* machine) {
            const auto* list = machine->getThisAs<ListObject>();
            elementType->copyValue(list->getValue(machine->getIntegerParameter(0)), machine->getResult());
        },
        std::move(setter),
        writable
            ? NomadDoc("Reads or replaces an element at a zero-based index. Out-of-range indices fault.")
            : NomadDoc("Reads an element at a zero-based index. Out-of-range indices fault.")
    );
    if (iteratorId == NOMAD_INVALID_ID || countId == NOMAD_INVALID_ID || indexerId == NOMAD_INVALID_ID) {
        throw NomadException("Failed to register List readers");
    }
    runtime->registerIterableType(objectType, elementType, runtime->getNativeMemberMethod(iteratorId));
}

} // namespace

void registerListType(Runtime* runtime) {
    if (!runtime->registerParameterizedType(
        "ReadOnlyList", 1,
        [](const std::vector<const Type*>& arguments) {
            return std::make_unique<ReadOnlyListType>(arguments[0]);
        },
        [runtime](const Type* type) {
            registerListReaders(runtime, static_cast<const ObjectType*>(type), false);
        }
    )) {
        throw NomadException("Failed to register ReadOnlyList");
    }
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
            registerListReaders(runtime, objectType, true);
            const auto* readOnlyType = runtime->getParameterizedType("ReadOnlyList", {elementType});
            if (readOnlyType == nullptr || runtime->registerNativeMethod(
                objectType, "toReadOnly",
                [](VirtualMachine* machine) {
                    const auto* source = machine->getThisAs<ListObject>();
                    auto snapshot = std::make_unique<ListObject>(source->getElementType());
                    for (auto index = NomadIndex{0}; index < source->getCount(); ++index) {
                        (void)snapshot->append(source->getValue(static_cast<NomadInteger>(index)));
                    }
                    machine->getResult().setObjectValue(snapshot.release());
                },
                {}, readOnlyType,
                NomadDoc("Creates an independent read-only snapshot. Object elements share references.")
            ) == NOMAD_INVALID_ID) {
                throw NomadException("Failed to register List.toReadOnly");
            }
            const auto constructorId = runtime->registerNativeConstructor(
                objectType, {},
                [elementType](std::span<const nomad::RuntimeValue>) {
                    return new nomad::ListObject(elementType);
                }, ""
            );
            const auto appendId = runtime->registerNativeMethod(
                objectType,
                "append",
                [](nomad::VirtualMachine* vm) {
                    auto* list = vm->getThisAs<nomad::ListObject>();
                    const auto& value = vm->getParameter(0);
                    const auto index = list->append(value);
                    vm->setIntegerResult(index);
                },
                {nomad::defParameter("value", elementType, NomadDoc("The value to append."))},
                runtime->getIntegerType(),
                NomadDoc("Appends a value and returns its zero-based index.")
            );
            const auto prependId = runtime->registerNativeMethod(
                objectType,
                "prepend",
                [](nomad::VirtualMachine* machine) {
                    auto* list = machine->getThisAs<nomad::ListObject>();
                    const auto& value = machine->getParameter(0);
                    list->prepend(value);
                },
                {nomad::defParameter("value", elementType, NomadDoc("The value to insert at the beginning."))},
                runtime->getVoidType(),
                NomadDoc("Inserts a value at the beginning, shifting existing elements right.")
            );
            const auto insertId = runtime->registerNativeMethod(
                objectType,
                "insert",
                [](nomad::VirtualMachine* machine) {
                    auto* list = machine->getThisAs<nomad::ListObject>();
                    const auto index = machine->getIntegerParameter(0);
                    const auto& value = machine->getParameter(1);
                    list->insert(index, value);
                },
                {
                    nomad::defParameter("index", runtime->getIntegerType(), NomadDoc("The insertion position from zero through count.")),
                    nomad::defParameter("value", elementType, NomadDoc("The value to insert."))
                },
                runtime->getVoidType(),
                NomadDoc("Inserts before the index, shifting later elements right. Index count appends; out-of-range indices fault.")
            );
            const auto removeId = runtime->registerNativeMethod(
                objectType,
                "remove",
                [](nomad::VirtualMachine* machine) {
                    auto* list = machine->getThisAs<nomad::ListObject>();
                    const auto index = machine->getIntegerParameter(0);
                    const auto removed = list->remove(index);
                    machine->setBooleanResult(removed);
                },
                {nomad::defParameter("index", runtime->getIntegerType(), NomadDoc("The zero-based index to remove."))},
                runtime->getBooleanType(),
                NomadDoc("Removes the indexed element and shifts later elements left. Returns false without logging for out-of-range indices.")
            );
            const auto removeFirstId = runtime->registerNativeMethod(
                objectType,
                "removeFirst",
                [](nomad::VirtualMachine* machine) {
                    auto* list = machine->getThisAs<nomad::ListObject>();
                    const auto removed = list->removeFirst();
                    machine->setBooleanResult(removed);
                },
                {},
                runtime->getBooleanType(),
                NomadDoc("Removes the first element. Returns false without logging if the list is empty.")
            );
            const auto removeLastId = runtime->registerNativeMethod(
                objectType,
                "removeLast",
                [](nomad::VirtualMachine* machine) {
                    auto* list = machine->getThisAs<nomad::ListObject>();
                    const auto removed = list->removeLast();
                    machine->setBooleanResult(removed);
                },
                {},
                runtime->getBooleanType(),
                NomadDoc("Removes the last element. Returns false without logging if the list is empty.")
            );
            const auto clearId = runtime->registerNativeMethod(
                objectType,
                "clear",
                [](nomad::VirtualMachine* machine) {
                    auto* list = machine->getThisAs<nomad::ListObject>();
                    list->clear();
                },
                {},
                runtime->getVoidType(),
                NomadDoc("Removes all elements. Clearing an empty list does nothing and does not log.")
            );
            if (constructorId == nomad::NOMAD_INVALID_ID ||
                appendId == nomad::NOMAD_INVALID_ID || prependId == nomad::NOMAD_INVALID_ID ||
                insertId == nomad::NOMAD_INVALID_ID || removeId == nomad::NOMAD_INVALID_ID ||
                removeFirstId == nomad::NOMAD_INVALID_ID || removeLastId == nomad::NOMAD_INVALID_ID ||
                clearId == nomad::NOMAD_INVALID_ID) {
                throw nomad::NomadException("Failed to register List members");
            }
        }
    )) {
        throw nomad::NomadException("Failed to register List");
    }
}

} // namespace nomad
