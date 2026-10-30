// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/log/Logger.hpp>

#include <variant>
#include <vector>

namespace nomad {

class NomadConfiguration;

struct LogConfiguration {
    LogLevel level = LogLevel::Info;
    LogDeliveryMode deliveryMode = LogDeliveryMode::Synchronous;
    std::vector<std::variant<ConsoleSinkConfiguration, FileSinkConfiguration>> sinks{ConsoleSinkConfiguration{}};
};

[[nodiscard]] LogConfiguration readLogConfiguration(
    const NomadConfiguration& configuration,
    const NomadPath& projectDirectory
);

} // namespace nomad
