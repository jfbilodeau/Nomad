// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

namespace nomad {

// Basic type definitions
using NomadBoolean = bool;
using NomadInteger = std::int64_t;
// Using 32 bits `float` instead of 64 bits `double` to avoid typecasting with Box2D.
using NomadFloat = float;

constexpr NomadBoolean NOMAD_FALSE = false;
constexpr NomadBoolean NOMAD_TRUE = true;

constexpr NomadFloat NOMAD_PI = static_cast<NomadFloat>(std::numbers::pi);

using NomadChar = char;
using NomadString = std::string;
using NomadStringView = std::string_view;

struct NomadStringHash {
    using is_transparent = void;

    [[nodiscard]] std::size_t operator()(const NomadStringView value) const noexcept {
        return std::hash<NomadStringView>{}(value);
    }
};

struct NomadStringEqual {
    using is_transparent = void;

    [[nodiscard]] bool operator()(const NomadStringView lhs, const NomadStringView rhs) const noexcept {
        return lhs == rhs;
    }
};

// Updated to fix the constant expression issue
extern const NomadString NOMAD_EMPTY_STRING;

using NomadShort = std::int16_t;

// An internal index type for Nomad objects
using NomadIndex = std::size_t;
constexpr NomadIndex NOMAD_INVALID_INDEX = std::numeric_limits<NomadIndex>::max();

// An internal ID type for Nomad objects
using NomadId = std::int32_t;
constexpr NomadId NOMAD_INVALID_ID = -1;
constexpr NomadId NOMAD_ID_MIN = 0;
constexpr NomadId NOMAD_ID_MAX = 1000000;  // std::numeric_limits<NomadId>::max() - 1;

// Returns true when an ID is within the Nomad ID domain.
[[nodiscard]] constexpr bool isValidId(const NomadId id) noexcept {
    return id >= NOMAD_ID_MIN && id < NOMAD_ID_MAX;
}

// Returns true when an ID is outside the Nomad ID domain.
[[nodiscard]] constexpr bool isInvalidId(const NomadId id) noexcept {
    return !isValidId(id);
}

// Returns true when an ID is below the exclusive upper bound.
[[nodiscard]] constexpr bool isIdInRange(const NomadId id, const NomadIndex exclusiveUpperBound) noexcept {
    return isValidId(id) && static_cast<NomadIndex>(id) < exclusiveUpperBound;
}

// Returns true when an ID is not below the exclusive upper bound.
[[nodiscard]] constexpr bool isIdOutOfRange(const NomadId id, const NomadIndex exclusiveUpperBound) noexcept {
    return !isIdInRange(id, exclusiveUpperBound);
}

template<typename T>
NomadId toNomadId(T value) {
   return static_cast<NomadId>(value);
}

template<typename T>
NomadIndex toNomadIndex(T value) {
   return static_cast<NomadIndex>(value);
}

class NomadException : public std::exception {
public:
   explicit NomadException(const NomadString& message) {
       m_message = message;
   }

   [[nodiscard]] const char* what() const noexcept override {
       return m_message.c_str();
   }

   [[nodiscard]] const NomadString& message() const {
       return m_message;
   }

private:
   static NomadString m_message;
};

class NomadBug : public NomadException {
public:
    explicit NomadBug(const NomadString& message):
    NomadException(message) { }
};

// Default values
constexpr NomadInteger NOMAD_DEFAULT_INTEGER = 0;
constexpr NomadFloat NOMAD_DEFAULT_FLOAT = 0.0;
constexpr NomadBoolean NOMAD_DEFAULT_BOOLEAN = false;
const NomadString NOMAD_DEFAULT_STRING = NOMAD_EMPTY_STRING;

} // namespace nomad
