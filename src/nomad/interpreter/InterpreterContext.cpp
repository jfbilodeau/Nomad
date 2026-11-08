// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/interpreter/InterpreterContext.hpp>

#include <nomad/interpreter/Interpreter.hpp>
#include <nomad/interpreter/InterpreterObjectReceiver.hpp>
#include <nomad/interpreter/InterpreterStorage.hpp>
#include <nomad/interpreter/InterpreterStructReceiver.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/StructType.hpp>
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
        const auto destination = getVirtualMachine()->getLocalStorage(
            m_frameCallable->getVariableOffset(frameId), type->getSize());
        type->assignValue(storage.data(), destination.data(), scratch.get());
        return;
    }

    if (frameId.isValid()) {
        const auto index = toNomadIndex(frameId.id);
        if (index < m_variables.size() && m_variables[index] != nullptr &&
            m_variables[index]->getType() == type) {
            m_variables[index]->assign(storage);
            m_function->setVariableType(frameId, type);
            return;
        }
    }

    auto replacement = std::make_unique<InterpreterStorage>(type, storage);
    if (m_evaluationDepth != 0 && frameId.isValid() &&
        toNomadIndex(frameId.id) < m_variables.size() && m_variables[toNomadIndex(frameId.id)] != nullptr) {
        m_retiredStorage.reserve(m_retiredStorage.size() + 1);
    }

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
    if (m_evaluationDepth != 0 && m_variables[index] != nullptr) {
        m_retiredStorage.push_back(std::move(m_variables[index]));
    }
    m_variables[index] = std::move(replacement);
}

void InterpreterContext::clearVariableValue(const NomadString& name) {
    const auto variableId = m_function->getVariableId(name);
    if (variableId.isValid() && !isFrameVariable(variableId) &&
        toNomadIndex(variableId.id) < m_variables.size()) {
        auto& storage = m_variables[toNomadIndex(variableId.id)];
        if (m_evaluationDepth != 0 && storage != nullptr) {
            m_retiredStorage.push_back(std::move(storage));
        } else {
            storage.reset();
        }
    }
}

InterpreterEvaluationGuard::InterpreterEvaluationGuard(InterpreterContext& context) noexcept:
    m_context(context)
{}

InterpreterEvaluationGuard::~InterpreterEvaluationGuard() {
    m_context.endEvaluation();
}

InterpreterEvaluationGuard InterpreterContext::beginEvaluation() {
    ++m_evaluationDepth;
    return InterpreterEvaluationGuard(*this);
}

void InterpreterContext::endEvaluation() {
    if (m_evaluationDepth == 0) {
        throw NomadBug("Interpreter evaluation exits must match their entries");
    }
    if (--m_evaluationDepth == 0) {
        m_retiredStorage.clear();
    }
}

void InterpreterContext::setVariableProperty(
    const NomadString& name,
    const PropertyId propertyId,
    const InterpreterValue& value
) {
    const auto* type = getVariableType(name);
    if (type == nullptr) {
        throw std::invalid_argument("Stored property assignment requires an assigned variable");
    }
    const InterpreterValue current(type, getVariableStorage(name));
    const auto replacement = m_interpreter.setProperty(current, propertyId, value);
    setVariable(name, type, replacement.getStorage());
}

