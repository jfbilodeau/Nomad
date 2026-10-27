// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/NativeFunctions.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <iostream>

namespace nomad {

void registerCoreNativeFunctions(Runtime* runtime) {
    const auto printId = runtime->registerNativeFunction(
        "print",
        [](const VirtualMachine* machine) {
            std::cout << machine->getStringParameter(0)->getString() << std::endl;
            if (!std::cout) {
                throw NomadException("Failed to write to standard output");
            }
        },
        {defParameter("message", runtime->getStringType(), NomadParamDoc("The text to print"))},
        runtime->getVoidType(),
        NomadDoc("Writes plain text followed by a newline to standard output, independently of logging.")
    );
    if (printId.isInvalid()) {
        throw NomadException("Failed to register print");
    }
}

} // namespace nomad
