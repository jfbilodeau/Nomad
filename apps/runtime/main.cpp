// Copyright (c) 2025-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/SourceLoader.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/Runtime.hpp>

#include <nomad/system/Path.hpp>
#include <nomad/system/ProjectSession.hpp>

#include <nomad/Version.hpp>

#include <boost/nowide/args.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    boost::nowide::args nowideArgs(argc, argv);
    using namespace nomad;
    try {
        std::vector<NomadString> arguments;
        auto projectPath = std::filesystem::current_path();
        for (auto index = 1; index < argc; ++index) {
            arguments.emplace_back(argv[index]);
            if (arguments.back() == "--help") {
                std::cout << "nomad-runtime [--project-path <directory-or-nomad.toml>] [module arguments]\n";
                return EXIT_SUCCESS;
            }
            if (arguments.back() == "--project-path") {
                if (index + 1 == argc) {
                    throw NomadException("--project-path requires a project directory or nomad.toml");
                }
                arguments.emplace_back(argv[++index]);
                if (arguments.back().empty() || arguments.back().starts_with("--")) {
                    throw NomadException("--project-path requires a project directory or nomad.toml");
                }
                projectPath = pathFromUtf8(arguments.back());
            }
        }
        const auto project = discoverNomadProject(projectPath);
        const auto resources = resolveProjectResourcePath(project);
        validateNomadVersionCompatibility(project.configuration.sdk.version, getNomadVersion());
        auto session = ModuleHost(
            project.document, project.root, resources,
            resolveProjectModules(project, getSdkDirectory()), arguments, ModuleMode::Application
        );
        session.compile([&] {
            auto compiler = session.getRuntime()->createCompiler();
            CompilerContext context(compiler.get());
            std::vector<NomadPath> paths;
            for (const auto& directory : project.document.getStringArray("scripts.directories").value_or(
                std::vector<NomadString>{"scripts"}
            )) {
                paths.push_back(resources / pathFromUtf8(directory));
            }
            compiler->registerSources(&context, loadSourcePaths(paths));
            const auto succeeded = compiler->compileScripts(&context);
            for (const auto& diagnostic : context.getDiagnostics()) {
                std::cerr << formatDiagnostic(diagnostic) << '\n';
            }
            if (!succeeded) {
                throw NomadException("Script compilation failed");
            }
        });
        return session.run(project.gameOptions.entryFunction);
    } catch (const std::exception& error) {
        log::error(error.what());
        return EXIT_FAILURE;
    }
}
