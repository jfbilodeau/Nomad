// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <BindingTestFixture.hpp>

#include <nomad/compiler/Compilation.hpp>
#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/SourceLoader.hpp>

#include <nomad/system/Path.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>

namespace nomad::test {

namespace {

NomadString readBindingFile(const NomadPath& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw NomadException("Cannot read binding test file: " + pathToUtf8(path));
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} // namespace

BindingTestFixture::BindingTestFixture() {
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (auto index = 0; index < 100; ++index) {
        m_directory = std::filesystem::current_path() /
            ("binding-test-" + std::to_string(timestamp) + "-" + std::to_string(index));
        if (std::filesystem::create_directory(m_directory)) {
            write("module.toml", "schema = 1\n[module]\nname = 'test-module'\n");
            setScript("class Settings\nactive = true\ncount = 3\nratio = 1.5\nname = \"initial\"\nend\n");
            return;
        }
    }
    throw NomadException("Cannot create binding test directory");
}

BindingTestFixture::~BindingTestFixture() {
    std::error_code error;
    std::filesystem::remove_all(m_directory, error);
}

const NomadPath& BindingTestFixture::getDirectory() const {
    return m_directory;
}

BindingGenerationOptions BindingTestFixture::getOptions() const {
    return {m_directory, std::nullopt, std::nullopt, false};
}

void BindingTestFixture::write(const NomadPath& path, const NomadString& content) const {
    const auto absolute = m_directory / path;
    std::filesystem::create_directories(absolute.parent_path());
    std::ofstream output(absolute, std::ios::binary);
    output << content;
    output.close();
    if (!output) {
        throw NomadException("Cannot write binding test file: " + pathToUtf8(absolute));
    }
}

NomadString BindingTestFixture::read(const NomadPath& path) const {
    return readBindingFile(m_directory / path);
}

void BindingTestFixture::setScript(const NomadString& source) const {
    write(NomadPath("scripts") / "settings.nomad", source);
}

NomadPath getLogBindingModuleDirectory() {
    return pathFromUtf8(NOMAD_TEST_LOG_BINDING_MODULE_DIRECTORY);
}

NomadString getLogBindingSource() {
    auto source = readBindingFile(getLogBindingModuleDirectory() / "scripts" / "configuration.nomad");
    std::erase(source, '\r');
    return source;
}

void compileLogBindingSource(Runtime& runtime, const NomadString& source) {
    auto compiler = runtime.createCompiler();
    const auto result = compileSources({{{"log.configuration", {"configuration.nomad", source}, "log",
        "configuration.nomad"}}, {}}, *compiler);
    if (!result.succeeded()) {
        NomadString message;
        for (const auto& diagnostic : result.diagnostics) {
            message += formatDiagnostic(diagnostic) + "\n";
        }
        throw NomadException(message);
    }
}

} // namespace nomad::test
