// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/system/String.hpp>

#include <boost/locale.hpp>
#include <boost/locale/encoding_utf.hpp>

#include <iomanip>
#include <locale>
#include <sstream>

namespace nomad {

namespace {

const std::locale& getStringLocale() {
    static const auto locale = boost::locale::generator{}("en_US.UTF-8");
    return locale;
}

bool isUnicodeWhitespace(const char32_t character) {
    return
        (character >= U'\u0009' && character <= U'\u000D') ||
        character == U'\u0020' ||
        character == U'\u0085' ||
        character == U'\u00A0' ||
        character == U'\u1680' ||
        (character >= U'\u2000' && character <= U'\u200A') ||
        character == U'\u2028' ||
        character == U'\u2029' ||
        character == U'\u202F' ||
        character == U'\u205F' ||
        character == U'\u3000';
}

NomadString trimUnicode(const NomadStringView text, const bool trimStart, const bool trimEnd) {
    const auto characters = boost::locale::conv::utf_to_utf<char32_t>(
        NomadString(text),
        boost::locale::conv::stop
    );
    NomadIndex start = 0;
    auto end = characters.size();

    if (trimStart) {
        while (start < end && isUnicodeWhitespace(characters[start])) {
            ++start;
        }
    }
    if (trimEnd) {
        while (end > start && isUnicodeWhitespace(characters[end - 1])) {
            --end;
        }
    }

    return boost::locale::conv::utf_to_utf<NomadChar>(
        characters.substr(start, end - start),
        boost::locale::conv::stop
    );
}

} // namespace

NomadString stringToLower(const NomadStringView text) {
    const auto value = NomadString(text);
    (void)boost::locale::conv::utf_to_utf<char32_t>(value, boost::locale::conv::stop);
    return boost::locale::to_lower(value, getStringLocale());
}

NomadString stringToUpper(const NomadStringView text) {
    const auto value = NomadString(text);
    (void)boost::locale::conv::utf_to_utf<char32_t>(value, boost::locale::conv::stop);
    return boost::locale::to_upper(value, getStringLocale());
}

NomadBoolean isValidUtf8(const NomadStringView text) {
    try {
        (void)boost::locale::conv::utf_to_utf<char32_t>(NomadString(text), boost::locale::conv::stop);
        return true;
    } catch (const boost::locale::conv::conversion_error&) {
        return false;
    }
}

NomadString& stringLeftTrim(NomadString& text) {
    text = trimUnicode(text, true, false);
    return text;
}

NomadString& stringRightTrim(NomadString& text) {
    text = trimUnicode(text, false, true);
    return text;
}

NomadString& stringTrim(NomadString& text) {
    text = trimUnicode(text, true, true);
    return text;
}

NomadString stringLeftTrimCopy(NomadString text) {
    stringLeftTrim(text);
    return text;
}

NomadString stringRightTrimCopy(NomadString text) {
    stringRightTrim(text);
    return text;
}

NomadString stringTrimCopy(NomadString text) {
    stringTrim(text);
    return text;
}

NomadString toString(NomadBoolean value) {
    return value ? "true" : "false";
}

NomadString toString(NomadId value) {
    return std::to_string(value);
}

NomadString toString(NomadInteger value) {
    return std::to_string(value);
}

NomadString toString(NomadFloat value) {
    std::stringstream ss;

    ss << std::setprecision(15) << std::noshowpoint << value;

    return ss.str();
}

NomadString toString(NomadIndex index) {
    return std::to_string(index);
}

void split(const NomadStringView text, const NomadStringView separator, TempStringVector& lines) {
    size_t start = 0;
    size_t end = text.find(separator);

    while (end != NomadString::npos) {
        lines.emplace_back(text.substr(start, end - start));
        start = end + separator.length();
        end = text.find(separator, start);
    }

    lines.emplace_back(text.substr(start));
}

void splitLines(const NomadStringView text, std::vector<NomadString>& lines) {
    size_t start = 0;
    size_t end = text.find_first_of("\r\n");

    while (end != NomadString::npos) {
        lines.emplace_back(text.substr(start, end - start));
        if (text[end] == '\r' && end + 1 < text.size() && text[end + 1] == '\n') {
            start = end + 2;
        } else {
            start = end + 1;
        }
        end = text.find_first_of("\r\n", start);
    }

    lines.emplace_back(text.substr(start));
}

void splitLines(const NomadStringView text, TempStringVector& lines) {
    size_t start = 0;
    size_t end = text.find_first_of("\r\n");

    while (end != NomadString::npos) {
        lines.emplace_back(text.substr(start, end - start));
        if (text[end] == '\r' && end + 1 < text.size() && text[end + 1] == '\n') {
            start = end + 2;
        } else {
            start = end + 1;
        }
        end = text.find_first_of("\r\n", start);
    }

    lines.emplace_back(text.substr(start));
}

} // nomad
