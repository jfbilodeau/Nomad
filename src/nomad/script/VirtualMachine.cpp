// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <ranges>

#include <nomad/script/Closure.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/NomadStringObject.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/ScriptObject.hpp>
#include <nomad/script/Type.hpp>

#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/String.hpp>

#ifdef NOMAD_DEBUG
#define TEST_STACK_OVERFLOW if (m_stackIndex >= m_stack.size()) { fault("Stack overflow"); }
#define TEST_STACK_UNDERFLOW if (m_stackIndex >= m_stack.size()) { fault("Stack underflow"); }
#else
#define TEST_STACK_OVERFLOW
#define TEST_STACK_UNDERFLOW
#endif

namespace nomad {

VirtualMachine::VirtualMachine(Runtime* runtime, NomadIndex stack_size):
    m_runtime(runtime),
    m_instructions(runtime->getInstructions())
{
    m_stack.resize(stack_size);
}

VirtualMachine::~VirtualMachine() = default;

void VirtualMachine::setFunctionVariableValue(const VariableId variableId, const RuntimeValue& value) {
    m_stack[m_variableIndex + variableId.id] = value;
}

const RuntimeValue& VirtualMachine::getFunctionVariableValue(const VariableId variableId) const {
    return m_stack[m_variableIndex + variableId.id];
}

NomadObject* VirtualMachine::constructObject(
    const CallableId constructorId,
    const std::vector<RuntimeValue>& arguments
) {
    const auto* constructor = m_runtime->getConstructor(constructorId);
    if (constructor == nullptr || constructor->getParameters().size() != arguments.size()) {
        fault("Cannot construct object: invalid constructor or argument count");
    }
    const auto* native = constructor->asNativeConstructor();
    const auto* script = constructor->asScriptConstructor();
    if (native == nullptr && (script == nullptr || script->getFunctionStart() == NOMAD_INVALID_INDEX ||
        script->getFunctionStart() >= script->getFunctionEnd())) {
        fault("Class constructor has not been compiled");
    }
    const auto savedResult = m_result;
    const auto savedIntermediate = m_intermediate;
    try {
        auto* object = native != nullptr
            ? runNativeConstructor(native, arguments)
            : runScriptConstructor(script, arguments);
        m_result = savedResult;
        m_intermediate = savedIntermediate;
        return object;
    } catch (...) {
        m_result = savedResult;
        m_intermediate = savedIntermediate;
        throw;
    }
}

void VirtualMachine::run(
    const ScriptFunction* function,
    const std::vector<RuntimeValue>& args,
    const std::vector<RuntimeValue>& closures
) {
    const auto state = beginScriptRun();
    std::vector<const Type*> argumentTypes;
    const auto releaseArguments = [&]() {
        freeHostArguments(state.stackIndex + 2, argumentTypes);
    };

    try {
        if (function == nullptr || args.size() + closures.size() != function->getParameterCount() ||
            closures.size() != function->getCaptureCount()) {
            fault("Incorrect script argument or capture count");
        }
        if (m_stackIndex >= m_stack.size() ||
            m_stack.size() - m_stackIndex < s_scriptFrameSlotCount + 2 ||
            function->getParameterCount() > m_stack.size() - m_stackIndex - s_scriptFrameSlotCount - 2) {
            fault("Stack overflow");
        }
        pushThis();
        argumentTypes.reserve(function->getParameterCount());
        for (auto index = closures.size(); index > 0; --index) {
            pushHostArgument(
                function->getParameterType(toNomadId(args.size() + index - 1)),
                closures[index - 1],
                argumentTypes
            );
        }
        for (auto index = args.size(); index > 0; --index) {
            pushHostArgument(function->getParameterType(toNomadId(index - 1)), args[index - 1], argumentTypes);
        }

        callScriptFunction(function->getFunctionStart());
        executeScript();
        if (m_yielded && m_this.getObjectValue() != nullptr) {
            fault("Class initialization or construction cannot yield");
        }
    } catch (...) {
        releaseArguments();
        m_yielded = false;
        restoreScriptRun(state);

        throw;
    }

    if (!m_yielded) {
        releaseArguments();
        restoreScriptRun(state);
    } else {
        m_running = false;
    }
}

VirtualMachine::ScriptRunState VirtualMachine::beginScriptRun() {
    const ScriptRunState state{
        m_instructionIndex, m_stackIndex, m_parameterIndex, m_variableIndex, m_running, m_this
    };
    m_running = true;
    m_yielded = false;
    // Returning to instruction zero executes op_stop.
    m_instructionIndex = 0;
    return state;
}

void VirtualMachine::restoreScriptRun(const ScriptRunState& state) {
    m_instructionIndex = state.instructionIndex;
    m_stackIndex = state.stackIndex;
    m_parameterIndex = state.parameterIndex;
    m_variableIndex = state.variableIndex;
    m_running = state.running;
    m_this = state.receiver;
}

void VirtualMachine::executeScript() {
    do {
        auto& instruction = m_instructions[m_instructionIndex++];
        instruction.fn(this);
    } while (m_running);
}

void VirtualMachine::pushHostArgument(
    const Type* type,
    const RuntimeValue& value,
    std::vector<const Type*>& argumentTypes
) {
    if (type == nullptr) {
        fault("Host argument has no parameter type");
    }
    pushValue(RuntimeValue{});
    argumentTypes.push_back(type);
    type->copyValue(value, m_stack[m_stackIndex]);
}

void VirtualMachine::freeHostArguments(
    const NomadIndex firstIndex,
    const std::vector<const Type*>& argumentTypes
) {
    for (auto index = NomadIndex{0}; index < argumentTypes.size(); ++index) {
        argumentTypes[index]->freeValue(m_stack[firstIndex + index]);
    }
}

NomadObject* VirtualMachine::runScriptConstructor(
    const ScriptConstructor* constructor,
    const std::vector<RuntimeValue>& args
) {
    const auto state = beginScriptRun();
    std::vector<const Type*> argumentTypes;
    const auto release = [](NomadObject* object) { object->releaseRef(); };
    std::unique_ptr<NomadObject, decltype(release)> object(nullptr, release);
    m_result = RuntimeValue{};
    m_intermediate = RuntimeValue{};
    try {
        if (m_stackIndex >= m_stack.size() ||
            m_stack.size() - m_stackIndex < s_scriptFrameSlotCount + 2 ||
            args.size() > m_stack.size() - m_stackIndex - s_scriptFrameSlotCount - 2) {
            fault("Stack overflow");
        }
        pushThis();
        argumentTypes.reserve(args.size());
        for (auto index = args.size(); index > 0; --index) {
            pushHostArgument(constructor->getParameterType(toNomadId(index - 1)), args[index - 1], argumentTypes);
        }
        callScriptConstructor(constructor->getFunctionStart(), constructor->getObjectType()->getId());
        object.reset(m_this.getObjectValue());
        executeScript();
        if (m_yielded) {
            fault("Class initialization or construction cannot yield");
        }
        moveThisToResult();
    } catch (...) {
        freeHostArguments(state.stackIndex + 2, argumentTypes);
        m_yielded = false;
        restoreScriptRun(state);
        throw;
    }
    freeHostArguments(state.stackIndex + 2, argumentTypes);
    restoreScriptRun(state);
    return object.release();
}

void VirtualMachine::yield(const RuntimeValue& result) {
    m_result = result;

    m_running = false;
    m_yielded = true;
}

void VirtualMachine::stop(const RuntimeValue& result) {
    m_result = result;

    m_running = false;
    m_yielded = false;
}

void VirtualMachine::fault(const NomadString& faultMessage) const {
    throw VirtualMachineException(faultMessage);
}

ScriptFunction* VirtualMachine::getCurrentFunction() const {
    return m_runtime->getFunctionAtInstructionIndex(m_instructionIndex);
}

const ScriptCallableBase* VirtualMachine::getCurrentCallable() const {
    return m_runtime->getScriptCallableAtInstructionIndex(m_instructionIndex);
}

void VirtualMachine::callScriptFunction(const NomadIndex address) {
    pushScriptFrame();
    m_this = RuntimeValue{};
    jump(address);
}

void VirtualMachine::callScriptMethod(const NomadIndex address) {
    pushScriptFrame();
    jump(address);
}

void VirtualMachine::callScriptConstructor(const NomadIndex address, const TypeId typeId) {
    createScriptObject(typeId);
    pushScriptFrame();
    jump(address);
}

void VirtualMachine::createScriptObject(const TypeId typeId) {
    const auto* objectType = m_runtime->getType(typeId).value()->asObjectType();
    m_this.setObjectValue(new ScriptObject(objectType));
}

void VirtualMachine::pushScriptFrame() {
    if (m_stackIndex >= m_stack.size() ||
        m_stack.size() - m_stackIndex - 1 < s_scriptFrameSlotCount) {
        fault("Stack overflow");
    }

    const auto argumentStackIndex = m_stackIndex;
    const auto returnAddress = m_instructionIndex;
    const auto previousParameterIndex = m_parameterIndex;
    const auto previousVariableIndex = m_variableIndex;
    const auto previousThis = m_this;

    pushIndex(returnAddress);
    pushIndex(previousParameterIndex);
    pushIndex(previousVariableIndex);
    pushValue(previousThis);

    // Variables start after the saved call frame slots.
    m_parameterIndex = argumentStackIndex;
    m_variableIndex = m_stackIndex + 1;
}

void VirtualMachine::callNativeFunction(const CallableId nativeFunctionId) {
    const auto* function = m_runtime->getNativeFunction(nativeFunctionId);
    const auto frame = beginNativeCall();
    m_this = RuntimeValue{};
    try {
        function->getFn()(this);
    } catch (...) {
        endNativeCall(frame);
        throw;
    }
    endNativeCall(frame);
}

void VirtualMachine::callNativeConstructor(const CallableId constructorId) {
    const auto* constructor = m_runtime->getNativeConstructor(constructorId);
    const auto frame = beginNativeCall();
    m_this = RuntimeValue{};
    m_result = RuntimeValue{};
    try {
        constructor->getFn()(this);
        if (m_result.getObjectValue() == nullptr) {
            fault("Native constructor for '" + constructor->getName() + "' returned null");
        }
    } catch (...) {
        constructor->getReturnType()->freeValue(m_result);
        endNativeCall(frame);
        throw;
    }
    endNativeCall(frame);
    m_this.moveObjectValue(m_result);
}

VirtualMachine::NativeCallFrame VirtualMachine::beginNativeCall() {
    const NativeCallFrame frame{m_parameterIndex, m_this};
    m_parameterIndex = m_stackIndex;
    return frame;
}

void VirtualMachine::endNativeCall(const NativeCallFrame& frame) {
    m_this = frame.receiver;
    m_parameterIndex = frame.parameterIndex;
}

void VirtualMachine::callNativeMethod(const CallableId memberId) {
    const auto* method = m_runtime->getNativeMemberMethod(memberId);
    const auto frame = beginNativeCall();
    try {
        method->getFn()(this);
    } catch (...) {
        endNativeCall(frame);
        throw;
    }
    endNativeCall(frame);
}

NomadObject* VirtualMachine::getThis() const {
    auto* instance = m_this.getObjectValue();
    if (instance == nullptr) {
        fault("Object method has no instance");
    }
    return instance;
}

void VirtualMachine::invokeNativeMember(const NativeFunctionFn& callback, const RuntimeValue& receiver) {
    const auto previousThis = m_this;
    m_this = receiver;
    try {
        (void)getThis();
        callback(this);
    } catch (...) {
        m_this = previousThis;
        throw;
    }
    m_this = previousThis;
}

void VirtualMachine::getNativeProperty(
    const PropertyId memberId,
    const RuntimeValue& receiver,
    RuntimeValue& result
) {
    const auto info = m_runtime->getNativePropertyInfo(memberId);
    if (!info || !info->getFn) {
        fault("Invalid native property getter");
    }
    invokeNativeMember([&](VirtualMachine* machine) { info->getFn(machine, result); }, receiver);
}

void VirtualMachine::getProperty(
    const PropertyId memberId,
    const RuntimeValue& receiver,
    RuntimeValue& result
) {
    const auto* property = m_runtime->getProperty(memberId);
    if (property == nullptr) {
        fault("Invalid property getter");
    }
    if (property->asNativeProperty() != nullptr) {
        getNativeProperty(memberId, receiver, result);
        return;
    }
    auto* object = dynamic_cast<ScriptObject*>(receiver.getObjectValue());
    if (object == nullptr || object->getObjectType() != property->getObjectType()) {
        fault("Stored property receiver has no matching field storage");
    }
    const auto index = property->getObjectType()->getFieldIndex(memberId);
    property->getValueType()->copyValue(object->getField(index), result);
}

void VirtualMachine::setProperty(
    const PropertyId memberId,
    const RuntimeValue& receiver,
    const RuntimeValue& value
) {
    const auto* property = m_runtime->getProperty(memberId);
    if (property == nullptr) {
        fault("Invalid property setter");
    }
    if (property->asNativeProperty() != nullptr) {
        setNativeProperty(memberId, receiver, value);
        return;
    }
    auto* object = dynamic_cast<ScriptObject*>(receiver.getObjectValue());
    if (object == nullptr || object->getObjectType() != property->getObjectType()) {
        fault("Stored property receiver has no matching field storage");
    }
    object->setField(property->getObjectType()->getFieldIndex(memberId), value);
}

void VirtualMachine::setNativeProperty(
    const PropertyId memberId,
    const RuntimeValue& receiver,
    const RuntimeValue& value
) {
    const auto info = m_runtime->getNativePropertyInfo(memberId);
    if (!info || !info->setFn) {
        fault("Invalid native property setter");
    }
    invokeNativeMember([&](VirtualMachine* machine) { info->setFn(machine, value); }, receiver);
}

void VirtualMachine::callNativeIndexer(
    const IndexerId indexerId,
    const RuntimeValue& receiver,
    const bool write
) {
    const auto info = m_runtime->getNativeIndexerInfo(indexerId);
    if (!info) {
        fault("Invalid native indexer");
    }
    const auto& callback = write ? info->setFn : info->getFn;
    if (!callback) {
        fault(write ? "Invalid native indexer setter" : "Invalid native indexer getter");
    }
    m_result = RuntimeValue{};
    const auto frame = beginNativeCall();
    m_this = receiver;
    try {
        callback(this);
    } catch (...) {
        endNativeCall(frame);
        throw;
    }
    endNativeCall(frame);
}

void VirtualMachine::runNativeIndexer(
    const IndexerId indexerId,
    const std::vector<RuntimeValue>& args,
    const RuntimeValue& receiver,
    const bool write
) {
    const auto info = m_runtime->getNativeIndexerInfo(indexerId);
    if (!info) {
        fault("Invalid native indexer");
    }
    if (args.size() != info->indexTypes.size() + (write ? 1 : 0)) {
        fault("Incorrect argument count when calling native indexer");
    }
    if (m_stackIndex >= m_stack.size() || m_stack.size() - m_stackIndex < 3 ||
        args.size() > m_stack.size() - m_stackIndex - 3) {
        fault("Stack overflow");
    }
    const auto initialStackIndex = m_stackIndex;
    const auto savedThis = m_this;
    const auto parameterType = [&](const NomadIndex index) {
        return index < info->indexTypes.size() ? info->indexTypes[index] : info->valueType;
    };
    const auto releaseArguments = [&]() {
        while (m_stackIndex > initialStackIndex + 2) {
            const auto index = args.size() - (m_stackIndex - initialStackIndex - 2);
            parameterType(index)->freeValue(m_stack[m_stackIndex]);
            --m_stackIndex;
        }
        if (m_stackIndex > initialStackIndex + 1) {
            info->objectType->freeValue(m_stack[initialStackIndex + 2]);
        }
        m_stackIndex = initialStackIndex;
        m_this = savedThis;
    };
    m_result = RuntimeValue{};
    try {
        pushThis();
        pushObjectValue(receiver);
        for (auto index = args.size(); index > 0; --index) {
            ++m_stackIndex;
            m_stack[m_stackIndex] = RuntimeValue{};
            parameterType(index - 1)->copyValue(args[index - 1], m_stack[m_stackIndex]);
        }
        callNativeIndexer(indexerId, receiver, write);
    } catch (...) {
        releaseArguments();
        throw;
    }
    releaseArguments();
}

void VirtualMachine::runNativeFunction(const CallableId nativeFunctionId, const std::vector<RuntimeValue>& args) {
    const auto* function = m_runtime->getNativeFunction(nativeFunctionId);
    if (function == nullptr) {
        fault("Invalid native function id: " + toString(nativeFunctionId.id));
    }
    if (function->getReturnType() == nullptr) {
        fault("NativeFunction '" + function->getName() + "' has no return type");
    }
    if (!function->getReturnType()->isVoid()) {
        m_result = RuntimeValue{};
    }
    if (args.size() != function->getParameterCount()) {
        fault("Incorrect argument count when calling native function '" + function->getName() + "'");
    }
    if (m_stackIndex >= m_stack.size() || m_stack.size() - m_stackIndex < 2 ||
        args.size() > m_stack.size() - m_stackIndex - 2) {
        fault("Stack overflow");
    }

    const auto initialStackIndex = m_stackIndex;
    const auto savedThis = m_this;
    std::vector<const Type*> argumentTypes;
    argumentTypes.reserve(args.size());

    const auto releaseArguments = [&]() {
        freeHostArguments(initialStackIndex + 2, argumentTypes);
        m_stackIndex = initialStackIndex;
        m_this = savedThis;
    };

    try {
        pushThis();
        for (NomadIndex index = args.size(); index > 0; --index) {
            const auto argumentIndex = index - 1;
            const auto* type = function->getParameters()[argumentIndex].type;
            pushHostArgument(type, args[argumentIndex], argumentTypes);
        }

        callNativeFunction(nativeFunctionId);
    } catch (...) {
        releaseArguments();
        throw;
    }

    releaseArguments();
}

NomadObject* VirtualMachine::runNativeConstructor(
    const NativeConstructor* constructor,
    const std::vector<RuntimeValue>& args
) {
    if (m_stackIndex >= m_stack.size() || m_stack.size() - m_stackIndex < 2 ||
        args.size() > m_stack.size() - m_stackIndex - 2) {
        fault("Stack overflow");
    }
    const auto initialStackIndex = m_stackIndex;
    const auto savedThis = m_this;
    std::vector<const Type*> argumentTypes;
    argumentTypes.reserve(args.size());
    const auto releaseArguments = [&]() {
        freeHostArguments(initialStackIndex + 2, argumentTypes);
        m_stackIndex = initialStackIndex;
        m_this = savedThis;
    };
    try {
        pushThis();
        for (auto index = args.size(); index > 0; --index) {
            pushHostArgument(constructor->getParameters()[index - 1].type, args[index - 1], argumentTypes);
        }
        callNativeConstructor(constructor->getId());
        moveThisToResult();
    } catch (...) {
        releaseArguments();
        throw;
    }
    auto* object = m_result.getObjectValue();
    releaseArguments();
    return object;
}

void VirtualMachine::runScriptMethod(
    const CallableId memberId,
    const std::vector<RuntimeValue>& args,
    const RuntimeValue& receiver
) {
    const auto* method = m_runtime->getMethod(memberId);
    const auto* script = method != nullptr ? method->asScriptMethod() : nullptr;
    if (script == nullptr || method->getReturnType() == nullptr || script->getFunctionStart() == NOMAD_INVALID_INDEX ||
        script->getFunctionStart() >= script->getFunctionEnd()) {
        fault("Cannot call script method: invalid or uncompiled method");
    }
    if (args.size() != method->getParameterCount()) {
        fault("Incorrect argument count when calling script method '" + method->getName() + "'");
    }
    if (!method->getReturnType()->isVoid()) {
        m_result = RuntimeValue{};
    }
    const auto state = beginScriptRun();
    std::vector<const Type*> argumentTypes;
    const auto releaseArguments = [&]() {
        freeHostArguments(state.stackIndex + 3, argumentTypes);
        if (m_stackIndex > state.stackIndex + 1) {
            method->getObjectType()->freeValue(m_stack[state.stackIndex + 2]);
        }
    };
    try {
        if (m_stackIndex >= m_stack.size() ||
            m_stack.size() - m_stackIndex < s_scriptFrameSlotCount + 3 ||
            args.size() > m_stack.size() - m_stackIndex - s_scriptFrameSlotCount - 3) {
            fault("Stack overflow");
        }
        argumentTypes.reserve(args.size());
        pushThis();
        pushObjectValue(receiver);
        for (auto index = args.size(); index > 0; --index) {
            pushHostArgument(method->getParameters()[index - 1].type, args[index - 1], argumentTypes);
        }
        bindThisFromStack(args.size());
        callScriptMethod(script->getFunctionStart());
        executeScript();
        if (m_yielded) {
            fault("Script methods cannot yield");
        }
    } catch (...) {
        releaseArguments();
        m_yielded = false;
        restoreScriptRun(state);
        throw;
    }
    releaseArguments();
    restoreScriptRun(state);
}

void VirtualMachine::runNativeMethod(const CallableId memberId, const std::vector<RuntimeValue>& args,
                                        RuntimeValue& receiver) {
    const auto* method = m_runtime->getNativeMemberMethod(memberId);
    if (method == nullptr) {
        fault("Invalid native method id: " + toString(memberId.id));
    }
    if (method->getReturnType() == nullptr) {
        fault("Native method '" + method->getName() + "' has no return type");
    }
    if (!method->getReturnType()->isVoid()) {
        m_result = RuntimeValue{};
    }
    if (args.size() != method->getParameterCount()) {
        fault("Incorrect argument count when calling native method '" + method->getName() + "'");
    }
    if (m_stackIndex >= m_stack.size() || m_stack.size() - m_stackIndex < 3 ||
        args.size() > m_stack.size() - m_stackIndex - 3) {
        fault("Stack overflow");
    }

    const auto initialStackIndex = m_stackIndex;
    const auto savedThis = m_this;
    std::vector<const Type*> argumentTypes;
    argumentTypes.reserve(args.size());

    const auto releaseArguments = [&]() {
        freeHostArguments(initialStackIndex + 3, argumentTypes);
        if (m_stackIndex > initialStackIndex + 1) {
            method->getObjectType()->freeValue(m_stack[initialStackIndex + 2]);
        }
        m_stackIndex = initialStackIndex;
        m_this = savedThis;
    };

    try {
        pushThis();
        pushObjectValue(receiver);
        for (NomadIndex index = args.size(); index > 0; --index) {
            const auto argumentIndex = index - 1;
            const auto* type = method->getParameters()[argumentIndex].type;
            pushHostArgument(type, args[argumentIndex], argumentTypes);
        }

        bindThisFromStack(args.size());
        callNativeMethod(memberId);
    } catch (...) {
        releaseArguments();
        throw;
    }

    releaseArguments();
}

std::vector<TypedValue> VirtualMachine::captureCurrentFrame(const ScriptFunction* function) const {
    if (function == nullptr || function->getParentId() == CallableId{}) {
        fault("Invalid function for current-frame capture");
    }
    const auto* parent = m_runtime->getScriptCallable(function->getParentId());
    if (parent == nullptr || m_instructionIndex < parent->getFunctionStart() ||
        m_instructionIndex >= parent->getFunctionEnd()) {
        fault("Format expression does not belong to the current script frame");
    }

    std::vector<TypedValue> values;
    values.reserve(function->getCaptureCount());
    for (const auto& capture : function->getCaptures()) {
        if (capture.type == nullptr || capture.type->isVoid()) {
            fault("Invalid value type for current-frame capture");
        }
        if (capture.source == CaptureSource::Parameter) {
            if (!isIdInRange(capture.parameterId, parent->getParameterCount())) {
                fault("Invalid parameter for current-frame capture");
            }
            values.emplace_back(capture.type, getParameter(capture.parameterId));
        } else if (capture.source == CaptureSource::Variable) {
            if (!isIdInRange(capture.variableId.id, parent->getVariableCount())) {
                fault("Invalid variable for current-frame capture");
            }
            values.emplace_back(capture.type, getFunctionVariableValue(capture.variableId));
        } else if (capture.source == CaptureSource::Receiver) {
            (void)getThis();
            values.emplace_back(capture.type, m_this);
        } else {
            fault("Invalid source for current-frame capture");
        }
    }
    return values;
}

std::unique_ptr<Closure> VirtualMachine::createClosure(const Runtime* runtime, const CallableId functionId) const {
    const auto function = runtime->getScriptFunction(functionId);

    if (function == nullptr) {
        return nullptr;
    }

    if (function->getCaptureCount() != 0 && m_variableIndex <= s_scriptFrameSlotCount) {
        fault("Cannot capture without an active script frame");
    }

    const auto& captures = function->getCaptures();
    std::vector<RuntimeValue> captureValues;

    for (const auto& capture : captures) {
        const auto captureType = capture.type;
        const auto captureSource = capture.source;

        RuntimeValue captureValue;

        if (captureSource == CaptureSource::Parameter) {
            // Capture from the enclosing function's parameter frame
            const auto& value = getEnclosingParameter(capture.parameterId);

            captureType->copyValue(value, captureValue);
        } else if (captureSource == CaptureSource::Variable) {
            // Capture from the enclosing function's variable frame
            const auto& value = getFunctionVariableValue(capture.variableId);

            captureType->copyValue(value, captureValue);
        } else if (captureSource == CaptureSource::Receiver) {
            // Receiver captures originate in a method or constructor's script frame.
            const auto& receiver = m_stack[m_variableIndex - 1];
            if (receiver.getObjectValue() == nullptr) {
                fault("Cannot capture a missing enclosing receiver");
            }
            captureType->copyValue(receiver, captureValue);
        } else {
            fault("Invalid capture type");
        }

        captureValues.emplace_back(captureValue);
    }

    return std::make_unique<Closure>(function, captureValues);
}

void VirtualMachine::returnFunction(const NomadIndex variableCount) {
    popN(variableCount);

    m_this = popValue();
    m_variableIndex = popIndex();
    m_parameterIndex = popIndex();
    m_instructionIndex = popIndex();
}

void VirtualMachine::setParameter(const NomadIndex parameterIndex, const RuntimeValue& value) {
    m_stack[m_parameterIndex - parameterIndex] = value;
}

void VirtualMachine::freeObjectParameter(const NomadIndex parameterIndex) {
    m_stack[m_parameterIndex - parameterIndex].freeObjectValue();
}

void VirtualMachine::reserveLocals(const NomadIndex count) {
    if (m_stackIndex >= m_stack.size() || count > m_stack.size() - m_stackIndex - 1) {
        fault("Stack overflow");
    }
    for (auto index = NomadIndex{0}; index < count; ++index) {
        pushValue(RuntimeValue{});
    }
}

void VirtualMachine::pushThis() {
    pushValue(m_this);
}

void VirtualMachine::popThis() {
    m_this = popValue();
}

void VirtualMachine::moveThisToResult() {
    m_result.moveObjectValue(m_this);
}

void VirtualMachine::copyThisToResult() {
    (void)getThis();
    m_result.copyObjectValue(m_this);
}

void VirtualMachine::copyThisToIntermediate() {
    (void)getThis();
    m_intermediate.copyObjectValue(m_this);
}

void VirtualMachine::bindThisFromStack(const NomadIndex offset) {
    m_this = peekStack(offset);
    if (m_this.getObjectValue() == nullptr) {
        fault("Object method has no instance");
    }
}

void VirtualMachine::pushResult() {
    m_stackIndex++;
    TEST_STACK_OVERFLOW
    m_stack[m_stackIndex] = m_result;
}

void VirtualMachine::pushIntermediate() {
    m_stackIndex++;
    TEST_STACK_OVERFLOW
    m_stack[m_stackIndex] = m_intermediate;
}

void VirtualMachine::pushObjectResult() {
    m_stackIndex++;
    TEST_STACK_OVERFLOW
    m_stack[m_stackIndex].copyObjectValue(m_result);
}

void VirtualMachine::pushObjectIntermediate() {
    m_stackIndex++;
    TEST_STACK_OVERFLOW
    m_stack[m_stackIndex].moveObjectValue(m_intermediate);
}

void VirtualMachine::moveObjectResultToStack() {
    m_stackIndex++;
    TEST_STACK_OVERFLOW
    m_stack[m_stackIndex].moveObjectValue(m_result);
}

void VirtualMachine::freeObjectVariable(const VariableId variableId) {
    m_stack[m_variableIndex + variableId.id].freeObjectValue();
}

void VirtualMachine::freeObjectStack(const NomadIndex index) {
    m_stack[m_stackIndex - index].freeObjectValue();
}

void VirtualMachine::popResult() {
    m_result = m_stack[m_stackIndex];
    m_stackIndex--;
    TEST_STACK_UNDERFLOW
}

void VirtualMachine::popIntermediate() {
    m_intermediate = m_stack[m_stackIndex];
    m_stackIndex--;
    TEST_STACK_UNDERFLOW
}

void VirtualMachine::popObjectResult() {
    m_result.moveObjectValue(m_stack[m_stackIndex]);
    m_stackIndex--;
    TEST_STACK_UNDERFLOW
}

void VirtualMachine::popObjectIntermediate() {
    m_intermediate.moveObjectValue(m_stack[m_stackIndex]);
    m_stackIndex--;
    TEST_STACK_UNDERFLOW
}


void VirtualMachine::pushValue(const RuntimeValue& value) {
    m_stackIndex++;
    m_stack[m_stackIndex] = value;
    TEST_STACK_OVERFLOW
}

void VirtualMachine::pushObjectValue(const RuntimeValue& value) {
    m_stackIndex++;
    TEST_STACK_OVERFLOW
    m_stack[m_stackIndex].copyObjectValue(value);
}

void VirtualMachine::pushBoolean(const NomadBoolean value) {
    m_stackIndex++;
    m_stack[m_stackIndex].setBooleanValue(value);
    TEST_STACK_OVERFLOW
}

void VirtualMachine::pushInteger(const NomadInteger value) {
    m_stackIndex++;
    m_stack[m_stackIndex].setIntegerValue(value);
    TEST_STACK_OVERFLOW
}

void VirtualMachine::pushFloat(const NomadFloat value) {
    m_stackIndex++;
    m_stack[m_stackIndex].setFloatValue(value);
    TEST_STACK_OVERFLOW
}

void VirtualMachine::pushId(const NomadId value) {
    m_stackIndex++;
    m_stack[m_stackIndex].setIdValue(value);
    TEST_STACK_OVERFLOW
}

void VirtualMachine::pushString(const NomadChar* value) {
    m_stackIndex++;
    TEST_STACK_OVERFLOW
    m_stack[m_stackIndex].setStringValue(value);
}

void VirtualMachine::pushString(const NomadString& value) {
    m_stackIndex++;
    TEST_STACK_OVERFLOW
    m_stack[m_stackIndex].setStringValue(value);
}

void VirtualMachine::pushString(NomadStringObject* value) {
    m_stackIndex++;
    TEST_STACK_OVERFLOW
    m_stack[m_stackIndex].setObjectValue(value);
}

void VirtualMachine::pushN(const NomadIndex count) {
    m_stackIndex += count;
    TEST_STACK_OVERFLOW
}

void VirtualMachine::pop1() {
    m_stackIndex--;
    TEST_STACK_OVERFLOW
}

void VirtualMachine::pop2() {
    m_stackIndex -= 2;
    TEST_STACK_OVERFLOW
}

void VirtualMachine::pop3() {
    m_stackIndex -= 3;
    TEST_STACK_OVERFLOW
}

void VirtualMachine::popN(const NomadIndex count) {
    m_stackIndex -= count;
    TEST_STACK_UNDERFLOW
}

const RuntimeValue& VirtualMachine::popValue() {
    const auto& value= m_stack[m_stackIndex];
    m_stackIndex--;
    TEST_STACK_UNDERFLOW

    return value;
}

NomadBoolean VirtualMachine::popBoolean() {
    const auto value = m_stack[m_stackIndex].getBooleanValue();
    m_stackIndex--;
    TEST_STACK_UNDERFLOW
    return value;
}

NomadInteger VirtualMachine::popInteger() {
    const auto value = m_stack[m_stackIndex].getIntegerValue();
    m_stackIndex--;
    TEST_STACK_UNDERFLOW
    return value;
}

NomadFloat VirtualMachine::popFloat() {
    const auto value = m_stack[m_stackIndex].getFloatValue();
    m_stackIndex--;
    TEST_STACK_UNDERFLOW
    return value;
}

NomadId VirtualMachine::popId() {
    const auto value = m_stack[m_stackIndex].getIdValue();
    m_stackIndex--;
    TEST_STACK_UNDERFLOW
    return value;
}

NomadString VirtualMachine::popString() {
    auto* object = m_stack[m_stackIndex].getStringObject();
    auto value = object->getString();
    object->releaseRef();
    m_stackIndex--;
    TEST_STACK_UNDERFLOW
    return value;
}

NomadId VirtualMachine::nextId() {
    const auto id = m_instructions[m_instructionIndex].value.getIdValue();

    m_instructionIndex++;

    return id;
}

NomadIndex VirtualMachine::nextIndex() {
    const auto index = m_instructions[m_instructionIndex].value.getIndexValue();

    m_instructionIndex++;

    return index;
}

NomadInteger VirtualMachine::nextInteger() {
    const auto integer_value = m_instructions[m_instructionIndex].value.getIntegerValue();

    m_instructionIndex++;

    return integer_value;
}

NomadFloat VirtualMachine::nextFloat() {
    const auto float_value = m_instructions[m_instructionIndex].value.getFloatValue();

    m_instructionIndex++;

    return float_value;
}

void VirtualMachine::jump(NomadIndex index) {
    m_instructionIndex = index;
}

bool VirtualMachine::getVariableValue(IdentifierType identifierType, NomadId variableId, RuntimeValue& value) {
    switch (identifierType) {
    case IdentifierType::Receiver:
        (void)getThis();
        value = m_this;
        return true;
    case IdentifierType::Constant:
        m_runtime->getConstantValue(ConstantId{variableId}, value);
        return true;

    case IdentifierType::DynamicVariable:
        m_runtime->getDynamicVariableValue(this, DynamicVariableId{variableId}, value);
        return true;

    case IdentifierType::ContextVariable:
        m_runtime->getContextVariableValue(ContextVariableId{variableId}, value);
        return true;

    case IdentifierType::FunctionVariable:
        getFunctionVariableValue(VariableId{variableId}, value);
        return true;

    case IdentifierType::Parameter:
        value = getParameter(variableId);
        return true;

    default:
        log::warning("Unexpected identifier type: " + toString(static_cast<int>(identifierType)));
    }

    return false;
}

void VirtualMachine::pushIndex(const NomadIndex value) {
    m_stackIndex++;
    m_stack[m_stackIndex].setIndexValue(value);
    TEST_STACK_OVERFLOW
}

NomadIndex VirtualMachine::popIndex() {
    const auto value = m_stack[m_stackIndex].getIndexValue();
    m_stackIndex--;
    TEST_STACK_UNDERFLOW
    return value;
}

void VirtualMachine::pushInstructionIndex() {
    m_stackIndex++;
    m_stack[m_stackIndex].setIndexValue(m_instructionIndex);
    TEST_STACK_OVERFLOW
}

void VirtualMachine::popInstructionIndex() {
    m_instructionIndex = m_stack[m_stackIndex].getIndexValue();
    m_stackIndex--;
    TEST_STACK_UNDERFLOW
}

} // nomad
