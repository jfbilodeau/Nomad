#include <nomad/interpreter/InterpreterCallFrame.hpp>

#include <nomad/script/Callable.hpp>
#include <nomad/script/Closure.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <stdexcept>

namespace nomad {

InterpreterCallFrame::InterpreterCallFrame(
    VirtualMachine& machine,
    const Callable& callable,
    const std::span<const InterpreterValue> arguments,
    const Type* resultType,
    RuntimeValue* receiverStorage,
    const Closure* closure
):
    InterpreterCallFrame(machine, callable, resultType, receiverStorage)
{
    const auto captureCount = closure != nullptr ? closure->getCaptureCount() : 0;
    if (arguments.size() + captureCount != callable.getParameterCount()) {
        throw NomadBug("Interpreter call argument count does not match the callable");
    }

    if (closure != nullptr) {
        copyCaptures(*closure);
    }

    for (auto index = arguments.size(); index > 0; --index) {
        const auto& argument = arguments[index - 1];
        copyArgument(*argument.getType(), argument.getStorage());
    }

}

InterpreterCallFrame::InterpreterCallFrame(
    VirtualMachine& machine,
    const Closure& closure,
    const std::span<const RuntimeValue> arguments
):
    InterpreterCallFrame(
        machine,
        *closure.getFunction(),
        closure.getFunction()->getReturnType(),
        nullptr
    )
{
    const auto* function = closure.getFunction();
    if (arguments.size() + closure.getCaptureCount() != function->getParameterCount()) {
        throw NomadBug("Interpreter call argument count does not match the callable");
    }

    copyCaptures(closure);
    for (auto index = arguments.size(); index > 0; --index) {
        copyArgument(
            *function->getParameterType(toNomadId(index - 1)),
            std::span<const RuntimeValue>{&arguments[index - 1], 1}
        );
    }

}

InterpreterCallFrame::InterpreterCallFrame(
    VirtualMachine& machine,
    const Callable& callable,
    const Type* resultType,
    RuntimeValue* receiverStorage
):
    m_machine(machine),
    m_resultType(resultType),
    m_savedResult(machine.getResult()),
    m_savedIntermediate(machine.getIntermediate())
{
    const auto layout = callable.getCallStorageLayout();
    if (m_resultType->isStruct()) {
        m_resultStorage.resize(m_resultType->getSize());
    }

    if (
        layout.receiverOffset.has_value() &&
        callable.getType() == CallableType::Method &&
        receiverStorage == nullptr
    ) {
        throw NomadBug("Struct method frame requires borrowed receiver storage");
    }

    m_arguments.reserve(callable.getParameterCount());
    m_machine.reserveLocals(layout.stackSlotCount);
    m_onStack = true;
    if (layout.stackSlotCount != 0) {
        m_argumentStorage = {
            m_machine.getStackAddress(layout.stackSlotCount - 1),
            layout.stackSlotCount
        };
    }

    if (layout.returnOffset.has_value()) {
        m_argumentStorage[layout.stackSlotCount - 1 - *layout.returnOffset].setStorageAddress(
            getDestination()
        );
    }

    if (layout.receiverOffset.has_value()) {
        m_argumentStorage[layout.stackSlotCount - 1 - *layout.receiverOffset].setStorageAddress(
            receiverStorage != nullptr ? receiverStorage : getDestination()
        );
    }

    m_argumentOffset = layout.stackSlotCount - layout.argumentSlotCount;
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
    m_machine.setResult(m_savedResult);
    m_machine.setIntermediate(m_savedIntermediate);
}

RuntimeValue* InterpreterCallFrame::getDestination() {

    return m_resultStorage.empty() ? nullptr : m_resultStorage.data();
}

InterpreterValue InterpreterCallFrame::getResult() const {

    return InterpreterValue(m_resultType, getResultStorage());
}

RuntimeValue InterpreterCallFrame::releaseResult() {
    if (m_resultType->isStruct()) {
        throw std::invalid_argument("Raw call results require scalar or reference storage");
    }

    const auto result = m_machine.getResult();
    m_machine.setResult(RuntimeValue{});

    return result;
}

void InterpreterCallFrame::releaseArgumentsToVirtualMachine() {
    m_onStack = false;
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

void InterpreterCallFrame::copyArgument(
    const Type& type,
    const std::span<const RuntimeValue> storage
) {
    if (
        storage.size() != type.getSize() ||
        m_argumentOffset > m_argumentStorage.size() ||
        storage.size() > m_argumentStorage.size() - m_argumentOffset
    ) {
        throw NomadBug("Interpreter call argument storage does not match the callable");
    }

    m_arguments.push_back({&type, m_argumentOffset, storage.size()});
    if (!storage.empty()) {
        type.copyValue(storage.data(), m_argumentStorage.data() + m_argumentOffset);
    }

    m_argumentOffset += storage.size();
}

void InterpreterCallFrame::copyCaptures(const Closure& closure) {
    for (auto index = closure.getCaptureCount(); index > 0; --index) {
        copyArgument(
            *closure.getCaptureType(index - 1),
            closure.getCaptureStorage(index - 1)
        );
    }

}

void InterpreterCallFrame::releaseArguments() noexcept {
    if (!m_onStack) {

        return;
    }

    for (const auto& argument : m_arguments) {
        if (argument.size != 0) {
            argument.type->freeValue(m_argumentStorage.data() + argument.offset);
        }
    }

    m_machine.popN(m_argumentStorage.size());
}

} // namespace nomad
