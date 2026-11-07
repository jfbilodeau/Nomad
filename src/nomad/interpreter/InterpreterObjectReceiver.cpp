#include <nomad/interpreter/InterpreterObjectReceiver.hpp>

#include <nomad/interpreter/Interpreter.hpp>

#include <nomad/script/ClassType.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptObject.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <stdexcept>
#include <vector>

namespace nomad {

InterpreterObjectReceiver::InterpreterObjectReceiver(
    const Interpreter& interpreter,
    const Type& type,
    NomadObject* object
):
    InterpreterReceiver(interpreter, type), m_object(object)
{}

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
    try {
        getInterpreter().getVirtualMachine().getProperty(propertyId, receiver, result);
        InterpreterValue value(type, result);
        type->freeValue(&result);
        return value;
    } catch (...) {
        type->freeValue(&result);
        throw;
    }
}

void InterpreterObjectReceiver::setProperty(const PropertyId propertyId, const InterpreterValue& value) {
    const auto* property = getType()->getRuntime()->getProperty(propertyId);
    if (property == nullptr) {
        throw std::invalid_argument("Invalid interpreter property setter");
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

} // namespace nomad
