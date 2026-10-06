// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/script/RuntimeValue.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace nomad {

class Type;
class Runtime;
class Function;
class VirtualMachine;

class TypedValue {
public:
    TypedValue(const Type* type, const RuntimeValue& value);
    TypedValue(const TypedValue& other);
    TypedValue(TypedValue&& other) noexcept;
    TypedValue& operator=(const TypedValue& other);
    TypedValue& operator=(TypedValue&& other) noexcept;
    ~TypedValue();

    [[nodiscard]] const Type* getType() const;
    [[nodiscard]] const RuntimeValue& getValue() const;

private:
    void release() noexcept;

    const Type* m_type;
    RuntimeValue m_value;
};

struct InterpreterVariableInfo {
    NomadString name;
    const Type* type;
    NomadString value;
};

class Interpreter {
public:
    explicit Interpreter(Runtime* runtime = nullptr);
    Interpreter(const Interpreter&) = delete;
    Interpreter& operator=(const Interpreter&) = delete;
    ~Interpreter();

    [[nodiscard]] Runtime* getRuntime() const;
    // Virtual machine used to read and write dynamic variables. Created on
    // first use so interpreters without a runtime stay cheap to construct.
    [[nodiscard]] VirtualMachine* getVirtualMachine();
    // Console function holding variable definitions (names and types). Not registered with the Runtime.
    [[nodiscard]] const Function* getFunction() const;

    void setResult(const Type* type, const RuntimeValue& value);
    void setResult(const TypedValue& value);
    void setVoidResult();
    void clearResult();
    [[nodiscard]] const TypedValue* getResult() const;

    void setError(const NomadString& error);
    void clearError();
    [[nodiscard]] NomadBoolean hasError() const;
    [[nodiscard]] const NomadString* getError() const;

    [[nodiscard]] NomadBoolean execute(const NomadString& source);

    void setVariable(const NomadString& name, const Type* type, const RuntimeValue& value);

    [[nodiscard]] const Type* getVariableType(const NomadString& name) const;
    [[nodiscard]] const Type* getVariableTypeById(NomadId variableId) const;
    [[nodiscard]] const RuntimeValue* getVariableValue(const NomadString& name) const;
    [[nodiscard]] const RuntimeValue* getVariableValueById(NomadId variableId) const;
    [[nodiscard]] std::vector<InterpreterVariableInfo> listVariables() const;

private:
    Runtime* m_runtime;
    std::unique_ptr<Function> m_function;
    std::unique_ptr<VirtualMachine> m_virtualMachine;
    std::optional<TypedValue> m_result;
    std::optional<NomadString> m_error;
    // Indexed by the console function's variable id.
    std::vector<std::unique_ptr<TypedValue>> m_variables;
};

} // namespace nomad
