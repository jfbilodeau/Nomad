// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/system/NomadConfiguration.hpp>

#include <nomad/system/Path.hpp>

#include <toml++/toml.hpp>

#include <fstream>
#include <sstream>
#include <type_traits>
#include <utility>

namespace nomad {

namespace {

std::vector<NomadStringView> splitPath(const NomadStringView path) {
    std::vector<NomadStringView> keys;
    auto begin = NomadIndex{0};

    while (begin <= path.size()) {
        const auto end = path.find('.', begin);
        const auto key = path.substr(begin, end == NomadStringView::npos ? end : end - begin);
        if (key.empty()) {
            throw NomadConfigurationException("Configuration path '" + NomadString(path) + "' is invalid");
        }
        keys.push_back(key);
        if (end == NomadStringView::npos) {
            break;
        }
        begin = end + 1;
    }
    return keys;
}

NomadConfigurationException wrongType(const NomadStringView path, const NomadStringView expected) {
    return NomadConfigurationException(
        "Configuration field '" + NomadString(path) + "' must be " + NomadString(expected)
    );
}

const toml::node* findNode(const toml::table& document, const NomadStringView path) {
    const auto keys = splitPath(path);
    const auto* table = &document;
    for (auto index = NomadIndex{0}; index < keys.size(); ++index) {
        const auto* node = table->get(keys[index]);
        if (node == nullptr || index + 1 == keys.size()) {
            return node;
        }
        table = node->as_table();
        if (table == nullptr) {
            throw wrongType(path, "a path through tables");
        }
    }
    return nullptr;
}

template<typename Value>
std::optional<Value> readValue(const toml::table& document, const NomadStringView path) {
    const auto* node = findNode(document, path);
    if (node == nullptr) {
        return std::nullopt;
    }
    const auto value = node->value_exact<Value>();
    if (!value) {
        if constexpr (std::is_same_v<Value, NomadString>) {
            throw wrongType(path, "a string");
        } else if constexpr (std::is_same_v<Value, bool>) {
            throw wrongType(path, "a boolean");
        } else {
            throw wrongType(path, "an integer");
        }
    }
    return value;
}

template<typename Value>
Value requireValue(std::optional<Value> value, const NomadStringView path) {
    if (!value) {
        throw NomadConfigurationException("Configuration field '" + NomadString(path) + "' is required");
    }
    return std::move(*value);
}

template<typename Value>
std::optional<std::vector<Value>> readArray(
    const toml::table& document,
    const NomadStringView path,
    const NomadStringView expected
) {
    const auto* node = findNode(document, path);
    if (node == nullptr) {
        return std::nullopt;
    }
    const auto* array = node->as_array();
    if (array == nullptr) {
        throw wrongType(path, expected);
    }
    std::vector<Value> values;
    values.reserve(array->size());
    for (const auto& element : *array) {
        const auto value = element.value_exact<Value>();
        if (!value) {
            throw wrongType(path, expected);
        }
        values.push_back(*value);
    }
    return values;
}

toml::table* parentTable(
    toml::table& document,
    const std::vector<NomadStringView>& keys,
    const NomadStringView path,
    const bool create
) {
    auto* table = &document;
    for (auto index = NomadIndex{0}; index + 1 < keys.size(); ++index) {
        auto* node = table->get(keys[index]);
        if (node == nullptr) {
            if (!create) {
                return nullptr;
            }
            table->insert(keys[index], toml::table{});
            node = table->get(keys[index]);
        }
        table = node->as_table();
        if (table == nullptr) {
            throw wrongType(path, "a path through tables");
        }
    }
    return table;
}

template<typename Value>
void setValue(toml::table& document, const NomadStringView path, Value&& value) {
    const auto keys = splitPath(path);
    auto* table = parentTable(document, keys, path, true);
    table->insert_or_assign(keys.back(), std::forward<Value>(value));
}

void mergeTables(toml::table& destination, const toml::table& source) {
    for (const auto& [key, node] : source) {
        auto* existing = destination.get(key);
        if (existing != nullptr && existing->is_table() && node.is_table()) {
            mergeTables(*existing->as_table(), *node.as_table());
        } else {
            destination.insert_or_assign(key, node);
        }
    }
}

} // namespace

struct NomadConfiguration::Implementation {
    toml::table document;
};

NomadConfiguration::NomadConfiguration() : m_implementation(std::make_unique<Implementation>()) {}
NomadConfiguration::~NomadConfiguration() = default;
NomadConfiguration::NomadConfiguration(NomadConfiguration&& other) noexcept = default;
NomadConfiguration& NomadConfiguration::operator=(NomadConfiguration&& other) noexcept = default;

NomadConfiguration::NomadConfiguration(const NomadConfiguration& other)
    : m_implementation(std::make_unique<Implementation>(*other.m_implementation)) {}

NomadConfiguration& NomadConfiguration::operator=(const NomadConfiguration& other) {
    if (this != &other) {
        auto implementation = std::make_unique<Implementation>(*other.m_implementation);
        m_implementation = std::move(implementation);
    }
    return *this;
}

NomadConfiguration NomadConfiguration::parse(const NomadStringView text) {
    NomadConfiguration result;
    try {
        result.m_implementation->document = toml::parse(text);
    } catch (const toml::parse_error& error) {
        std::ostringstream message;
        message << "Failed to parse configuration at " << error.source().begin.line
                << ':' << error.source().begin.column << ": " << error.description();
        throw NomadConfigurationException(message.str());
    }
    return result;
}

NomadConfiguration NomadConfiguration::readFile(const NomadPath& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw NomadConfigurationException(
            "Failed to open configuration '" + pathToUtf8(path) + "' for reading"
        );
    }
    std::ostringstream text;
    text << input.rdbuf();
    if (input.bad()) {
        throw NomadConfigurationException("Failed to read configuration '" + pathToUtf8(path) + "'");
    }
    return parse(text.str());
}

