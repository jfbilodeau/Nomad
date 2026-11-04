#pragma once

#include <nomad/script/RuntimeValue.hpp>
#include <nomad/script/TypedValue.hpp>

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
        std::span<const TypedValue> arguments,
        const Type* resultType
    );
    InterpreterCallFrame(const InterpreterCallFrame&) = delete;
    InterpreterCallFrame& operator=(const InterpreterCallFrame&) = delete;
    ~InterpreterCallFrame();

    [[nodiscard]] RuntimeValue* getDestination();
    [[nodiscard]] std::span<const RuntimeValue> getResultStorage() const;

private:
    struct ArgumentStorage {
        const Type* type;
        NomadIndex offset;
        NomadIndex size;
    };

    void releaseArguments() noexcept;

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
