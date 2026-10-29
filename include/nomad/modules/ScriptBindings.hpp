// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/modules/ScriptClassBinding.hpp>

#include <memory>
#include <vector>

namespace nomad {

// The runtime must outlive this cache, which must outlive its bindings and wrappers.
class ScriptBindings final {
public:
    explicit ScriptBindings(Runtime& runtime);
    ScriptBindings(const ScriptBindings&) = delete;
    ScriptBindings& operator=(const ScriptBindings&) = delete;

    [[nodiscard]] Runtime& getRuntime() const;
    [[nodiscard]] const ScriptClassBinding& getClassBinding(const BindingClass& contract);

private:
    Runtime* m_runtime;
    std::vector<std::unique_ptr<ScriptClassBinding>> m_classBindings;
};

} // namespace nomad
