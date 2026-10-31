// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/Array1dObject.hpp>
#include <nomad/collections/MapObject.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>

#include <nomad/system/Module.hpp>
#include <nomad/system/Path.hpp>

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/nowide/convert.hpp>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <memory>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <crt_externs.h>
#else
extern char** environ;
#endif

namespace nomad {

namespace {

NomadInteger getPlatform() {
#if defined(_WIN32)
    return 1;
#elif defined(__linux__)
    return 2;
#elif defined(__APPLE__)
    return 3;
#else
    throw NomadException("os does not support this platform");
#endif
}

NomadInteger getArchitecture() {
#if defined(_M_ARM64) || defined(_M_ARM64EC) || defined(__aarch64__)
    return 4;
#elif defined(_M_ARM) || defined(__arm__)
    return 3;
#elif defined(_M_X64) || defined(__x86_64__)
    return 2;
#elif defined(_M_IX86) || defined(__i386__)
    return 1;
#else
    throw NomadException("os does not support this architecture");
#endif
}

// registerConstant retains object values; release the original reference even if registration fails.
void registerSnapshot(Runtime* runtime, const NomadString& name, RuntimeValue value, const Type* type) {
    try {
        if (runtime->registerConstant(name, value, type).isInvalid()) {
            throw NomadException("Failed to register " + name);
        }
    } catch (...) {
        type->freeValue(&value);
        throw;
    }
    type->freeValue(&value);
}

void setEnvironmentEntry(MapObject& environment, const NomadStringView entry) {
    const auto separator = entry.find('=');
    // Windows includes hidden drive-current-directory entries whose names begin with '='.
    if (separator == NomadStringView::npos || separator == 0) {
        return;
    }
    RuntimeValue value(NomadString(entry.substr(separator + 1)));
    try {
        environment.setValue(entry.substr(0, separator), value);
    } catch (...) {
        value.freeObjectValue();
        throw;
    }
    value.freeObjectValue();
}

void captureEnvironment(MapObject& environment) {
#if defined(_WIN32)
    const auto releaseEnvironment = [](wchar_t* block) { FreeEnvironmentStringsW(block); };
    const auto block = std::unique_ptr<wchar_t, decltype(releaseEnvironment)>(
        GetEnvironmentStringsW(), releaseEnvironment
    );
    if (!block) {
        throw NomadException("Could not read the process environment");
    }
    for (auto* entry = block.get(); *entry != L'\0';) {
        const auto text = boost::nowide::narrow(entry);
        setEnvironmentEntry(environment, text);
        while (*entry != L'\0') {
            ++entry;
        }
        ++entry;
    }
#else
#if defined(__APPLE__)
    auto** entries = *_NSGetEnviron();
#else
    auto** entries = environ;
#endif
    if (entries != nullptr) {
        for (auto** entry = entries; *entry != nullptr; ++entry) {
            setEnvironmentEntry(environment, *entry);
        }
    }
#endif
}

std::unique_ptr<Array1dObject> captureArguments(const ModuleContext& context, const Type* stringType) {
    auto applicationArguments = std::span<const NomadString>{};
    if (context.mode != ModuleMode::Analysis) {
        const auto separator = std::ranges::find(context.arguments, "--");
        if (separator != context.arguments.end()) {
            applicationArguments = context.arguments.subspan(
                static_cast<NomadIndex>(separator - context.arguments.begin()) + 1
            );
        }
    }
    if (applicationArguments.size() > static_cast<NomadIndex>(std::numeric_limits<NomadInteger>::max())) {
        throw NomadException("Too many application arguments");
    }
    auto arguments = applicationArguments.empty()
        ? std::make_unique<Array1dObject>(stringType)
        : std::make_unique<Array1dObject>(
            stringType, static_cast<NomadInteger>(applicationArguments.size())
        );
    for (auto index = NomadIndex{0}; index < applicationArguments.size(); ++index) {
        RuntimeValue value(applicationArguments[index]);
        try {
            arguments->setValue(static_cast<NomadInteger>(index), value);
        } catch (...) {
            value.freeObjectValue();
            throw;
        }
        value.freeObjectValue();
    }
    return arguments;
}

} // namespace

void registerOs(ModuleContext* context) {
    auto* runtime = context->runtime;
    if (runtime->registerHub("os", HubMode::Closed).isInvalid()) {
        throw NomadException("Failed to register os hub");
    }
    const auto platformTypeId = runtime->createEnum("os.Platform", {
        {"Windows", 1}, {"Linux", 2}, {"MacOS", 3}
    });
    const auto architectureTypeId = runtime->createEnum("os.Architecture", {
        {"X86", 1}, {"X64", 2}, {"Arm32", 3}, {"Arm64", 4}
    });
    const auto platformType = runtime->getType(platformTypeId);
    const auto architectureType = runtime->getType(architectureTypeId);
    const auto* stringType = runtime->getStringType();
    const auto* argumentsType = runtime->getParameterizedType("ReadOnlyArray1", {stringType});
    const auto* environmentType = runtime->getParameterizedType("ReadOnlyMap", {stringType, stringType});
    if (!platformType || !architectureType ||
        argumentsType == nullptr || environmentType == nullptr) {
        throw NomadException("Failed to resolve os types; collections is required");
    }

    const auto analysis = context->mode == ModuleMode::Analysis;
    auto arguments = captureArguments(*context, stringType);
    auto environment = std::make_unique<MapObject>(stringType);
    if (!analysis) {
        captureEnvironment(*environment);
    }

    RuntimeValue argumentsValue;
    argumentsValue.setObjectValue(arguments.release());
    registerSnapshot(runtime, "os.argv", argumentsValue, argumentsType);
    RuntimeValue environmentValue;
    environmentValue.setObjectValue(environment.release());
    registerSnapshot(runtime, "os.env", environmentValue, environmentType);
    registerSnapshot(runtime, "os.platform", RuntimeValue(getPlatform()), *platformType);
    registerSnapshot(runtime, "os.architecture", RuntimeValue(getArchitecture()), *architectureType);
    registerSnapshot(runtime, "os.currentDirectory",
        RuntimeValue(analysis ? NomadString{} : pathToUtf8(std::filesystem::current_path())), stringType);
    registerSnapshot(runtime, "os.executablePath",
        RuntimeValue(analysis ? NomadString{} : pathToUtf8(NomadPath(boost::dll::program_location().native()))),
        stringType);
    registerSnapshot(runtime, "os.tempDirectory",
        RuntimeValue(analysis ? NomadString{} : pathToUtf8(std::filesystem::temp_directory_path())), stringType);
}

} // namespace nomad

NOMAD_MODULE_EXPORT void registerModule(nomad::ModuleContext* context, nomad::ModuleCallbacks*) noexcept(false) {
    if (context->mode == nomad::ModuleMode::Packaging) {
        return;
    }
    nomad::registerOs(context);
}
