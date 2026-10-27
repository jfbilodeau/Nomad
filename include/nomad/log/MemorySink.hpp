// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/log/LogSink.hpp>

#include <vector>

namespace nomad {

class MemorySink : public LogSink {
public:
    explicit MemorySink(const MemorySinkConfiguration& configuration = {});
    MemorySink(const MemorySink&) = delete;
    ~MemorySink() override = default;

    void log(const LogEntry* entry) override;

    [[nodiscard]] const std::vector<LogEntry>& getEntries() const;

    void clear();

private:
    std::vector<LogEntry> m_entries;
};

} // nomad
