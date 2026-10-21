// Copyright (c) 2025-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/log/Logger.hpp>

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
        auto applicationArguments = false;
        for (auto index = 1; index < argc; ++index) {
            arguments.emplace_back(argv[index]);
            if (applicationArguments) {
                continue;
            }
            if (arguments.back() == "--") {
                applicationArguments = true;
                continue;
            }
            if (arguments.back() == "--help") {
                std::cout << "nomad-runtime [--project-path <directory-or-nomad.toml>] "
                    "[module options] [-- application-arguments...]\n";
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
        validateNomadVersionCompatibility(project.configuration.sdk.version, getNomadVersion());
        auto session = ModuleHost::create(
            project.document, project.root,
            resolveProjectModules(project, getSdkDirectory()), arguments, ModuleMode::Application,
            project.configuration.project
        );
        const auto result = session->compileProject();
        for (const auto& diagnostic : result.diagnostics) {
            std::cerr << formatDiagnostic(diagnostic) << '\n';
        }
        if (!result.succeeded()) {
            throw NomadException("Script compilation failed");
        }
        return session->run();
    } catch (const std::exception& error) {
        log::error(error.what());
        return EXIT_FAILURE;
    }
}
