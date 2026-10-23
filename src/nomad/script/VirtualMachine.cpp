// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <ranges>

#include <nomad/script/Closure.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/NomadStringObject.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>
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

void VirtualMachine::setFunctionVariableValue(const NomadId variableId, const RuntimeValue& value) {
    m_stack[m_variableIndex + variableId] = value;
}

const RuntimeValue& VirtualMachine::getFunctionVariableValue(const NomadId variableId) const {
    return m_stack[m_variableIndex + variableId];
}

void VirtualMachine::run(const ScriptFunction* function, const std::vector<RuntimeValue>& args, const std::vector<RuntimeValue>& closures) {
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

        callFunction(function);

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
    } catch (...) {
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

void VirtualMachine::initializeFunctionVariables(const ScriptFunction* function) {
    if (function == nullptr) {
        return;
    }

    const auto variableCount = function->getVariableCount();

    for (NomadIndex variableIndex = 0; variableIndex < variableCount; ++variableIndex) {
        const auto type = function->getVariableType(toNomadId(variableIndex));
        m_stack[m_variableIndex + variableIndex] = RuntimeValue{};

        const auto parameterId = function->getParameterId(function->getVariableName(toNomadId(variableIndex)));
        if (type != nullptr && parameterId != NOMAD_INVALID_ID) {
            // Mutable bindings own copies; argument storage remains owned by the caller.
            type->copyValue(getParameter(toNomadIndex(parameterId)), m_stack[m_variableIndex + variableIndex]);
        } else if (type != nullptr && type->isString()) {
            initStringVariable(variableIndex);
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
        initializeFunctionVariables(function);
    }

    auto functionStart = function->getFunctionStart();

    jump(functionStart);
}

void VirtualMachine::callNativeFunction(const CallableId nativeFunctionId) {
    const auto previousParameterIndex = m_parameterIndex;
    const auto previousEnclosingParameterIndex = m_enclosingParameterIndex;

    m_parameterIndex = m_stackIndex;

    // Save the enclosing function's parameter frame for closure capture
    m_enclosingParameterIndex = previousParameterIndex;

    const auto nativeFunctionFn = m_runtime->getNativeFunctionFn(nativeFunctionId);

#ifdef NOMAD_DEBUG
    if (nativeFunctionFn == nullptr) {
        log::error("NativeFunction not found: " + toString(nativeFunctionId.id));
        m_parameterIndex = previousParameterIndex;
        m_enclosingParameterIndex = previousEnclosingParameterIndex;
        return;
    }
#endif

    try {
        nativeFunctionFn(this);
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
    NativePropertyDefinition definition;
    if (!m_runtime->getNativePropertyDefinition(memberId, definition) || !definition.getFn) {
        fault("Invalid native property getter");
    }
    invokeNativeMember([&](VirtualMachine* machine) { definition.getFn(machine, result); }, receiver);
}

void VirtualMachine::setNativeProperty(
    const PropertyId memberId,
    const RuntimeValue& receiver,
    const RuntimeValue& value
) {
    NativePropertyDefinition definition;
    if (!m_runtime->getNativePropertyDefinition(memberId, definition) || !definition.setFn) {
        fault("Invalid native property setter");
    }
    invokeNativeMember([&](VirtualMachine* machine) { definition.setFn(machine, value); }, receiver);
}

void VirtualMachine::callNativeIndexer(
    const IndexerId indexerId,
    const RuntimeValue& receiver,
    const bool write
) {
    NativeIndexerDefinition definition;
    if (!m_runtime->getNativeIndexerDefinition(indexerId, definition)) {
        fault("Invalid native indexer");
    }
    const auto& callback = write ? definition.setFn : definition.getFn;
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
    NativeIndexerDefinition definition;
    if (!m_runtime->getNativeIndexerDefinition(indexerId, definition)) {
        fault("Invalid native indexer");
    }
    if (args.size() != definition.indexTypes.size() + (write ? 1 : 0)) {
        fault("Incorrect argument count when calling native indexer");
    }
    if (m_stackIndex >= m_stack.size() || args.size() > m_stack.size() - m_stackIndex - 1) {
        fault("Stack overflow");
    }
    const auto initialStackIndex = m_stackIndex;
    const auto parameterType = [&](const NomadIndex index) {
        return index < definition.indexTypes.size() ? definition.indexTypes[index] : definition.valueType;
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

        callNativeFunction(nativeFunctionId);
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
    const auto* parent = m_runtime->getScriptFunction(function->getParentId());
    if (parent == nullptr || parent != getCurrentFunction()) {
        fault("Format expression does not belong to the current script frame");
    }

    std::vector<TypedValue> values;
    values.reserve(function->getCaptureCount());
    for (const auto& capture : function->getCaptures()) {
        if (capture.type == nullptr || capture.type->isVoid()) {
            fault("Invalid value type for current-frame capture");
        }
        if (capture.source == CaptureSource::Parameter) {
            if (!isIdInRange(capture.id, parent->getParameterCount())) {
                fault("Invalid parameter for current-frame capture");
            }
            values.emplace_back(capture.type, getParameter(capture.id));
        } else if (capture.source == CaptureSource::Variable) {
            if (!isIdInRange(capture.id, parent->getVariableCount())) {
                fault("Invalid variable for current-frame capture");
            }
            values.emplace_back(capture.type, getFunctionVariableValue(capture.id));
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
        const auto captureId = capture.id;
        const auto captureType = capture.type;
        const auto captureSource = capture.source;

        RuntimeValue captureValue;

        if (captureSource == CaptureSource::Parameter) {
            // Capture from the enclosing function's parameter frame
            const auto& value = getEnclosingParameter(captureId);

            captureType->copyValue(value, captureValue);
        } else if (captureSource == CaptureSource::Variable) {
            // Capture from the enclosing function's variable frame
            const auto& value = getFunctionVariableValue(captureId);

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

void VirtualMachine::freeObjectVariable(const NomadIndex variable_index) {
    m_stack[m_variableIndex + variable_index].freeObjectValue();
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
        getFunctionVariableValue(variableId, value);
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
