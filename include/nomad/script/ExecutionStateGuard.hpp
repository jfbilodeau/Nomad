// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/VirtualMachine.hpp>

namespace nomad {

class [[nodiscard]] ExecutionStateGuard final {
public:
    // Borrows the VM, receiver, and storage; they must outlive the guard.
    explicit ExecutionStateGuard(VirtualMachine& machine) noexcept;
    ExecutionStateGuard(const ExecutionStateGuard&) = delete;
    ExecutionStateGuard& operator=(const ExecutionStateGuard&) = delete;
    ExecutionStateGuard(ExecutionStateGuard&&) = delete;
    ExecutionStateGuard& operator=(ExecutionStateGuard&&) = delete;
    ~ExecutionStateGuard() noexcept;

    // Leaves the current execution state active instead of restoring the snapshot.
    void release() noexcept;

private:
    VirtualMachine* m_machine;
    const VirtualMachine::ExecutionState m_state;
};

} // namespace nomad
