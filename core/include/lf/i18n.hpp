#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace lf {

struct StringHash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
};

// 文字列テーブル。JSON (入れ子可、"a.b.c" に平坦化) を読み込む。
// loadFile を複数回呼ぶと上書きマージされる (en → ja の順で読めば未翻訳キーは en にフォールバック)。
// UI スレッド専用 (スレッドセーフではない)。
class Translator {
public:
    // 失敗時は false を返し、既存テーブルは変更しない。error に理由を入れる。
    bool loadFile(const std::filesystem::path& path, std::string* error = nullptr);
    bool loadString(std::string_view json, std::string* error = nullptr);
    void clear();

    // 未登録キーはキー文字列そのものを返す (欠落が画面で分かるように)。戻り値は次の clear() まで有効。
    const char* tr(std::string_view key) const;

    bool has(std::string_view key) const { return m_map.find(key) != m_map.end(); }
    size_t size() const { return m_map.size(); }

private:
    std::unordered_map<std::string, std::string, StringHash, std::equal_to<>> m_map;
    mutable std::unordered_set<std::string> m_missing;
};

}  // namespace lf
