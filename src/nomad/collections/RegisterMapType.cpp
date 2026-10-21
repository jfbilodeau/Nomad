// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/RegisterMapType.hpp>

#include <nomad/collections/Array1dObject.hpp>
#include <nomad/collections/KeyValueType.hpp>
#include <nomad/collections/MapObject.hpp>
#include <nomad/collections/MapType.hpp>
#include <nomad/collections/ReadOnlyMapType.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <utility>

namespace nomad {

namespace {

void registerMapFamily(
    Runtime* runtime,
    const NomadString& name,
    Runtime::ParameterizedTypeFactory factory,
    const bool writable
) {
    if (!runtime->registerParameterizedType(
        name,
        2,
        std::move(factory),
        [runtime, writable](const nomad::Type* type) {
            const auto* objectType = static_cast<const nomad::ObjectType*>(type);
            const auto* valueType = objectType->getTypeArguments()[1];
            const auto* keyType = objectType->getTypeArguments()[0];
            const auto* keysType = runtime->getParameterizedType("Array1", {keyType});
            const auto* valuesType = runtime->getParameterizedType("Array1", {valueType});
            const auto* entryType = dynamic_cast<const KeyValueType*>(
                runtime->getParameterizedType("KeyValue", {keyType, valueType})
            );
            const auto* entriesType = entryType == nullptr ? nullptr :
                runtime->getParameterizedType("Array1", {entryType});
            if (keysType == nullptr || valuesType == nullptr || entriesType == nullptr) {
                throw nomad::NomadException("Failed to resolve Map snapshot types");
            }
            if (writable && runtime->registerNativeConstructor(
                objectType, {},
                [valueType](std::span<const nomad::RuntimeValue>) {
                    return new nomad::MapObject(valueType);
                },
                NomadDoc("Creates an empty map with string keys.")
            ) == NOMAD_INVALID_ID) {
                throw NomadException("Failed to register Map constructor");
            }
            const auto countId = runtime->registerNativeProperty(
                objectType,
                "count",
                runtime->getIntegerType(),
                [](nomad::VirtualMachine* machine, nomad::RuntimeValue& result) {
                    const auto* map = machine->getThisAs<nomad::MapObject>();
                    const auto count = static_cast<nomad::NomadInteger>(map->getCount());
                    result.setIntegerValue(count);
                },
                {},
                NomadDoc("The number of entries in the map.")
            );
            const auto keysId = runtime->registerNativeMethod(
                objectType,
                "keys",
                [keyType](nomad::VirtualMachine* machine) {
                    const auto* map = machine->getThisAs<nomad::MapObject>();
                    auto keys = map->getKeys(keyType);
                    machine->getResult().setObjectValue(keys.release());
                },
                {},
                keysType,
                NomadDoc("A fixed-size snapshot of all keys in unspecified order. Changes to the array and map are independent.")
            );
            const auto valuesId = runtime->registerNativeMethod(
                objectType,
                "values",
                [](nomad::VirtualMachine* machine) {
                    const auto* map = machine->getThisAs<nomad::MapObject>();
                    auto values = map->getValues();
                    machine->getResult().setObjectValue(values.release());
                },
                {},
                valuesType,
                NomadDoc("A fixed-size snapshot of all values in unspecified order. Object values share references.")
            );
            const auto entriesId = runtime->registerNativeMethod(
                objectType,
                "entries",
                [entryType](nomad::VirtualMachine* machine) {
                    const auto* map = machine->getThisAs<nomad::MapObject>();
                    auto entries = map->getEntries(entryType);
                    machine->getResult().setObjectValue(entries.release());
                },
                {},
                entriesType,
                NomadDoc("A fixed-size snapshot of fresh read-only key/value pairs in unspecified order. Object values share references.")
            );
            NativeFunctionFn setter;
            if (writable) {
                setter = [](VirtualMachine* machine) {
                    auto* map = machine->getThisAs<MapObject>();
                    const auto& key = machine->getStringParameter(0)->getString();
                    map->setValue(key, machine->getParameter(1));
                };
            }
            const auto indexerId = runtime->registerNativeIndexer(
                objectType,
                {runtime->getStringType()},
                valueType,
                [](nomad::VirtualMachine* machine) {
                    const auto* map = machine->getThisAs<nomad::MapObject>();
                    const auto& key = machine->getStringParameter(0)->getString();
                    auto& result = machine->getResult();
                    map->getValue(key, result);
                },
                std::move(setter),
                writable
                    ? NomadDoc("Reads, inserts, or replaces an entry by string key. Missing keys log an error and return a default without inserting.")
                    : NomadDoc("Reads an entry by string key. Missing keys log an error and return a default without inserting.")
            );
            const auto containsId = runtime->registerNativeMethod(
                objectType,
                "contains",
                [](nomad::VirtualMachine* machine) {
                    const auto* map = machine->getThisAs<nomad::MapObject>();
                    const auto& key = machine->getStringParameter(0)->getString();
                    const auto contains = map->contains(key);
                    machine->setBooleanResult(contains);
                },
                {nomad::defParameter("key", runtime->getStringType(), NomadDoc("The case-sensitive key to check."))},
                runtime->getBooleanType(),
                NomadDoc("Returns whether the key exists, without logging or changing the map.")
            );
            if (countId == NOMAD_INVALID_ID || indexerId == NOMAD_INVALID_ID || containsId == NOMAD_INVALID_ID ||
                keysId == NOMAD_INVALID_ID || valuesId == NOMAD_INVALID_ID || entriesId == NOMAD_INVALID_ID) {
                throw NomadException("Failed to register Map readers");
            }
            if (!writable) {
                return;
            }
            const auto* readOnlyType = runtime->getParameterizedType("ReadOnlyMap", {keyType, valueType});
            if (readOnlyType == nullptr || runtime->registerNativeMethod(
                objectType, "toReadOnly",
                [](VirtualMachine* machine) {
                    auto snapshot = machine->getThisAs<MapObject>()->createCopy();
                    machine->getResult().setObjectValue(snapshot.release());
                },
                {}, readOnlyType,
                NomadDoc("Creates an independent read-only snapshot. Object values share references.")
            ) == NOMAD_INVALID_ID) {
                throw NomadException("Failed to register Map.toReadOnly");
            }
            const auto removeId = runtime->registerNativeMethod(
                objectType,
                "remove",
                [](nomad::VirtualMachine* machine) {
                    auto* map = machine->getThisAs<nomad::MapObject>();
                    const auto& key = machine->getStringParameter(0)->getString();
                    const auto removed = map->remove(key);
                    machine->setBooleanResult(removed);
                },
                {nomad::defParameter("key", runtime->getStringType(), NomadDoc("The case-sensitive key to remove."))},
                runtime->getBooleanType(),
                NomadDoc("Removes an entry and returns true if it existed. Missing keys return false without logging.")
            );
            const auto clearId = runtime->registerNativeMethod(
                objectType,
                "clear",
                [](nomad::VirtualMachine* machine) {
                    auto* map = machine->getThisAs<nomad::MapObject>();
                    map->clear();
                },
                {},
                runtime->getVoidType(),
                NomadDoc("Removes all entries. Clearing an empty map does nothing and does not log.")
            );
            if (removeId == nomad::NOMAD_INVALID_ID || clearId == nomad::NOMAD_INVALID_ID) {
                throw nomad::NomadException("Failed to register Map members");
            }
        }
    )) {
        throw nomad::NomadException("Failed to register " + name);
    }
}

} // namespace

void registerMapType(Runtime* runtime) {
    registerMapFamily(runtime, "ReadOnlyMap", [](const std::vector<const Type*>& arguments) {
        return std::make_unique<ReadOnlyMapType>(arguments[0], arguments[1]);
    }, false);
    registerMapFamily(runtime, "Map", [](const std::vector<const Type*>& arguments) {
        return std::make_unique<MapType>(arguments[0], arguments[1]);
    }, true);
}

} // namespace nomad
