// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/system/TempHeap.hpp>
#include <nomad/Nomad.hpp>

#include <cctype>
#include <cstring>
#include <vector>

namespace nomad {

class NomadStringBase {
public:
    NomadStringBase();
    NomadStringBase(const NomadStringBase& other);
    NomadStringBase(NomadStringBase&& other) noexcept;
    NomadStringBase(const NomadChar* cStr);
    NomadStringBase(const std::string& stdString);

    const NomadChar* toCString() const;
    const std::string& toStdString() const;
};

inline NomadString stringConcatenate(const NomadStringView a, const NomadStringView b) {
    NomadString result;
    result.reserve(a.size() + b.size());
    result.append(a);
    result.append(b);
    return result;
}

inline NomadBoolean stringEqualTo(const NomadStringView a, const NomadStringView b) {
    return a == b;
}

inline NomadBoolean stringEqualTo(const NomadChar* a, const NomadChar* b) {
    return std::strcmp(a, b) == 0;
}

inline NomadBoolean stringNotEqualTo(const NomadStringView a, const NomadStringView b) {
    return a != b;
}

inline NomadBoolean stringLessThan(const NomadStringView a, const NomadStringView b) {
    return a < b;
}

inline NomadBoolean stringLessThanOrEqualTo(const NomadStringView a, const NomadStringView b) {
    return a <= b;
}

inline NomadBoolean stringGreaterThan(const NomadStringView a, const NomadStringView b) {
    return a > b;
}

inline NomadBoolean stringGreaterThanOrEqualTo(const NomadStringView a, const NomadStringView b) {
    return a >= b;
}

[[nodiscard]] inline NomadChar characterToLowerAscii(const NomadChar character) {
    return static_cast<NomadChar>(std::tolower(static_cast<unsigned char>(character)));
}

[[nodiscard]] inline NomadChar characterToUpperAscii(const NomadChar character) {
    return static_cast<NomadChar>(std::toupper(static_cast<unsigned char>(character)));
}

[[nodiscard]] inline NomadString stringToLowerAscii(const NomadStringView text) {
    NomadString result(text);

    for (auto& character : result) {
        character = characterToLowerAscii(character);
    }

    return result;
}

[[nodiscard]] inline NomadString stringToUpperAscii(const NomadStringView text) {
    NomadString result(text);

    for (auto& character : result) {
        character = characterToUpperAscii(character);
    }

    return result;
}

[[nodiscard]] NomadString stringToLower(NomadStringView text);
[[nodiscard]] NomadString stringToUpper(NomadStringView text);
[[nodiscard]] NomadBoolean isValidUtf8(NomadStringView text);

NomadString& stringLeftTrim(NomadString& text);
NomadString& stringRightTrim(NomadString& text);
NomadString& stringTrim(NomadString& text);
[[nodiscard]] NomadString stringLeftTrimCopy(NomadString text);
[[nodiscard]] NomadString stringRightTrimCopy(NomadString text);
[[nodiscard]] NomadString stringTrimCopy(NomadString text);

NomadString toString(NomadBoolean value);
NomadString toString(NomadId value);
NomadString toString(NomadInteger value);
NomadString toString(NomadFloat value);
NomadString toString(NomadIndex index);

// Utility to split a string into a vector of strings at newline characters
void split(NomadStringView text, NomadStringView separator, TempStringVector& lines);

void splitLines(NomadStringView text, std::vector<NomadString>& lines);
void splitLines(NomadStringView text, TempStringVector& lines);

} // nomad
