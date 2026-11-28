// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/system/SystemFunctions.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/Float.hpp>
#include <nomad/system/Integer.hpp>

#include <cmath>
#include <cstdlib>

namespace nomad {

namespace {

void registerBreakpoint(Runtime* runtime) {
    const auto id = runtime->registerNativeFunction(
        "breakpoint",
        [](const VirtualMachine* interpreter) {
            const auto instructionIndex = interpreter->getInstructionIndex();

            #if defined(__cpp_lib_breakpoint) && __cpp_lib_breakpoint >= 202202L
            std::breakpoint();
            #elif defined(_MSC_VER)
            __debugbreak();
            #elif defined(__GNUC__)
            __builtin_trap();
            #endif

            log::debug("BREAKPOINT: " + toString(instructionIndex));
        },
        {
        },
        runtime->getVoidType(),
        NomadDoc("Set a breakpoint")
    );

    if (id.isInvalid()) {
        throw NomadException("Failed to register breakpoint");
    }
}

void registerConversions(Runtime* runtime) {
    const auto stringToFloatId = runtime->registerNativeFunction(
        "toFloat",
        [](VirtualMachine* interpreter) {
            const auto* value = interpreter->getStringParameter(0)->getText();

            NomadChar* end = nullptr;
            const auto convertedValue = static_cast<NomadFloat>(std::strtod(value, &end));

            // Anything that is not a number converts to zero rather than aborting the function.
            interpreter->setFloatResult(end == value ? NomadFloat{0} : convertedValue);
        },
        {
            defParameter("value", runtime->getStringType(), NomadParamDoc("The string to convert to a float"))
        },
        runtime->getFloatType(),
        NomadDoc("Convert a string to a float. Returns 0 if the string is not a number.")
    );

    const auto integerToFloatId = runtime->registerNativeFunction(
        "toFloat",
        [](VirtualMachine* interpreter) {
            const auto value = interpreter->getIntegerParameter(0);

            const auto converted_value = integerToFloat(value);

            interpreter->setFloatResult(converted_value);
        },  // NativeFunction function
        {
            defParameter("value", runtime->getIntegerType(), NomadParamDoc("The integer to convert to a float"))
        },
        runtime->getFloatType(),
        NomadDoc("Convert an integer to a float.")
    );

    const auto floatToIntegerId = runtime->registerNativeFunction(
        "toInt",
        [](VirtualMachine* interpreter) {
            const auto value = interpreter->getFloatParameter(0);

            const auto converted_value = floatToInteger(value);

            interpreter->setIntegerResult(converted_value);
        },
        {
            defParameter("value", runtime->getFloatType(), NomadParamDoc("The float to convert to an integer"))
        },
        runtime->getIntegerType(),
        NomadDoc("Convert a float to an integer.")
    );

    const auto stringToIntegerId = runtime->registerNativeFunction(
        "toInt",
        [](VirtualMachine* interpreter) {
            const auto* value = interpreter->getStringParameter(0)->getText();

            NomadChar* end = nullptr;
            const auto convertedValue = static_cast<NomadInteger>(std::strtoll(value, &end, 10));

            // Anything that is not a number converts to zero rather than aborting the function.
            interpreter->setIntegerResult(end == value ? 0 : convertedValue);
        },
        {
            defParameter("value", runtime->getStringType(), NomadParamDoc("The string to convert to an integer"))
        },
        runtime->getIntegerType(),
        NomadDoc("Convert a string to an integer. Returns 0 if the string is not a number.")
    );

    if (
        stringToFloatId.isInvalid() ||
        integerToFloatId.isInvalid() ||
        floatToIntegerId.isInvalid() ||
        stringToIntegerId.isInvalid()
    ) {
        throw NomadException("Failed to register system conversions");
    }
}

} // namespace

void registerSystemFunctions(Runtime* runtime) {
    registerBreakpoint(runtime);

    const auto ceilingId = runtime->registerNativeFunction(
        "math.ceiling",
        floatCeiling,
        {{"value", NomadParamDoc("The float to ceiling")}},
        NomadDoc("Ceiling a float value.")
    );
    const auto floorId = runtime->registerNativeFunction(
        "math.floor",
        floatFloor,
        {{"value", NomadParamDoc("The float to floor")}},
        NomadDoc("Floor a float value.")
    );

    if (ceilingId.isInvalid() || floorId.isInvalid()) {
        throw NomadException("Failed to register math functions");
    }

    registerConversions(runtime);
}

void registerSystemHubFunctions(Runtime* runtime) {
    const auto hubId = runtime->getHubId("math");
    if (hubId.isInvalid()) {
        throw NomadException("System module requires a script-declared math hub");
    }

    const auto ceilingId = runtime->registerHubNativeFunction(
        hubId,
        "ceiling",
        floatCeiling,
        {{"value", NomadParamDoc("The float to ceiling")}},
        NomadDoc("Ceiling a float value.")
    );
    const auto floorId = runtime->registerHubNativeFunction(
        hubId,
        "floor",
        floatFloor,
        {{"value", NomadParamDoc("The float to floor")}},
        NomadDoc("Floor a float value.")
    );

    if (ceilingId.isInvalid() || floorId.isInvalid()) {
        throw NomadException("Failed to register math hub functions");
    }
}

void registerSystemPrimitives(Runtime* runtime) {
    registerBreakpoint(runtime);
    registerConversions(runtime);
}

} // nomad
