// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/script/VariableContext.hpp>

#include <memory>
#include <unordered_map>
#include <variant>
#include <vector>

namespace nomad {

struct HubVariable {
    NomadId id;
};

struct HubFunctions {
    std::vector<NomadId> ids;
};

struct HubConstant {
    NomadId id;
};

struct HubType {
    const Type* type;
};

struct HubEvent {
    NomadId id;
};

struct HubChild {
    NomadId id;
};

struct HubDynamicVariable {
    NomadId id;
};

struct HubTypeFamily {
    NomadIndex argumentCount;
};

using HubMember = std::variant<
    HubVariable, HubFunctions, HubConstant, HubType, HubEvent, HubChild, HubDynamicVariable, HubTypeFamily
>;

class Hub {
public:
    Hub(NomadString name, NomadId parentId);
    Hub(const Hub&) = delete;
    Hub& operator=(const Hub&) = delete;

    [[nodiscard]] const NomadString& getName() const;
    [[nodiscard]] NomadId getParentId() const;
    [[nodiscard]] const HubMember* getMember(const NomadString& name) const;
    [[nodiscard]] const std::unordered_map<NomadString, HubMember>& getMembers() const;

private:
    friend class Runtime;

    NomadString m_name;
    NomadId m_parentId;
    std::unordered_map<NomadString, HubMember> m_members;
    // The context table indexes this same storage for existing variable bytecode.
    std::shared_ptr<VariableContext> m_variables;
    NomadId m_variableContextId = NOMAD_INVALID_ID;
};

} // namespace nomad
