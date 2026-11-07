#include <nomad/interpreter/InterpreterStruct.hpp>

#include <nomad/interpreter/Interpreter.hpp>

#include <nomad/script/StructType.hpp>

#include <stdexcept>
#include <utility>
#include <vector>

namespace nomad {

namespace {

const StructType& getStructType(const InterpreterValue& value) {
    const auto* type = value.getType();
    const auto* owner = type != nullptr ? type->asStructType() : nullptr;
    if (owner == nullptr) {
        throw std::invalid_argument("InterpreterStruct requires a struct value");
    }
    return *owner;
}

} // namespace

InterpreterStruct::InterpreterStruct(
    const Interpreter& interpreter,
    const StructType& type,
    RuntimeValue* storage
):
    InterpreterComposite(interpreter, type), m_storage(storage)
{
    if (type.getSize() != 0 && storage == nullptr) {
        throw std::invalid_argument("Borrowed struct storage cannot be null");
    }
}

InterpreterStruct::InterpreterStruct(const Interpreter& interpreter, InterpreterValue value):
    InterpreterComposite(interpreter, getStructType(value)), m_storage(std::move(value))
{}

InterpreterValue InterpreterStruct::getValue() const {
    if (const auto* storage = std::get_if<RuntimeValue*>(&m_storage)) {
        return InterpreterValue(getType(), getType()->getSize() == 0 ? std::span<const RuntimeValue>{} :
            std::span<const RuntimeValue>{*storage, getType()->getSize()});
    }
    return std::get<InterpreterValue>(m_storage);
}

InterpreterValue InterpreterStruct::getProperty(const PropertyId propertyId) const {
    return getInterpreter().getProperty(getValue(), propertyId);
}

void InterpreterStruct::setProperty(const PropertyId propertyId, const InterpreterValue& value) {
    replaceValue(getInterpreter().setProperty(getValue(), propertyId, value));
}

void InterpreterStruct::replaceValue(InterpreterValue value) {
    if (const auto* storage = std::get_if<RuntimeValue*>(&m_storage)) {
        std::vector<RuntimeValue> scratch(getType()->getAssignmentScratchSize());
        getType()->assignValue(value.getStorage().data(), *storage, scratch.empty() ? nullptr : scratch.data());
    } else {
        m_storage = std::move(value);
    }
}

} // namespace nomad
