// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/script/RuntimeValue.hpp>

#include <optional>

namespace boost::json {
class object;
class value;
} // namespace boost::json

namespace nomad {

class Runtime;
class Type;
class VariableContext;

// Maps Nomad variables to flat JSON keys: the variable `inventory.weapon.attack` is stored as
// `{"inventory.weapon.attack": ...}`. Only `int`, `float`, `bool` and `string` variables are
// persisted. The declared type of the variable, not the JSON value, determines how a value is read.

// Returns the file name (`<saveName>.json`) for a save name or `std::nullopt` if the name is empty or could
// escape the save directory.
[[nodiscard]] std::optional<NomadString> makeSaveFileName(const NomadString& saveName);

// Inserts a variable value into `root`. Returns false (and logs an error) if the type is not persistable or
// if the variable is already present in `root`.
[[nodiscard]] bool writeJsonVariable(
    const Runtime* runtime,
    boost::json::object& root,
    const NomadString& variableName,
    const Type* type,
    const RuntimeValue& value
);

// Reads a variable value from `root`. Returns false if the variable is absent or if the JSON value cannot be
// converted to `type` (logged as a warning). On success, string values are newly allocated and owned by the
// caller.
[[nodiscard]] bool readJsonVariable(
    const Runtime* runtime,
    const boost::json::object& root,
    const NomadString& variableName,
    const Type* type,
    RuntimeValue& value
);

// Writes every persistable variable of `context` into `root`. Returns false if any variable could not be
// written.
[[nodiscard]] bool saveVariableContext(const Runtime* runtime, VariableContext& context, boost::json::object& root);

// Assigns every variable of `context` found in `root`. Variables absent from `root` keep their current value.
void loadVariableContext(const Runtime* runtime, VariableContext& context, const boost::json::object& root);

[[nodiscard]] NomadString toPrettyJson(const boost::json::value& value);

// Paths are UTF-8 encoded.
[[nodiscard]] bool pathExists(const NomadString& path);
[[nodiscard]] bool writeJsonFile(const NomadString& fileName, const boost::json::object& root);
[[nodiscard]] std::optional<boost::json::object> readJsonFile(const NomadString& fileName);

} // namespace nomad
