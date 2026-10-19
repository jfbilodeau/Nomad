// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/script/Documentation.hpp>
#include <nomad/script/RuntimeValue.hpp>

#include <functional>
#include <span>
#include <utility>
#include <vector>

namespace nomad {

class NomadObject;
class ObjectTypeBase;
class Type;
class VirtualMachine;

using NativeFunctionFn = std::function<void(VirtualMachine*)>;

using ConstructorFactory = std::function<NomadObject*(std::span<const RuntimeValue>)>;

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

class NativeFunction : public Function {
public:
    NativeFunction(
        NomadString name,
        NativeFunctionFn fn,
        std::vector<Parameter> parameters,
        const Type* returnType,
        NomadString doc
    ):
        Function(CallableSource::Native, std::move(name), std::move(parameters), returnType, std::move(doc)),
        m_fn(std::move(fn))
    {
    }

    [[nodiscard]] const NativeFunctionFn& getFn() const { return m_fn; }

private:
    NativeFunctionFn m_fn;
};

class NativeMethod : public Method {
public:
    NativeMethod(
        NomadString name,
        const Type* objectType,
        NativeFunctionFn fn,
        std::vector<Parameter> parameters,
        const Type* returnType,
        NomadString doc
    ):
        Method(CallableSource::Native, std::move(name), objectType, std::move(parameters), returnType, std::move(doc)),
        m_fn(std::move(fn))
    {
    }

    [[nodiscard]] const NativeFunctionFn& getFn() const { return m_fn; }

private:
    NativeFunctionFn m_fn;
};

class NativeConstructor : public Constructor {
public:
    NativeConstructor(
        NomadString name,
        const Type* objectType,
        const Type* createdType,
        ConstructorFactory factory,
        std::vector<Parameter> parameters,
        NomadString doc
    ):
        Constructor(CallableSource::Native, std::move(name), objectType, std::move(parameters), createdType, std::move(doc)),
        m_factory(std::move(factory))
    {
    }

    [[nodiscard]] const ConstructorFactory& getFactory() const { return m_factory; }

private:
    ConstructorFactory m_factory;
};

struct NativeFunctionDefinition {
    NomadId id = NOMAD_INVALID_ID;
    NomadString name;
    std::vector<Parameter> parameters;
    const Type* returnType = nullptr;
    NomadDocField;
};

struct NativeMethodDefinition {
    NomadId id = NOMAD_INVALID_ID;
    const Type* objectType = nullptr;
    NomadString name;
    std::vector<Parameter> parameters;
    const Type* returnType = nullptr;
    NomadDocField;
};

struct ConstructorDefinition {
    NomadId id = NOMAD_INVALID_ID;
    const Type* objectType = nullptr;
    NomadString name;
    std::vector<Parameter> parameters;
    const Type* returnType = nullptr;
    ConstructorFactory factory;
    NomadDocField;
};

// Where a callable is implemented. Both kinds are called with the same syntax in Nomad code.
enum class CallableKind {
    NativeFunction = 1, // Implemented in C++ and registered by the host runtime.
    Function,           // Compiled from Nomad source, either from a script or from a `fun` declaration.
};

// Identifies a single callable in the unified catalog. `id` indexes the registry of the matching kind, so the
// pair is required to address a callable unambiguously.
struct CallableId {
    CallableKind kind = CallableKind::NativeFunction;
    NomadId id = NOMAD_INVALID_ID;

    [[nodiscard]] bool isValid() const { return id != NOMAD_INVALID_ID; }

    bool operator==(const CallableId& other) const {
        return kind == other.kind && id == other.id;
    }

    bool operator!=(const CallableId& other) const { return !(*this == other); }
};

inline constexpr CallableId NOMAD_INVALID_CALLABLE_ID = CallableId{};

// All callables sharing one name. A set holds more than one entry when the name is overloaded; overloads may differ
// only in the types of their `ParameterShape::Value` parameters.
struct CallableOverloadSet {
    NomadString name;
    std::vector<CallableId> overloads;
};

} // nomad
