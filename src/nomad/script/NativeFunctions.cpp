// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <cmath>

#include <nomad/script/NativeFunctions.hpp>

#include <nomad/script/VirtualMachine.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/system/Float.hpp>
#include <nomad/system/Integer.hpp>

#include <cstdlib>

namespace nomad {

void registerBuildInNativeFunctions(Runtime *runtime) {
    runtime->registerNativeFunction(
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

    runtime->registerNativeFunction(
        "log.info",
        [](const VirtualMachine* interpreter) {
            const auto fileNameValue = interpreter->getStringParameter(0);
            const auto functionNameValue = interpreter->getStringParameter(1);
            const auto lineNumber = interpreter->getIntegerParameter(2);
            const auto messageValue = interpreter->getStringParameter(3);

            const auto fileName = fileNameValue != nullptr ? fileNameValue : "<unknown>";
            const auto functionName = functionNameValue != nullptr ? functionNameValue : "<unknown>";
            const NomadString message = messageValue != nullptr ? messageValue : "";

            const Location location(
                functionName,
                fileName,
                lineNumber,
                0
            );

           log::info(message, location);
        },  // NativeFunction function
        {
            defParameter("$file", runtime->getFileNameType(), NomadParamDoc("(hidden) The file name from which the log is issued")),
            defParameter("$function", runtime->getFunctionNameType(), NomadParamDoc("(hidden) The function name from which the log is issued")),
            defParameter("$line", runtime->getLineNumberType(), NomadParamDoc("(hidden) The line number from which the log is issued")),
            defParameter("message", runtime->getStringRefType(), NomadParamDoc("The text to log"))
        },
        runtime->getVoidType(),
        NomadDoc("Write a 'info' level string to the log.")
    );

    runtime->registerNativeFunction(
        "math.ceiling",
        [](VirtualMachine* interpreter) {
            auto value = interpreter->getFloatParameter(0);

            auto ceiled_value = floatCeiling(value);

            interpreter->setFloatResult(ceiled_value);
        },  // NativeFunction function
        {
            defParameter("value", runtime->getFloatType(), NomadParamDoc("The float to ceiling"))
        },
        runtime->getFloatType(),
        NomadDoc("Ceiling a float value.")
    );

    runtime->registerNativeFunction(
        "math.floor",
        [](VirtualMachine* interpreter) {
            auto value = interpreter->getFloatParameter(0);

            auto floored_value = floatFloor(value);

            interpreter->setFloatResult(floored_value);
        },  // NativeFunction function
        {
            defParameter("value", runtime->getFloatType(), NomadParamDoc("The float to floor"))
        },
        runtime->getFloatType(),
        NomadDoc("Floor a float value.")
    );

    runtime->registerNativeFunction(
        "toFloat",
        [](VirtualMachine* interpreter) {
            const auto* value = interpreter->getStringParameter(0);

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

    runtime->registerNativeFunction(
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

    runtime->registerNativeFunction(
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

    runtime->registerNativeFunction(
        "toInt",
        [](VirtualMachine* interpreter) {
            const auto* value = interpreter->getStringParameter(0);

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
}

} // nomad
