// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/compiler/Identifier.hpp>

#include <nomad/script/Documentation.hpp>
#include <nomad/script/NativeMember.hpp>
#include <nomad/script/NomadStringObject.hpp>
#include <nomad/script/OpCode.hpp>
#include <nomad/script/Property.hpp>
#include <nomad/script/RuntimeValue.hpp>
#include <nomad/script/ScriptCallable.hpp>

#include <functional>
#include <vector>

namespace nomad {

class VirtualMachine;
class Runtime;
class ScriptFunction;
class ScriptCallableBase;
class ScriptObject;
class Closure;
class TypedValue;

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

    void setFunctionVariableValue(VariableId variableId, const RuntimeValue& value);
    [[nodiscard]] const RuntimeValue& getFunctionVariableValue(VariableId variableId) const;

    void run(const ScriptFunction* function, const std::vector<RuntimeValue>& args, const std::vector<RuntimeValue>& closures = {});
    void initializeObject(ScriptObject& object);
    [[nodiscard]] NomadObject* constructObject(
        CallableId constructorId,
        const std::vector<RuntimeValue>& arguments
    );
    void yield(const RuntimeValue& result);
    void stop(const RuntimeValue& result);
    void fault(const NomadString& faultMessage) const;

    [[nodiscard]]
    ScriptFunction* getCurrentFunction() const;

    void callFunction(const ScriptFunction* function);
    void callNativeFunction(CallableId nativeFunctionId);
    void callNativeMethod(CallableId memberId, const RuntimeValue& receiver);
    void returnFunction(NomadIndex variableCount);
    void runNativeMethod(CallableId memberId, const std::vector<RuntimeValue>& args, RuntimeValue& receiver);
    void runNativeFunction(CallableId nativeFunctionId, const std::vector<RuntimeValue>& args);

    void getNativeProperty(PropertyId memberId, const RuntimeValue& receiver, RuntimeValue& result);
    void setNativeProperty(PropertyId memberId, const RuntimeValue& receiver, const RuntimeValue& value);
    void getProperty(PropertyId memberId, const RuntimeValue& receiver, RuntimeValue& result);
    void setProperty(PropertyId memberId, const RuntimeValue& receiver, const RuntimeValue& value);
    void callNativeIndexer(IndexerId indexerId, const RuntimeValue& receiver, bool write);
    void runNativeIndexer(
        IndexerId indexerId,
        const std::vector<RuntimeValue>& args,
        const RuntimeValue& receiver,
        bool write
    );
    [[nodiscard]] NomadObject* getThis() const;

    template<typename Object>
    [[nodiscard]] Object* getThisAs() const;

    [[nodiscard]]
    std::unique_ptr<Closure> createClosure(const Runtime* runtime, CallableId functionId) const;

    // Copies captures from the active script frame, not an enclosing native-call parameter frame.
    [[nodiscard]] std::vector<TypedValue> captureCurrentFrame(const ScriptFunction* function) const;

    void setResult(const RuntimeValue& result) { m_result = result; }
    void setIntegerResult(NomadInteger result) { m_result.setIntegerValue(result); }
    void setFloatResult(NomadFloat result) { m_result.setFloatValue(result); }
    void setBooleanResult(NomadBoolean result) { m_result.setBooleanValue(result); }
    void setIdResult(NomadId result) { m_result.setIdValue(result); }
    void setStringResult(const NomadString& result) { m_result.setStringValue(result); }
    void setStringResult(const NomadChar* result) { m_result.setStringValue(result); }
    // Takes over the caller's reference.
    void setStringResult(NomadStringObject* result) { m_result.setObjectValue(result); }

    [[nodiscard]] const RuntimeValue& getResult() const { return m_result; }
    [[nodiscard]] RuntimeValue& getResult() { return m_result; }
    [[nodiscard]] NomadInteger getIntegerResult() const { return m_result.getIntegerValue(); }
    [[nodiscard]] NomadFloat getFloatResult() const { return m_result.getFloatValue(); }
    [[nodiscard]] NomadBoolean getBooleanResult() const { return m_result.getBooleanValue(); }
    [[nodiscard]] NomadId getIdResult() const { return m_result.getIdValue(); }
    [[nodiscard]] NomadStringObject* getStringResult() const { return m_result.getStringObject(); }

