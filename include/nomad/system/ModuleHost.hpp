// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/Compilation.hpp>
#include <nomad/compiler/Source.hpp>

#include <nomad/project/ProjectMetadata.hpp>

#include <nomad/system/Module.hpp>
#include <nomad/system/ModuleManifest.hpp>
#include <nomad/system/NomadConfiguration.hpp>

#include <memory>
#include <span>
#include <vector>

namespace nomad {

class ModuleHost {
public:
    [[nodiscard]] static std::unique_ptr<ModuleHost> create(
        NomadConfiguration configuration,
        NomadPath projectDirectory,
        std::vector<ModuleManifest> modules,
        std::vector<NomadString> arguments,
        ModuleMode mode,
        ProjectMetadata projectMetadata = {}
    );
    ModuleHost(const ModuleHost&) = delete;
    ModuleHost& operator=(const ModuleHost&) = delete;
    virtual ~ModuleHost() = default;

    [[nodiscard]] virtual Runtime* getRuntime() const = 0;
    [[nodiscard]] virtual Compiler* getCompiler() const = 0;
    [[nodiscard]] virtual bool pollExit() = 0;
    virtual void requestExit(int status = 0) = 0;
    [[nodiscard]] virtual int run() = 0;
    [[nodiscard]] virtual CompilationResult compileProject() = 0;
    [[nodiscard]] virtual SourceBatch loadSources(
        std::span<const NomadPath> paths,
        std::span<const ScriptSource> overrides = {}
    ) const = 0;
    [[nodiscard]] virtual CompilationResult compileSources(
        const SourceBatch& sources,
        CompilationTarget target = CompilationTarget::Executable
    ) = 0;
    virtual void package(ModulePackageContext& context) = 0;
    [[nodiscard]] virtual NomadString checkResources() = 0;

protected:
    ModuleHost() = default;
};

} // namespace nomad
