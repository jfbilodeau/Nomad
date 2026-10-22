// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/script/Documentation.hpp>

#include <variant>
#include <vector>

namespace nomad {

class NativeConstructor;
class NativeFunction;
class NativeMethod;
class ScriptConstructor;
class ScriptFunction;
class ScriptMethod;
class Runtime;
class Type;

struct Parameter {
    NomadString name;
    const Type* type;
    NomadDocField;
};

Parameter defParameter(const NomadString& name, const Type* type, NomadDocArg);

struct TypeParameterIndex {
    NomadIndex index;
};

struct TemplateParameter {
    NomadString name;
    std::variant<const Type*, TypeParameterIndex> type;
    NomadDocField;
};

TemplateParameter defTemplateParameter(
    const NomadString& name,
    std::variant<const Type*, TypeParameterIndex> type,
    NomadDocArg
);

enum class CallableType {
    Function = 1,
    Method,
    Constructor,
};

enum class CallableSource {
    Script = 1, // Compiled from Nomad source.
    Native,     // Implemented in C++ and registered by the host.
};

// Root of everything that can be called, whatever its kind or source. All callables share one ID space in `Runtime`'s
// catalog; ownership belongs to Runtime for free functions and to the object type for constructors and methods.
class Callable {
public:
    Callable(const Callable&) = delete;
    Callable& operator=(const Callable&) = delete;
    virtual ~Callable();

    [[nodiscard]] NomadId getId() const;
    [[nodiscard]] const NomadString& getName() const;
    [[nodiscard]] CallableType getType() const;
    [[nodiscard]] CallableSource getSource() const;
    [[nodiscard]] const std::vector<Parameter>& getParameters() const;
    [[nodiscard]] NomadIndex getParameterCount() const;
    // The type of the value the callable yields; void when it yields nothing.
    [[nodiscard]] const Type* getReturnType() const;
    // Method: the receiver's type. Constructor: the class being constructed (e.g. `Array`), which may differ from
    // the return type (e.g. `Array1<int>`). Null for functions.
    [[nodiscard]] const Type* getObjectType() const;
    [[nodiscard]] const NomadString& getDoc() const;
    [[nodiscard]] NomadId registerWithRuntime(Runtime& runtime);

protected:
    Callable(
        CallableType type,
        CallableSource source,
        NomadString name,
        std::vector<Parameter> parameters,
        const Type* returnType,
        const Type* objectType,
        NomadString doc
    );

    void setId(NomadId id);
    void setReturnType(const Type* returnType);
    [[nodiscard]] std::vector<Parameter>& getEditableParameters();

private:
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
    [[nodiscard]] virtual const NativeFunction* asNativeFunction() const;
    [[nodiscard]] virtual ScriptFunction* asScriptFunction();
    [[nodiscard]] virtual const ScriptFunction* asScriptFunction() const;

protected:
    Function(
        const CallableSource source,
        NomadString name,
        std::vector<Parameter> parameters,
        const Type* returnType,
        NomadString doc
    );
};

// A callable invoked on an object of `objectType`.
class Method : public Callable {
public:
    [[nodiscard]] virtual const NativeMethod* asNativeMethod() const;
    [[nodiscard]] virtual const ScriptMethod* asScriptMethod() const;

protected:
    Method(
        const CallableSource source,
        NomadString name,
        const Type* objectType,
        std::vector<Parameter> parameters,
        const Type* returnType,
        NomadString doc
    );
};

// Creates an object. The name is the one of the class being constructed.
class Constructor : public Callable {
public:
    [[nodiscard]] virtual const NativeConstructor* asNativeConstructor() const;
    [[nodiscard]] virtual const ScriptConstructor* asScriptConstructor() const;

protected:
    Constructor(
        const CallableSource source,
        NomadString name,
        const Type* objectType,
        std::vector<Parameter> parameters,
        const Type* createdType,
        NomadString doc
    );
};

} // nomad
