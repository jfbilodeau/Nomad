// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/system/ModuleHost.hpp>

#include <boost/dll/shared_library.hpp>

#include <atomic>
#include <mutex>

namespace nomad {

class ModuleHostImpl final : public ModuleHost {
public:
    ModuleHostImpl(
        NomadConfiguration configuration,
        NomadPath projectDirectory,
        std::vector<NomadString> arguments,
        ModuleMode mode,
        ProjectMetadata projectMetadata
    );
    ~ModuleHostImpl() override;

    void loadModules(const std::vector<ModuleManifest>& modules);
    [[nodiscard]] Runtime* getRuntime() const override;
    [[nodiscard]] bool pollExit() override;
    void requestExit(int status = 0) override;
    [[nodiscard]] int run() override;
    [[nodiscard]] CompilationResult compileProject() override;
    [[nodiscard]] CompilationResult compileSources(
        const SourceBatch& sources,
        CompilationTarget target
    ) override;
    void package(ModulePackageContext& context) override;
    [[nodiscard]] NomadString checkResources() override;

private:
    struct LoadedModule {
        NomadString name;
        boost::dll::shared_library library;
        ModuleContext context;
        ModuleCallbacks callbacks;
    };

    void prepareCompilation(const std::function<void()>& compileScripts);

    NomadConfiguration m_configuration;
    ProjectMetadata m_projectMetadata;
    NomadPath m_projectDirectory;
    std::vector<NomadString> m_arguments;
    std::vector<std::unique_ptr<LoadedModule>> m_modules;
    std::unique_ptr<Runtime> m_runtime;
    std::atomic<bool> m_exitRequested = false;
    std::atomic<int> m_exitStatus = 0;
    std::mutex m_exitMutex;
    bool m_exitNotified = false;
    ModuleMode m_mode;
    bool m_compilationFailed = false;
};

} // namespace nomad
