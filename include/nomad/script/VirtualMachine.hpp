// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/compiler/Identifier.hpp>

#include <nomad/script/Documentation.hpp>
#include <nomad/script/OpCode.hpp>
#include <nomad/script/Function.hpp>
#include <nomad/script/RuntimeValue.hpp>

#include <vector>

namespace nomad {

class VirtualMachine;
class Runtime;
class Function;
class Closure;

struct Instruction {
    explicit Instruction(const InstructionFn fn) : fn(fn) {}
    explicit Instruction(const RuntimeValue& value) : value(value) {}
    Instruction(const Instruction& other) { fn = other.fn; }

    ~Instruction() {}

    union {
        InstructionFn fn = nullptr;
        RuntimeValue value;
    };
};

class VirtualMachineException : public NomadException {
public:
    explicit VirtualMachineException(const NomadString& message) : NomadException(message) {}
};

class VirtualMachine {
public:
    explicit VirtualMachine(Runtime* runtime, NomadIndex stack_size = 1024);
    VirtualMachine(const VirtualMachine& other) = delete;
    ~VirtualMachine();

    [[nodiscard]] Runtime* getRuntime() const { return m_runtime; }

    void setFunctionVariableValue(NomadId variableId, const RuntimeValue& value);
    [[nodiscard]] const RuntimeValue& getFunctionVariableValue(NomadId variableId) const;

    void run(const Function* function, const std::vector<RuntimeValue>& args, const std::vector<RuntimeValue>& closures = {});
    void yield(const RuntimeValue& result);
    void stop(const RuntimeValue& result);
    void fault(const NomadString& faultMessage) const;

    [[nodiscard]]
    Function* getCurrentFunction() const;

    void callFunction(const Function* function);
    void callNativeFunction(NomadId nativeFunctionId);
    void returnFunction(NomadIndex variableCount);

    [[nodiscard]]
    std::unique_ptr<Closure> createClosure(const Runtime* runtime, NomadId functionId) const;

    void setResult(const RuntimeValue& result) { m_result = result; }
    void setIntegerResult(NomadInteger result) { m_result.setIntegerValue(result); }
    void setFloatResult(NomadFloat result) { m_result.setFloatValue(result); }
    void setBooleanResult(NomadBoolean result) { m_result.setBooleanValue(result); }
    void setIdResult(NomadId result) { m_result.setIdValue(result); }
    void setStringResult(const NomadString& result) { m_result.setStringValue(result); }
    void setStringResult(const NomadChar* result) { m_result.setStringValue(result); }
    void setStringRefResult(const NomadChar* result) { m_result.setStringRefValue(result); }

    [[nodiscard]] const RuntimeValue& getResult() const { return m_result; }
    [[nodiscard]] RuntimeValue& getResult() { return m_result; }
    [[nodiscard]] NomadInteger getIntegerResult() const { return m_result.getIntegerValue(); }
    [[nodiscard]] NomadFloat getFloatResult() const { return m_result.getFloatValue(); }
    [[nodiscard]] NomadBoolean getBooleanResult() const { return m_result.getBooleanValue(); }
    [[nodiscard]] NomadId getIdResult() const { return m_result.getIdValue(); }
    [[nodiscard]] NomadString getStringResult() const { return m_result.getStringValue(); }

    void setIntermediate(const RuntimeValue& intermediate) { m_intermediate = intermediate; }
    void setBooleanIntermediate(const NomadBoolean intermediate) { m_intermediate.setBooleanValue(intermediate); }
    void setIntegerIntermediate(const NomadInteger intermediate) { m_intermediate.setIntegerValue(intermediate); }
    void setFloatIntermediate(const NomadFloat intermediate) { m_intermediate.setFloatValue(intermediate); }
    void setIdIntermediate(const NomadId intermediate) { m_intermediate.setIdValue(intermediate); }
    void setStringIntermediate(const NomadString& intermediate) { m_intermediate.setStringValue(intermediate); }
    void setStringIntermediate(const NomadChar* intermediate) { m_intermediate.setStringValue(intermediate); }
    void setStringRefIntermediate(const NomadChar* intermediate) { m_intermediate.setStringRefValue(intermediate); }

