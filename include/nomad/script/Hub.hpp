// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/script/ConstantId.hpp>
#include <nomad/script/DynamicVariable.hpp>
#include <nomad/script/Event.hpp>
#include <nomad/script/VariableContext.hpp>

#include <memory>
#include <unordered_map>
#include <variant>
#include <vector>

namespace nomad {

DeclareId(HubId);

struct HubVariable {
    NomadId id;
};

struct HubFunctions {
    std::vector<CallableId> ids;
};

struct HubConstant {
    ConstantId id;
};

struct HubType {
    const Type* type;
};

struct HubEvent {
    EventId id;
};

struct HubChild {
    HubId id;
};

struct HubDynamicVariable {
    DynamicVariableId id;
};

struct HubTypeFamily {
    NomadIndex argumentCount;
};

using HubMember = std::variant<
    HubVariable, HubFunctions, HubConstant, HubType, HubEvent, HubChild, HubDynamicVariable, HubTypeFamily
>;

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
