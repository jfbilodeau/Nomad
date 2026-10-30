// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <GenerateBindingsCommand.hpp>

#include <nomad/modules/BindingGenerator.hpp>

#include <nomad/system/Path.hpp>
#include <nomad/system/ProgramOptions.hpp>

#include <cstdlib>
#include <iostream>

namespace nomad {

namespace {

struct GenerateBindingsParameters {
    NomadString directory;
    std::optional<NomadString> output;
    std::optional<NomadString> sdk;
    bool check = false;
};

int generateBindingsCommand(GenerateBindingsParameters& parameters) {
    BindingGenerationOptions options;
    options.moduleDirectory = pathFromUtf8(parameters.directory);
    if (parameters.output) {
        options.outputDirectory = pathFromUtf8(*parameters.output);
    }
    if (parameters.sdk) {
        options.sdkDirectory = pathFromUtf8(*parameters.sdk);
    }
    options.check = parameters.check;
    const auto result = generateModuleBindings(options);
    if (parameters.check && !result.fresh) {
        std::cerr << "Generated bindings are stale or missing:\n";
        for (const auto& file : result.changedFiles) {
            std::cerr << "  " << pathToUtf8(file) << '\n';
        }
        return EXIT_FAILURE;
    }
    std::cout << (parameters.check ? "Bindings are current.\n" :
        result.fresh ? "Bindings unchanged.\n" : "Bindings generated.\n");
    return EXIT_SUCCESS;
}

} // namespace

void registerGenerateBindingsCommand(ProgramOptions& program) {
    program.addVerb<GenerateBindingsParameters>(
        "generate-bindings", &generateBindingsCommand, "Generate experimental C++ interfaces from module scripts"
    )
        .addRequiredPositional("module-directory", &GenerateBindingsParameters::directory, "Module containing module.toml")
        .addOption("output", "", &GenerateBindingsParameters::output, "Output directory (default: module-local generated)")
        .addOption("sdk", "", &GenerateBindingsParameters::sdk, "Explicit SDK root for script dependencies under modules")
        .addFlag("check", "", &GenerateBindingsParameters::check, "Check freshness without writing files");
}

} // namespace nomad
