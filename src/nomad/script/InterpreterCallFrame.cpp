#include <nomad/script/InterpreterCallFrame.hpp>

#include <nomad/script/Callable.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

namespace nomad {

InterpreterCallFrame::InterpreterCallFrame(
    VirtualMachine& machine,
    const Callable& callable,
    const std::span<const InterpreterValue> arguments,
    const Type* resultType,
    const InterpreterValue* receiver
):
    m_machine(machine),
    m_resultType(resultType),
    m_receiverType(receiver != nullptr ? receiver->getType() : nullptr),
    m_savedResult(machine.getResult()),
    m_savedIntermediate(machine.getIntermediate())
{
    if (arguments.size() != callable.getParameterCount()) {
        throw NomadBug("Interpreter call argument count does not match the callable");
    }
    const auto layout = callable.getCallStorageLayout();
    if (m_resultType->isStruct()) {
        m_resultStorage.resize(m_resultType->getSize());
    }
    if (m_receiverType != nullptr) {
        m_receiverStorage.resize(m_receiverType->getSize());
    }
    m_argumentStorage.resize(layout.stackSlotCount);
    m_arguments.reserve(arguments.size());
    if (layout.returnOffset.has_value()) {
        m_argumentStorage[layout.stackSlotCount - 1 - *layout.returnOffset].setStorageAddress(getDestination());
    }
    if (layout.receiverOffset.has_value()) {
        m_argumentStorage[layout.stackSlotCount - 1 - *layout.receiverOffset].setStorageAddress(
            receiver != nullptr ? m_receiverStorage.data() : getDestination());
    }
    auto offset = layout.stackSlotCount - layout.argumentSlotCount;
    try {
        if (!m_receiverStorage.empty()) {
            m_receiverType->copyValue(receiver->getStorage().data(), m_receiverStorage.data());
        }
        for (auto index = arguments.size(); index > 0; --index) {
            const auto& argument = arguments[index - 1];
            const auto storage = argument.getStorage();
            const auto* type = argument.getType();
            m_arguments.push_back({type, offset, storage.size()});
            if (!storage.empty()) {
                type->copyValue(storage.data(), m_argumentStorage.data() + offset);
            }
            offset += storage.size();
        }
        // Ownership transfers to the stack; the buffer contains only raw, non-owning slot copies afterward.
        m_machine.pushStorage(m_argumentStorage);
        m_onStack = true;
    } catch (...) {
        releaseArguments();
        releaseReceiver();
        throw;
    }
    m_machine.setResult(RuntimeValue{});
    m_machine.setIntermediate(RuntimeValue{});
}

InterpreterCallFrame::~InterpreterCallFrame() {
    if (m_resultType->isStruct()) {
        if (!m_resultStorage.empty()) {
            m_resultType->freeValue(m_resultStorage.data());
        }
    } else {
        m_resultType->freeValue(&m_machine.getResult());
    }
    releaseArguments();
    releaseReceiver();
    m_machine.setResult(m_savedResult);
    m_machine.setIntermediate(m_savedIntermediate);
}

RuntimeValue* InterpreterCallFrame::getDestination() {
    return m_resultStorage.empty() ? nullptr : m_resultStorage.data();
}

InterpreterValue InterpreterCallFrame::getReceiver() const {
    if (m_receiverType == nullptr) {
        throw NomadBug("Interpreter call frame has no receiver");
    }
    return InterpreterValue(m_receiverType,
        m_receiverStorage.empty() ? std::span<const RuntimeValue>{} : m_receiverStorage);
}

InterpreterValue InterpreterCallFrame::getResult() const {
    return InterpreterValue(m_resultType, getResultStorage());
}

std::span<const RuntimeValue> InterpreterCallFrame::getResultStorage() const {
    if (m_resultType->isVoid()) {
        return {};
    }
    if (m_resultType->isStruct()) {
        return m_resultStorage.empty() ? std::span<const RuntimeValue>{} : m_resultStorage;
    }
    return {&m_machine.getResult(), 1};
}

void InterpreterCallFrame::releaseArguments() noexcept {
    auto* storage = m_argumentStorage.data();
    if (m_onStack && !m_argumentStorage.empty()) {
        storage = m_machine.getStackAddress(m_argumentStorage.size() - 1);
    }
    for (const auto& argument : m_arguments) {
        if (argument.size != 0) {
            argument.type->freeValue(storage + argument.offset);
        }
    }
    if (m_onStack) {
        m_machine.popN(m_argumentStorage.size());
    }
}

void InterpreterCallFrame::releaseReceiver() noexcept {
    if (!m_receiverStorage.empty()) {
        m_receiverType->freeValue(m_receiverStorage.data());
    }
}

} // namespace nomad