NomadString NomadConfiguration::serialize() const {
    std::ostringstream text;
    text << toml::toml_formatter{m_implementation->document};
    return text.str();
}

bool NomadConfiguration::contains(const NomadStringView path) const {
    return findNode(m_implementation->document, path) != nullptr;
}

void NomadConfiguration::validateSection(const NomadStringView path) const {
    const auto* node = findNode(m_implementation->document, path);
    if (node != nullptr && !node->is_table()) {
        throw wrongType(path, "a configuration section");
    }
}

std::optional<NomadString> NomadConfiguration::getString(const NomadStringView path) const {
    return readValue<NomadString>(m_implementation->document, path);
}

std::optional<NomadInteger> NomadConfiguration::getInteger(const NomadStringView path) const {
    return readValue<NomadInteger>(m_implementation->document, path);
}

std::optional<bool> NomadConfiguration::getBoolean(const NomadStringView path) const {
    return readValue<bool>(m_implementation->document, path);
}

std::optional<std::vector<NomadString>> NomadConfiguration::getStringArray(
    const NomadStringView path
) const {
    return readArray<NomadString>(m_implementation->document, path, "an array of strings");
}

std::optional<std::vector<NomadInteger>> NomadConfiguration::getIntegerArray(
    const NomadStringView path
) const {
    return readArray<NomadInteger>(m_implementation->document, path, "an array of integers");
}

NomadString NomadConfiguration::requireString(const NomadStringView path) const {
    return requireValue(getString(path), path);
}

NomadInteger NomadConfiguration::requireInteger(const NomadStringView path) const {
    return requireValue(getInteger(path), path);
}

bool NomadConfiguration::requireBoolean(const NomadStringView path) const {
    return requireValue(getBoolean(path), path);
}

std::vector<NomadString> NomadConfiguration::requireStringArray(const NomadStringView path) const {
    return requireValue(getStringArray(path), path);
}

std::vector<NomadInteger> NomadConfiguration::requireIntegerArray(const NomadStringView path) const {
    return requireValue(getIntegerArray(path), path);
}

void NomadConfiguration::setString(const NomadStringView path, const NomadStringView value) {
    setValue(m_implementation->document, path, value);
}

void NomadConfiguration::setInteger(const NomadStringView path, const NomadInteger value) {
    setValue(m_implementation->document, path, value);
}

void NomadConfiguration::setBoolean(const NomadStringView path, const bool value) {
    setValue(m_implementation->document, path, value);
}

void NomadConfiguration::setStringArray(
    const NomadStringView path,
    const std::vector<NomadString>& values
) {
    toml::array array;
    for (const auto& value : values) {
        array.push_back(value);
    }
    setValue(m_implementation->document, path, std::move(array));
}

void NomadConfiguration::merge(const NomadConfiguration& other) {
    if (this != &other) {
        mergeTables(m_implementation->document, other.m_implementation->document);
    }
}

bool NomadConfiguration::erase(const NomadStringView path) {
    const auto keys = splitPath(path);
    auto* table = parentTable(m_implementation->document, keys, path, false);
    return table != nullptr && table->erase(keys.back()) != 0;
}

} // namespace nomad
