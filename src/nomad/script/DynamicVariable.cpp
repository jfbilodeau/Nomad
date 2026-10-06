// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/DynamicVariable.hpp>

#include <nomad/script/Variable.hpp>
#include <nomad/script/VirtualMachine.hpp>
#include <nomad/script/Runtime.hpp>

namespace nomad {

void invalidSetFn(VirtualMachine* /*interpreter*/, RuntimeValue& /*value*/) {
    log::warning("Cannot set variable");
}

void invalidGetFn(VirtualMachine* /*interpreter*/, const RuntimeValue& /*value*/) {
    log::warning("Cannot get variable");
}

} // namespace nomad
