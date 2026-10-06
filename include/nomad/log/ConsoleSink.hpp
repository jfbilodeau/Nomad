// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/log/Logger.hpp>

namespace nomad {

    class ConsoleSink final : public LogSink {
    public:
        ConsoleSink() = default;
        ConsoleSink(const ConsoleSink&) = delete;
        ~ConsoleSink() override = default;

        void log(const LogEntry* entry) override;
        void end() override; // Flushes the console
    };

} // nomad
