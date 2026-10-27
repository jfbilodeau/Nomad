// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/log/LogSink.hpp>

namespace nomad {

    class ConsoleSink final : public LogSink {
    public:
        explicit ConsoleSink(const ConsoleSinkConfiguration& configuration = {});
        ConsoleSink(const ConsoleSink&) = delete;
        ~ConsoleSink() override = default;

        void log(const LogEntry* entry) override;
        void end() override; // Flushes the console
    };

} // nomad
