// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/log/LogConfiguration.hpp>

#include <nomad/system/NomadConfiguration.hpp>
#include <nomad/system/Path.hpp>

#include <algorithm>
#include <initializer_list>

namespace nomad {

namespace {

LogLevel readLogLevel(const NomadConfiguration& configuration, const NomadStringView field) {
    const auto value = configuration.getString(field).value_or("info");
    if (value == "debug") {
        return LogLevel::Debug;
    }
    if (value == "info") {
        return LogLevel::Info;
    }
    if (value == "warning") {
        return LogLevel::Warning;
    }
    if (value == "error") {
        return LogLevel::Error;
    }
    if (value == "critical") {
        return LogLevel::Critical;
    }
    throw NomadConfigurationException(
        "Configuration field '" + NomadString(field) + "' must be debug, info, warning, error, or critical"
    );
}

} // namespace

LogConfiguration readLogConfiguration(
    const NomadConfiguration& configuration,
    const NomadPath& projectDirectory
) {
    for (const auto& key : configuration.getKeys("log")) {
        if (key != "level" && key != "delivery-mode" && key != "sinks") {
            throw NomadConfigurationException("Unknown configuration field 'log." + key + "'");
        }
    }
    LogConfiguration settings;
    settings.level = readLogLevel(configuration, "log.level");
    const auto deliveryMode = configuration.getString("log.delivery-mode").value_or("synchronous");
    if (deliveryMode == "buffered") {
        settings.deliveryMode = LogDeliveryMode::Buffered;
    } else if (deliveryMode != "synchronous") {
        throw NomadConfigurationException(
            "Configuration field 'log.delivery-mode' must be buffered or synchronous"
        );
    }
    const auto sinks = configuration.getSectionArray("log.sinks");
    if (!sinks) {
        return settings;
    }
    settings.sinks.clear();
    for (auto index = NomadIndex{0}; index < sinks->size(); ++index) {
        const auto& sink = (*sinks)[index];
        const auto field = "log.sinks[" + std::to_string(index) + "]";
        const auto type = sink.requireString("type");
        if (type != "console" && type != "file") {
            throw NomadConfigurationException("Configuration field '" + field + ".type' must be console or file");
        }
        for (const auto& key : sink.getKeys()) {
            const auto commonKeys = {"type", "active", "level"};
            const auto fileKeys = {
                "directory", "name", "rotation-mode", "rotate-at-startup", "max-size-bytes", "max-files"
            };
            const auto common = std::ranges::find(commonKeys, key) != commonKeys.end();
            const auto file = type == "file" && std::ranges::find(fileKeys, key) != fileKeys.end();
            if (!common && !file) {
                throw NomadConfigurationException(
                    "Unknown configuration field '" + field + "." + key + "' for " + type + " sink"
                );
            }
        }
        const auto active = sink.getBoolean("active").value_or(true);
        const auto level = readLogLevel(sink, "level");
        if (type == "console") {
            ConsoleSinkConfiguration console;
            console.active = active;
            console.level = level;
            settings.sinks.emplace_back(console);
        } else {
            FileSinkConfiguration file;
            file.active = active;
            file.level = level;
            file.directory = pathFromUtf8(sink.getString("directory").value_or("logs"));
            if (!file.directory.empty() && file.directory.is_relative()) {
                file.directory = projectDirectory / file.directory;
            }
            file.name = sink.getString("name").value_or(file.name);
            const auto rotationMode = sink.getString("rotation-mode").value_or("size");
            if (rotationMode == "none") {
                file.rotationMode = FileRotationMode::None;
            } else if (rotationMode != "size") {
                throw NomadConfigurationException(
                    "Configuration field '" + field + ".rotation-mode' must be none or size"
                );
            }
            file.rotateAtStartup = sink.getBoolean("rotate-at-startup").value_or(file.rotateAtStartup);
            file.maxSizeBytes = sink.getInteger("max-size-bytes").value_or(file.maxSizeBytes);
            file.maxFiles = sink.getInteger("max-files").value_or(file.maxFiles);

            auto validation = file;
            validation.active = false;
            const FileSink validated(validation);

            settings.sinks.emplace_back(std::move(file));
        }
    }
    return settings;
}

} // namespace nomad
