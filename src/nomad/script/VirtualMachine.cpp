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

void VirtualMachine::run(const ScriptFunction* function, const std::vector<RuntimeValue>& args, const std::vector<RuntimeValue>& closures) {
    runCallable(function, args, closures, nullptr);
}

void VirtualMachine::initializeObject(ScriptObject& object) {
    const auto* type = object.getObjectType();
    if (type->getRuntime() != m_runtime) {
        fault("Cannot initialize an object from another runtime");
    }
    const ScriptCallableBase* initializer = nullptr;
    for (const auto* method : type->getMethods()) {
        if (method->getName() == "$init") {
            initializer = method->asScriptCallable();
            break;
        }
    }
    if (initializer == nullptr || initializer->getFunctionStart() == NOMAD_INVALID_INDEX ||
        initializer->getFunctionStart() >= initializer->getFunctionEnd()) {
        fault("Class initializer has not been compiled");
    }
    object.initializeFields();
    RuntimeValue receiver;
    receiver.setObjectValue(&object);
    const auto savedResult = m_result;
    const auto savedIntermediate = m_intermediate;
    m_result = RuntimeValue{};
    m_intermediate = RuntimeValue{};
    try {
        runCallable(initializer, {}, {}, &receiver);
    } catch (...) {
        m_result = savedResult;
        m_intermediate = savedIntermediate;
        throw;
    }
    m_result = savedResult;
    m_intermediate = savedIntermediate;
}

NomadObject* VirtualMachine::constructObject(
    const CallableId constructorId,
    const std::vector<RuntimeValue>& arguments
) {
    const auto* constructor = m_runtime->getConstructor(constructorId);
    if (constructor == nullptr || constructor->getParameters().size() != arguments.size()) {
        fault("Cannot construct object: invalid constructor or argument count");
    }
    if (constructor->asNativeConstructor() != nullptr) {
        const auto savedResult = m_result;
        const auto savedIntermediate = m_intermediate;
        try {
            runNativeCallable(constructor, arguments);
        } catch (...) {
            m_result = savedResult;
            m_intermediate = savedIntermediate;
            throw;
        }
        auto* object = m_result.getObjectValue();
        m_result = savedResult;
        m_intermediate = savedIntermediate;
        return object;
    }
    const auto* script = constructor->asScriptCallable();
    if (script == nullptr || script->getFunctionStart() == NOMAD_INVALID_INDEX ||
        script->getFunctionStart() >= script->getFunctionEnd()) {
        fault("Class constructor has not been compiled");
    }
    const auto release = [](ScriptObject* object) { object->releaseRef(); };
    auto object = std::unique_ptr<ScriptObject, decltype(release)>(
        new ScriptObject(constructor->getObjectType()->asObjectType()), release
    );
    initializeObject(*object);
    RuntimeValue receiver;
    receiver.setObjectValue(object.get());
    const auto savedResult = m_result;
    const auto savedIntermediate = m_intermediate;
    m_result = RuntimeValue{};
    m_intermediate = RuntimeValue{};
    try {
        runCallable(script, arguments, {}, &receiver);
    } catch (...) {
        m_result = savedResult;
        m_intermediate = savedIntermediate;
        throw;
    }
    m_result = savedResult;
    m_intermediate = savedIntermediate;
    return object.release();
}

