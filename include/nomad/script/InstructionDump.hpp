// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <iosfwd>

namespace nomad {

class Runtime;
class ScriptCallableBase;

// Formats compiled instructions without compiling or executing scripts.
void dumpInstructions(const Runtime& runtime, std::ostream& stream);
void dumpInstructions(const Runtime& runtime, const ScriptCallableBase& callable, std::ostream& stream);

} // namespace nomad
