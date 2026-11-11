#include <nomad/interpreter/InterpreterObjectReceiver.hpp>

#include <nomad/interpreter/Interpreter.hpp>
#include <nomad/interpreter/InterpreterStorage.hpp>

#include <nomad/script/ClassType.hpp>
#include <nomad/script/NativeProperty.hpp>
#include <nomad/script/NomadObject.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/RuntimeValueGuard.hpp>
#include <nomad/script/ScriptObject.hpp>
#include <nomad/script/StructType.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <stdexcept>
#include <vector>

namespace nomad {

InterpreterObjectReceiver::InterpreterObjectReceiver(
    const Interpreter& interpreter,
    const ClassType& type,
    NomadObject* object
):
    InterpreterReceiver(interpreter, type),
    m_object(object)
{
    nomad::addRef(m_object);
}

InterpreterObjectReceiver::~InterpreterObjectReceiver() {
    nomad::releaseRef(m_object);
}

InterpreterValue InterpreterObjectReceiver::getValue() const {
    RuntimeValue value;
    value.setObjectValue(m_object);
    return InterpreterValue(getType(), value);
}

InterpreterValue InterpreterObjectReceiver::getProperty(const PropertyId propertyId) const {
    const auto* property = getType()->getRuntime()->getProperty(propertyId);
    if (property == nullptr) {
        throw std::invalid_argument("Invalid interpreter property getter");
    }
    if (const auto* native = property->asNativeProperty()) {
        const auto info = native->getInfo();
        if (info.getterMethodId.isValid()) {
            return getInterpreter().runObjectMethod(*getType()->asClassType(), m_object, info.getterMethodId, {});
        }
        if (info.setterMethodId.isValid()) {
            throw std::invalid_argument("Native property '" + info.name + "' is write-only");
        }
    }
    const auto* type = property->getValueType();
    if (type->isStruct()) {
        const auto* object = dynamic_cast<const ScriptObject*>(m_object);
        if (property->asNativeProperty() != nullptr || object == nullptr ||
            object->getClassType() != property->getOwnerType()) {
            getInterpreter().getVirtualMachine().fault("Stored property receiver has no matching field storage");
        }
        return InterpreterValue(type, object->getFieldStorage(
            property->getOwnerType()->getFieldIndex(propertyId)));
    }
    RuntimeValue receiver;
    receiver.setObjectValue(m_object);
    RuntimeValue result;
    const RuntimeValueGuard guard(*type, &result);
    getInterpreter().getVirtualMachine().getProperty(propertyId, receiver, result);

    return InterpreterValue(type, result);
}

void InterpreterObjectReceiver::setProperty(const PropertyId propertyId, const InterpreterValue& value) {
    const auto* property = getType()->getRuntime()->getProperty(propertyId);
    if (property == nullptr) {
        throw std::invalid_argument("Invalid interpreter property setter");
    }
    if (const auto* native = property->asNativeProperty()) {
        const auto info = native->getInfo();
        if (info.setterMethodId.isValid()) {
            static_cast<void>(getInterpreter().runObjectMethod(
                *getType()->asClassType(), m_object, info.setterMethodId, {&value, 1}));
            return;
        }
        if (info.getterMethodId.isValid()) {
            throw std::invalid_argument("Native property '" + info.name + "' is read-only");
        }
    }
    const auto* type = property->getValueType();
    if (type->isStruct()) {
        auto* object = dynamic_cast<ScriptObject*>(m_object);
        if (property->asNativeProperty() != nullptr || object == nullptr ||
            object->getClassType() != property->getOwnerType()) {
            getInterpreter().getVirtualMachine().fault("Stored property receiver has no matching field storage");
        }
        if (type != value.getType()) {
            throw std::invalid_argument("Stored property assignment requires a matching value type");
        }
        std::vector<RuntimeValue> scratch(type->getAssignmentScratchSize());
        object->setFieldStorage(property->getOwnerType()->getFieldIndex(propertyId), value.getStorage(), scratch);
        return;
    }
    RuntimeValue receiver;
    receiver.setObjectValue(m_object);
    getInterpreter().getVirtualMachine().setProperty(propertyId, receiver, value.getValue());
}

InterpreterValue InterpreterObjectReceiver::callMethod(
    const CallableId methodId,
    const std::span<const InterpreterValue> arguments
) {
    return getInterpreter().runObjectMethod(*getType()->asClassType(), m_object, methodId, arguments);
}

void InterpreterObjectReceiver::setProperty(
    const NomadStringView propertyPath,
    const InterpreterValue& value
) {
    const auto separator = propertyPath.find('.');
    const auto propertyId = resolveProperty(propertyPath.substr(0, separator));
    if (separator == NomadStringView::npos) {
        setProperty(propertyId, value);
        return;
    }
    const auto* property = getType()->getRuntime()->getProperty(propertyId);
    const auto* type = property->getValueType()->asStructType();
    if (type == nullptr) {
        throw std::invalid_argument("Stored property paths require struct owners");
    }
    if (const auto* native = property->asNativeProperty()) {
        const auto info = native->getInfo();
        if (info.getterMethodId.isValid() || info.setterMethodId.isValid()) {
            const auto current = getProperty(propertyId);
            InterpreterStorage temporary(type, current.getStorage());
            getInterpreter().setProperty(*type, temporary.getMutableStorage(), propertyPath.substr(separator + 1), value);
            return;
        }
    }
    auto* object = dynamic_cast<ScriptObject*>(m_object);
    if (property->asNativeProperty() != nullptr || object == nullptr ||
        object->getClassType() != property->getOwnerType()) {
        getInterpreter().getVirtualMachine().fault("Stored property receiver has no matching field storage");
    }
    getInterpreter().setProperty(*type,
        object->getFieldStorage(property->getOwnerType()->getFieldIndex(propertyId)),
        propertyPath.substr(separator + 1), value);
}

} // namespace nomad
