// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <charconv>
#include <compare>
#include <ostream>
#include <system_error>

namespace nomad {

class NomadVersionError final : public NomadException {
public:
    using NomadException::NomadException;
};

class NomadVersion final {
public:
    constexpr NomadVersion(
        const NomadInteger major,
        const NomadInteger minor,
        const NomadInteger patch
    ) :
        m_major(major),
        m_minor(minor),
        m_patch(patch)
    {
        if (major < 0 || minor < 0 || patch < 0) {
            throw NomadVersionError("Nomad version components cannot be negative");
        }
    }

    [[nodiscard]] static NomadVersion parse(const NomadStringView value) {
        const auto firstSeparator = value.find('.');
        const auto secondSeparator = firstSeparator == NomadStringView::npos
            ? NomadStringView::npos
            : value.find('.', firstSeparator + 1U);

        if (
            firstSeparator == NomadStringView::npos ||
            secondSeparator == NomadStringView::npos ||
            value.find('.', secondSeparator + 1U) != NomadStringView::npos
        ) {
            throw NomadVersionError(
                "Invalid Nomad version '" + NomadString(value) + "'; expected major.minor.patch"
            );
        }

        return {
            parseComponent(value.substr(0U, firstSeparator), value),
            parseComponent(
                value.substr(firstSeparator + 1U, secondSeparator - firstSeparator - 1U),
                value
            ),
            parseComponent(value.substr(secondSeparator + 1U), value)
        };
    }

    [[nodiscard]] constexpr NomadInteger getMajor() const noexcept {
        return m_major;
    }

    [[nodiscard]] constexpr NomadInteger getMinor() const noexcept {
        return m_minor;
    }

    [[nodiscard]] constexpr NomadInteger getPatch() const noexcept {
        return m_patch;
    }

    [[nodiscard]] NomadString toString() const {
        return
            std::to_string(m_major) + "." +
            std::to_string(m_minor) + "." +
            std::to_string(m_patch);
    }

    auto operator<=>(const NomadVersion&) const = default;

private:
    [[nodiscard]] static NomadInteger parseComponent(
        const NomadStringView component,
        const NomadStringView version
    ) {
        NomadInteger result = 0;
        const auto* const begin = component.data();
        const auto* const end = begin + component.size();
        const auto [position, error] = std::from_chars(begin, end, result);

        if (
            component.empty() ||
            (component.size() > 1U && component.front() == '0') ||
            error != std::errc() ||
            position != end
        ) {
            throw NomadVersionError(
                "Invalid Nomad version '" + NomadString(version) + "'; expected major.minor.patch"
            );
        }

        return result;
    }

    NomadInteger m_major;
    NomadInteger m_minor;
    NomadInteger m_patch;
};

inline std::ostream& operator<<(std::ostream& output, const NomadVersion& version) {
    return output << version.toString();
}

} // namespace nomad
