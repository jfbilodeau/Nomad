// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/InterpreterContext.hpp>

#include <nomad/script/InterpreterCallFrame.hpp>
#include <nomad/script/NativeCallable.hpp>
#include <nomad/script/NativeFunction.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <algorithm>
#include <stdexcept>

namespace nomad {

namespace {

// Name and path of the console function; identifies console input in diagnostics.
constexpr auto CONSOLE_SOURCE_NAME = "<console>";

std::span<const RuntimeValue> getScalarStorage(const Type* type, const RuntimeValue& value) {
    if (type != nullptr && type->isStruct()) {
        throw std::invalid_argument("Struct values require complete storage views");
    }
    return type != nullptr && type->isVoid() ? std::span<const RuntimeValue>{} :
        std::span<const RuntimeValue>{&value, 1};
}

void validateValueStorage(const Type* type, const std::span<const RuntimeValue> storage) {
    if (type == nullptr) {
        throw std::invalid_argument("Interpreter value type cannot be null");
    }
    if (storage.size() != type->getSize()) {
        throw std::invalid_argument("Interpreter value storage must match its type's physical size");
    }
}

} // namespace

void InterpreterContext::setVariable(
    const NomadString& name,
    const Type* type,
    const RuntimeValue& value
) {
    setVariable(name, type, getScalarStorage(type, value));
}

void InterpreterContext::setVariable(
    const NomadString& name,
    const Type* type,
    const std::span<const RuntimeValue> storage
) {
    if (name.empty() || name.find('.') != NomadString::npos) {
        throw std::invalid_argument("Interpreter variable name must be unqualified and non-empty");
    }
    if (type == nullptr || type->isVoid()) {
        throw std::invalid_argument("Interpreter variables must have a non-void type");
    }
    validateValueStorage(type, storage);
    const auto parameterId = m_function->getParameterId(name);
    if (parameterId != NOMAD_INVALID_ID) {
        if (m_function->getParameterType(parameterId) != type) {
            throw std::invalid_argument("Frame parameters cannot change type");
        }
        setParameter(parameterId, storage);
        return;
    }
    const auto frameId = m_function->getVariableId(name);
    if (isFrameVariable(frameId)) {
        validateFrame();
        if (m_function->getVariableType(frameId) != type) {
            throw std::invalid_argument("Frame variables cannot change type");
        }
        auto scratch = type->getSize() > 1 ? std::make_unique<RuntimeValue[]>(type->getSize()) : nullptr;
        const auto destination = m_virtualMachine.getLocalStorage(
            m_frameCallable->getVariableOffset(frameId), type->getSize());
        type->assignValue(storage.data(), destination.data(), scratch.get());
        return;
    }

    auto replacement = std::make_unique<TypedValue>(type, storage);

    auto variableId = m_function->getVariableId(name);
    if (variableId.isInvalid()) {
        variableId = m_function->registerVariable(name, type);
    } else {
        // Unlike compiled functions, console variables may change type on reassignment.
        m_function->setVariableType(variableId, type);
    }

    const auto index = toNomadIndex(variableId.id);
    if (index >= m_variables.size()) {
        m_variables.resize(index + 1);
    }
    m_variables[index] = std::move(replacement);
}

void InterpreterContext::clearVariableValue(const NomadString& name) {
    const auto variableId = m_function->getVariableId(name);
    if (variableId.isValid() && !isFrameVariable(variableId) &&
        toNomadIndex(variableId.id) < m_variables.size()) {
        m_variables[toNomadIndex(variableId.id)].reset();
    }
}

const Type* InterpreterContext::getVariableType(const NomadString& name) const {
    const auto parameterId = m_function->getParameterId(name);
    if (parameterId != NOMAD_INVALID_ID) {
        return m_function->getParameterType(parameterId);
    }
    return getVariableTypeById(getCallable()->getVariableId(name));
}

const Type* InterpreterContext::getVariableTypeById(const VariableId variableId) const {
    if (isFrameVariable(variableId)) {
        return m_function->getVariableType(variableId);
    }
    if (variableId.isInvalid()) {
        return nullptr;
    }

    const auto index = toNomadIndex(variableId.id);
    if (index >= m_variables.size()) {
        return nullptr;
    }

    const auto& variable = m_variables[index];
    return variable != nullptr ? variable->getType() : nullptr;
}

const RuntimeValue* InterpreterContext::getVariableValue(const NomadString& name) const {
    const auto* type = getVariableType(name);
    if (type != nullptr && type->isStruct()) {
        throw std::invalid_argument("Struct values require complete storage views");
    }
    return getVariableStorage(name).data();
}

std::span<const RuntimeValue> InterpreterContext::getVariableStorage(const NomadString& name) const {
    const auto parameterId = m_function->getParameterId(name);
    if (parameterId != NOMAD_INVALID_ID) {
        return getParameterStorage(parameterId);
    }
    return getVariableStorageById(getCallable()->getVariableId(name));
}

const RuntimeValue* InterpreterContext::getVariableValueById(const VariableId variableId) const {
    const auto* type = getVariableTypeById(variableId);
    if (type != nullptr && type->isStruct()) {
        throw std::invalid_argument("Struct values require complete storage views");
    }
    return getVariableStorageById(variableId).data();
}

std::span<const RuntimeValue> InterpreterContext::getVariableStorageById(const VariableId variableId) const {
    if (isFrameVariable(variableId)) {
        validateFrame();
        return m_virtualMachine.getLocalStorage(m_frameCallable->getVariableOffset(variableId),
            m_frameCallable->getVariableType(variableId)->getSize());
    }
    if (variableId.isInvalid()) {
        return {};
    }

    const auto index = toNomadIndex(variableId.id);
    if (index >= m_variables.size()) {
        return {};
    }

    const auto& variable = m_variables[index];
    return variable != nullptr ? variable->getStorage() : std::span<const RuntimeValue>{};
}

std::vector<InterpreterVariableInfo> InterpreterContext::listVariables() const {
    std::vector<InterpreterVariableInfo> variables;
    variables.reserve(m_variables.size());

    for (NomadIndex variableIndex = 0; variableIndex < m_variables.size(); ++variableIndex) {
        const auto& variable = m_variables[variableIndex];
        if (variable == nullptr) {
            continue;
        }

        const auto variableId = VariableId{toNomadId(variableIndex)};
        variables.push_back({
            m_function->getVariableName(variableId),
            variable->getType(),
            variable->getType()->isStruct() ? variable->getType()->getTypeName() :
                variable->getType()->toString(variable->getValue())
        });
    }

    std::ranges::sort(variables, {}, &InterpreterVariableInfo::name);

    return variables;
}

InterpreterContext::InterpreterContext(VirtualMachine& virtualMachine):
    m_runtime(virtualMachine.getRuntime()),
    m_function(std::make_unique<ScriptFunction>(
        CallableId{}, CONSOLE_SOURCE_NAME, CONSOLE_SOURCE_NAME, ""
    )),
    m_virtualMachine(virtualMachine)
{
}

InterpreterContext::~InterpreterContext() = default;

InterpreterContext::InterpreterContext(VirtualMachine& virtualMachine, const ScriptCallableBase& function):
    InterpreterContext(virtualMachine)
{
    if (virtualMachine.getCurrentCallable() != &function || function.getFunctionStart() == NOMAD_INVALID_INDEX) {
        throw NomadBug("Frame interpretation requires an active compiled script callable");
    }
    m_frameCallable = &function;
    m_parameterIndex = virtualMachine.m_parameterIndex;
    m_variableIndex = virtualMachine.m_variableIndex;
    m_frameVariableCount = function.getVariableCount();
    if (m_runtime->getConstructor(function.getId()) != nullptr) {
        m_function = std::make_unique<ScriptConstructor>(
            function.getClassType(), std::vector<Parameter>{}, function.getPath(), ""
        );
    } else if (function.getClassType() != nullptr) {
        m_function = std::make_unique<ScriptMethod>(
            function.getName(), function.getClassType(), std::vector<Parameter>{},
            function.getReturnType(), function.getPath(), ""
        );
    } else {
        m_function = std::make_unique<ScriptFunction>(function.getId(), function.getName(), function.getPath(), "");
    }
    m_function->setHub(function.getHub());
    m_function->setParentId(function.getParentId());
    for (NomadIndex index = 0; index < m_frameVariableCount; ++index) {
        const auto id = VariableId{toNomadId(index)};
        m_function->registerVariable(function.getVariableName(id), function.getVariableType(id));
    }
    for (NomadIndex index = 0; index < function.getParameterCount(); ++index) {
        const auto id = toNomadId(index);
        m_function->addParameter(
            function.getParameterName(id),
            function.getParameterType(id),
            function.getParameters()[index].doc
        );
    }
    m_variables.resize(m_frameVariableCount);
}

bool InterpreterContext::isFrameBacked() const {
    return m_frameCallable != nullptr;
}

bool InterpreterContext::isFrameVariable(const VariableId variableId) const {
    return isIdInRange(variableId.id, m_frameVariableCount);
}

void InterpreterContext::validateFrame() const {
    if (isFrameBacked() &&
        (m_virtualMachine.getCurrentCallable() != m_frameCallable ||
         m_virtualMachine.m_parameterIndex != m_parameterIndex ||
         m_virtualMachine.m_variableIndex != m_variableIndex)) {
        throw NomadBug("Interpreter context is not bound to the active script frame");
    }
}

const ScriptFunction* InterpreterContext::getFunction() const {
    return dynamic_cast<const ScriptFunction*>(m_function.get());
}

const ScriptCallableBase* InterpreterContext::getCallable() const {
    return m_function.get();
}

Runtime* InterpreterContext::getRuntime() const {
    return m_runtime;
}

VirtualMachine* InterpreterContext::getVirtualMachine() const {
    return &m_virtualMachine;
}

void InterpreterContext::setResult(const Type* type, const RuntimeValue& value) {
    setResult(type, getScalarStorage(type, value));
}

void InterpreterContext::setResult(const Type* type, const std::span<const RuntimeValue> storage) {
    TypedValue replacement(type, storage);
    m_result = std::move(replacement);
}

void InterpreterContext::setResult(const TypedValue& value) {
    TypedValue replacement(value);
    m_result = std::move(replacement);
}

void InterpreterContext::clearResult() {
    m_result.reset();
}

void InterpreterContext::setVoidResult() {
    setResult(getRuntime()->getVoidType(), RuntimeValue{});
}

const TypedValue* InterpreterContext::getResult() const {
    return m_result ? &*m_result : nullptr;
}

void InterpreterContext::setError(const NomadString& error) {
    addDiagnostic({DiagnosticSeverity::Error, error, m_function->getPath(),
        NOMAD_INVALID_INDEX, NOMAD_INVALID_INDEX});
}

void InterpreterContext::clearError() {
    m_diagnostics.clear();
}

void InterpreterContext::addDiagnostic(const Diagnostic& diagnostic) {
    m_diagnostics.push_back(diagnostic);
}

std::span<const Diagnostic> InterpreterContext::getDiagnostics() const {
    return m_diagnostics;
}

NomadBoolean InterpreterContext::hasError() const {
    return std::ranges::any_of(m_diagnostics, [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    });
}

NomadBoolean InterpreterContext::hasWarning() const {
    return std::ranges::any_of(m_diagnostics, [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Warning;
    });
}

const NomadString* InterpreterContext::getError() const {
    const auto error = std::ranges::find_if(m_diagnostics, [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    });
    return error != m_diagnostics.end() ? &error->message : nullptr;
}

void InterpreterContext::setParameterResult(const NomadId parameterId) {
    if (!isFrameBacked() || !isIdInRange(parameterId, m_frameCallable->getParameterCount())) {
        throw NomadBug("Invalid frame-backed parameter access");
    }
    validateFrame();
    setResult(m_function->getParameterType(parameterId), getParameterStorage(parameterId));
}

void InterpreterContext::setParameter(const NomadId parameterId, const RuntimeValue& value) {
    if (!isFrameBacked() || !isIdInRange(parameterId, m_frameCallable->getParameterCount())) {
        throw NomadBug("Invalid frame-backed parameter assignment");
    }
    setParameter(parameterId, getScalarStorage(m_function->getParameterType(parameterId), value));
}

std::span<const RuntimeValue> InterpreterContext::getParameterStorage(const NomadId parameterId) const {
    if (!isFrameBacked() || !isIdInRange(parameterId, m_frameCallable->getParameterCount())) {
        throw NomadBug("Invalid frame-backed parameter access");
    }
    validateFrame();
    return m_virtualMachine.getEnclosingParameterStorage(m_frameCallable->getParameterOffset(parameterId),
        m_frameCallable->getParameterType(parameterId)->getSize());
}

void InterpreterContext::setParameter(
    const NomadId parameterId,
    const std::span<const RuntimeValue> storage
) {
    if (!isFrameBacked() || !isIdInRange(parameterId, m_frameCallable->getParameterCount())) {
        throw NomadBug("Invalid frame-backed parameter assignment");
    }
    validateFrame();
    const auto* type = m_frameCallable->getParameterType(parameterId);
    validateValueStorage(type, storage);
    auto scratch = type->getSize() > 1 ? std::make_unique<RuntimeValue[]>(type->getSize()) : nullptr;
    const auto destination = m_virtualMachine.getEnclosingParameterStorage(
        m_frameCallable->getParameterOffset(parameterId), type->getSize());
    type->assignValue(storage.data(), destination.data(), scratch.get());
}

void InterpreterContext::evaluateFunctionCall(const CallableId functionId, const std::vector<RuntimeValue>& arguments) {
    const auto* function = getRuntime()->getScriptFunction(functionId);
    if (function == nullptr || function->getReturnType() == nullptr) {
        throw NomadBug("Invalid interpreted function call");
    }
    if (function->getReturnType()->isStruct()) {
        setError("Struct-returning function calls are not supported by the interpreter");
        return;
    }
    const auto* type = function->getReturnType();
    auto* machine = getVirtualMachine();
    const auto savedResult = machine->getResult();
    const auto savedIntermediate = machine->getIntermediate();
    machine->setResult(RuntimeValue{});
    try {
        machine->run(function, arguments);
        try {
            setResult(type, machine->getResult());
        } catch (...) {
            type->freeValue(&machine->getResult());
            throw;
        }
        type->freeValue(&machine->getResult());
    } catch (...) {
        machine->setResult(savedResult);
        machine->setIntermediate(savedIntermediate);
        throw;
    }
    machine->setResult(savedResult);
    machine->setIntermediate(savedIntermediate);
}

void InterpreterContext::evaluateNativeFunctionCall(const CallableId functionId, const std::vector<RuntimeValue>& arguments) {
    const auto* function = getRuntime()->getNativeFunction(functionId);
    if (function == nullptr || function->getReturnType() == nullptr) {
        throw NomadBug("Invalid interpreted native function call");
    }
    if (function->getReturnType()->isStruct()) {
        setError("Struct-returning function calls are not supported by the interpreter");
        return;
    }
    auto* machine = getVirtualMachine();
    const auto savedResult = machine->getResult();
    const auto savedIntermediate = machine->getIntermediate();
    machine->setResult(RuntimeValue{});
    try {
        machine->runNativeFunction(functionId, arguments);
        setResult(function->getReturnType(), machine->getResult());
        function->getReturnType()->freeValue(&machine->getResult());
    } catch (...) {
        function->getReturnType()->freeValue(&machine->getResult());
        machine->setResult(savedResult);
        machine->setIntermediate(savedIntermediate);
        throw;
    }
    machine->setResult(savedResult);
    machine->setIntermediate(savedIntermediate);
}

void InterpreterContext::evaluateFunctionCallWithStorage(
    const CallableId functionId,
    const std::span<const TypedValue> arguments
) {
    const auto* function = getRuntime()->getScriptFunction(functionId);
    if (function == nullptr || function->getReturnType() == nullptr) {
        throw NomadBug("Invalid interpreted function call");
    }
    if (!function->getReturnType()->isStruct() &&
        std::ranges::none_of(arguments, [](const auto& argument) { return argument.getType()->isStruct(); })) {
        std::vector<RuntimeValue> values;
        values.reserve(arguments.size());
        for (const auto& argument : arguments) {
            values.push_back(argument.getValue());
        }
        evaluateFunctionCall(functionId, values);
        return;
    }
    InterpreterCallFrame frame(m_virtualMachine, *function, arguments, function->getReturnType());
    m_virtualMachine.runPreparedFunction(function);
    setResult(function->getReturnType(), frame.getResultStorage());
}

void InterpreterContext::evaluateNativeFunctionCallWithStorage(
    const CallableId functionId,
    const std::span<const TypedValue> arguments
) {
    const auto* function = getRuntime()->getNativeFunction(functionId);
    if (function == nullptr || function->getReturnType() == nullptr) {
        throw NomadBug("Invalid interpreted native function call");
    }
    InterpreterCallFrame frame(m_virtualMachine, *function, arguments, function->getReturnType());
    m_virtualMachine.callNativeFunction(functionId);
    setResult(function->getReturnType(), frame.getResultStorage());
}

void InterpreterContext::evaluateConstructorCall(
    const CallableId constructorId,
    const std::span<const TypedValue> arguments
) {
    const auto* constructor = getRuntime()->getConstructor(constructorId);
    if (constructor == nullptr || constructor->getReturnType() == nullptr) {
        throw NomadBug("Invalid interpreted constructor call");
    }
    const auto* type = constructor->getConstructedType();
    InterpreterCallFrame frame(m_virtualMachine, *constructor, arguments, type);
    if (type->isStruct()) {
        if (constructor->asNativeConstructor() != nullptr) {
            m_virtualMachine.createNativeStruct(frame.getDestination(), constructorId);
        } else {
            m_virtualMachine.createScriptStruct(frame.getDestination(), constructorId);
        }
        if (!hasError()) {
            setResult(type, frame.getResultStorage());
        }
        return;
    }
    auto* created = constructor->asNativeConstructor() != nullptr
        ? m_virtualMachine.createNativeObject(constructorId)
        : m_virtualMachine.createScriptObject(constructorId);
    const auto release = [](NomadObject* object) { object->releaseRef(); };
    const std::unique_ptr<NomadObject, decltype(release)> object(created, release);
    if (!hasError()) {
        RuntimeValue value;
        value.setObjectValue(object.get());
        setResult(type, value);
    }
}

} // namespace nomad
