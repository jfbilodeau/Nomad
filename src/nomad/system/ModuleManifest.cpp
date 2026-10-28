// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/system/ModuleManifest.hpp>

#include <nomad/system/NomadConfiguration.hpp>
#include <nomad/system/Path.hpp>

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <utility>

namespace nomad {

bool isValidModuleName(const NomadStringView name) {
    return !name.empty() && name.front() >= 'a' && name.front() <= 'z' &&
        std::ranges::all_of(name, [](const auto character) {
            return (character >= 'a' && character <= 'z') ||
                (character >= '0' && character <= '9') || character == '-';
        });
}

NomadPath getModuleLibraryName(const NomadStringView name) {
    if (!isValidModuleName(name)) {
        throw ModuleException("Invalid module name: '" + NomadString(name) + "'");
    }
#if defined(_WIN32)
    return pathFromUtf8(NomadString(name) + ".nomad.dll");
#elif defined(__APPLE__)
    return pathFromUtf8("lib" + NomadString(name) + ".nomad.dylib");
#else
    return pathFromUtf8("lib" + NomadString(name) + ".nomad.so");
#endif
}

ModuleManifest loadModuleManifest(const NomadPath& directory) {
    const auto path = directory / "module.toml";
    NomadConfiguration document;
    try {
        document = NomadConfiguration::readFile(path);
        if (document.requireInteger("schema") != 1) {
            throw ModuleException("Module manifest '" + pathToUtf8(path) + "' requires schema = 1");
        }
        const auto name = document.requireString("module.name");
        if (!isValidModuleName(name)) {
            throw ModuleException("Module manifest '" + pathToUtf8(path) + "' requires a valid module.name");
        }
        ModuleManifest result{name, directory, {}};
        const auto dependencies = document.getStringArray("nomad.modules");
        if (dependencies) {
            for (const auto& dependency : *dependencies) {
                if (!isValidModuleName(dependency)) {
                    throw ModuleException("Module manifest '" + pathToUtf8(path) + "' contains an invalid dependency");
                }
                if (std::ranges::find(result.dependencies, dependency) != result.dependencies.end()) {
                    throw ModuleException("Module manifest '" + pathToUtf8(path) + "' repeats dependency '" + dependency + "'");
                }
                result.dependencies.push_back(dependency);
            }
        }
        return result;
    } catch (const NomadConfigurationException& error) {
        throw ModuleException("Invalid module manifest '" + pathToUtf8(path) + "': " +
            NomadString(error.what()));
    }
}

std::vector<ModuleManifest> resolveModules(
    const NomadPath& modulesDirectory,
    const std::vector<NomadString>& requestedModules
) {
    enum class VisitState {
        Visiting = 1,
        Complete
    };
    std::unordered_map<NomadString, VisitState> visited;
    std::vector<NomadString> chain;
    std::vector<ModuleManifest> result;
    const std::function<void(const NomadString&)> visit = [&](const auto& name) {
        if (!isValidModuleName(name)) {
            throw ModuleException("Invalid module name: '" + name + "'");
        }
        if (const auto entry = visited.find(name); entry != visited.end()) {
            if (entry->second == VisitState::Complete) {
                return;
            }
            NomadString message = "Circular module dependency: ";
            for (const auto& dependency : chain) {
                message += dependency + " -> ";
            }
            throw ModuleException(message + name);
        }
        visited.emplace(name, VisitState::Visiting);
        chain.push_back(name);
        auto manifest = loadModuleManifest(modulesDirectory / pathFromUtf8(name));
        if (manifest.name != name) {
            throw ModuleException("Module '" + name + "' has mismatched manifest name '" + manifest.name + "'");
        }
        for (const auto& dependency : manifest.dependencies) {
            visit(dependency);
        }
        chain.pop_back();
        visited.at(name) = VisitState::Complete;
        result.push_back(std::move(manifest));
    };
    for (const auto& name : requestedModules) {
        visit(name);
    }
    return result;
}

} // namespace nomad
