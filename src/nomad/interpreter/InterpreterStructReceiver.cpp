#include <nomad/interpreter/InterpreterStructReceiver.hpp>

#include <nomad/interpreter/Interpreter.hpp>

#include <nomad/script/StructType.hpp>

#include <stdexcept>
#include <vector>

namespace nomad {

InterpreterStructReceiver::InterpreterStructReceiver(
    const Interpreter& interpreter,
    const StructType& type,
    RuntimeValue* storage
):
    InterpreterReceiver(interpreter, type), m_storage(storage)
{
    if (type.getSize() != 0 && storage == nullptr) {
        throw std::invalid_argument("Borrowed struct storage cannot be null");
    }
}

InterpreterValue InterpreterStructReceiver::getValue() const {
    return InterpreterValue(getType(), getType()->getSize() == 0 ? std::span<const RuntimeValue>{} :
        std::span<const RuntimeValue>{m_storage, getType()->getSize()});
}

InterpreterValue InterpreterStructReceiver::getProperty(const PropertyId propertyId) const {
    const auto* owner = getType()->asStructType();
    const auto* property = owner->getProperty(propertyId);
    if (property == nullptr || property->asNativeProperty() != nullptr) {
        throw std::invalid_argument("Stored property path does not match the struct type");
    }
    const auto* type = property->getValueType();
    return InterpreterValue(type, type->getSize() == 0 ? std::span<const RuntimeValue>{} :
        std::span<const RuntimeValue>{m_storage + owner->getFieldOffset(propertyId), type->getSize()});
}

void InterpreterStructReceiver::setProperty(const PropertyId propertyId, const InterpreterValue& value) {
    const auto replacement = getInterpreter().setProperty(getValue(), propertyId, value);
    std::vector<RuntimeValue> scratch(getType()->getAssignmentScratchSize());
    getType()->assignValue(replacement.getStorage().data(), m_storage, scratch.empty() ? nullptr : scratch.data());
}

InterpreterValue InterpreterStructReceiver::callMethod(
    const CallableId methodId,
    const std::span<const InterpreterValue> arguments
) {
    return getInterpreter().runStructMethod(*getType()->asStructType(),
        getType()->getSize() == 0 ? std::span<RuntimeValue>{} :
            std::span<RuntimeValue>{m_storage, getType()->getSize()},
        methodId, arguments);
}

} // namespace nomad
