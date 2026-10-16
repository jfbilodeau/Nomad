// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/system/Module.hpp>
#include <nomad/system/ModuleManifest.hpp>
#include <nomad/system/NomadConfiguration.hpp>

#include <memory>
#include <vector>

namespace nomad {

class ModuleHost {
public:
    ModuleHost(
        NomadConfiguration configuration,
        NomadPath projectDirectory,
        NomadPath resourceDirectory,
        std::vector<ModuleManifest> modules,
        std::vector<NomadString> arguments,
        ModuleMode mode
    );
    ModuleHost(const ModuleHost&) = delete;
    ModuleHost& operator=(const ModuleHost&) = delete;
    ~ModuleHost();

    [[nodiscard]] Runtime* getRuntime() const;
    [[nodiscard]] bool pollExit();
    void requestExit(int status = 0);
    [[nodiscard]] int run(NomadStringView entry);
    void compile(const std::function<void()>& compileScripts);
    void package(ModulePackageContext& context);
    [[nodiscard]] NomadString checkResources();

private:
    struct Implementation;
    std::unique_ptr<Implementation> m_implementation;
};

} // namespace nomad
