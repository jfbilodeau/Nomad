// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#ifndef NOMAD_DECOMPILER_HPP
#define NOMAD_DECOMPILER_HPP

#include <nomad/Nomad.hpp>

namespace nomad {

// Forward declarations
class Runtime;
class Function;

NomadString decompile(Runtime* runtime, Function* function);

} // nomad

#endif //NOMAD_DECOMPILER_HPP
