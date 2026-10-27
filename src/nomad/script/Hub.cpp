// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Hub.hpp>

#include <nomad/script/VariableContext.hpp>

#include <utility>

namespace nomad {

Hub::Hub(NomadString name, const HubMode mode):
    m_name(std::move(name)),
    m_mode(mode),
    m_variables(std::make_shared<SimpleVariableContext>())
{}

const NomadString& Hub::getName() const {
    return m_name;
}

HubMode Hub::getMode() const {
    return m_mode;
}

const HubMember* Hub::getMember(const NomadString& name) const {
    const auto member = m_members.find(name);
    return member == m_members.end() ? nullptr : &member->second;
}

const std::unordered_map<NomadString, HubMember>& Hub::getMembers() const {
    return m_members;
}

} // namespace nomad