void VirtualMachine::runCallable(
    const ScriptCallableBase* function,
    const std::vector<RuntimeValue>& args,
    const std::vector<RuntimeValue>& closures,
    const RuntimeValue* receiver
) {
    // Save the frame so `run` can be re-entered (e.g. from a nativeFunction) and so a fault leaves the VM usable.
    const auto savedInstructionIndex = m_instructionIndex;
    const auto savedStackIndex = m_stackIndex;
    const auto savedParameterIndex = m_parameterIndex;
    const auto savedEnclosingParameterIndex = m_enclosingParameterIndex;
    const auto savedVariableIndex = m_variableIndex;
    const auto savedRunning = m_running;

    const auto restoreFrame = [&]() {
        m_instructionIndex = savedInstructionIndex;
        m_stackIndex = savedStackIndex;
        m_parameterIndex = savedParameterIndex;
        m_enclosingParameterIndex = savedEnclosingParameterIndex;
        m_variableIndex = savedVariableIndex;
        m_running = savedRunning;
    };

    m_running = true;
    m_yielded = false;

    // Returning from the function jumps to instruction 0 (`op_stop`).
    m_instructionIndex = 0;

    auto entryVariableIndex = NOMAD_INVALID_INDEX;
    try {
        // Push closures and parameters onto the stack (right-to-left)
        // Closures first, then parameters
        auto closureCount = closures.size();
        for (auto i = closureCount - 1; i < closureCount; i--) {
            pushValue(closures[i]);
        }

        auto parameterCount = args.size();
        for (auto i = parameterCount - 1; i < parameterCount; i--) {
            pushValue(args[i]);
        }

        if (receiver != nullptr) {
            if (m_stackIndex >= m_stack.size() || m_stack.size() - m_stackIndex - 1 < 3 ||
                function->getVariableCount() > m_stack.size() - m_stackIndex - 4) {
                fault("Stack overflow");
            }
            entryVariableIndex = m_stackIndex + 4;
        }
        callScriptCallable(function, receiver);

        do {
#ifdef NOMAD_DEBUG
            // auto fn = m_instructions[m_instructionIndex].fn;
            // auto id = m_runtime->getInstructionId(fn);
            // auto& name = m_runtime->getInstructionName(id);
            // log::debug("Executing: " + name + "(ip: " + toString(m_instructionIndex) + ", sp: " + toString(m_stack_index) + ")");
            // log::flush();
#endif
            auto& instruction = m_instructions[m_instructionIndex];

            m_instructionIndex++;

            instruction.fn(this);
        } while (m_running);
        if (receiver != nullptr && m_yielded) {
            fault("Class initialization or construction cannot yield");
        }
    } catch (...) {
        if (entryVariableIndex != NOMAD_INVALID_INDEX) {
            for (auto index = NomadIndex{0}; index < function->getVariableCount(); ++index) {
                const auto* type = function->getVariableType(VariableId{toNomadId(index)});
                if (type != nullptr) {
                    type->freeValue(m_stack[entryVariableIndex + index]);
                }
            }
        }
        m_yielded = false;
        restoreFrame();

        throw;
    }

    if (!m_yielded) {
        restoreFrame();
    } else {
        m_running = false;
    }
}

