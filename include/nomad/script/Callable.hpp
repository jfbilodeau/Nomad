// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/script/Documentation.hpp>

#include <utility>
#include <vector>

namespace nomad {

class NativeConstructor;
class NativeFunction;
class NativeMethod;
class ScriptConstructor;
class ScriptFunction;
class ScriptMethod;
class Type;

struct Parameter {
    NomadString name;
    const Type* type;
    NomadDocField;
};

Parameter defParameter(const NomadString& name, const Type* type, NomadDocArg);

enum class CallableType {
    Function = 1,
    Method,
    Constructor,
};

enum class CallableSource {
    Script = 1, // Compiled from Nomad source.
    Native,     // Implemented in C++ and registered by the host.
};

// Root of everything that can be called, whatever its kind or source. All callables share one id space and are owned
// by `Runtime`, which hands them out as const pointers through typed getters.
class Callable {
public:
    Callable(const Callable&) = delete;
    Callable& operator=(const Callable&) = delete;
    virtual ~Callable() = default;

    [[nodiscard]] NomadId getId() const { return m_id; }
    [[nodiscard]] const NomadString& getName() const { return m_name; }
    [[nodiscard]] CallableType getType() const { return m_type; }
    [[nodiscard]] CallableSource getSource() const { return m_source; }
    [[nodiscard]] const std::vector<Parameter>& getParameters() const { return m_parameters; }
    [[nodiscard]] NomadIndex getParameterCount() const { return m_parameters.size(); }
    // The type of the value the callable yields; void when it yields nothing.
    [[nodiscard]] const Type* getReturnType() const { return m_returnType; }
    // Method: the receiver's type. Constructor: the class being constructed (e.g. `Array`), which may differ from
    // the return type (e.g. `Array1<int>`). Null for functions.
    [[nodiscard]] const Type* getObjectType() const { return m_objectType; }
    [[nodiscard]] const NomadString& getDoc() const { return m_doc; }

protected:
    Callable(
        CallableType type,
        CallableSource source,
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

    void setId(const NomadId id) { m_id = id; }
    void setReturnType(const Type* returnType) { m_returnType = returnType; }
    [[nodiscard]] std::vector<Parameter>& getEditableParameters() { return m_parameters; }

private:
    friend class Runtime; // Assigns the id when the callable joins the function table.

    NomadId m_id = NOMAD_INVALID_ID;
    NomadString m_name;
    CallableType m_type;
    CallableSource m_source;
    std::vector<Parameter> m_parameters;
    const Type* m_returnType;
    const Type* m_objectType;
    NomadString m_doc;
};

// A callable that does not need a receiver.
class Function : public Callable {
public:
    [[nodiscard]] virtual const NativeFunction* asNativeFunction() const { return nullptr; }
    [[nodiscard]] virtual ScriptFunction* asScriptFunction() { return nullptr; }
    [[nodiscard]] virtual const ScriptFunction* asScriptFunction() const { return nullptr; }

protected:
    Function(
        const CallableSource source,
        NomadString name,
        std::vector<Parameter> parameters,
        const Type* returnType,
        NomadString doc
    ):
        Callable(CallableType::Function, source, std::move(name), std::move(parameters), returnType, nullptr, std::move(doc))
    {
    }
};

// A callable invoked on an object of `objectType`.
class Method : public Callable {
public:
    [[nodiscard]] virtual const NativeMethod* asNativeMethod() const { return nullptr; }
    [[nodiscard]] virtual const ScriptMethod* asScriptMethod() const { return nullptr; }

protected:
    Method(
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
};

// Creates an object. The name is the one of the class being constructed.
class Constructor : public Callable {
public:
    [[nodiscard]] virtual const NativeConstructor* asNativeConstructor() const { return nullptr; }
    [[nodiscard]] virtual const ScriptConstructor* asScriptConstructor() const { return nullptr; }

protected:
    Constructor(
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
};

// All callables sharing one name. A set holds more than one entry when the name is overloaded; overloads may differ
// only in the types of their `ParameterShape::Value` parameters.
struct CallableOverloadSet {
    NomadString name;
    std::vector<NomadId> overloads;
};

} // nomad
