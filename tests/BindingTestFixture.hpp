// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/BindingGenerator.hpp>

namespace nomad::test {

class BindingTestFixture final {
public:
    BindingTestFixture();
    BindingTestFixture(const BindingTestFixture&) = delete;
    BindingTestFixture& operator=(const BindingTestFixture&) = delete;
    ~BindingTestFixture();

    [[nodiscard]] const NomadPath& getDirectory() const;
    [[nodiscard]] BindingGenerationOptions getOptions() const;
    void write(const NomadPath& path, const NomadString& content) const;
    [[nodiscard]] NomadString read(const NomadPath& path) const;
    void setScript(const NomadString& source) const;

private:
    NomadPath m_directory;
};

[[nodiscard]] NomadPath getLogBindingModuleDirectory();
[[nodiscard]] NomadString getLogBindingSource();
void compileLogBindingSource(Runtime& runtime, const NomadString& source = getLogBindingSource());

} // namespace nomad::test
