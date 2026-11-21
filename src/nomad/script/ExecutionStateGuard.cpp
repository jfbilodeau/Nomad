// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/ExecutionStateGuard.hpp>

namespace nomad {

ExecutionStateGuard::ExecutionStateGuard(VirtualMachine& machine) noexcept:
    m_machine(&machine),
    m_state(machine.captureExecutionState())
{}

ExecutionStateGuard::~ExecutionStateGuard() noexcept {
    if (m_machine != nullptr) {
        m_machine->restoreExecutionState(m_state);
    }
}

void ExecutionStateGuard::release() noexcept {
    m_machine = nullptr;
}

} // namespace nomad
