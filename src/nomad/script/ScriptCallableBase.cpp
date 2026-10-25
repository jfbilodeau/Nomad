// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/ScriptCallableBase.hpp>

#include <utility>

namespace nomad {

ScriptCallableBase::ScriptCallableBase(NomadString path, NomadString source):
    m_path(std::move(path)),
    m_source(std::move(source))
{}

ScriptCallableBase::~ScriptCallableBase() = default;

} // namespace nomad
