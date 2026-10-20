// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/compiler/Compilation.hpp>

#include <functional>
#include <span>

#if defined(_WIN32)
#define NOMAD_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define NOMAD_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace nomad {

class CompilerContext;
class Logger;
class NomadConfiguration;
class Runtime;

enum class ModuleMode {
    Application = 1,
    Analysis,
    Packaging
};

struct ModuleContext {
    Runtime* runtime;
    const NomadConfiguration* configuration;
    Logger* logger;
    ModuleMode mode;
    NomadPath projectDirectory;
    NomadPath resourceDirectory;
    NomadPath moduleDirectory;
    // Runtime launches supply module options followed by optional "--" application arguments, without argv[0].
    std::span<const NomadString> arguments;
    std::function<void(int)> requestExit;
    std::function<bool()> pollExit;
    std::function<void()> compileScripts;
};

struct ModulePackageContext {
    const NomadConfiguration* source;
    NomadConfiguration* release;
    NomadPath projectDirectory;
    NomadPath resourceDirectory;
    NomadPath moduleDirectory;
    bool dryRun;
    NomadPath stagingDirectory;
    std::function<void(const NomadPath&, const NomadPath&)> copyFile;
    std::function<void(const NomadPath&, NomadStringView)> writeFile;
    std::function<void(const NomadPath&)> recordFile;
    std::function<void(const NomadPath&)> excludeResource;
    std::function<bool(const NomadPath&)> isResourceExcluded;
};

struct ModuleCallbacks {
    std::function<void(CompilerContext*)> configureCompiler;
    std::function<void(CompilerContext*)> preParse;
    std::function<void(CompilerContext*)> parse;
    std::function<void(CompilerContext*)> prepareFormatStrings;
    std::function<void(CompilerContext*)> resolve;
    std::function<void(CompilerContext*)> emit;
    std::function<void(CompilerContext*)> link;
    std::function<void(CompilerContext*)> verify;
    std::function<void(CompilerContext*)> compilationCompleted;
    std::function<void()> prepareCompilation;
    std::function<int()> run;
    std::function<void()> exit;
    std::function<void()> shutdown;
    std::function<NomadString()> checkResources;
    std::function<void(ModulePackageContext&)> package;
};

using RegisterModuleFn = void(ModuleContext*, ModuleCallbacks*);

} // namespace nomad