    [[nodiscard]] const RuntimeValue& getIntermediate() const { return m_intermediate; }
    [[nodiscard]] NomadBoolean getBooleanIntermediate() const { return m_intermediate.getBooleanValue(); }
    [[nodiscard]] NomadInteger getIntegerIntermediate() const { return m_intermediate.getIntegerValue(); }
    [[nodiscard]] NomadFloat getFloatIntermediate() const { return m_intermediate.getFloatValue(); }
    [[nodiscard]] NomadId getIdIntermediate() const { return m_intermediate.getIdValue(); }
    [[nodiscard]] NomadString getStringIntermediate() const { return m_intermediate.getStringValue(); }

    [[nodiscard]] const RuntimeValue& getParameter(const NomadIndex parameterIndex) const { return m_stack[m_parameterIndex - parameterIndex]; }
    [[nodiscard]] NomadBoolean getBooleanParameter(const NomadIndex parameterIndex) const { return m_stack[m_parameterIndex - parameterIndex].getBooleanValue(); }
    [[nodiscard]] NomadInteger getIntegerParameter(const NomadIndex parameterIndex) const { return m_stack[m_parameterIndex - parameterIndex].getIntegerValue(); }
    [[nodiscard]] NomadFloat getFloatParameter(const NomadIndex parameterIndex) const { return m_stack[m_parameterIndex - parameterIndex].getFloatValue(); }
    [[nodiscard]] NomadId getIdParameter(const NomadIndex parameterIndex) const { return m_stack[m_parameterIndex - parameterIndex].getIdValue(); }
    [[nodiscard]] const NomadChar* getStringParameter(const NomadIndex parameterIndex) const { return m_stack[m_parameterIndex - parameterIndex].getStringValue(); }

    // Closure capture: read from enclosing function's parameter frame (not the nativeFunction's frame)
    [[nodiscard]] const RuntimeValue& getEnclosingParameter(const NomadIndex parameterIndex) const { return m_stack[m_enclosingParameterIndex - parameterIndex]; }
    [[nodiscard]] NomadBoolean getBooleanEnclosingParameter(const NomadIndex parameterIndex) const { return m_stack[m_enclosingParameterIndex - parameterIndex].getBooleanValue(); }
    [[nodiscard]] NomadInteger getIntegerEnclosingParameter(const NomadIndex parameterIndex) const { return m_stack[m_enclosingParameterIndex - parameterIndex].getIntegerValue(); }
    [[nodiscard]] NomadFloat getFloatEnclosingParameter(const NomadIndex parameterIndex) const { return m_stack[m_enclosingParameterIndex - parameterIndex].getFloatValue(); }
    [[nodiscard]] NomadId getIdEnclosingParameter(const NomadIndex parameterIndex) const { return m_stack[m_enclosingParameterIndex - parameterIndex].getIdValue(); }
    [[nodiscard]] const NomadChar* getStringEnclosingParameter(const NomadIndex parameterIndex) const { return m_stack[m_enclosingParameterIndex - parameterIndex].getStringValue(); }

    void setVariableValue(const NomadId variableId, const RuntimeValue& value) { m_stack[m_variableIndex + variableId] = value; }
    void setBooleanVariableValue(const NomadId variableId, const NomadBoolean value) { m_stack[m_variableIndex + variableId].setBooleanValue(value); }
    void setIntegerVariableValue(const NomadId variableId, const NomadInteger value) { m_stack[m_variableIndex + variableId].setIntegerValue(value); }
    void setFloatVariableValue(const NomadId variableId, const NomadFloat value) { m_stack[m_variableIndex + variableId].setFloatValue(value); }
    void setIdVariableValue(const NomadId variableId, const NomadId value) { m_stack[m_variableIndex + variableId].setIdValue(value); }
    void setStringVariableValue(const NomadId variableId, const NomadChar* value) { m_stack[m_variableIndex + variableId].setStringValue(value); }
    void setStringVariableValue(const NomadId variableId, const NomadString& value) { m_stack[m_variableIndex + variableId].setStringValue(value); }

