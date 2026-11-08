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
    // Borrows receiver slots; struct constructors use their result storage.
    InterpreterCallFrame(
        VirtualMachine& machine,
        const Callable& callable,
        std::span<const InterpreterValue> arguments,
        const Type* resultType,
        RuntimeValue* receiverStorage = nullptr
    );
    InterpreterCallFrame(const InterpreterCallFrame&) = delete;
    InterpreterCallFrame& operator=(const InterpreterCallFrame&) = delete;
    ~InterpreterCallFrame();

    [[nodiscard]] RuntimeValue* getDestination();
    [[nodiscard]] InterpreterValue getResult() const;

private:
    struct ArgumentStorage {
        const Type* type;
        NomadIndex offset;
        NomadIndex size;
    };

    void releaseArguments() noexcept;
    [[nodiscard]] std::span<const RuntimeValue> getResultStorage() const;

    VirtualMachine& m_machine;
    const Type* m_resultType;
    RuntimeValue m_savedResult;
    RuntimeValue m_savedIntermediate;
    std::vector<RuntimeValue> m_resultStorage;
    std::vector<RuntimeValue> m_argumentStorage;
    std::vector<ArgumentStorage> m_arguments;
    bool m_onStack = false;
};

} // namespace nomad
