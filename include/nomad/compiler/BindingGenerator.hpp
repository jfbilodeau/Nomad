// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/ScriptBindingObject.hpp>

#include <optional>

namespace nomad {

struct BindingModel {
    NomadString module;
    std::vector<BindingEnum> enums;
    std::vector<BindingClass> classes;
};

struct BindingOutput {
    NomadPath path;
    NomadString content;
};

struct BindingGenerationOptions {
    NomadPath moduleDirectory;
    std::optional<NomadPath> outputDirectory;
    std::optional<NomadPath> sdkDirectory;
    bool check = false;
};

struct BindingGenerationResult {
    bool fresh = true;
    std::vector<NomadPath> changedFiles;
};

[[nodiscard]] BindingModel analyzeModuleBindings(const BindingGenerationOptions& options);
[[nodiscard]] std::vector<BindingOutput> renderModuleBindings(const BindingModel& model);
[[nodiscard]] BindingGenerationResult generateModuleBindings(const BindingGenerationOptions& options);

} // namespace nomad