    void getFunctionVariableValue(const NomadId variableId, RuntimeValue& value) const { value = m_stack[m_variableIndex + variableId]; }
    [[nodiscard]] NomadBoolean getFunctionBooleanVariableValue(const NomadId variable_id) const { return m_stack[m_variableIndex + variable_id].getBooleanValue(); }
    [[nodiscard]] NomadInteger getFunctionIntegerVariableValue(const NomadId variable_id) const { return m_stack[m_variableIndex + variable_id].getIntegerValue(); }
    [[nodiscard]] NomadFloat getFunctionFloatVariableValue(const NomadId variable_id) const { return m_stack[m_variableIndex + variable_id].getFloatValue(); }
    [[nodiscard]] NomadId getFunctionIdVariableValue(const NomadId variable_id) const { return m_stack[m_variableIndex + variable_id].getIdValue(); }
    [[nodiscard]] const NomadChar* getFunctionStringVariableValue(const NomadId variable_id) { return m_stack[m_variableIndex + variable_id].getStringValue(); }
    [[nodiscard]] NomadString getFunctionStringVariableValue(const NomadId variable_id) const { return m_stack[m_variableIndex + variable_id].getStringValue(); }

    [[nodiscard]]
    const RuntimeValue& peekStack(const NomadIndex index) const { return m_stack[m_stackIndex - index]; }

    void initStringVariable(const NomadIndex variableIndex) { m_stack[m_variableIndex + variableIndex].initStringValue(); }
    void initStringResult() { m_result.initStringValue(); }
    void initStringIntermediate() { m_intermediate.initStringValue(); }

    void freeStringResult() { m_result.freeStringValue(); }
    void freeStringIntermediate() { m_intermediate.freeStringValue(); }

    // Transfer ownership of the `r` string buffer without copying it. `r` must not be read as a string afterward.
    void moveStringResultToIntermediate() { m_intermediate.moveStringValue(m_result); }
    void moveStringResultToStack();

    void freeStringVariable(NomadIndex variable);
    void freeStringStack(NomadIndex index);

    void pushResult();
    void pushIntermediate();
    void pushStringResult();
    // Moves (does not copy) the `i` string onto the stack. popStringIntermediate() moves it back.
    void pushStringIntermediate();

    void popResult();
    void popIntermediate();
    void popStringResult();
    void popStringIntermediate();

    void pushValue(const RuntimeValue& value);
    void pushBoolean(NomadBoolean value);
    void pushInteger(NomadInteger value);
    void pushIndex(NomadIndex value);
    void pushFloat(NomadFloat value);
    void pushId(NomadId value);
    void pushString(const NomadChar* value);
    void pushString(const NomadString& value);
    void pushInstructionIndex();

    void pushN(NomadIndex count);
    void pop1();
    void pop2();
    void pop3();
    void popN(NomadIndex count);
    const RuntimeValue& popValue();
    NomadBoolean popBoolean();
    NomadInteger popInteger();
    NomadIndex popIndex();
    NomadFloat popFloat();
    NomadId popId();
    NomadString popString();
    void popInstructionIndex();

    // NomadId nextInstruction();
    NomadId nextId();
    NomadIndex nextIndex();
    // NomadShort nextShort();
    NomadInteger nextInteger();
    NomadFloat nextFloat();

    void jump(NomadIndex index);

    bool getVariableValue(IdentifierType identifierType, NomadId variableId, RuntimeValue& value);

    [[nodiscard]] bool isRunning() const { return m_running; }
    [[nodiscard]] bool hasYielded() const { return m_yielded; }

    [[nodiscard]] size_t getInstructionIndex() const { return m_instructionIndex; }

private:
    friend class Runtime;

    void initializeFunctionVariables(const Function* function);
    void runNativeFunction(NomadId nativeFunctionId, const std::vector<RuntimeValue>& args);

    Runtime* m_runtime;
    NomadIndex m_instructionIndex = 0;
    const std::vector<Instruction>& m_instructions;
    RuntimeValue m_result;
    RuntimeValue m_intermediate;
    bool m_running = false;
    bool m_yielded = false;
//    std::vector<NomadId> m_op_codes;
    NomadIndex m_stackIndex = 0;
    std::vector<RuntimeValue> m_stack;
    NomadIndex m_parameterIndex = 0;
    NomadIndex m_enclosingParameterIndex = 0;  // Enclosing function's parameter frame for closure capture
    NomadIndex m_variableIndex = 0;
};

} // nomad
