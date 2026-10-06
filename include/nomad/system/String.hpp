// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/system/TempHeap.hpp>
#include <nomad/Nomad.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <locale>
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

// trim from start (in place)
inline NomadString& stringLeftTrim(NomadString &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));

    return s;
}

// trim from end (in place)
inline NomadString& stringRightTrim(NomadString &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());

    return s;
}

// trim from both ends (in place)
inline NomadString& stringTrim(NomadString &s) {
    stringRightTrim(s);
    stringLeftTrim(s);

    return s;
}

// trim from start (copying)
inline NomadString stringLeftTrimCopy(NomadString s) {
    stringLeftTrim(s);
    return s;
}

// trim from end (copying)
inline NomadString stringRightTrimCopy(NomadString s) {
    stringRightTrim(s);
    return s;
}

// trim from both ends (copying)
inline NomadString stringTrimCopy(NomadString s) {
    stringTrim(s);
    return s;
}

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
