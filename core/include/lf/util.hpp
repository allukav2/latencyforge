#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "lf/result.hpp"

namespace lf {

using Clock = std::function<std::string()>;  // ISO-8601 UTC 文字列を返す (テストで差し替え可能)

std::string nowIso8601Utc();
// "2026-01-01T00:00:00Z" 形式 (末尾の Z は省略可) を解釈。形式が違えば nullopt。
std::optional<std::chrono::system_clock::time_point> parseIso8601Utc(std::string_view s);

std::wstring widen(std::string_view utf8);
std::string narrow(std::wstring_view wide);

// 一時ファイルに書き込み → flush → 置換。途中でクラッシュしても元ファイルは壊れない。
Result<void> atomicWriteFile(const std::filesystem::path& path, std::string_view data);

// ファイルが無ければ NotFound。
Result<std::string> readFileText(const std::filesystem::path& path);

}  // namespace lf
