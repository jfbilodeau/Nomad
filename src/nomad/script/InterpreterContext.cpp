// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/InterpreterContext.hpp>

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

} // namespace

TypedValue::TypedValue(const Type* type, const RuntimeValue& value):
    m_type(type)
{
    if (m_type == nullptr) {
        throw std::invalid_argument("VirtualMachine value type cannot be null");
    }

    if (m_type->isVoid()) {
        return;
    }

    try {
        m_type->copyValue(value, m_value);
    } catch (...) {
        m_type->freeValue(m_value);
        m_type = nullptr;
        throw;
    }
}

TypedValue::TypedValue(const TypedValue& other):
    TypedValue(other.m_type, other.m_value)
{
}

TypedValue::TypedValue(TypedValue&& other) noexcept:
    m_type(other.m_type),
    m_value(other.m_value)
{
    other.m_type = nullptr;
}

TypedValue& TypedValue::operator=(const TypedValue& other) {
    if (this != &other) {
        TypedValue copy(other);
        std::swap(m_type, copy.m_type);
        std::swap(m_value, copy.m_value);
    }

    return *this;
}

TypedValue& TypedValue::operator=(TypedValue&& other) noexcept {
    if (this != &other) {
        release();
        m_type = other.m_type;
        m_value = other.m_value;
        other.m_type = nullptr;
    }

    return *this;
}

TypedValue::~TypedValue() {
    release();
}

const Type* TypedValue::getType() const {
    return m_type;
}

const RuntimeValue& TypedValue::getValue() const {
    return m_value;
}

void TypedValue::release() noexcept {
    if (m_type != nullptr && !m_type->isVoid()) {
        m_type->freeValue(m_value);
    }
    m_type = nullptr;
}

void InterpreterContext::setVariable(
    const NomadString& name,
    const Type* type,
    const RuntimeValue& value
) {
    if (name.empty() || name.find('.') != NomadString::npos) {
        throw std::invalid_argument("Interpreter variable name must be unqualified and non-empty");
    }
    if (type == nullptr || type->isVoid()) {
        throw std::invalid_argument("Interpreter variables must have a non-void type");
    }
    if (m_function->getParameterId(name) != NOMAD_INVALID_ID) {
        throw std::invalid_argument("Parameters are read-only");
    }
    const auto frameId = m_function->getVariableId(name);
    if (isFrameVariable(frameId)) {
        validateFrame();
        if (m_function->getVariableType(frameId) != type) {
            throw std::invalid_argument("Frame variables cannot change type");
        }
        RuntimeValue replacement;
        try {
            type->copyValue(value, replacement);
        } catch (...) {
            type->freeValue(replacement);
            throw;
        }
        auto& previous = m_virtualMachine.m_stack[m_variableIndex + frameId];
        type->freeValue(previous);
        previous = replacement;
        return;
    }

    auto replacement = std::make_unique<TypedValue>(type, value);

    auto variableId = m_function->getVariableId(name);
    if (variableId == NOMAD_INVALID_ID) {
        variableId = m_function->registerVariable(name, type);
    } else {
        // Unlike compiled functions, console variables may change type on reassignment.
        m_function->setVariableType(variableId, type);
    }

    const auto index = toNomadIndex(variableId);
    if (index >= m_variables.size()) {
        m_variables.resize(index + 1);
    }
    m_variables[index] = std::move(replacement);
}

void InterpreterContext::clearVariableValue(const NomadString& name) {
    const auto variableId = m_function->getVariableId(name);
    if (variableId != NOMAD_INVALID_ID && !isFrameVariable(variableId) &&
        toNomadIndex(variableId) < m_variables.size()) {
        m_variables[toNomadIndex(variableId)].reset();
    }
}

const Type* InterpreterContext::getVariableType(const NomadString& name) const {
    return getVariableTypeById(getFunction()->getVariableId(name));
}

const Type* InterpreterContext::getVariableTypeById(const NomadId variableId) const {
    if (isFrameVariable(variableId)) {
        return m_function->getVariableType(variableId);
    }
    if (variableId == NOMAD_INVALID_ID) {
        return nullptr;
    }

    const auto index = toNomadIndex(variableId);
    if (index >= m_variables.size()) {
        return nullptr;
    }

    const auto& variable = m_variables[index];
    return variable != nullptr ? variable->getType() : nullptr;
}

const RuntimeValue* InterpreterContext::getVariableValue(const NomadString& name) const {
    return getVariableValueById(getFunction()->getVariableId(name));
}

const RuntimeValue* InterpreterContext::getVariableValueById(const NomadId variableId) const {
    if (isFrameVariable(variableId)) {
        validateFrame();
        return &m_virtualMachine.m_stack[m_variableIndex + variableId];
    }
    if (variableId == NOMAD_INVALID_ID) {
        return nullptr;
    }

    const auto index = toNomadIndex(variableId);
    if (index >= m_variables.size()) {
        return nullptr;
    }

    const auto& variable = m_variables[index];
    return variable != nullptr ? &variable->getValue() : nullptr;
}

