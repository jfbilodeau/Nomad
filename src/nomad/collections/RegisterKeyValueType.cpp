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
        [](Runtime* owningRuntime, TypeId id, const std::vector<const Type*>& arguments) {
            const auto* keyType = arguments[0];
            const auto* valueType = arguments[1];
            return std::make_unique<KeyValueType>(
                owningRuntime, id, "KeyValue<" + keyType->getTypeName() + " " + valueType->getTypeName() + ">",
                ObjectTypeMode::Closed, keyType, valueType
            );
        },
        [](Runtime* runtime, const TypeId id) {
            const auto* type = runtime->getType(id).value();
            const auto* objectType = static_cast<const KeyValueType*>(type);
            const auto* keyType = objectType->getTypeArguments()[0];
            const auto* valueType = objectType->getTypeArguments()[1];
            const auto constructorId = runtime->addConstructor(id, {
                .parameters = {
                    {keyType->getId(), "key", NomadDoc("The key to store.")},
                    {valueType->getId(), "value", NomadDoc("The value to store.")}
                },
                .function = [keyType, valueType](VirtualMachine* machine) {
                    const auto& key = machine->getParameter(0);
                    const auto& value = machine->getParameter(1);
                    machine->getResult().setObjectValue(new KeyValueObject(keyType, valueType, key, value));
                },
                .documentation = NomadDoc("Creates a pair with read-only key and value properties.")
            });
            const auto keyId = runtime->addProperty(id, {
                .valueTypeId = keyType->getId(),
                .name = "key",
                .getter = [keyType](VirtualMachine* machine, RuntimeValue& result) {
                    const auto* pair = machine->getThisAs<KeyValueObject>();
                    const auto& key = pair->getKey();
                    RuntimeValue keyCopy;
                    keyType->assignValue(key, keyCopy);
                    result = keyCopy;
                },
                .setter = {},
                .documentation = NomadDoc("The read-only key. Object keys share their referenced object.")
            });
            const auto valueId = runtime->addProperty(id, {
                .valueTypeId = valueType->getId(),
                .name = "value",
                .getter = [valueType](VirtualMachine* machine, RuntimeValue& result) {
                    const auto* pair = machine->getThisAs<KeyValueObject>();
                    const auto& value = pair->getValue();
                    RuntimeValue valueCopy;
                    valueType->assignValue(value, valueCopy);
                    result = valueCopy;
                },
                .setter = {},
                .documentation = NomadDoc("The read-only value. Object values share their referenced object.")
            });
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
