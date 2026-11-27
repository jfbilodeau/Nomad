// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/script/VirtualMachine.hpp>

namespace nomad {

void registerSystemFunctions(Runtime* runtime);
// Requires an existing math hub.
void registerSystemHubFunctions(Runtime* runtime);
void registerSystemPrimitives(Runtime* runtime);

} // nomad
