#pragma once

#include <nomad/interpreter/InterpreterValue.hpp>

#include <nomad/script/RuntimeValue.hpp>

#include <span>
#include <vector>

namespace nomad {

class Callable;
class Closure;
class ScriptFunction;
class Type;
class VirtualMachine;

class InterpreterCallFrame {
public:
    // Borrows receiver slots; struct constructors use their result storage.
    InterpreterCallFrame(
        VirtualMachine& machine,
        const Callable& callable,
        std::span<const InterpreterValue> arguments,
        const Type* resultType,
        RuntimeValue* receiverStorage = nullptr,
        const Closure* closure = nullptr
    );
    InterpreterCallFrame(
        VirtualMachine& machine,
        const Closure& closure,
        std::span<const RuntimeValue> arguments
    );
    InterpreterCallFrame(
        VirtualMachine& machine,
        const ScriptFunction& function
    );
    InterpreterCallFrame(const InterpreterCallFrame&) = delete;
    InterpreterCallFrame& operator=(const InterpreterCallFrame&) = delete;
    ~InterpreterCallFrame();

    [[nodiscard]] RuntimeValue* getDestination();
    [[nodiscard]] InterpreterValue getResult() const;
    [[nodiscard]] std::span<const RuntimeValue> getResultStorage() const;
    // Transfers a scalar result without copying or retaining it.
    [[nodiscard]] RuntimeValue releaseResult();
    // A suspended scalar call keeps its owned arguments on the VM stack.
    void releaseArgumentsToVirtualMachine();

private:
    InterpreterCallFrame(
        VirtualMachine& machine,
        const Callable& callable,
        const Type* resultType,
        RuntimeValue* receiverStorage
    );

    struct ArgumentStorage {
        const Type* type;
        NomadIndex offset;
        NomadIndex size;
    };

    void copyArgument(const Type& type, std::span<const RuntimeValue> storage);
    void copyCaptures(const Closure& closure);
    void releaseArguments() noexcept;

    VirtualMachine& m_machine;
    const Type* m_resultType;
    RuntimeValue m_savedResult;
    RuntimeValue m_savedIntermediate;
    std::vector<RuntimeValue> m_resultStorage;
    std::span<RuntimeValue> m_argumentStorage;
    std::vector<ArgumentStorage> m_arguments;
    NomadIndex m_argumentOffset = 0;
    bool m_onStack = false;
};

} // namespace nomad
