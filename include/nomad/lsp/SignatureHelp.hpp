// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <boost/json/value.hpp>

#include <functional>

namespace nomad {

class Runtime;
class Type;

[[nodiscard]] boost::json::value createSignatureHelp(
    Runtime& runtime,
    const NomadString& sourceName,
    NomadIndex line,
    const NomadString& linePrefix,
    const std::function<const Type*(const NomadString&)>& findValueType
);

} // namespace nomad
