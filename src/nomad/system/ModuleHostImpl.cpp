// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/system/ModuleHostImpl.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/SourceLoader.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/Runtime.hpp>

#include <nomad/system/Path.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <exception>
#include <utility>

namespace nomad {

namespace {

constexpr std::array PHASE_CALLBACKS{
    &ModuleCallbacks::configureCompiler,
    &ModuleCallbacks::preParse,
    &ModuleCallbacks::parse,
    &ModuleCallbacks::prepareFormatStrings,
    &ModuleCallbacks::resolve,
    &ModuleCallbacks::emit,
    &ModuleCallbacks::link,
    &ModuleCallbacks::verify,
    &ModuleCallbacks::compilationCompleted
};

} // namespace

ModuleHostImpl::~ModuleHostImpl() {
    if (!m_modules.empty() && m_modules.front()->context.mode == ModuleMode::Application) {
        requestExit(0);
        try {
            (void)pollExit();
        } catch (const std::exception& error) {
            log::error("Module exit notification failed: " + NomadString(error.what()));
        }
    }
    for (auto module = m_modules.rbegin(); module != m_modules.rend(); ++module) {
        if ((*module)->callbacks.shutdown) {
            try {
                (*module)->callbacks.shutdown();
            } catch (const std::exception& error) {
                log::error("Module shutdown failed: " + NomadString(error.what()));
            }
        }
    }
    m_compiler.reset();
    m_runtime.reset();
    for (auto module = m_modules.rbegin(); module != m_modules.rend(); ++module) {
        (*module)->callbacks = {};
    }
    while (!m_modules.empty()) {
        m_modules.pop_back();
    }
}

void ModuleHostImpl::requestExit(const int status) {
    const auto lock = std::lock_guard(m_exitMutex);
    if (!m_exitRequested.load()) {
        m_exitStatus.store(status);
        m_exitRequested.store(true);
    }
}

bool ModuleHostImpl::pollExit() {
    if (!m_exitRequested.load()) {
        return false;
    }
    if (!m_exitNotified) {
        m_exitNotified = true;
        for (auto module = m_modules.rbegin(); module != m_modules.rend(); ++module) {
            if ((*module)->callbacks.exit) {
                (*module)->callbacks.exit();
            }
        }
    }
    return true;
}

ModuleHostImpl::ModuleHostImpl(
    NomadConfiguration configuration,
    NomadPath projectDirectory,
    std::vector<NomadString> arguments,
    const ModuleMode mode,
    ProjectMetadata projectMetadata
) : m_configuration(std::move(configuration)),
    m_projectMetadata(std::move(projectMetadata)),
    m_projectDirectory(std::move(projectDirectory)),
    m_arguments(std::move(arguments)),
    m_mode(mode) {
    if (mode != ModuleMode::Packaging) {
        m_runtime = std::make_unique<Runtime>();
        m_compiler = m_runtime->createCompiler();
    }
}

void ModuleHostImpl::loadModules(const std::vector<ModuleManifest>& modules) {
    m_moduleScriptRoots = getModuleScriptRoots(modules);
    for (const auto& manifest : modules) {
        auto loaded = std::make_unique<LoadedModule>();
        loaded->name = manifest.name;
        const auto libraryPath = manifest.directory / getModuleLibraryName(manifest.name);
        try {
            loaded->library.load(libraryPath.native(), boost::dll::load_mode::load_with_altered_search_path);
        } catch (const std::exception& error) {
            throw ModuleException("Failed to load module '" + manifest.name + "' at '" +
                pathToUtf8(libraryPath) + "': " + error.what());
        }
        if (!loaded->library.has("registerModule")) {
            throw ModuleException("Module '" + manifest.name + "' does not export registerModule");
        }
        loaded->context = {
            m_runtime.get(), m_compiler.get(), &m_configuration, &log::getLogger(), m_mode,
            m_projectDirectory, manifest.directory, m_arguments,
            [this](const int status) { requestExit(status); },
            [this] { return pollExit(); }, {},
            &m_projectMetadata
        };
        m_modules.push_back(std::move(loaded));
    }

    for (auto& loaded : m_modules) {
        try {
            loaded->library.get<RegisterModuleFn>("registerModule")(&loaded->context, &loaded->callbacks);
        } catch (const NomadBug&) {
            throw;
        } catch (const NomadException& error) {
            throw ModuleException("Module '" + loaded->name + "' registration failed: " + error.what());
        }
    }
    auto runProviders = 0;
    for (const auto& loaded : m_modules) {
        if (loaded->callbacks.run) {
            ++runProviders;
        }
    }
    if (runProviders > 1) {
        throw ModuleException("Only one configured module may provide an application run callback");
    }
    if (m_runtime != nullptr) {
        const auto configureCompiler = [this](Compiler* compiler) {
            compiler->registerSourceRoots(m_moduleScriptRoots);
            compiler->registerPhaseHook([this](CompilerContext*, const CompilationPhase phase) {
                if (phase == CompilationPhase::Configure) {
                    m_compilationFailed = true;
                }
            });
        };

        configureCompiler(m_compiler.get());
        m_runtime->registerCompilerSetup(configureCompiler);
        for (const auto& loaded : m_modules) {
            auto* module = loaded.get();
            m_compiler->registerPhaseHook([module](CompilerContext* context, const CompilationPhase phase) {
                const auto index = static_cast<NomadIndex>(phase) - 1U;
                if (index < PHASE_CALLBACKS.size()) {
                    const auto& callback = module->callbacks.*PHASE_CALLBACKS[index];
                    if (callback) {
                        try {
                            callback(context);
                        } catch (const NomadBug&) {
                            throw;
                        } catch (const NomadException& error) {
                            throw ModuleException("Module '" + module->name + "' compiler hook failed: " + error.what());
                        }
                    }
                }
            });
        }
        m_compiler->registerPhaseHook([this](CompilerContext* context, const CompilationPhase phase) {
            if (phase != CompilationPhase::Completed) {
                return;
            }
            m_compilationFailed = context->hasError();
        });
    }
}

Runtime* ModuleHostImpl::getRuntime() const {
    return m_runtime.get();
}

Compiler* ModuleHostImpl::getCompiler() const {
    return m_compiler.get();
}

int ModuleHostImpl::run() {
    if (m_mode != ModuleMode::Application) {
        throw ModuleException("Only application sessions can run");
    }
    const auto runnable = std::ranges::find_if(m_modules, [](const auto& module) {
        return module->callbacks.run != nullptr;
    });
    if (runnable == m_modules.end()) {
        throw ModuleException("An application requires a module with a run callback");
    }
    if (m_compilationFailed) {
        throw ModuleException("Cannot run an application after script compilation failed");
    }
    m_runtime->initializeScripts();
    if (!pollExit()) {
        const auto status = (*runnable)->callbacks.run();
        if (!m_exitRequested.load()) {
            requestExit(status);
        }
    }
    if (!m_exitRequested.load()) {
        requestExit();
    }
    (void)pollExit();
    return m_exitStatus.load();
}

void ModuleHostImpl::package(ModulePackageContext& context) {
    for (const auto& module : m_modules) {
        if (module->callbacks.package) {
            context.moduleDirectory = module->context.moduleDirectory;
            module->callbacks.package(context);
        }
    }
}

CompilationResult ModuleHostImpl::compileProject() {
    if (m_mode == ModuleMode::Packaging) {
        throw ModuleException("Packaging sessions cannot compile scripts");
    }
    m_compilationFailed = true;
    const auto paths = resolveScriptPaths(m_configuration, m_projectDirectory);
    return compileSources(loadSources(paths),
        m_mode == ModuleMode::Analysis ? CompilationTarget::Analysis : CompilationTarget::Executable);
}

SourceBatch ModuleHostImpl::loadSources(
    const std::span<const NomadPath> paths,
    const std::span<const ScriptSource> overrides
) const {
    auto roots = m_moduleScriptRoots;
    for (const auto& path : paths) {
        roots.push_back({path});
    }
    return loadSourceRoots(roots, overrides);
}

CompilationResult ModuleHostImpl::compileSources(const SourceBatch& sources, const CompilationTarget target) {
    if (m_mode == ModuleMode::Packaging) {
        throw ModuleException("Packaging sessions cannot compile scripts");
    }
    m_compilationFailed = true;
    CompilationResult result;
    prepareCompilation([&] {
        result = nomad::compileSources(sources, *m_compiler, target);
    });
    m_compilationFailed = !result.succeeded();
    return result;
}

void ModuleHostImpl::prepareCompilation(const std::function<void()>& compileScripts) {
    if (m_mode == ModuleMode::Packaging) {
        throw ModuleException("Packaging sessions cannot compile scripts");
    }
    LoadedModule* coordinator = nullptr;
    for (const auto& module : m_modules) {
        if (!module->callbacks.prepareCompilation || module->context.mode != ModuleMode::Application) {
            continue;
        }
        if (coordinator != nullptr) {
            throw ModuleException("Only one module may coordinate compilation");
        }
        coordinator = module.get();
    }
    if (coordinator == nullptr) {
        compileScripts();
        return;
    }
    auto invoked = std::atomic<bool>{false};
    coordinator->context.compileScripts = [&] {
        if (invoked.exchange(true)) {
            throw ModuleException("Compilation coordinator invoked compilation more than once");
        }
        compileScripts();
    };
    try {
        coordinator->callbacks.prepareCompilation();
    } catch (...) {
        coordinator->context.compileScripts = {};
        throw;
    }
    coordinator->context.compileScripts = {};
    if (!invoked.load()) {
        throw ModuleException("Compilation coordinator did not compile scripts");
    }
}

NomadString ModuleHostImpl::checkResources() {
    NomadString report;
    for (const auto& module : m_modules) {
        if (module->callbacks.checkResources) {
            report += module->callbacks.checkResources();
        }
    }
    return report;
}

} // namespace nomad
