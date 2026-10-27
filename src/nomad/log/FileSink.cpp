// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/log/FileSink.hpp>

#include <algorithm>
#include <charconv>
#include <functional>
#include <limits>
#include <string_view>
#include <system_error>
#include <vector>

namespace nomad {

FileSink::FileSink(const FileSinkConfiguration& configuration) :
    LogSink(configuration),
    m_name(configuration.name),
    m_rotationMode(configuration.rotationMode),
    m_rotateAtStartup(configuration.rotateAtStartup),
    m_maxSizeBytes(configuration.maxSizeBytes),
    m_maxFiles(configuration.maxFiles)
{
    if (configuration.directory.empty()) {
        throw NomadException("A log file directory is required");
    }
    if (m_name.empty() || m_name.find_first_of("<>:\"/\\|?*") != NomadString::npos
        || m_name.back() == '.' || m_name.back() == ' '
        || std::ranges::any_of(m_name, [](const unsigned char character) { return character < 32; })) {
        throw NomadException("Invalid log file base name");
    }
    setRotationMode(configuration.rotationMode);
    setRotationLimits(configuration.maxSizeBytes, configuration.maxFiles);
    m_path = std::filesystem::absolute(configuration.directory) / (m_name + ".log");
    if (getActive()) {
        begin();
    }
}

const std::filesystem::path& FileSink::getPath() const {
    return m_path;
}

FileRotationMode FileSink::getRotationMode() const {
    return m_rotationMode;
}

void FileSink::setRotationMode(const FileRotationMode mode) {
    if (mode != FileRotationMode::None && mode != FileRotationMode::Size) {
        throw NomadException("Invalid file log rotation mode");
    }
    m_rotationMode = mode;
}

bool FileSink::getRotateAtStartup() const {
    return m_rotateAtStartup;
}

void FileSink::setRotateAtStartup(const bool rotateAtStartup) {
    m_rotateAtStartup = rotateAtStartup;
}

NomadIndex FileSink::getMaxSizeBytes() const {
    return m_maxSizeBytes;
}

NomadIndex FileSink::getMaxFiles() const {
    return m_maxFiles;
}

void FileSink::setRotationLimits(const NomadIndex maxSizeBytes, const NomadIndex maxFiles) {
    if (maxSizeBytes == 0) {
        throw NomadException("The log file size limit must be positive; use None to disable rotation");
    }
    m_maxSizeBytes = maxSizeBytes;
    m_maxFiles = maxFiles;
}

void FileSink::open() {
    std::filesystem::create_directories(m_path.parent_path());
    m_output.clear();
    m_output.open(m_path, std::ios::binary | std::ios::app);
    if (!m_output) {
        throw NomadException("Failed to open log file: " + m_path.string());
    }
    m_sizeBytes = std::filesystem::file_size(m_path);
}

void FileSink::begin() {
    if (!m_output.is_open()) {
        open();
    }
    if (!m_started) {
        if (m_rotateAtStartup && m_sizeBytes != 0) {
            rotate();
        }
        m_started = true;
    }
}

void FileSink::log(const LogEntry* entry) {
    begin();
    const auto messageSize = entry->message.size();
    if (messageSize >= static_cast<NomadIndex>(std::numeric_limits<std::streamsize>::max())) {
        throw NomadException("Log record is too large to write");
    }
    const auto recordSize = static_cast<NomadIndex>(messageSize) + 1;
    if (m_rotationMode == FileRotationMode::Size && m_sizeBytes != 0
        && (m_sizeBytes >= m_maxSizeBytes || recordSize > m_maxSizeBytes - m_sizeBytes)) {
        rotate();
    }
    m_output.write(entry->message.data(), static_cast<std::streamsize>(messageSize));
    m_output.put('\n');
    if (!m_output) {
        throw NomadException("Failed to write log file: " + m_path.string());
    }
    m_sizeBytes += recordSize;
}

void FileSink::end() {
    if (m_output.is_open()) {
        m_output.flush();
        if (!m_output) {
            throw NomadException("Failed to flush log file: " + m_path.string());
        }
    }
}

std::filesystem::path FileSink::getArchivePath(const NomadIndex index) const {
    return m_path.parent_path() / (m_name + "." + std::to_string(index) + ".log");
}

void FileSink::rotate() {
    end();
    struct Archive {
        NomadIndex index;
        std::filesystem::path path;
    };
    std::vector<Archive> archives;
    const auto prefix = m_name + ".";
    for (const auto& entry : std::filesystem::directory_iterator(m_path.parent_path())) {
        const auto filename = entry.path().filename().string();
        if (!filename.starts_with(prefix) || !filename.ends_with(".log") || filename.size() <= prefix.size() + 4) {
            continue;
        }
        const auto digits = std::string_view(filename).substr(prefix.size(), filename.size() - prefix.size() - 4);
        auto index = NomadIndex{0};
        const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), index);
        if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size()
            || index == 0 || digits != std::to_string(index)) {
            continue;
        }
        if (!entry.is_regular_file()) {
            throw NomadException("Log archive is not a regular file: " + entry.path().string());
        }
        archives.push_back({index, entry.path()});
    }
    std::ranges::sort(archives, std::greater{}, &Archive::index);
    m_output.close();
    if (!m_output) {
        throw NomadException("Failed to close log file for rotation: " + m_path.string());
    }
    for (const auto& archive : archives) {
        if (archive.index >= m_maxFiles) {
            std::filesystem::remove(archive.path);
        } else {
            std::filesystem::rename(archive.path, getArchivePath(archive.index + 1));
        }
    }
    if (m_maxFiles != 0) {
        std::filesystem::rename(m_path, getArchivePath(1));
    } else {
        m_output.clear();
        m_output.open(m_path, std::ios::binary | std::ios::trunc);
        if (!m_output) {
            throw NomadException("Failed to truncate log file: " + m_path.string());
        }
        m_sizeBytes = 0;
        return;
    }
    open();
}

} // namespace nomad
