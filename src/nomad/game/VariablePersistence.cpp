// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/VariablePersistence.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VariableContext.hpp>

#include <boost/json.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <vector>

namespace nomad {

namespace {

constexpr std::string_view SAVE_FILE_EXTENSION = ".json";
constexpr std::string_view INVALID_SAVE_NAME_CHARACTERS = "<>:\"/\\|?*";
constexpr NomadIndex JSON_INDENT_SIZE = 4;

// Splits `inventory.weapon.attack` into its segments. Returns an empty vector if any segment is empty.
std::vector<NomadString> splitVariableName(const NomadString& variableName) {
    std::vector<NomadString> segments;
    NomadIndex segmentStart = 0;

    while (true) {
        const auto separator = variableName.find('.', segmentStart);
        const auto segmentEnd = separator == NomadString::npos ? variableName.size() : separator;

        if (segmentEnd == segmentStart) {
            return {};
        }

        segments.emplace_back(variableName.substr(segmentStart, segmentEnd - segmentStart));

        if (separator == NomadString::npos) {
            return segments;
        }

        segmentStart = separator + 1;
    }
}

bool isPersistableType(const Runtime* runtime, const Type* type) {
    return
        type == runtime->getIntegerType() ||
        type == runtime->getFloatType() ||
        type == runtime->getBooleanType() ||
        type == runtime->getStringType();
}

// Returns the double closest to the shortest decimal representation of `value` so that `0.1f` is written
// as `0.1` instead of `0.10000000149011612`.
double toShortestDouble(const NomadFloat value) {
    std::array<char, 32> buffer{};

    for (auto precision = 1; precision <= std::numeric_limits<NomadFloat>::max_digits10; ++precision) {
        std::snprintf(buffer.data(), buffer.size(), "%.*g", precision, static_cast<double>(value));

        if (std::strtof(buffer.data(), nullptr) == value) {
            return std::strtod(buffer.data(), nullptr);
        }
    }

    return static_cast<double>(value);
}

NomadString formatDouble(const double value) {
    std::array<char, 32> buffer{};

    for (auto precision = 1; precision <= std::numeric_limits<double>::max_digits10; ++precision) {
        std::snprintf(buffer.data(), buffer.size(), "%.*g", precision, value);

        if (std::strtod(buffer.data(), nullptr) == value) {
            break;
        }
    }

    return buffer.data();
}

void appendIndent(NomadString& text, const NomadIndex indent) {
    text.append(indent * JSON_INDENT_SIZE, ' ');
}

void appendJsonValue(NomadString& text, const boost::json::value& value, NomadIndex indent);

void appendJsonObject(NomadString& text, const boost::json::object& object, const NomadIndex indent) {
    if (object.empty()) {
        text += "{}";
        return;
    }

    std::vector<const boost::json::key_value_pair*> entries;
    entries.reserve(object.size());

    for (const auto& entry : object) {
        entries.push_back(&entry);
    }

    std::ranges::sort(entries, [](const auto* left, const auto* right) {
        return left->key() < right->key();
    });

    text += "{\n";

    for (NomadIndex index = 0; index < entries.size(); ++index) {
        appendIndent(text, indent + 1);
        text += boost::json::serialize(boost::json::value(entries[index]->key()));
        text += ": ";
        appendJsonValue(text, entries[index]->value(), indent + 1);

        if (index + 1 < entries.size()) {
            text += ',';
        }

        text += '\n';
    }

    appendIndent(text, indent);
    text += '}';
}

void appendJsonArray(NomadString& text, const boost::json::array& array, const NomadIndex indent) {
    if (array.empty()) {
        text += "[]";
        return;
    }

    text += "[\n";

    for (NomadIndex index = 0; index < array.size(); ++index) {
        appendIndent(text, indent + 1);
        appendJsonValue(text, array[index], indent + 1);

        if (index + 1 < array.size()) {
            text += ',';
        }

        text += '\n';
    }

    appendIndent(text, indent);
    text += ']';
}

void appendJsonValue(NomadString& text, const boost::json::value& value, const NomadIndex indent) {
    switch (value.kind()) {
    case boost::json::kind::object:
        appendJsonObject(text, value.get_object(), indent);
        break;
    case boost::json::kind::array:
        appendJsonArray(text, value.get_array(), indent);
        break;
    case boost::json::kind::double_:
        text += formatDouble(value.get_double());
        break;
    default:
        text += boost::json::serialize(value);
        break;
    }
}

std::optional<boost::json::value> toJsonValue(
    const Runtime* runtime,
    const NomadString& variableName,
    const Type* type,
    const RuntimeValue& value
) {
    if (type == runtime->getIntegerType()) {
        return boost::json::value(value.getIntegerValue());
    }

    if (type == runtime->getFloatType()) {
        const auto floatValue = value.getFloatValue();

        if (!std::isfinite(floatValue)) {
            log::error("Cannot save '" + variableName + "': " + std::to_string(floatValue) + " is not a finite number");
            return std::nullopt;
        }

        return boost::json::value(toShortestDouble(floatValue));
    }

    if (type == runtime->getBooleanType()) {
        return boost::json::value(value.getBooleanValue());
    }

    if (type == runtime->getStringType()) {
        const auto* stringValue = value.getStringValue();

        return boost::json::value(stringValue == nullptr ? "" : stringValue);
    }

    const auto typeName = type == nullptr ? NomadString("unknown") : type->getTypeName();
    log::error("Cannot save '" + variableName + "': variables of type '" + typeName + "' cannot be saved");

    return std::nullopt;
}

bool toIntegerValue(const boost::json::value& json, NomadInteger& integerValue) {
    if (json.is_int64()) {
        integerValue = json.get_int64();
        return true;
    }

    if (json.is_double()) {
        // 2^63 is exactly representable as a double, unlike INT64_MAX.
        constexpr auto integerLimit = 9223372036854775808.0;
        const auto doubleValue = json.get_double();

        if (std::isfinite(doubleValue) &&
            std::trunc(doubleValue) == doubleValue &&
            doubleValue >= -integerLimit &&
            doubleValue < integerLimit) {
            integerValue = static_cast<NomadInteger>(doubleValue);
            return true;
        }
    }

    // Values that fit in an int64 are parsed as int64, so any uint64 is out of range.
    return false;
}

bool toFloatValue(const boost::json::value& json, NomadFloat& floatValue) {
    double doubleValue;

    if (json.is_double()) {
        doubleValue = json.get_double();
    } else if (json.is_int64()) {
        doubleValue = static_cast<double>(json.get_int64());
    } else if (json.is_uint64()) {
        doubleValue = static_cast<double>(json.get_uint64());
    } else {
        return false;
    }

    if (!std::isfinite(doubleValue) || std::abs(doubleValue) > std::numeric_limits<NomadFloat>::max()) {
        return false;
    }

    floatValue = static_cast<NomadFloat>(doubleValue);
    return true;
}

bool fromJsonValue(const Runtime* runtime, const boost::json::value& json, const Type* type, RuntimeValue& value) {
    if (type == runtime->getIntegerType()) {
        NomadInteger integerValue = 0;

        if (!toIntegerValue(json, integerValue)) {
            return false;
        }

        value.setIntegerValue(integerValue);
        return true;
    }

    if (type == runtime->getFloatType()) {
        NomadFloat floatValue = 0.0f;

        if (!toFloatValue(json, floatValue)) {
            return false;
        }

        value.setFloatValue(floatValue);
        return true;
    }

    if (type == runtime->getBooleanType()) {
        if (!json.is_bool()) {
            return false;
        }

        value.setBooleanValue(json.get_bool());
        return true;
    }

    if (type == runtime->getStringType()) {
        if (!json.is_string()) {
            return false;
        }

        value.setStringValue(NomadString(json.get_string()));
        return true;
    }

    return false;
}

} // namespace

std::optional<NomadString> makeSaveFileName(const NomadString& saveName) {
    auto name = saveName;

    if (name.size() > SAVE_FILE_EXTENSION.size() && name.ends_with(SAVE_FILE_EXTENSION)) {
        name.resize(name.size() - SAVE_FILE_EXTENSION.size());
    }

    if (name.empty() || name == "." || name == "..") {
        return std::nullopt;
    }

    for (const auto character : name) {
        const auto code = static_cast<unsigned char>(character);

        if (code < 0x20 || code == 0x7F || INVALID_SAVE_NAME_CHARACTERS.find(character) != std::string_view::npos) {
            return std::nullopt;
        }
    }

    // Windows silently strips trailing dots and spaces from file names.
    if (name.back() == '.' || name.back() == ' ') {
        return std::nullopt;
    }

    return name + NomadString(SAVE_FILE_EXTENSION);
}

bool writeJsonVariable(
    const Runtime* runtime,
    boost::json::object& root,
    const NomadString& variableName,
    const Type* type,
    const RuntimeValue& value
) {
    const auto segments = splitVariableName(variableName);

    if (segments.empty()) {
        log::error("Cannot save '" + variableName + "': invalid variable name");
        return false;
    }

    auto jsonValue = toJsonValue(runtime, variableName, type, value);

    if (!jsonValue) {
        return false;
    }

    if (root.contains(variableName)) {
        log::error("Cannot save '" + variableName + "': it is already saved");
        return false;
    }

    root.emplace(variableName, std::move(*jsonValue));

    return true;
}

bool readJsonVariable(
    const Runtime* runtime,
    const boost::json::object& root,
    const NomadString& variableName,
    const Type* type,
    RuntimeValue& value
) {
    const auto segments = splitVariableName(variableName);

    if (segments.empty() || !isPersistableType(runtime, type)) {
        return false;
    }

    const auto entry = root.find(variableName);

    if (entry == root.end()) {
        return false;
    }

    if (!fromJsonValue(runtime, entry->value(), type, value)) {
        log::warning(
            "Ignoring saved value of '" + variableName + "': expected " + type->getTypeName() +
            " but found " + boost::json::serialize(entry->value())
        );
        return false;
    }

    return true;
}

bool saveVariableContext(const Runtime* runtime, VariableContext& context, boost::json::object& root) {
    auto success = true;

    for (auto variableId = context.getFirstVariableId();
         variableId != NOMAD_INVALID_ID;
         variableId = context.getNextVariableId(variableId)) {
        const auto* type = context.getVariableType(variableId);
        const auto& variableName = context.getVariableName(variableId);

        // Declared but never assigned.
        if (type == nullptr) {
            continue;
        }

        if (!isPersistableType(runtime, type)) {
            log::warning("Not saving '" + variableName + "': variables of type '" + type->getTypeName() + "' cannot be saved");
            continue;
        }

        RuntimeValue value;
        context.getValue(variableId, value);

        if (!writeJsonVariable(runtime, root, variableName, type, value)) {
            success = false;
        }
    }

    return success;
}

void loadVariableContext(const Runtime* runtime, VariableContext& context, const boost::json::object& root) {
    struct PendingValue {
        NomadId variableId;
        const Type* type;
        RuntimeValue value;
    };

    std::vector<PendingValue> pendingValues;

    for (auto variableId = context.getFirstVariableId();
         variableId != NOMAD_INVALID_ID;
         variableId = context.getNextVariableId(variableId)) {
        const auto* type = context.getVariableType(variableId);

        RuntimeValue value;

        if (readJsonVariable(runtime, root, context.getVariableName(variableId), type, value)) {
            pendingValues.push_back({variableId, type, value});
        }
    }

    for (auto& pendingValue : pendingValues) {
        // The variable context copies what it keeps.
        context.setValue(pendingValue.variableId, pendingValue.value);
        pendingValue.type->freeValue(pendingValue.value);
    }
}

NomadString toPrettyJson(const boost::json::value& value) {
    NomadString text;

    appendJsonValue(text, value, 0);
    text += '\n';

    return text;
}

bool pathExists(const NomadString& path) {
    return SDL_GetPathInfo(path.c_str(), nullptr);
}

bool writeJsonFile(const NomadString& fileName, const boost::json::object& root) {
    NomadString text;
    appendJsonObject(text, root, 0);
    text += '\n';

    // Write to a temporary file first so that a failure never leaves a truncated file behind.
    const auto temporaryFileName = fileName + ".tmp";

    auto* stream = SDL_IOFromFile(temporaryFileName.c_str(), "wb");

    if (stream == nullptr) {
        log::error("Failed to open '" + temporaryFileName + "' for writing: " + SDL_GetError());
        return false;
    }

    const auto writtenSize = SDL_WriteIO(stream, text.data(), text.size());
    const auto closed = SDL_CloseIO(stream);

    if (writtenSize != text.size() || !closed) {
        log::error("Failed to write '" + temporaryFileName + "': " + SDL_GetError());
        SDL_RemovePath(temporaryFileName.c_str());
        return false;
    }

    if (!SDL_RenamePath(temporaryFileName.c_str(), fileName.c_str())) {
        log::error("Failed to replace '" + fileName + "': " + SDL_GetError());
        SDL_RemovePath(temporaryFileName.c_str());
        return false;
    }

    return true;
}

std::optional<boost::json::object> readJsonFile(const NomadString& fileName) {
    std::size_t size = 0;
    auto* data = SDL_LoadFile(fileName.c_str(), &size);

    if (data == nullptr) {
        log::error("Failed to read '" + fileName + "': " + SDL_GetError());
        return std::nullopt;
    }

    const NomadString text(static_cast<const char*>(data), size);
    SDL_free(data);

    boost::system::error_code error;
    auto json = boost::json::parse(text, error);

    if (error) {
        log::error("Failed to parse '" + fileName + "': " + error.message());
        return std::nullopt;
    }

    if (!json.is_object()) {
        log::error("Failed to load '" + fileName + "': the root JSON value must be an object");
        return std::nullopt;
    }

    return std::move(json.get_object());
}

} // namespace nomad
