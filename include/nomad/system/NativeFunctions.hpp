// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/script/VirtualMachine.hpp>

namespace nomad {

void registerBuildInNativeFunctions(Runtime* runtime);
// void nativeFunctionLogInfo(VirtualMachine* interpreter);
// void nativeFunctionToFloat(VirtualMachine* interpreter);
// void nativeFunctionToInteger(VirtualMachine* interpreter);

} // nomad
