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
    const auto iteratorId = runtime->addMethod(objectType->getId(), {
        .name = "iterator",
        .parameters = {},
        .returnTypeId = runtime->getIteratorType(elementType)->getId(),
        .function = [](VirtualMachine* machine) {
            machine->getResult().setObjectValue(new ListIterator(machine->getThisAs<ListObject>()));
        },
        .documentation = NomadDoc("Creates an independent live iterator. Structural changes invalidate its next advance.")
    });
    const auto countId = runtime->addProperty(objectType->getId(), {
        .valueTypeId = runtime->getTypeId("int"),
        .name = "count",
        .getter = [](VirtualMachine* machine, RuntimeValue& result) {
            result.setIntegerValue(static_cast<NomadInteger>(machine->getThisAs<ListObject>()->getCount()));
        },
        .setter = {},
        .documentation = NomadDoc("The number of elements in the list.")
    });
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
    if (iteratorId.isInvalid() || countId.isInvalid() || indexerId.isInvalid()) {
        throw NomadException("Failed to register List readers");
    }
    runtime->registerIterableType(objectType, elementType, runtime->getNativeMemberMethod(iteratorId));
}

} // namespace

void registerListType(Runtime* runtime) {
    if (!runtime->registerParameterizedType(
        "ReadOnlyList", 1,
        [](Runtime* owningRuntime, TypeId id, const std::vector<const Type*>& arguments) {
            return std::make_unique<ReadOnlyListType>(
                owningRuntime, id, "ReadOnlyList<" + arguments[0]->getTypeName() + ">",
                ObjectTypeMode::Closed, arguments[0]
            );
        },
        [](Runtime* runtime, const TypeId id) {
            const auto* type = runtime->getType(id).value();
            registerListReaders(runtime, static_cast<const ObjectType*>(type), false);
        }
    )) {
        throw NomadException("Failed to register ReadOnlyList");
    }
    if (!runtime->registerParameterizedType(
        "List",
        1,
        [](Runtime* owningRuntime, TypeId id, const std::vector<const nomad::Type*>& arguments) {
            const auto* elementType = arguments[0];
            return std::make_unique<nomad::ListType>(
                owningRuntime, id, "List<" + elementType->getTypeName() + ">", ObjectTypeMode::Closed, elementType
            );
        },
        [](Runtime* runtime, const TypeId id) {
            const auto* type = runtime->getType(id).value();
            const auto* objectType = static_cast<const nomad::ObjectType*>(type);
            const auto* elementType = objectType->getTypeArguments()[0];
            registerListReaders(runtime, objectType, true);
            const auto* readOnlyType = runtime->getParameterizedType("ReadOnlyList", {elementType});
            if (readOnlyType == nullptr || runtime->addMethod(id, {
                .name = "toReadOnly",
                .parameters = {},
                .returnTypeId = readOnlyType->getId(),
                .function = [](VirtualMachine* machine) {
                    const auto* source = machine->getThisAs<ListObject>();
                    auto snapshot = std::make_unique<ListObject>(source->getElementType());
                    for (auto index = NomadIndex{0}; index < source->getCount(); ++index) {
                        (void)snapshot->append(source->getValue(static_cast<NomadInteger>(index)));
                    }
                    machine->getResult().setObjectValue(snapshot.release());
                },
                .documentation = NomadDoc("Creates an independent read-only snapshot. Object elements share references.")
            }).isInvalid()) {
                throw NomadException("Failed to register List.toReadOnly");
            }
            const auto constructorId = runtime->registerNativeConstructor(
                objectType, {},
                [elementType](std::span<const nomad::RuntimeValue>) {
                    return new nomad::ListObject(elementType);
                }, ""
            );
            const auto appendId = runtime->addMethod(id, {
                .name = "append",
                .parameters = {{elementType->getId(), "value", NomadDoc("The value to append.")}},
                .returnTypeId = runtime->getTypeId("int"),
                .function = [](nomad::VirtualMachine* vm) {
                    auto* list = vm->getThisAs<nomad::ListObject>();
                    const auto& value = vm->getParameter(0);
                    const auto index = list->append(value);
                    vm->setIntegerResult(index);
                },
                .documentation = NomadDoc("Appends a value and returns its zero-based index.")
            });
            const auto prependId = runtime->addMethod(id, {
                .name = "prepend",
                .parameters = {{elementType->getId(), "value", NomadDoc("The value to insert at the beginning.")}},
                .returnTypeId = runtime->getTypeId("void"),
                .function = [](nomad::VirtualMachine* machine) {
                    auto* list = machine->getThisAs<nomad::ListObject>();
                    const auto& value = machine->getParameter(0);
                    list->prepend(value);
                },
                .documentation = NomadDoc("Inserts a value at the beginning, shifting existing elements right.")
            });
            const auto insertId = runtime->addMethod(id, {
                .name = "insert",
                .parameters = {
                    {runtime->getTypeId("int"), "index", NomadDoc("The insertion position from zero through count.")},
                    {elementType->getId(), "value", NomadDoc("The value to insert.")}
                },
                .returnTypeId = runtime->getTypeId("void"),
                .function = [](nomad::VirtualMachine* machine) {
                    auto* list = machine->getThisAs<nomad::ListObject>();
                    const auto index = machine->getIntegerParameter(0);
                    const auto& value = machine->getParameter(1);
                    list->insert(index, value);
                },
                .documentation = NomadDoc("Inserts before the index, shifting later elements right. Index count appends; out-of-range indices fault.")
            });
            const auto removeId = runtime->addMethod(id, {
                .name = "remove",
                .parameters = {{runtime->getTypeId("int"), "index", NomadDoc("The zero-based index to remove.")}},
                .returnTypeId = runtime->getTypeId("bool"),
                .function = [](nomad::VirtualMachine* machine) {
                    auto* list = machine->getThisAs<nomad::ListObject>();
                    const auto index = machine->getIntegerParameter(0);
                    const auto removed = list->remove(index);
                    machine->setBooleanResult(removed);
                },
                .documentation = NomadDoc("Removes the indexed element and shifts later elements left. Returns false without logging for out-of-range indices.")
            });
            const auto removeFirstId = runtime->addMethod(id, {
                .name = "removeFirst",
                .parameters = {},
                .returnTypeId = runtime->getTypeId("bool"),
                .function = [](nomad::VirtualMachine* machine) {
                    auto* list = machine->getThisAs<nomad::ListObject>();
                    const auto removed = list->removeFirst();
                    machine->setBooleanResult(removed);
                },
                .documentation = NomadDoc("Removes the first element. Returns false without logging if the list is empty.")
            });
            const auto removeLastId = runtime->addMethod(id, {
                .name = "removeLast",
                .parameters = {},
                .returnTypeId = runtime->getTypeId("bool"),
                .function = [](nomad::VirtualMachine* machine) {
                    auto* list = machine->getThisAs<nomad::ListObject>();
                    const auto removed = list->removeLast();
                    machine->setBooleanResult(removed);
                },
                .documentation = NomadDoc("Removes the last element. Returns false without logging if the list is empty.")
            });
            const auto clearId = runtime->addMethod(id, {
                .name = "clear",
                .parameters = {},
                .returnTypeId = runtime->getTypeId("void"),
                .function = [](nomad::VirtualMachine* machine) {
                    auto* list = machine->getThisAs<nomad::ListObject>();
                    list->clear();
                },
                .documentation = NomadDoc("Removes all elements. Clearing an empty list does nothing and does not log.")
            });
            if (constructorId.isInvalid() ||
                appendId.isInvalid() || prependId.isInvalid() ||
                insertId.isInvalid() || removeId.isInvalid() ||
                removeFirstId.isInvalid() || removeLastId.isInvalid() ||
                clearId.isInvalid()) {
                throw nomad::NomadException("Failed to register List members");
            }
        }
    )) {
        throw nomad::NomadException("Failed to register List");
    }
}

} // namespace nomad
