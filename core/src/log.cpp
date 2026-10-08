#include "lf/log.hpp"

#include <fstream>

namespace lf {

const char* levelName(LogLevel l) {
    switch (l) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}

Logger::Logger(size_t capacity, Clock clock) : m_capacity(capacity ? capacity : 1), m_clock(std::move(clock)) {}

std::string Logger::format(const LogEntry& e) {
    std::string s = e.time + " [" + levelName(e.level) + "] " + e.category + ": " + e.message;
    if (!e.detail.empty()) s += " | " + e.detail;
    return s;
}

void Logger::setFile(const std::filesystem::path& path, size_t maxBytes) {
    std::lock_guard lk(m_mutex);
    m_file = path;
    m_maxBytes = maxBytes;
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
}

void Logger::setMinLevel(LogLevel level) {
    std::lock_guard lk(m_mutex);
    m_min = level;
}

void Logger::log(LogLevel level, std::string_view category, std::string message, std::string detail) {
    std::lock_guard lk(m_mutex);
    if (level < m_min) return;
    LogEntry e{m_clock(), level, std::string(category), std::move(message), std::move(detail)};
    if (!m_file.empty()) {
        std::error_code ec;
        if (m_maxBytes && std::filesystem::exists(m_file, ec) && std::filesystem::file_size(m_file, ec) > m_maxBytes) {
            auto old = m_file;
            old += L".1";
            std::filesystem::remove(old, ec);
            std::filesystem::rename(m_file, old, ec);
        }
        // ログ出力の失敗は握りつぶす (ログのためにアプリ動作を止めない)。メモリ上には残る。
        std::ofstream f(m_file, std::ios::app | std::ios::binary);
        if (f) f << format(e) << "\r\n";
    }
    m_entries.push_back(std::move(e));
    while (m_entries.size() > m_capacity) m_entries.pop_front();
}

std::vector<LogEntry> Logger::snapshot() const {
    std::lock_guard lk(m_mutex);
    return {m_entries.begin(), m_entries.end()};
}

std::string Logger::exportText() const {
    std::lock_guard lk(m_mutex);
    std::string out;
    for (const auto& e : m_entries) out += format(e) + "\r\n";
    return out;
}

void Logger::clear() {
    std::lock_guard lk(m_mutex);
    m_entries.clear();
}

}  // namespace lf
