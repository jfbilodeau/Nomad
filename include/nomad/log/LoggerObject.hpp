// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/NomadObject.hpp>

namespace nomad {

class Logger;

class LoggerObject final : public NomadObject {
public:
    explicit LoggerObject(Logger& logger);
    LoggerObject(const LoggerObject&) = delete;
    LoggerObject& operator=(const LoggerObject&) = delete;
    ~LoggerObject() override = default;

    [[nodiscard]] Logger& getLogger() const;

private:
    Logger& m_logger;
};

} // namespace nomad
