// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/VirtualMachine.hpp>

#include <functional>

namespace nomad {

using DynamicVariableSetFn = std::function<void(VirtualMachine*, const RuntimeValue&)>;
using DynamicVariableGetFn = std::function<void(VirtualMachine*, RuntimeValue&)> ;

void invalidSetFn(VirtualMachine* interpreter, RuntimeValue& value);
void invalidGetFn(VirtualMachine* interpreter, const RuntimeValue& value);

} // namespace nomad