void VirtualMachine::initializeFunctionVariables(const ScriptCallableBase* function, const RuntimeValue* receiver) {
    if (function == nullptr) {
        return;
    }

    const auto variableCount = function->getVariableCount();

    for (auto variableIndex = NomadIndex{0}; variableIndex < variableCount; ++variableIndex) {
        m_stack[m_variableIndex + variableIndex] = RuntimeValue{};
    }
    for (NomadIndex variableIndex = 0; variableIndex < variableCount; ++variableIndex) {
        const auto type = function->getVariableType(VariableId{toNomadId(variableIndex)});

        const auto parameterId = function->getParameterId(function->getVariableName(VariableId{toNomadId(variableIndex)}));
        if (receiver != nullptr && function->getVariableName(VariableId{toNomadId(variableIndex)}) == "this") {
            type->copyValue(*receiver, m_stack[m_variableIndex + variableIndex]);
        } else if (type != nullptr && parameterId != NOMAD_INVALID_ID) {
            // Mutable bindings own copies; argument storage remains owned by the caller.
            type->copyValue(getParameter(toNomadIndex(parameterId)), m_stack[m_variableIndex + variableIndex]);
        } else if (type != nullptr && type->isString()) {
            initStringVariable(VariableId{toNomadId(variableIndex)});
        }
    }
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

void VirtualMachine::callFunction(const ScriptFunction* function) {
    callScriptCallable(function, nullptr);
}

void VirtualMachine::callScriptCallable(const ScriptCallableBase* function, const RuntimeValue* receiver) {
    if (function == nullptr) {
        fault("Invalid function");
        return;
    }

    auto previousParameterIndex = m_parameterIndex;
    m_parameterIndex = m_stackIndex;

    pushIndex(m_instructionIndex);
    pushIndex(previousParameterIndex);
    pushIndex(m_variableIndex);

    // Variables start after the saved call frame slots.
    m_variableIndex = m_stackIndex + 1;

    const auto variableCount = function->getVariableCount();

    if (variableCount > 0) {
        pushN(variableCount);
        initializeFunctionVariables(function, receiver);
    }

    auto functionStart = function->getFunctionStart();

    jump(functionStart);
}

void VirtualMachine::callNativeFunction(const CallableId nativeFunctionId) {
    if (m_runtime->getNativeConstructor(nativeFunctionId) != nullptr) {
        callNativeConstructor(nativeFunctionId);
        return;
    }
    const auto* function = m_runtime->getNativeFunction(nativeFunctionId);
    if (function == nullptr || !function->getFn()) {
        fault("Invalid native function id: " + toString(nativeFunctionId.id));
    }
    invokeNativeFunction(function->getFn());
}

void VirtualMachine::callNativeConstructor(const CallableId constructorId) {
    const auto* constructor = m_runtime->getNativeConstructor(constructorId);
    if (constructor == nullptr || !constructor->getFn()) {
        fault("Invalid native constructor id: " + toString(constructorId.id));
    }
    m_result = RuntimeValue{};
    try {
        invokeNativeFunction(constructor->getFn());
        if (m_result.getObjectValue() == nullptr) {
            fault("Native constructor for '" + constructor->getName() + "' returned null");
        }
    } catch (...) {
        constructor->getReturnType()->freeValue(m_result);
        throw;
    }
}

void VirtualMachine::invokeNativeFunction(const NativeFunctionFn& callback) {
    const auto previousParameterIndex = m_parameterIndex;
    const auto previousEnclosingParameterIndex = m_enclosingParameterIndex;

    m_parameterIndex = m_stackIndex;

    // Save the enclosing function's parameter frame for closure capture
    m_enclosingParameterIndex = previousParameterIndex;

    try {
        callback(this);
    } catch (...) {
        m_parameterIndex = previousParameterIndex;
        m_enclosingParameterIndex = previousEnclosingParameterIndex;
        throw;
    }

    m_parameterIndex = previousParameterIndex;
    m_enclosingParameterIndex = previousEnclosingParameterIndex;
}

void VirtualMachine::callNativeMethod(const CallableId memberId, const RuntimeValue& receiver) {
    const auto* method = m_runtime->getNativeMemberMethod(memberId);
    if (method == nullptr || !method->getFn()) {
        fault("Invalid native method id: " + toString(memberId.id));
    }
    invokeNativeMethod(method->getFn(), receiver);
}

void VirtualMachine::invokeNativeMethod(const NativeFunctionFn& callback, const RuntimeValue& receiver) {
    const auto previousParameterIndex = m_parameterIndex;
    const auto previousEnclosingParameterIndex = m_enclosingParameterIndex;
    const auto previousThis = m_this;
    m_this = receiver;
    m_parameterIndex = m_stackIndex;
    m_enclosingParameterIndex = previousParameterIndex;

    try {
        (void)getThis();
        callback(this);
    } catch (...) {
        m_parameterIndex = previousParameterIndex;
        m_enclosingParameterIndex = previousEnclosingParameterIndex;
        m_this = previousThis;
        throw;
    }

    m_parameterIndex = previousParameterIndex;
    m_enclosingParameterIndex = previousEnclosingParameterIndex;
    m_this = previousThis;
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
    invokeNativeMethod(callback, receiver);
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
    if (m_stackIndex >= m_stack.size() || args.size() > m_stack.size() - m_stackIndex - 1) {
        fault("Stack overflow");
    }
    const auto initialStackIndex = m_stackIndex;
    const auto parameterType = [&](const NomadIndex index) {
        return index < info->indexTypes.size() ? info->indexTypes[index] : info->valueType;
    };
    const auto releaseArguments = [&]() {
        while (m_stackIndex > initialStackIndex) {
            const auto index = args.size() - (m_stackIndex - initialStackIndex);
            parameterType(index)->freeValue(m_stack[m_stackIndex]);
            --m_stackIndex;
        }
    };
    m_result = RuntimeValue{};
    try {
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
    runNativeCallable(function, args);
}

void VirtualMachine::runNativeCallable(
    const Callable* function,
    const std::vector<RuntimeValue>& args
) {
    if (function->getReturnType() == nullptr) {
        fault("NativeFunction '" + function->getName() + "' has no return type");
    }
    if (!function->getReturnType()->isVoid()) {
        m_result = RuntimeValue{};
    }
    if (args.size() != function->getParameterCount()) {
        fault("Incorrect argument count when calling native function '" + function->getName() + "'");
    }
    if (m_stackIndex >= m_stack.size() ||
        args.size() > m_stack.size() - m_stackIndex - 1) {
        fault("Stack overflow");
    }

    const auto initialStackIndex = m_stackIndex;
    std::vector<const Type*> argumentTypes;
    argumentTypes.reserve(args.size());

    const auto releaseArguments = [&]() {
        for (NomadIndex index = 0; index < argumentTypes.size(); ++index) {
            const auto stackIndex = initialStackIndex + index + 1;
            argumentTypes[index]->freeValue(m_stack[stackIndex]);
        }
        m_stackIndex = initialStackIndex;
    };

    try {
        for (NomadIndex index = args.size(); index > 0; --index) {
            const auto argumentIndex = index - 1;
            const auto* type = function->getParameters()[argumentIndex].type;
            if (type == nullptr) {
                fault("NativeFunction '" + function->getName() + "' has an invalid parameter type");
            }
            ++m_stackIndex;
            argumentTypes.push_back(type);
            m_stack[m_stackIndex] = RuntimeValue{};
            type->copyValue(args[argumentIndex], m_stack[m_stackIndex]);
        }

        callNativeFunction(function->getId());
    } catch (...) {
        releaseArguments();
        throw;
    }

    releaseArguments();
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
    if (m_stackIndex >= m_stack.size() || args.size() > m_stack.size() - m_stackIndex - 1) {
        fault("Stack overflow");
    }

    const auto initialStackIndex = m_stackIndex;
    std::vector<const Type*> argumentTypes;
    argumentTypes.reserve(args.size());

    const auto releaseArguments = [&]() {
        for (NomadIndex index = 0; index < argumentTypes.size(); ++index) {
            argumentTypes[index]->freeValue(m_stack[initialStackIndex + index + 1]);
        }
        m_stackIndex = initialStackIndex;
    };

    try {
        for (NomadIndex index = args.size(); index > 0; --index) {
            const auto argumentIndex = index - 1;
            const auto* type = method->getParameters()[argumentIndex].type;
            if (type == nullptr) {
                fault("Native method '" + method->getName() + "' has an invalid parameter type");
            }

            ++m_stackIndex;
            argumentTypes.push_back(type);
            m_stack[m_stackIndex] = RuntimeValue{};
            type->copyValue(args[argumentIndex], m_stack[m_stackIndex]);
        }

        callNativeMethod(memberId, receiver);
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
        } else {
            fault("Invalid capture type");
        }

        captureValues.emplace_back(captureValue);
    }

    return std::make_unique<Closure>(function, captureValues);
}

void VirtualMachine::returnFunction(const NomadIndex variableCount) {
    popN(variableCount);

    m_variableIndex = popIndex();
    m_parameterIndex = popIndex();
    m_instructionIndex = popIndex();
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