    void setIntermediate(const RuntimeValue& intermediate) { m_intermediate = intermediate; }
    void setBooleanIntermediate(const NomadBoolean intermediate) { m_intermediate.setBooleanValue(intermediate); }
    void setIntegerIntermediate(const NomadInteger intermediate) { m_intermediate.setIntegerValue(intermediate); }
    void setFloatIntermediate(const NomadFloat intermediate) { m_intermediate.setFloatValue(intermediate); }
    void setIdIntermediate(const NomadId intermediate) { m_intermediate.setIdValue(intermediate); }
    void setStringIntermediate(const NomadString& intermediate) { m_intermediate.setStringValue(intermediate); }
    void setStringIntermediate(const NomadChar* intermediate) { m_intermediate.setStringValue(intermediate); }
    // Takes over the caller's reference.
    void setStringIntermediate(NomadStringObject* intermediate) { m_intermediate.setObjectValue(intermediate); }

    [[nodiscard]] const RuntimeValue& getIntermediate() const { return m_intermediate; }
    [[nodiscard]] RuntimeValue& getIntermediate() { return m_intermediate; }
    [[nodiscard]] NomadBoolean getBooleanIntermediate() const { return m_intermediate.getBooleanValue(); }
    [[nodiscard]] NomadInteger getIntegerIntermediate() const { return m_intermediate.getIntegerValue(); }
    [[nodiscard]] NomadFloat getFloatIntermediate() const { return m_intermediate.getFloatValue(); }
    [[nodiscard]] NomadId getIdIntermediate() const { return m_intermediate.getIdValue(); }
    [[nodiscard]] NomadStringObject* getStringIntermediate() const { return m_intermediate.getStringObject(); }

    [[nodiscard]] const RuntimeValue& getParameter(const NomadIndex parameterIndex) const { return m_stack[m_parameterIndex - parameterIndex]; }
    [[nodiscard]] NomadBoolean getBooleanParameter(const NomadIndex parameterIndex) const { return m_stack[m_parameterIndex - parameterIndex].getBooleanValue(); }
    [[nodiscard]] NomadInteger getIntegerParameter(const NomadIndex parameterIndex) const { return m_stack[m_parameterIndex - parameterIndex].getIntegerValue(); }
    [[nodiscard]] NomadFloat getFloatParameter(const NomadIndex parameterIndex) const { return m_stack[m_parameterIndex - parameterIndex].getFloatValue(); }
    [[nodiscard]] NomadId getIdParameter(const NomadIndex parameterIndex) const { return m_stack[m_parameterIndex - parameterIndex].getIdValue(); }
    [[nodiscard]] CallableId getCallableIdParameter(const NomadIndex parameterIndex) const { return CallableId{m_stack[m_parameterIndex - parameterIndex].getIdValue() }; }
    [[nodiscard]] NomadStringObject* getStringParameter(const NomadIndex parameterIndex) const { return m_stack[m_parameterIndex - parameterIndex].getStringObject(); }

    // Closure capture: read from enclosing function's parameter frame (not the nativeFunction's frame)
    [[nodiscard]] const RuntimeValue& getEnclosingParameter(const NomadIndex parameterIndex) const { return m_stack[m_enclosingParameterIndex - parameterIndex]; }
    [[nodiscard]] NomadBoolean getBooleanEnclosingParameter(const NomadIndex parameterIndex) const { return m_stack[m_enclosingParameterIndex - parameterIndex].getBooleanValue(); }
    [[nodiscard]] NomadInteger getIntegerEnclosingParameter(const NomadIndex parameterIndex) const { return m_stack[m_enclosingParameterIndex - parameterIndex].getIntegerValue(); }
    [[nodiscard]] NomadFloat getFloatEnclosingParameter(const NomadIndex parameterIndex) const { return m_stack[m_enclosingParameterIndex - parameterIndex].getFloatValue(); }
    [[nodiscard]] NomadId getIdEnclosingParameter(const NomadIndex parameterIndex) const { return m_stack[m_enclosingParameterIndex - parameterIndex].getIdValue(); }
    [[nodiscard]] NomadStringObject* getStringEnclosingParameter(const NomadIndex parameterIndex) const { return m_stack[m_enclosingParameterIndex - parameterIndex].getStringObject(); }

    void setVariableValue(const VariableId variableId, const RuntimeValue& value) { m_stack[m_variableIndex + variableId.id] = value; }
    void setBooleanVariableValue(const VariableId variableId, const NomadBoolean value) { m_stack[m_variableIndex + variableId.id].setBooleanValue(value); }
    void setIntegerVariableValue(const VariableId variableId, const NomadInteger value) { m_stack[m_variableIndex + variableId.id].setIntegerValue(value); }
    void setFloatVariableValue(const VariableId variableId, const NomadFloat value) { m_stack[m_variableIndex + variableId.id].setFloatValue(value); }
    void setIdVariableValue(const VariableId variableId, const NomadId value) { m_stack[m_variableIndex + variableId.id].setIdValue(value); }
    void setStringVariableValue(const VariableId variableId, const NomadChar* value) { m_stack[m_variableIndex + variableId.id].setStringValue(value); }
    void setStringVariableValue(const VariableId variableId, const NomadString& value) { m_stack[m_variableIndex + variableId.id].setStringValue(value); }

