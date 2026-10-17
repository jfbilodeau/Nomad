// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/system/ModuleHost.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/CompilerTools.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/Runtime.hpp>

#include <nomad/system/Path.hpp>

#include <boost/dll/shared_library.hpp>

#include <array>
#include <atomic>
#include <exception>
#include <mutex>
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

struct ModuleHost::Implementation {
    struct LoadedModule {
        NomadString name;
        boost::dll::shared_library library;
        ModuleContext context;
        ModuleCallbacks callbacks;
    };

    NomadConfiguration configuration;
    std::vector<NomadString> arguments;
    std::vector<std::unique_ptr<LoadedModule>> modules;
    std::unique_ptr<Runtime> runtime;
    std::atomic<bool> exitRequested = false;
    std::atomic<int> exitStatus = 0;
    std::mutex exitMutex;
    bool exitNotified = false;

    ~Implementation() {
        if (!modules.empty() && modules.front()->context.mode == ModuleMode::Application) {
            requestExit(0);
            try {
                (void)pollExit();
            } catch (const std::exception& error) {
                log::error("Module exit notification failed: " + NomadString(error.what()));
            }
        }
        for (auto module = modules.rbegin(); module != modules.rend(); ++module) {
            if ((*module)->callbacks.shutdown) {
                try {
                    (*module)->callbacks.shutdown();
                } catch (const std::exception& error) {
                    log::error("Module shutdown failed: " + NomadString(error.what()));
                }
            }
        }
        runtime.reset();
        for (auto module = modules.rbegin(); module != modules.rend(); ++module) {
            (*module)->callbacks = {};
        }
        while (!modules.empty()) {
            modules.pop_back();
        }
    }

    void requestExit(const int status) {
        const auto lock = std::lock_guard(exitMutex);
        if (!exitRequested.load()) {
            exitStatus.store(status);
            exitRequested.store(true);
        }
    }

    bool pollExit() {
        if (!exitRequested.load()) {
            return false;
        }
        if (!exitNotified) {
            exitNotified = true;
            for (auto module = modules.rbegin(); module != modules.rend(); ++module) {
                if ((*module)->callbacks.exit) {
                    (*module)->callbacks.exit();
                }
            }
        }
        return true;
    }
};

ModuleHost::ModuleHost(
    NomadConfiguration configuration,
    NomadPath projectDirectory,
    NomadPath resourceDirectory,
    std::vector<ModuleManifest> modules,
    std::vector<NomadString> arguments,
    const ModuleMode mode
) : m_implementation(std::make_unique<Implementation>()) {
    auto& implementation = *m_implementation;
    implementation.configuration = std::move(configuration);
    implementation.arguments = std::move(arguments);
    if (mode != ModuleMode::Packaging) {
        implementation.runtime = std::make_unique<Runtime>();
    }

    for (const auto& manifest : modules) {
        auto loaded = std::make_unique<Implementation::LoadedModule>();
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
            implementation.runtime.get(), &implementation.configuration, &log::getLogger(), mode,
            projectDirectory, resourceDirectory, manifest.directory, implementation.arguments,
            [&implementation](const int status) { implementation.requestExit(status); },
            [&implementation] { return implementation.pollExit(); }, {}
        };
        implementation.modules.push_back(std::move(loaded));
    }

    for (auto& loaded : implementation.modules) {
        try {
            loaded->library.get<RegisterModuleFn>("registerModule")(&loaded->context, &loaded->callbacks);
        } catch (const NomadBug&) {
            throw;
        } catch (const NomadException& error) {
            throw ModuleException("Module '" + loaded->name + "' registration failed: " + error.what());
        }
    }
    auto runProviders = 0;
    for (const auto& loaded : implementation.modules) {
        if (loaded->callbacks.run) {
            ++runProviders;
        }
    }
    if (runProviders > 1) {
        throw ModuleException("Only one configured module may provide an application run callback");
    }
    if (implementation.runtime != nullptr) {
        implementation.runtime->registerCompilerSetup([&implementation](Compiler* compiler) {
            for (const auto& loaded : implementation.modules) {
                auto* module = loaded.get();
                compiler->registerPhaseHook([module](CompilerContext* context, const CompilationPhase phase) {
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
        });
    }
}

ModuleHost::~ModuleHost() = default;

Runtime* ModuleHost::getRuntime() const {
    return m_implementation->runtime.get();
}

void ModuleHost::requestExit(const int status) {
    m_implementation->requestExit(status);
}

bool ModuleHost::pollExit() {
    return m_implementation->pollExit();
}

int ModuleHost::run(const NomadStringView entry) {
    auto& implementation = *m_implementation;
    if (implementation.runtime == nullptr) {
        throw ModuleException("Cannot execute scripts in a packaging session");
    }
    CompilationResult result;
    validateEntryFunction(result, implementation.runtime.get(), NomadString(entry), {});
    if (!result.succeeded()) {
        throw ModuleException(result.diagnostics.front().message);
    }
    implementation.runtime->executeFunction(implementation.runtime->getFunctionId(NomadString(entry)));
    if (!pollExit()) {
        for (const auto& module : implementation.modules) {
            if (module->callbacks.run) {
                const auto status = module->callbacks.run();
                if (!implementation.exitRequested.load()) {
                    requestExit(status);
                }
                break;
            }
        }
    }
    if (!implementation.exitRequested.load()) {
        requestExit();
    }
    (void)pollExit();
    return implementation.exitStatus.load();
}

void ModuleHost::package(ModulePackageContext& context) {
    for (const auto& module : m_implementation->modules) {
        if (module->callbacks.package) {
            context.moduleDirectory = module->context.moduleDirectory;
            module->callbacks.package(context);
        }
    }
}

void ModuleHost::compile(const std::function<void()>& compileScripts) {
    Implementation::LoadedModule* coordinator = nullptr;
    for (const auto& module : m_implementation->modules) {
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

NomadString ModuleHost::checkResources() {
    NomadString report;
    for (const auto& module : m_implementation->modules) {
        if (module->callbacks.checkResources) {
            report += module->callbacks.checkResources();
        }
    }
    return report;
}

} // namespace nomad
