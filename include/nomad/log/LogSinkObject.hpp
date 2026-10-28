// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/log/LogSink.hpp>

#include <nomad/script/NomadObject.hpp>

namespace nomad {

class Logger;

class LogSinkObject final : public NomadObject {
public:
    LogSinkObject(Logger& logger, LogSinkId sinkId);
    LogSinkObject(const LogSinkObject&) = delete;
    LogSinkObject& operator=(const LogSinkObject&) = delete;
    ~LogSinkObject() override = default;

    [[nodiscard]] LogSinkId getSinkId() const;
    [[nodiscard]] LogSink& getSink() const;

private:
    Logger& m_logger;
    LogSinkId m_sinkId;
};

} // namespace nomad