    void getFunctionVariableValue(const VariableId variableId, RuntimeValue& value) const { value = m_stack[m_variableIndex + variableId.id]; }
    [[nodiscard]] NomadBoolean getFunctionBooleanVariableValue(const VariableId variable_id) const { return m_stack[m_variableIndex + variable_id.id].getBooleanValue(); }
    [[nodiscard]] NomadInteger getFunctionIntegerVariableValue(const VariableId variable_id) const { return m_stack[m_variableIndex + variable_id.id].getIntegerValue(); }
    [[nodiscard]] NomadFloat getFunctionFloatVariableValue(const VariableId variable_id) const { return m_stack[m_variableIndex + variable_id.id].getFloatValue(); }
    [[nodiscard]] NomadId getFunctionIdVariableValue(const VariableId variable_id) const { return m_stack[m_variableIndex + variable_id.id].getIdValue(); }
    [[nodiscard]] NomadStringObject* getFunctionStringVariableValue(const VariableId variable_id) const { return m_stack[m_variableIndex + variable_id.id].getStringObject(); }

    [[nodiscard]]
    const RuntimeValue& peekStack(const NomadIndex index) const { return m_stack[m_stackIndex - index]; }

    void initStringVariable(const VariableId variableId) { m_stack[m_variableIndex + variableId.id].initStringValue(); }
    void initStringResult() { m_result.initStringValue(); }
    void initStringIntermediate() { m_intermediate.initStringValue(); }

    void freeObjectResult() { m_result.freeObjectValue(); }
    void freeObjectIntermediate() { m_intermediate.freeObjectValue(); }

    // Transfer ownership of the `r` object without touching its reference count. `r` is left null.
    void moveObjectResultToIntermediate() { m_intermediate.moveObjectValue(m_result); }
    void moveObjectResultToStack();

    void freeObjectVariable(VariableId variableId);
    void freeObjectStack(NomadIndex index);

    void pushResult();
    void pushIntermediate();
    // Adds a reference to the `r` object and pushes it onto the stack.
    void pushObjectResult();
    // Moves the `i` object onto the stack. popObjectIntermediate() moves it back.
    void pushObjectIntermediate();

    void popResult();
    void popIntermediate();
    void popObjectResult();
    void popObjectIntermediate();

    void pushValue(const RuntimeValue& value);
    // Adds a reference to the object held by `value` and pushes it onto the stack.
    void pushObjectValue(const RuntimeValue& value);
    void pushBoolean(NomadBoolean value);
    void pushInteger(NomadInteger value);
    void pushIndex(NomadIndex value);
    void pushFloat(NomadFloat value);
    void pushId(NomadId value);
    void pushString(const NomadChar* value);
    void pushString(const NomadString& value);
    // Takes over the caller's reference.
    void pushString(NomadStringObject* value);
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
    friend class InterpreterContext;

    void invokeNativeFunction(const NativeFunctionFn& callback);
    void callNativeConstructor(CallableId constructorId);
    void runNativeCallable(
        const Callable* callable,
        const std::vector<RuntimeValue>& args
    );
    void initializeFunctionVariables(const ScriptCallableBase* function, const RuntimeValue* receiver = nullptr);
    void callScriptCallable(const ScriptCallableBase* function, const RuntimeValue* receiver);
    void runCallable(
        const ScriptCallableBase* function,
        const std::vector<RuntimeValue>& args,
        const std::vector<RuntimeValue>& closures,
        const RuntimeValue* receiver
    );

    Runtime* m_runtime;
    NomadIndex m_instructionIndex = 0;
    const std::vector<Instruction>& m_instructions;
    RuntimeValue m_result;
    RuntimeValue m_intermediate;
    bool m_running = false;
    bool m_yielded = false;
    NomadIndex m_stackIndex = 0;
    std::vector<RuntimeValue> m_stack;
    NomadIndex m_parameterIndex = 0;
    NomadIndex m_enclosingParameterIndex = 0;  // Enclosing function's parameter frame for closure capture
    NomadIndex m_variableIndex = 0;
    void invokeNativeMember(const std::function<void(VirtualMachine*)>& callback, const RuntimeValue& receiver);
    void invokeNativeMethod(const std::function<void(VirtualMachine*)>& callback, const RuntimeValue& receiver);
    RuntimeValue m_this; // Receiver of the current member call; borrowed, not retained.
};

template<typename Object>
Object* VirtualMachine::getThisAs() const {
    static_assert(std::is_base_of_v<NomadObject, Object>);
    return static_cast<Object*>(getThis());
}

} // nomad
