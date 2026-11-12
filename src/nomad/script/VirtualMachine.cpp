// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/VirtualMachine.hpp>

#include <nomad/compiler/Identifier.hpp>

#include <nomad/interpreter/Interpreter.hpp>
#include <nomad/interpreter/InterpreterCallFrame.hpp>

#include <nomad/script/ClassType.hpp>
#include <nomad/script/Closure.hpp>
#include <nomad/script/NomadStringObject.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/RuntimeValueGuard.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/ScriptObject.hpp>
#include <nomad/script/StructType.hpp>

#include <nomad/system/String.hpp>

#include <boost/scope_exit.hpp>

#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <utility>

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

void VirtualMachine::executeFunction(const CallableId functionId) {
    executeFunction(functionId, {});
}

const ScriptFunction* VirtualMachine::runClosure(
    const Closure* closure,
    const std::vector<RuntimeValue>& args
) {
    const auto function = closure != nullptr ? closure->getFunction() : nullptr;

    if (function == nullptr) {
        log::error("Invalid function in closure");
        return nullptr;
    }

    run(*closure, args);

    return function;
}

void VirtualMachine::executeFunction(const Closure* closure, const std::vector<RuntimeValue>& args) {
    const auto function = runClosure(closure, args);

    if (function == nullptr) {
        return;
    }

    // The result is discarded.
    if (const auto returnType = function->getReturnType()) {
        returnType->freeValue(&getResult());
    }
}

void VirtualMachine::executeFunction(const Closure* closure, const std::vector<RuntimeValue>& args, RuntimeValue& returnValue) {
    const auto function = runClosure(closure, args);

    if (function == nullptr) {
        return;
    }

    if (const auto returnType = function->getReturnType()) {
        // The caller receives its own copy; the virtual machine's result is released.
        returnType->copyValue(&getResult(), &returnValue);
        returnType->freeValue(&getResult());
    }  // Else keep existing value
}

void VirtualMachine::executeFunction(const CallableId functionId, const std::vector<RuntimeValue> &args) {
    const auto function = m_runtime->getScriptFunction(functionId);

    if (function == nullptr) {
        log::error("Invalid function id");
        return;
    }

    run(function, args);

    // The result is discarded.
    if (const auto returnType = function->getReturnType()) {
        returnType->freeValue(&getResult());
    }
}

void VirtualMachine::executeFunction(const CallableId functionId, const std::vector<RuntimeValue> &args, RuntimeValue& returnValue) {
    const auto function = m_runtime->getScriptFunction(functionId);

    if (function == nullptr) {
        log::error("Invalid function id");
        return;
    }

    run(function, args);

    const auto returnType = function->getReturnType();

    if (returnType) {
        // The caller receives its own copy; the virtual machine's result is released.
        returnType->copyValue(&getResult(), &returnValue);
        returnType->freeValue(&getResult());
    }  // Else keep existing value
}


void VirtualMachine::setFunctionVariableValue(const VariableId variableId, const RuntimeValue& value) {
    getFunctionVariableStorage(variableId)[0] = value;
}

const RuntimeValue& VirtualMachine::getFunctionVariableValue(const VariableId variableId) const {
    return getFunctionVariableStorage(variableId)[0];
}

std::span<RuntimeValue> VirtualMachine::getFunctionVariableStorage(const VariableId variableId) {
    const auto* callable = getCurrentCallable();
    if (variableId.isValid() && variableId == callable->getNamedReturnVariable()) {
        const auto pointer = getEnclosingParameterStorage(*callable->getCallStorageLayout().returnOffset, 1);
        return {pointer[0].getStorageAddress(), callable->getVariableType(variableId)->getSize()};
    }
    return getLocalStorage(callable->getVariableOffset(variableId), callable->getVariableType(variableId)->getSize());
}

