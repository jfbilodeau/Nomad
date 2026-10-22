// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/ScriptObjectType.hpp>

namespace nomad {

ScriptObjectType::ScriptObjectType(NomadString name, const bool open, NomadString documentation):
    ObjectType(std::move(name), {}, std::move(documentation)),
    m_open(open)
{}

bool ScriptObjectType::isOpen() const {
    return m_open;
}

} // namespace nomad
