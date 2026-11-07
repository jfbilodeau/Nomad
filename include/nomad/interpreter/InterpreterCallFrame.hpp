#pragma once

#include <nomad/interpreter/InterpreterValue.hpp>

#include <nomad/script/RuntimeValue.hpp>

#include <span>
#include <vector>

namespace nomad {

class Callable;
class Type;
class VirtualMachine;

class InterpreterCallFrame {
public:
    InterpreterCallFrame(
        VirtualMachine& machine,
        const Callable& callable,
        std::span<const InterpreterValue> arguments,
        const Type* resultType,
        const InterpreterValue* receiver = nullptr
    );
    InterpreterCallFrame(const InterpreterCallFrame&) = delete;
    InterpreterCallFrame& operator=(const InterpreterCallFrame&) = delete;
    ~InterpreterCallFrame();

    [[nodiscard]] RuntimeValue* getDestination();
    [[nodiscard]] InterpreterValue getReceiver() const;
    [[nodiscard]] InterpreterValue getResult() const;

private:
    struct ArgumentStorage {
        const Type* type;
        NomadIndex offset;
        NomadIndex size;
    };

    void releaseArguments() noexcept;
    void releaseReceiver() noexcept;
    [[nodiscard]] std::span<const RuntimeValue> getResultStorage() const;

    VirtualMachine& m_machine;
    const Type* m_resultType;
    const Type* m_receiverType;
    RuntimeValue m_savedResult;
    RuntimeValue m_savedIntermediate;
    std::vector<RuntimeValue> m_receiverStorage;
    std::vector<RuntimeValue> m_resultStorage;
    std::vector<RuntimeValue> m_argumentStorage;
    std::vector<ArgumentStorage> m_arguments;
    bool m_onStack = false;
};

} // namespace nomad