std::span<const RuntimeValue> VirtualMachine::getFunctionVariableStorage(const VariableId variableId) const {
    const auto* callable = getCurrentCallable();
    if (variableId.isValid() && variableId == callable->getNamedReturnVariable()) {
        const auto pointer = getEnclosingParameterStorage(*callable->getCallStorageLayout().returnOffset, 1);
        return {pointer[0].getStorageAddress(), callable->getVariableType(variableId)->getSize()};
    }
    return getLocalStorage(callable->getVariableOffset(variableId), callable->getVariableType(variableId)->getSize());
}

std::span<RuntimeValue> VirtualMachine::getLocalStorage(const NomadIndex offset, const NomadIndex count) {
    if (count == 0) {
        return {};
    }
    return std::span<RuntimeValue>(m_stack).subspan(m_variableIndex + offset, count);
}

std::span<const RuntimeValue> VirtualMachine::getLocalStorage(
    const NomadIndex offset,
    const NomadIndex count
) const {
    if (count == 0) {
        return {};
    }
    const auto start = m_variableIndex + offset;
    return std::span<const RuntimeValue>(m_stack).subspan(start, count);
}

std::span<RuntimeValue> VirtualMachine::getParameterStorage(const NomadIndex offset, const NomadIndex count) {
    if (count == 0) {
        return {};
    }
    return std::span<RuntimeValue>(m_stack).subspan(m_parameterIndex - offset - (count - 1), count);
}

std::span<const RuntimeValue> VirtualMachine::getParameterStorage(
    const NomadIndex offset,
    const NomadIndex count
) const {
    if (count == 0) {
        return {};
    }
    const auto start = m_parameterIndex - offset - (count - 1);
    return std::span<const RuntimeValue>(m_stack).subspan(start, count);
}

std::span<RuntimeValue> VirtualMachine::getEnclosingParameterStorage(
    const NomadIndex offset,
    const NomadIndex count
) {
    if (count == 0) {
        return {};
    }
    const auto anchor = m_variableIndex - s_scriptFrameSlotCount - 1;
    return std::span<RuntimeValue>(m_stack).subspan(anchor - offset - (count - 1), count);
}

std::span<const RuntimeValue> VirtualMachine::getEnclosingParameterStorage(
    const NomadIndex offset,
    const NomadIndex count
) const {
    if (count == 0) {
        return {};
    }
    const auto anchor = m_variableIndex - s_scriptFrameSlotCount - 1;
    return std::span<const RuntimeValue>(m_stack).subspan(anchor - offset - (count - 1), count);
}

void VirtualMachine::pushStorageAddress(RuntimeValue* address) {
    RuntimeValue value;
    value.setStorageAddress(address);
    pushValue(value);
}

void VirtualMachine::pushStorage(const std::span<const RuntimeValue> storage) {
    if (m_stackIndex >= m_stack.size() || storage.size() > m_stack.size() - m_stackIndex - 1) {
        fault("Stack overflow");
    }
    if (!storage.empty()) {
        std::copy(storage.begin(), storage.end(), m_stack.begin() + m_stackIndex + 1);
        m_stackIndex += storage.size();
    }
}

VirtualMachine::ExecutionState VirtualMachine::captureExecutionState() const noexcept {
    return {
        m_instructionIndex,
        m_stackIndex, 
        m_parameterIndex, 
        m_variableIndex,
        m_running, 
        m_yielded, 
        m_this
    };
}

void VirtualMachine::restoreExecutionState(const ExecutionState& state) noexcept {
    restoreScriptRun(state);
    m_yielded = state.yielded;
}

void VirtualMachine::enterScriptFrame(const NomadIndex address, const RuntimeValue& receiver) {
    m_running = true;
    m_yielded = false;
    m_instructionIndex = 0;
    m_this = receiver;
    callScriptMethod(address);
}