std::vector<InterpreterVariableInfo> InterpreterContext::listVariables() const {
    std::vector<InterpreterVariableInfo> variables;
    variables.reserve(m_variables.size());

    for (NomadIndex variableIndex = 0; variableIndex < m_variables.size(); ++variableIndex) {
        const auto& variable = m_variables[variableIndex];
        if (variable == nullptr) {
            continue;
        }

        const auto variableId = toNomadId(variableIndex);
        variables.push_back({
            m_function->getVariableName(variableId),
            variable->getType(),
            variable->getType()->toString(variable->getValue())
        });
    }

    std::ranges::sort(variables, {}, &InterpreterVariableInfo::name);

    return variables;
}

InterpreterContext::InterpreterContext(VirtualMachine& virtualMachine):
    m_runtime(virtualMachine.getRuntime()),
    m_function(std::make_unique<ScriptFunction>(NOMAD_INVALID_ID, CONSOLE_SOURCE_NAME, CONSOLE_SOURCE_NAME, "")),
    m_virtualMachine(virtualMachine)
{
}

InterpreterContext::~InterpreterContext() = default;

InterpreterContext::InterpreterContext(VirtualMachine& virtualMachine, ScriptFunction& function):
    InterpreterContext(virtualMachine)
{
    if (virtualMachine.getCurrentFunction() != &function || function.getFunctionStart() == NOMAD_INVALID_INDEX) {
        throw NomadBug("Frame interpretation requires an active compiled script function");
    }
    m_frameFunction = &function;
    m_parameterIndex = virtualMachine.m_parameterIndex;
    m_variableIndex = virtualMachine.m_variableIndex;
    m_frameVariableCount = function.getVariableCount();
    m_function = std::make_unique<ScriptFunction>(function.getId(), function.getName(), function.getPath(), "");
    for (NomadIndex index = 0; index < function.getParameterCount(); ++index) {
        const auto id = toNomadId(index);
        m_function->addParameter(
            function.getParameterName(id),
            function.getParameterType(id),
            function.getParameters()[index].doc
        );
    }
    for (NomadIndex index = 0; index < m_frameVariableCount; ++index) {
        const auto id = toNomadId(index);
        m_function->registerVariable(function.getVariableName(id), function.getVariableType(id));
    }
    m_function->copyVariableScopesFrom(function);
    m_variables.resize(m_frameVariableCount);
}

bool InterpreterContext::isFrameBacked() const {
    return m_frameFunction != nullptr;
}

bool InterpreterContext::isFrameVariable(const NomadId variableId) const {
    return isIdInRange(variableId, m_frameVariableCount);
}

void InterpreterContext::validateFrame() const {
    if (isFrameBacked() &&
        (m_virtualMachine.getCurrentFunction() != m_frameFunction ||
         m_virtualMachine.m_parameterIndex != m_parameterIndex ||
         m_virtualMachine.m_variableIndex != m_variableIndex)) {
        throw NomadBug("Interpreter context is not bound to the active script frame");
    }
}

const ScriptFunction* InterpreterContext::getFunction() const {
    return m_function.get();
}

Runtime* InterpreterContext::getRuntime() const {
    return m_runtime;
}

VirtualMachine* InterpreterContext::getVirtualMachine() const {
    return &m_virtualMachine;
}

void InterpreterContext::setResult(const Type* type, const RuntimeValue& value) {
    m_result.emplace(type, value);
}

void InterpreterContext::setResult(const TypedValue& value) {
    m_result.emplace(value);
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
    if (!isFrameBacked() || !isIdInRange(parameterId, m_frameFunction->getParameterCount())) {
        throw NomadBug("Invalid frame-backed parameter access");
    }
    validateFrame();
    setResult(m_function->getParameterType(parameterId), m_virtualMachine.m_stack[m_parameterIndex - parameterId]);
}

void InterpreterContext::evaluateFunctionCall(const NomadId functionId, const std::vector<RuntimeValue>& arguments) {
    const auto* function = getRuntime()->getScriptFunction(functionId);
    if (function == nullptr || function->getReturnType() == nullptr) {
        throw NomadBug("Invalid interpreted function call");
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
            type->freeValue(machine->getResult());
            throw;
        }
        type->freeValue(machine->getResult());
    } catch (...) {
        machine->setResult(savedResult);
        machine->setIntermediate(savedIntermediate);
        throw;
    }
    machine->setResult(savedResult);
    machine->setIntermediate(savedIntermediate);
}

void InterpreterContext::evaluateNativeFunctionCall(const NomadId functionId, const std::vector<RuntimeValue>& arguments) {
    const auto* function = getRuntime()->getNativeFunction(functionId);
    if (function == nullptr || function->getReturnType() == nullptr) {
        throw NomadBug("Invalid interpreted native function call");
    }
    auto* machine = getVirtualMachine();
    const auto savedResult = machine->getResult();
    const auto savedIntermediate = machine->getIntermediate();
    machine->setResult(RuntimeValue{});
    try {
        machine->runNativeFunction(functionId, arguments);
        setResult(function->getReturnType(), machine->getResult());
        function->getReturnType()->freeValue(machine->getResult());
    } catch (...) {
        function->getReturnType()->freeValue(machine->getResult());
        machine->setResult(savedResult);
        machine->setIntermediate(savedIntermediate);
        throw;
    }
    machine->setResult(savedResult);
    machine->setIntermediate(savedIntermediate);
}

} // namespace nomad