void InterpreterContext::setVariableProperty(
    const NomadString& name,
    const NomadStringView propertyPath,
    const InterpreterValue& value
) {
    const auto* type = getVariableType(name);
    if (type == nullptr) {
        throw std::invalid_argument("Stored property assignment requires an assigned variable");
    }
    const InterpreterValue current(type, getVariableStorage(name));
    const auto replacement = m_interpreter.setProperty(current, propertyPath, value);
    setVariable(name, type, replacement.getStorage());
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
        return getVirtualMachine()->getLocalStorage(m_frameCallable->getVariableOffset(variableId),
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

std::span<RuntimeValue> InterpreterContext::getMutableVariableStorageById(const VariableId variableId) {
    if (isFrameVariable(variableId)) {
        validateFrame();
        return getVirtualMachine()->getLocalStorage(m_frameCallable->getVariableOffset(variableId),
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
    return variable != nullptr ? variable->getMutableStorage() : std::span<RuntimeValue>{};
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
                variable->getType()->toString(variable->getStorage().front())
        });
    }

    std::ranges::sort(variables, {}, &InterpreterVariableInfo::name);

    return variables;
}

InterpreterContext::InterpreterContext(const Interpreter& interpreter):
    m_interpreter(interpreter),
    m_runtime(interpreter.getVirtualMachine().getRuntime()),
    m_function(std::make_unique<ScriptFunction>(
        CallableId{}, CONSOLE_SOURCE_NAME, CONSOLE_SOURCE_NAME, ""
    ))
{
}

InterpreterContext::~InterpreterContext() = default;

InterpreterContext::InterpreterContext(
    const Interpreter& interpreter,
    const ScriptCallableBase& function
):
    InterpreterContext(interpreter)
{
    auto& virtualMachine = interpreter.getVirtualMachine();
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
    if (const auto* type = function.getClassType()) {
        if (const auto* owner = type->asStructType()) {
            RuntimeValue* storage = nullptr;
            const auto layout = function.getCallStorageLayout();
            if (layout.receiverOffset.has_value()) {
                storage = virtualMachine.getEnclosingParameterStorage(*layout.receiverOffset, 1)[0].getStorageAddress();
            }
            m_receiver = interpreter.createStructReceiver(*owner, storage);
        } else {
            m_receiver = interpreter.createObjectReceiver(*type->asClassType(), virtualMachine.getScriptReceiver());
        }
    }
}

bool InterpreterContext::isFrameBacked() const {
    return m_frameCallable != nullptr;
}

bool InterpreterContext::isFrameVariable(const VariableId variableId) const {
    return isIdInRange(variableId.id, m_frameVariableCount);
}

void InterpreterContext::validateFrame() const {
    if (isFrameBacked() &&
        (getVirtualMachine()->getCurrentCallable() != m_frameCallable ||
         getVirtualMachine()->m_parameterIndex != m_parameterIndex ||
         getVirtualMachine()->m_variableIndex != m_variableIndex)) {
        throw NomadBug("Interpreter context is not bound to the active script frame");
    }
}

const ScriptFunction* InterpreterContext::getFunction() const {
    return dynamic_cast<const ScriptFunction*>(m_function.get());
}

ScriptCallableBase* InterpreterContext::getCallable() {
    validateFrame();
    return m_function.get();
}

const ScriptCallableBase* InterpreterContext::getCallable() const {
    validateFrame();
    return m_function.get();
}

Runtime* InterpreterContext::getRuntime() const {
    return m_runtime;
}

const Interpreter& InterpreterContext::getInterpreter() const {
    return m_interpreter;
}

VirtualMachine* InterpreterContext::getVirtualMachine() const {
    return &m_interpreter.getVirtualMachine();
}

void InterpreterContext::setResult(const Type* type, const RuntimeValue& value) {
    setResult(type, getScalarStorage(type, value));
}

void InterpreterContext::setResult(const Type* type, const std::span<const RuntimeValue> storage) {
    InterpreterValue replacement(type, storage);
    m_result = std::move(replacement);
}

void InterpreterContext::setResult(const InterpreterValue& value) {
    InterpreterValue replacement(value);
    m_result = std::move(replacement);
}

void InterpreterContext::setResult(InterpreterValue&& value) {
    m_result = std::move(value);
}

void InterpreterContext::clearResult() {
    m_result.reset();
}

void InterpreterContext::setVoidResult() {
    setResult(getRuntime()->getVoidType(), RuntimeValue{});
}

const InterpreterValue* InterpreterContext::getResult() const {
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
    return getVirtualMachine()->getEnclosingParameterStorage(m_frameCallable->getParameterOffset(parameterId),
        m_frameCallable->getParameterType(parameterId)->getSize());
}

std::span<RuntimeValue> InterpreterContext::getMutableParameterStorage(const NomadId parameterId) {
    if (!isFrameBacked() || !isIdInRange(parameterId, m_frameCallable->getParameterCount())) {
        throw NomadBug("Invalid frame-backed parameter access");
    }
    validateFrame();
    return getVirtualMachine()->getEnclosingParameterStorage(m_frameCallable->getParameterOffset(parameterId),
        m_frameCallable->getParameterType(parameterId)->getSize());
}

std::span<const RuntimeValue> InterpreterContext::getReceiverStorage() const {
    if (!isFrameBacked() || m_frameCallable->getClassType() == nullptr ||
        !m_frameCallable->getClassType()->isStruct()) {
        throw NomadBug("Struct receiver storage requires an active struct callable frame");
    }
    validateFrame();
    const auto size = m_frameCallable->getClassType()->getSize();
    if (size == 0) {
        return {};
    }
    const auto layout = m_frameCallable->getCallStorageLayout();
    if (!layout.receiverOffset.has_value()) {
        throw NomadBug("Nonempty struct callable has no receiver storage pointer");
    }
    const auto pointer = getVirtualMachine()->getEnclosingParameterStorage(*layout.receiverOffset, 1);
    return {pointer[0].getStorageAddress(), size};
}

std::span<RuntimeValue> InterpreterContext::getMutableReceiverStorage() {
    if (!isFrameBacked() || m_frameCallable->getClassType() == nullptr ||
        !m_frameCallable->getClassType()->isStruct()) {
        throw NomadBug("Struct receiver storage requires an active struct callable frame");
    }
    validateFrame();
    const auto size = m_frameCallable->getClassType()->getSize();
    if (size == 0) {
        return {};
    }
    const auto layout = m_frameCallable->getCallStorageLayout();
    if (!layout.receiverOffset.has_value()) {
        throw NomadBug("Nonempty struct callable has no receiver storage pointer");
    }
    const auto pointer = getVirtualMachine()->getEnclosingParameterStorage(*layout.receiverOffset, 1);
    return {pointer[0].getStorageAddress(), size};
}

InterpreterReceiver* InterpreterContext::getReceiver() {
    validateFrame();
    return m_receiver.get();
}

const InterpreterReceiver* InterpreterContext::getReceiver() const {
    validateFrame();
    return m_receiver.get();
}

void InterpreterContext::setReceiverProperty(
    const PropertyId propertyId,
    const InterpreterValue& value
) {
    auto* receiver = getReceiver();
    if (receiver == nullptr) {
        throw NomadBug("Receiver property assignment requires an active composite frame");
    }
    receiver->setProperty(propertyId, value);
}

void InterpreterContext::setReceiverProperty(
    const NomadStringView propertyPath,
    const InterpreterValue& value
) {
    auto* receiver = getReceiver();
    if (receiver == nullptr) {
        throw NomadBug("Receiver property assignment requires an active composite frame");
    }
    receiver->setProperty(propertyPath, value);
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
    const auto destination = getVirtualMachine()->getEnclosingParameterStorage(
        m_frameCallable->getParameterOffset(parameterId), type->getSize());
    type->assignValue(storage.data(), destination.data(), scratch.get());
}

} // namespace nomad
