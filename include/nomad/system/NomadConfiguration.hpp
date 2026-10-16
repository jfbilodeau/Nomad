// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace nomad {

class NomadConfigurationError final : public NomadException {
public:
    using NomadException::NomadException;
};

class NomadConfiguration {
public:
    NomadConfiguration();
    ~NomadConfiguration();
    NomadConfiguration(const NomadConfiguration& other);
    NomadConfiguration& operator=(const NomadConfiguration& other);
    NomadConfiguration(NomadConfiguration&& other) noexcept;
    NomadConfiguration& operator=(NomadConfiguration&& other) noexcept;

    [[nodiscard]] static NomadConfiguration parse(NomadStringView text);
    [[nodiscard]] static NomadConfiguration readFile(const NomadPath& path);
    [[nodiscard]] NomadString serialize() const;

    [[nodiscard]] bool contains(NomadStringView path) const;
    [[nodiscard]] std::optional<NomadString> getString(NomadStringView path) const;
    [[nodiscard]] std::optional<NomadInteger> getInteger(NomadStringView path) const;
    [[nodiscard]] std::optional<bool> getBoolean(NomadStringView path) const;
    [[nodiscard]] std::optional<std::vector<NomadString>> getStringArray(NomadStringView path) const;
    [[nodiscard]] NomadString requireString(NomadStringView path) const;
    [[nodiscard]] NomadInteger requireInteger(NomadStringView path) const;
    [[nodiscard]] bool requireBoolean(NomadStringView path) const;
    [[nodiscard]] std::vector<NomadString> requireStringArray(NomadStringView path) const;

    void setString(NomadStringView path, NomadStringView value);
    void setInteger(NomadStringView path, NomadInteger value);
    void setBoolean(NomadStringView path, bool value);
    void setStringArray(NomadStringView path, const std::vector<NomadString>& values);
    void merge(const NomadConfiguration& other);
    bool erase(NomadStringView path);

private:
    struct Implementation;
    std::unique_ptr<Implementation> m_implementation;
};

} // namespace nomad
