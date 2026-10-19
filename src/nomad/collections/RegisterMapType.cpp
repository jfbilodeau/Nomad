// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/RegisterMapType.hpp>

#include <nomad/collections/Array1dObject.hpp>
#include <nomad/collections/KeyValueType.hpp>
#include <nomad/collections/MapObject.hpp>
#include <nomad/collections/MapType.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

namespace nomad {

void registerMapType(Runtime* runtime) {
    if (!runtime->registerParameterizedType(
        "Map",
        2,
        [](const std::vector<const nomad::Type*>& arguments) {
            const auto* keyType = arguments[0];
            const auto* valueType = arguments[1];
            return std::make_unique<nomad::MapType>(keyType, valueType);
        },
        [runtime](const nomad::Type* type) {
            const auto* objectType = static_cast<const nomad::MapType*>(type);
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
                    const auto* map = machine->getThisAs<nomad::MapObject>();
                    const auto count = static_cast<nomad::NomadInteger>(map->getCount());
                    result.setIntegerValue(count);
                },
                {},
                NomadDoc("The number of entries in the map.")
            );
            const auto keysId = runtime->registerNativeProperty(
                objectType,
                "keys",
                keysType,
                [keyType](nomad::VirtualMachine* machine, nomad::RuntimeValue& result) {
                    const auto* map = machine->getThisAs<nomad::MapObject>();
                    auto keys = map->getKeys(keyType);
                    result.setObjectValue(keys.release());
                },
                {},
                NomadDoc("A fixed-size snapshot of all keys in unspecified order. Changes to the array and map are independent.")
            );
            const auto valuesId = runtime->registerNativeProperty(
                objectType,
                "values",
                valuesType,
                [](nomad::VirtualMachine* machine, nomad::RuntimeValue& result) {
                    const auto* map = machine->getThisAs<nomad::MapObject>();
                    auto values = map->getValues();
                    result.setObjectValue(values.release());
                },
                {},
                NomadDoc("A fixed-size snapshot of all values in unspecified order. Object values share references.")
            );
            const auto entriesId = runtime->registerNativeProperty(
                objectType,
                "entries",
                entriesType,
                [entryType](nomad::VirtualMachine* machine, nomad::RuntimeValue& result) {
                    const auto* map = machine->getThisAs<nomad::MapObject>();
                    auto entries = map->getEntries(entryType);
                    result.setObjectValue(entries.release());
                },
                {},
                NomadDoc("A fixed-size snapshot of fresh read-only key/value pairs in unspecified order. Object values share references.")
            );
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
                [](nomad::VirtualMachine* machine) {
                    auto* map = machine->getThisAs<nomad::MapObject>();
                    const auto& key = machine->getStringParameter(0)->getString();
                    const auto& value = machine->getParameter(1);
                    map->setValue(key, value);
                },
                NomadDoc("Reads, inserts, or replaces an entry by string key. Missing keys log an error and return a default without inserting.")
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
            if (constructorId == nomad::NOMAD_INVALID_ID || countId == nomad::NOMAD_INVALID_ID ||
                indexerId == nomad::NOMAD_INVALID_ID || containsId == nomad::NOMAD_INVALID_ID ||
                removeId == nomad::NOMAD_INVALID_ID || clearId == nomad::NOMAD_INVALID_ID ||
                keysId == nomad::NOMAD_INVALID_ID || valuesId == nomad::NOMAD_INVALID_ID ||
                entriesId == nomad::NOMAD_INVALID_ID) {
                throw nomad::NomadException("Failed to register Map members");
            }
        }
    )) {
        throw nomad::NomadException("Failed to register Map");
    }
}

} // namespace nomad