void VirtualMachine::enterNativeFrame(const RuntimeValue& receiver) {
    m_parameterIndex = m_stackIndex;
    m_this = receiver;
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
    auto retainSuspendedFrame = false;
    BOOST_SCOPE_EXIT_ALL(&, this) {
        if (!retainSuspendedFrame) {
            m_yielded = false;
            restoreScriptRun(state);
        }

        if (!retainSuspendedFrame) {
            releaseArguments();
        }
    };

    if (function == nullptr || args.size() + closures.size() != function->getParameterCount() ||
        closures.size() != function->getCaptureCount()) {
        fault("Incorrect script argument or capture count");
    }

    if (function->getReturnType() != nullptr && function->getReturnType()->isStruct()) {
        fault("Host script calls do not support struct return storage");
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

    if (m_yielded) {
        retainSuspendedFrame = true;
        m_running = false;
    }
}

void VirtualMachine::run(
    const Closure& closure,
    const std::span<const RuntimeValue> arguments
) {
    const auto* function = closure.getFunction();
    if (
        m_runtime->getScriptFunction(function->getId()) != function ||
        function->getFunctionStart() == NOMAD_INVALID_INDEX ||
        function->getFunctionStart() >= function->getFunctionEnd()
    ) {
        throw std::invalid_argument(
            "Closure requires a compiled script function in the virtual machine's runtime"
        );
    }

    const auto captureCount = closure.getCaptureCount();
    if (
        captureCount != function->getCaptureCount() ||
        captureCount > function->getParameterCount() ||
        arguments.size() != function->getParameterCount() - captureCount
    ) {
        throw std::invalid_argument("Incorrect explicit argument or capture count for closure");
    }

    const auto* returnType = function->getReturnType();
    if (returnType == nullptr || returnType->isStruct()) {
        throw std::invalid_argument("Raw closure calls require a scalar, reference, or void result");
    }

    for (auto index = NomadIndex{0}; index < arguments.size(); ++index) {
        const auto& parameter = function->getParameters()[index];
        const auto* type = parameter.type;
        if (
            type == nullptr ||
            type->isStruct() ||
            type->getSize() != 1 ||
            parameter.getStackValueCount() != 1
        ) {
            throw std::invalid_argument("Raw closure arguments require scalar or reference storage");
        }

    }

    for (auto index = NomadIndex{0}; index < captureCount; ++index) {
        const auto& parameter = function->getParameters()[arguments.size() + index];
        if (
            parameter.type != closure.getCaptureType(index) ||
            parameter.getStackValueCount() != closure.getCaptureStorage(index).size()
        ) {
            throw std::invalid_argument("Closure capture storage does not match the callable");
        }

    }

    RuntimeValue result;
    {
        InterpreterCallFrame frame(*this, closure, arguments);
        if (!executeClosure(closure)) {
            frame.releaseArgumentsToVirtualMachine();
        }

        result = frame.releaseResult();
    }

    m_result = result;
}

bool VirtualMachine::executeClosure(const Closure& closure) {

    return executeFunction(*closure.getFunction());
}

bool VirtualMachine::executeFunction(const ScriptFunction& function) {
    const auto synchronous = function.getReturnType()->isStruct() || std::ranges::any_of(
        function.getParameters(),
        [](const auto& parameter) {

            return parameter.type->isStruct();
        }
    );
    const auto state = beginScriptRun();
    const auto restore = [&]() noexcept {
        if (synchronous) {
            restoreExecutionState(state);
        } else {
            m_yielded = false;
            restoreScriptRun(state);
        }
    };
    auto retainSuspendedFrame = false;
    BOOST_SCOPE_EXIT_ALL(&) {
        if (!retainSuspendedFrame) {
            restore();
        }
    };

    callScriptFunction(function.getFunctionStart());
    executeScript();
    if (m_yielded && synchronous) {
        fault("Aggregate closure calls cannot yield");
    }

    if (m_yielded && m_this.getObjectValue() != nullptr) {
        fault("Class initialization or construction cannot yield");
    }

    const auto completed = !m_yielded;
    if (!completed) {
        retainSuspendedFrame = true;
        m_running = false;
    }

    return completed;
}

VirtualMachine::ExecutionState VirtualMachine::beginScriptRun() {
    const auto state = captureExecutionState();
    m_running = true;
    m_yielded = false;
    // Returning to instruction zero executes op_stop.
    m_instructionIndex = 0;
    return state;
}

void VirtualMachine::restoreScriptRun(const ExecutionState& state) noexcept {
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
    if (type->isStruct()) {
        fault("Host calls do not support struct argument storage");
    }
    pushValue(RuntimeValue{});
    argumentTypes.push_back(type);
    type->copyValue(&value, &m_stack[m_stackIndex]);
}

void VirtualMachine::freeHostArguments(
    const NomadIndex firstIndex,
    const std::vector<const Type*>& argumentTypes
) {
    for (auto index = NomadIndex{0}; index < argumentTypes.size(); ++index) {
        argumentTypes[index]->freeValue(&m_stack[firstIndex + index]);
    }
}

void VirtualMachine::executeCreationBody(const ScriptCallableBase* callable, const RuntimeValue& receiver) {
    const auto state = beginScriptRun();
    const auto savedResult = m_result;
    const auto savedIntermediate = m_intermediate;
    BOOST_SCOPE_EXIT_ALL(&, this) {
        m_result = savedResult;
        m_intermediate = savedIntermediate;
    };

    BOOST_SCOPE_EXIT_ALL(&, this) {
        m_yielded = false;
        restoreScriptRun(state);
    };

    m_result = RuntimeValue{};
    m_intermediate = RuntimeValue{};
    m_this = receiver;
    callScriptMethod(callable->getFunctionStart());
    executeScript();
    if (m_yielded) {
        fault("Initialization or construction cannot yield");
    }
}

NomadObject* VirtualMachine::createScriptObject(const CallableId constructorId) {
    const auto* constructor = m_runtime->getConstructor(constructorId)->asScriptConstructor();
    const auto release = [](ScriptObject* object) { object->releaseRef(); };
    std::unique_ptr<ScriptObject, decltype(release)> object(
        new ScriptObject(constructor->getClassType()->asClassType()), release);
    RuntimeValue receiver;
    receiver.setObjectValue(object.get());
    executeCreationBody(constructor, receiver);
    return object.release();
}

void VirtualMachine::initializeStructStorage(RuntimeValue* destination, const Type* type) {
    const auto* owner = type->asStructType();
    const auto size = owner->getSize();
    if (size != 0) {
        std::fill_n(destination, size, RuntimeValue{});
    }
    const auto* initializer = owner->getInitializer();
    if (initializer == nullptr) {
        owner->initValue(destination);
        return;
    }
    const auto* script = initializer->asScriptMethod();
    if (size != 0) {
        pushStorageAddress(destination);
    }

    BOOST_SCOPE_EXIT_ALL(&, this) {
        if (size != 0) {
            pop1();
        }
    };

    executeCreationBody(script, RuntimeValue{});
}

void VirtualMachine::createScriptStruct(RuntimeValue* destination, const CallableId constructorId) {
    const auto* constructor = m_runtime->getConstructor(constructorId)->asScriptConstructor();
    initializeStructStorage(destination, constructor->getClassType());
    executeCreationBody(constructor, RuntimeValue{});
}

void VirtualMachine::createNativeStruct(RuntimeValue* destination, const CallableId constructorId) {
    const auto* constructor = m_runtime->getNativeConstructor(constructorId);
    initializeStructStorage(destination, constructor->getClassType());
    const auto frame = beginNativeCall();
    const auto savedResult = m_result;
    const auto savedIntermediate = m_intermediate;
    BOOST_SCOPE_EXIT_ALL(&, this) {
        m_result = savedResult;
        m_intermediate = savedIntermediate;
    };

    BOOST_SCOPE_EXIT_ALL(&, this) {
        endNativeCall(frame);
    };

    m_this = RuntimeValue{};
    constructor->getFn()(this);
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

void VirtualMachine::allocateScriptObject(const TypeId typeId) {
    const auto* classType = m_runtime->getType(typeId).value()->asClassType();
    m_this.setObjectValue(new ScriptObject(classType));
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
    BOOST_SCOPE_EXIT_ALL(&, this) {
        endNativeCall(frame);
    };

    m_this = RuntimeValue{};
    function->getFn()(this);
}

NomadObject* VirtualMachine::createNativeObject(const CallableId constructorId) {
    const auto* constructor = m_runtime->getNativeConstructor(constructorId);
    const auto frame = beginNativeCall();
    const auto savedResult = m_result;
    const auto savedIntermediate = m_intermediate;
    BOOST_SCOPE_EXIT_ALL(&, this) {
        m_result = savedResult;
        m_intermediate = savedIntermediate;
        endNativeCall(frame);
    };

    m_this = RuntimeValue{};
    m_result = RuntimeValue{};
    // Release partial results before restoring the enclosing frame and registers.
    RuntimeValueGuard resultGuard(*constructor->getReturnType(), &m_result);
    constructor->getFn()(this);
    if (m_result.getObjectValue() == nullptr) {
        fault("Native constructor for '" + constructor->getName() + "' returned null");
    }

    auto* object = m_result.getObjectValue();
    resultGuard.release();

    return object;
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
    BOOST_SCOPE_EXIT_ALL(&, this) {
        endNativeCall(frame);
    };

    method->getFn()(this);
}

NomadObject* VirtualMachine::getScriptReceiver() const {
    return m_stack[m_variableIndex - 1].getObjectValue();
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
    BOOST_SCOPE_EXIT_ALL(&, this) {
        m_this = previousThis;
    };

    m_this = receiver;
    (void)getThis();
    callback(this);
}

void VirtualMachine::getNativeProperty(
    const PropertyId memberId,
    const RuntimeValue& receiver,
    RuntimeValue& result
) {
    const auto info = m_runtime->getNativePropertyInfo(memberId);
    if (info && (info->getterMethodId.isValid() || info->setterMethodId.isValid())) {
        fault("Method-backed native properties require typed interpreter access");
    }
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
    if (object == nullptr || object->getClassType() != property->getClassType()) {
        fault("Stored property receiver has no matching field storage");
    }
    const auto index = property->getClassType()->getFieldIndex(memberId);
    property->getValueType()->copyValue(&object->getField(index), &result);
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
    if (object == nullptr || object->getClassType() != property->getClassType()) {
        fault("Stored property receiver has no matching field storage");
    }
    object->setField(property->getClassType()->getFieldIndex(memberId), value);
}

RuntimeValue& VirtualMachine::getStoredProperty(const NomadIndex offset) {
    return *getStoredPropertyAddress(m_intermediate, offset, offset);
}

RuntimeValue* VirtualMachine::getStoredPropertyAddress(
    const RuntimeValue& receiver,
    const NomadIndex firstOffset,
    const NomadIndex lastOffset
) {
    auto* object = dynamic_cast<ScriptObject*>(receiver.getObjectValue());
    if (object == nullptr) {
        fault("Stored property receiver has no matching field storage");
    }
    if (lastOffset >= object->getStorageSize()) {
        fault("Script object field index is out of bounds");
    }
    return &object->getStorageValue(firstOffset);
}

void VirtualMachine::setNativeProperty(
    const PropertyId memberId,
    const RuntimeValue& receiver,
    const RuntimeValue& value
) {
    const auto info = m_runtime->getNativePropertyInfo(memberId);
    if (info && (info->getterMethodId.isValid() || info->setterMethodId.isValid())) {
        fault("Method-backed native properties require typed interpreter access");
    }
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
    const auto* info = m_runtime->getNativeIndexerInfo(indexerId);
    if (!info) {
        fault("Invalid native indexer");
    }
    const auto& callback = write ? info->setFn : info->getFn;
    if (!callback) {
        fault(write ? "Invalid native indexer setter" : "Invalid native indexer getter");
    }
    m_result = RuntimeValue{};
    const auto frame = beginNativeCall();
    BOOST_SCOPE_EXIT_ALL(&, this) {
        endNativeCall(frame);
    };

    m_this = receiver;
    callback(this);
}

void VirtualMachine::runNativeIndexer(
    const IndexerId indexerId,
    const std::vector<RuntimeValue>& args,
    const RuntimeValue& receiver,
    const bool write
) {
    const auto* info = m_runtime->getNativeIndexerInfo(indexerId);
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
    const auto releaseArguments = [&]() noexcept {
        while (m_stackIndex > initialStackIndex + 2) {
            const auto index = args.size() - (m_stackIndex - initialStackIndex - 2);
            parameterType(index)->freeValue(&m_stack[m_stackIndex]);
            --m_stackIndex;
        }
        if (m_stackIndex > initialStackIndex + 1) {
            info->classType->freeValue(&m_stack[initialStackIndex + 2]);
        }
        m_stackIndex = initialStackIndex;
        m_this = savedThis;
    };
    BOOST_SCOPE_EXIT_ALL(&) {
        releaseArguments();
    };

    m_result = RuntimeValue{};
    pushThis();
    pushObjectValue(receiver);
    for (auto index = args.size(); index > 0; --index) {
        auto* destination = &m_stack[m_stackIndex + 1];
        *destination = RuntimeValue{};
        parameterType(index - 1)->copyValue(&args[index - 1], destination);
        ++m_stackIndex;
    }

    callNativeIndexer(indexerId, receiver, write);
}

std::vector<InterpreterValue> VirtualMachine::captureCurrentFrame(
    const ScriptFunction* function
) const {
    if (function == nullptr || function->getParentId() == CallableId{}) {
        fault("Invalid function for current-frame capture");
    }

    const auto* parent = m_runtime->getScriptCallable(function->getParentId());
    if (
        parent == nullptr ||
        m_instructionIndex < parent->getFunctionStart() ||
        m_instructionIndex >= parent->getFunctionEnd()
    ) {
        fault("Format expression does not belong to the current script frame");
    }

    std::vector<InterpreterValue> values;
    values.reserve(function->getCaptureCount());
    for (const auto& capture : function->getCaptures()) {
        if (capture.type == nullptr || capture.type->isVoid()) {
            fault("Invalid value type for current-frame capture");
        }

        const auto size = capture.type->getSize();
        switch (capture.source) {
            case CaptureSource::Parameter: {
                if (!isIdInRange(capture.parameterId, parent->getParameterCount())) {
                    fault("Invalid parameter for current-frame capture");
                }

                values.emplace_back(
                    capture.type,
                    getParameterStorage(parent->getParameterOffset(capture.parameterId), size)
                );
                break;
            }

            case CaptureSource::Variable: {
                if (!isIdInRange(capture.variableId.id, parent->getVariableCount())) {
                    fault("Invalid variable for current-frame capture");
                }

                values.emplace_back(
                    capture.type,
                    getFunctionVariableStorage(capture.variableId)
                );
                break;
            }

            case CaptureSource::Receiver: {
                if (capture.type->isStruct()) {
                    std::span<const RuntimeValue> storage;
                    if (size != 0) {
                        const auto layout = parent->getCallStorageLayout();
                        if (!layout.receiverOffset.has_value()) {
                            fault("Struct receiver capture has no borrowed receiver storage");
                        }

                        const auto pointer = getParameterStorage(*layout.receiverOffset, 1);
                        storage = {pointer[0].getStorageAddress(), size};
                    }

                    values.emplace_back(capture.type, storage);
                    break;
                }

                (void)getThis();
                values.emplace_back(capture.type, m_this);
                break;
            }

            default:
                fault("Invalid source for current-frame capture");
        }

    }

    return values;
}

std::unique_ptr<Closure> VirtualMachine::createClosure(
    const Runtime* runtime,
    const CallableId functionId
) const {
    if (runtime == nullptr || runtime != m_runtime) {
        fault("Closure capture requires the virtual machine's runtime");
    }

    const auto* function = runtime->getScriptFunction(functionId);
    if (function == nullptr) {
        fault("Invalid script function for closure capture");
    }

    const auto* parent = runtime->getScriptCallable(function->getParentId());
    if (function->getCaptureCount() != 0) {
        if (m_variableIndex <= s_scriptFrameSlotCount) {
            fault("Cannot capture without an active script frame");
        }

        if (parent == nullptr || getCurrentCallable() != parent) {
            fault("Closure captures do not belong to the active script frame");
        }

    }

    const auto& captures = function->getCaptures();
    std::vector<RuntimeValue> storage;
    auto storageSize = NomadIndex{0};
    for (const auto& capture : captures) {
        if (capture.type == nullptr || capture.type->isVoid()) {
            fault("Invalid value type for closure capture");
        }

        const auto size = capture.type->getSize();
        if (size > storage.max_size() - storageSize) {
            fault("Closure capture storage exceeds the maximum slot count");
        }

        storageSize += size;
    }

    storage.resize(storageSize);
    try {
        auto offset = NomadIndex{0};
        for (const auto& capture : captures) {
            const auto size = capture.type->getSize();
            std::span<const RuntimeValue> source;
            switch (capture.source) {
                case CaptureSource::Parameter: {
                    if (!isIdInRange(capture.parameterId, parent->getParameterCount())) {
                        fault("Invalid parameter for closure capture");
                    }

                    source = getEnclosingParameterStorage(
                        parent->getParameterOffset(capture.parameterId),
                        size
                    );
                    break;
                }

                case CaptureSource::Variable: {
                    if (!isIdInRange(capture.variableId.id, parent->getVariableCount())) {
                        fault("Invalid variable for closure capture");
                    }

                    source = getFunctionVariableStorage(capture.variableId);
                    break;
                }

                case CaptureSource::Receiver: {
                    if (capture.type->isStruct()) {
                        if (size != 0) {
                            const auto layout = parent->getCallStorageLayout();
                            if (!layout.receiverOffset.has_value()) {
                                fault("Struct receiver capture has no borrowed receiver storage");
                            }

                            const auto pointer = getEnclosingParameterStorage(*layout.receiverOffset, 1);
                            source = {pointer[0].getStorageAddress(), size};
                        }

                        break;
                    }

                    const auto& receiver = m_stack[m_variableIndex - 1];
                    if (receiver.getObjectValue() == nullptr) {
                        fault("Cannot capture a missing enclosing receiver");
                    }

                    source = {&receiver, 1};
                    break;
                }

                default:
                    fault("Invalid source for closure capture");
            }

            if (size != 0) {
                capture.type->copyValue(source.data(), storage.data() + offset);
            }

            offset += size;
        }

        return std::make_unique<Closure>(function, std::move(storage));
    } catch (...) {
        auto offset = NomadIndex{0};
        for (const auto& capture : captures) {
            const auto size = capture.type->getSize();
            if (size != 0) {
                capture.type->freeValue(storage.data() + offset);
            }

            offset += size;
        }

        throw;
    }
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
    getFunctionVariableStorage(variableId)[0].freeObjectValue();
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

    case IdentifierType::Parameter: {
        const auto* function = getCurrentCallable();
        if (function == nullptr) {
            fault("Parameter formatting requires an active script callable");
        }

        value = getParameter(function->getParameterOffset(variableId));

        return true;
    }

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
