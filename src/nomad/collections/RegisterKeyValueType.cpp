// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/RegisterKeyValueType.hpp>

#include <nomad/collections/KeyValueObject.hpp>
#include <nomad/collections/KeyValueType.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

namespace nomad {

void registerKeyValueType(Runtime* runtime) {
    if (!runtime->registerParameterizedType(
        "KeyValue",
        2,
        [](const std::vector<const Type*>& arguments) {
            const auto* keyType = arguments[0];
            const auto* valueType = arguments[1];
            return std::make_unique<KeyValueType>(keyType, valueType);
        },
        [runtime](const Type* type) {
            const auto* objectType = static_cast<const KeyValueType*>(type);
            const auto* keyType = objectType->getTypeArguments()[0];
            const auto* valueType = objectType->getTypeArguments()[1];
            const auto constructorId = runtime->registerNativeConstructor(
                objectType,
                {
                    defParameter("key", keyType, NomadDoc("The key to store.")),
                    defParameter("value", valueType, NomadDoc("The value to store."))
                },
                [keyType, valueType](const std::span<const RuntimeValue> arguments) {
                    const auto& key = arguments[0];
                    const auto& value = arguments[1];
                    return new KeyValueObject(keyType, valueType, key, value);
                },
                NomadDoc("Creates a pair with read-only key and value properties.")
            );
            const auto keyId = runtime->registerNativeProperty(
                objectType, "key", keyType,
                [keyType](VirtualMachine* machine, RuntimeValue& result) {
                    const auto* pair = machine->getThisAs<KeyValueObject>();
                    const auto& key = pair->getKey();
                    RuntimeValue keyCopy;
                    keyType->assignValue(key, keyCopy);
                    result = keyCopy;
                },
                {},
                NomadDoc("The read-only key. Object keys share their referenced object.")
            );
            const auto valueId = runtime->registerNativeProperty(
                objectType, "value", valueType,
                [valueType](VirtualMachine* machine, RuntimeValue& result) {
                    const auto* pair = machine->getThisAs<KeyValueObject>();
                    const auto& value = pair->getValue();
                    RuntimeValue valueCopy;
                    valueType->assignValue(value, valueCopy);
                    result = valueCopy;
                },
                {},
                NomadDoc("The read-only value. Object values share their referenced object.")
            );
            if (constructorId.isInvalid() || keyId.isInvalid() || valueId.isInvalid()) {
                throw NomadException("Failed to register KeyValue members");
            }
        }
    )) {
        throw NomadException("Failed to register KeyValue");
    }
    if (!runtime->registerMemberTemplate("KeyValue", "key", MemberType::Property) ||
        !runtime->registerMemberTemplate("KeyValue", "value", MemberType::Property)) {
        throw NomadException("Failed to register KeyValue member signatures");
    }
    if (!runtime->registerConstructorTemplate(
        "KeyValue",
        {
            defTemplateParameter("key", TypeParameterIndex{0}, NomadDoc("The key to store.")),
            defTemplateParameter("value", TypeParameterIndex{1}, NomadDoc("The value to store."))
        }
    )) {
        throw NomadException("Failed to register KeyValue constructor inference");
    }
}

} // namespace nomad
