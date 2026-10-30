// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Callable.hpp>

#include <nomad/script/FunctionResolution.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>

#include <limits>
#include <utility>

namespace nomad {

ScriptCallableBase* Callable::asScriptCallable() {
    return nullptr;
}

const ScriptCallableBase* Callable::asScriptCallable() const {
    return nullptr;
}

NomadString Parameter::getTypeName() const {
    if (type != nullptr) {
        return type->getTypeName();
    }
    return "unknown";
}

NomadIndex Parameter::getStackValueCount() const {
    if (type == nullptr) {
        throw NomadException("Cannot calculate parameter layout: type of '" + name + "' is unresolved");
    }

    switch (type->getRuntime()->getParameterShape(type)) {
    case ParameterShape::EventCallback:
        return 2;
    case ParameterShape::EventDispatch:
        throw NomadException("Parameter '" + name + "' has a call-site-dependent event-dispatch layout");
    default:
        return type->getSize();
    }
}

Parameter defParameter(const NomadString& name, const Type* type, const NomadString& doc) {
    return {
        name,
        type,
        doc
    };
}

TemplateParameter defTemplateParameter(
    const NomadString& name,
    std::variant<const Type*, TypeParameterIndex> type,
    const NomadString& doc
) {
    return {name, std::move(type), doc};
}

Callable::Callable(
    const CallableType type,
    const CallableSource source,
    NomadString name,
    std::vector<Parameter> parameters,
    const Type* returnType,
    const Type* classType,
    NomadString doc
):
    m_name(std::move(name)),
    m_type(type),
    m_source(source),
    m_parameters(std::move(parameters)),
    m_returnType(returnType),
    m_classType(classType),
    m_doc(std::move(doc))
{
}

Callable::~Callable() = default;

CallableId Callable::getId() const {
    return m_id;
}

const NomadString& Callable::getName() const {
    return m_name;
}

CallableType Callable::getType() const {
    return m_type;
}

CallableSource Callable::getSource() const {
    return m_source;
}

const std::vector<Parameter>& Callable::getParameters() const {
    return m_parameters;
}

NomadIndex Callable::getParameterCount() const {
    return m_parameters.size();
}

NomadIndex Callable::getParameterOffset(const NomadId parameterId) const {
    if (isIdOutOfRange(parameterId, m_parameters.size())) {
        throw NomadException("Parameter ID out of range");
    }

    auto offset = NomadIndex{0};
    for (auto index = NomadIndex{0}; index < toNomadIndex(parameterId); ++index) {
        const auto& parameter = m_parameters[index];
        const auto size = parameter.getStackValueCount();
        if (size > std::numeric_limits<NomadIndex>::max() - offset) {
            throw NomadException("Parameter storage size exceeds the maximum slot count at '" + parameter.name + "'");
        }
        offset += size;
    }
    return offset;
}

NomadIndex Callable::getParameterStorageSize() const {
    auto size = NomadIndex{0};
    for (const auto& parameter : m_parameters) {
        const auto parameterSize = parameter.getStackValueCount();
        if (parameterSize > std::numeric_limits<NomadIndex>::max() - size) {
            throw NomadException("Parameter storage size exceeds the maximum slot count at '" + parameter.name + "'");
        }
        size += parameterSize;
    }
    return size;
}

const Type* Callable::getReturnType() const {
    return m_returnType;
}

const Type* Callable::getClassType() const {
    return m_classType;
}

const NomadString& Callable::getDoc() const {
    return m_doc;
}

CallableId Callable::registerWithRuntime(Runtime& runtime) {
    if (m_id != CallableId{}) {
        return CallableId::INVALID;
    }

    m_id = runtime.catalogCallable(this);
    return m_id;
}

void Callable::setId(const CallableId id) {
    m_id = id;
}

void Callable::setReturnType(const Type* returnType) {
    m_returnType = returnType;
}

std::vector<Parameter>& Callable::getEditableParameters() {
    return m_parameters;
}

Function::Function(
    const CallableSource source,
    NomadString name,
    std::vector<Parameter> parameters,
    const Type* returnType,
    NomadString doc
):
    Callable(CallableType::Function, source, std::move(name), std::move(parameters), returnType, nullptr, std::move(doc))
{
}

const NativeFunction* Function::asNativeFunction() const {
    return nullptr;
}

ScriptFunction* Function::asScriptFunction() {
    return nullptr;
}

const ScriptFunction* Function::asScriptFunction() const {
    return nullptr;
}

Method::Method(
    const CallableSource source,
    NomadString name,
    const Type* classType,
    std::vector<Parameter> parameters,
    const Type* returnType,
    NomadString doc
):
    Callable(CallableType::Method, source, std::move(name), std::move(parameters), returnType, classType, std::move(doc))
{
}

const NativeMethod* Method::asNativeMethod() const {
    return nullptr;
}

const ScriptMethod* Method::asScriptMethod() const {
    return nullptr;
}

Constructor::Constructor(
    const CallableSource source,
    NomadString name,
    const Type* classType,
    std::vector<Parameter> parameters,
    const Type* createdType,
    NomadString doc
):
    Callable(CallableType::Constructor, source, std::move(name), std::move(parameters), createdType, classType, std::move(doc))
{
}

const NativeConstructor* Constructor::asNativeConstructor() const {
    return nullptr;
}

const ScriptConstructor* Constructor::asScriptConstructor() const {
    return nullptr;
}

} // namespace nomad
