// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/modules/ScriptBindings.hpp>

#include <utility>

namespace nomad {

ScriptBindings::ScriptBindings(Runtime& runtime) : m_runtime(&runtime) {
}

Runtime& ScriptBindings::getRuntime() const {
    return *m_runtime;
}

const ScriptClassBinding& ScriptBindings::getClassBinding(const BindingClass& contract) {
    for (const auto& binding : m_classBindings) {
        if (binding->getContract() == contract) {
            return *binding;
        }
    }
    auto binding = std::unique_ptr<ScriptClassBinding>(new ScriptClassBinding(*m_runtime, contract));
    const auto& result = *binding;
    m_classBindings.push_back(std::move(binding));
    return result;
}

} // namespace nomad
