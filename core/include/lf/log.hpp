#pragma once
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "lf/util.hpp"

namespace lf {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };
const char* levelName(LogLevel l);

struct LogEntry {
    std::string time;
    LogLevel level = LogLevel::Info;
    std::string category;
    std::string message;  // 1 行の要約 (英語)
    std::string detail;   // 技術詳細 (UI では展開表示)
};

// スレッドセーフ。メモリ上のリングバッファ (UI のログビューア用) + 任意でファイル出力。
class Logger {
public:
    explicit Logger(size_t capacity = 2000, Clock clock = nowIso8601Utc);

    void log(LogLevel level, std::string_view category, std::string message, std::string detail = {});
    void debug(std::string_view c, std::string m, std::string d = {}) { log(LogLevel::Debug, c, std::move(m), std::move(d)); }
    void info(std::string_view c, std::string m, std::string d = {}) { log(LogLevel::Info, c, std::move(m), std::move(d)); }
    void warn(std::string_view c, std::string m, std::string d = {}) { log(LogLevel::Warn, c, std::move(m), std::move(d)); }
    void error(std::string_view c, std::string m, std::string d = {}) { log(LogLevel::Error, c, std::move(m), std::move(d)); }

    // ファイル出力を有効化。maxBytes を超えたら 1 世代だけ ".1" に退避。
    void setFile(const std::filesystem::path& path, size_t maxBytes = 2u * 1024 * 1024);
    void setMinLevel(LogLevel level);

    std::vector<LogEntry> snapshot() const;
    std::string exportText() const;  // エクスポート/コピー用
    void clear();

    static std::string format(const LogEntry& e);

private:
    mutable std::mutex m_mutex;
    std::deque<LogEntry> m_entries;
    size_t m_capacity;
    Clock m_clock;
    LogLevel m_min = LogLevel::Debug;
    std::filesystem::path m_file;
    size_t m_maxBytes = 0;
};

}  // namespace lf
