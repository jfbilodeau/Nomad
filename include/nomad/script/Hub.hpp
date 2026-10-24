// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/Runtime.hpp>

#include <memory>
#include <unordered_map>

namespace nomad {

class Hub {
public:
    Hub(NomadString name, HubId parentId);
    Hub(const Hub&) = delete;
    Hub& operator=(const Hub&) = delete;

    [[nodiscard]] const NomadString& getName() const;
    [[nodiscard]] HubId getParentId() const;
    [[nodiscard]] const HubMember* getMember(const NomadString& name) const;
    [[nodiscard]] const std::unordered_map<NomadString, HubMember>& getMembers() const;

private:
    friend class Runtime;

    NomadString m_name;
    HubId m_parentId;
    std::unordered_map<NomadString, HubMember> m_members;
    // The context table indexes this same storage for existing variable bytecode.
    std::shared_ptr<VariableContext> m_variables;
    VariableContextId m_variableContextId = VariableContextId::INVALID;
};

} // namespace nomad
