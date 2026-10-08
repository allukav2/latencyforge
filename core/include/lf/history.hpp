#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "lf/registry.hpp"
#include "lf/util.hpp"

namespace lf {

// 変更履歴 1 行 (日時 / 対象 / 旧値 / 新値 / 結果)。
struct HistoryEntry {
    std::string time;
    std::string action;  // "apply" | "revert" | "rollback" | "recover"
    std::string tweakId;
    std::string target;  // RegPath::display()
    std::optional<RegValue> oldValue;  // nullopt = 存在しない
    std::optional<RegValue> newValue;
    std::string result;  // "ok" | "failed" | "noop"
    std::string message;
};

// JSON Lines で追記保存 (1 行 = 1 エントリ)。壊れた行は読み飛ばす。
class HistoryLog {
public:
    explicit HistoryLog(std::filesystem::path file) : m_file(std::move(file)) {}
    Result<void> append(const HistoryEntry& e) const;
    std::vector<HistoryEntry> readAll() const;

private:
    std::filesystem::path m_file;
};

}  // namespace lf
