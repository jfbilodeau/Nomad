// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Callable.hpp>

#include <utility>

namespace nomad {

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
    const Type* objectType,
    NomadString doc
):
    m_name(std::move(name)),
    m_type(type),
    m_source(source),
    m_parameters(std::move(parameters)),
    m_returnType(returnType),
    m_objectType(objectType),
    m_doc(std::move(doc))
{
}

Callable::~Callable() = default;

NomadId Callable::getId() const {
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

const Type* Callable::getReturnType() const {
    return m_returnType;
}

const Type* Callable::getObjectType() const {
    return m_objectType;
}

const NomadString& Callable::getDoc() const {
    return m_doc;
}

void Callable::setId(const NomadId id) {
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
    const Type* objectType,
    std::vector<Parameter> parameters,
    const Type* returnType,
    NomadString doc
):
    Callable(CallableType::Method, source, std::move(name), std::move(parameters), returnType, objectType, std::move(doc))
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
    const Type* objectType,
    std::vector<Parameter> parameters,
    const Type* createdType,
    NomadString doc
):
    Callable(CallableType::Constructor, source, std::move(name), std::move(parameters), createdType, objectType, std::move(doc))
{
}

const NativeConstructor* Constructor::asNativeConstructor() const {
    return nullptr;
}

const ScriptConstructor* Constructor::asScriptConstructor() const {
    return nullptr;
}

} // namespace nomad
