#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "lf/policy.hpp"
#include "lf/registry.hpp"

namespace lf {

enum class Risk { Low, Medium };

struct LText {
    std::string ja, en;
    const std::string& get(bool japanese) const { return japanese ? ja : en; }
};

constexpr uint32_t kMinSupportedBuild = 17763;  // Windows 10 1809

// 1 tweak = レジストリ値 1 つ。
struct TweakDef {
    std::string id;        // "kernel.timer_check_flags"
    std::string category;  // "kernel" など (UI のページ振り分け用)
    RegPath target;
    RegValue data;
    LText title, summary, details, note;  // details / note は空可
    Risk risk = Risk::Low;
    bool requiresReboot = false;
    uint32_t minBuild = kMinSupportedBuild;
    uint32_t maxBuild = 0xFFFFFFFFu;

    bool supportsBuild(uint32_t build) const { return build >= minBuild && build <= maxBuild; }
    // 電源設定の tweak (POWER\ACTIVE\... のテンプレート)。実行時に resolveForScheme で具体的な電源プランに解決して使う。
    bool isPowerTemplate() const { return target.hive == RegHive::Power; }
};

// 電源設定のテンプレートを、具体的な電源プラン (正規化済みの GUID) に解決する。
// 返す定義の id は "<元の id>@<電源プランの GUID>" になり、電源プランごとに別の適用記録 (バックアップ) として管理される。
// 電源設定でない定義はそのまま返す。
TweakDef resolveForScheme(const TweakDef& base, const std::string& schemeGuid);

// "usb.selective_suspend_ac@<guid>" → "usb.selective_suspend_ac"
std::string baseTweakId(std::string_view id);

struct DefinitionIssue {
    std::string source;   // ファイル名など
    std::string tweakId;  // 不明なら空
    std::string message;
};

// tweak 定義 JSON (schema 1) をパースして検証する。1 件でも不正なら out には何も入れず false (ドキュメント単位で拒否)。
// 検証内容: 構造/型/範囲/未知フィールド/ID 形式・重複 + Policy (許可パス・セキュリティ関連キーの拒否)。
// data/tweak.schema.json は同じ規則を JSON Schema で記述したエディタ補完用の文書 (正本はこのコード)。
bool parseTweakDocument(std::string_view json, std::string_view source, const Policy& policy, std::vector<TweakDef>& out,
                        std::vector<DefinitionIssue>& issues);

class TweakCatalog {
public:
    explicit TweakCatalog(Policy policy) : m_policy(std::move(policy)) {}

    // 文書単位でアトミック: 不正な文書は丸ごと無視され issues に理由が積まれる。他ファイルは読み込まれる。
    // 既に読み込んだ ID と重複する文書も拒否。
    bool loadString(std::string_view json, std::string_view source, std::vector<DefinitionIssue>& issues);
    std::vector<DefinitionIssue> loadDirectory(const std::filesystem::path& dir);  // *.json (ファイル名順)

    const std::vector<TweakDef>& all() const { return m_tweaks; }
    const TweakDef* find(std::string_view id) const;

private:
    Policy m_policy;
    std::vector<TweakDef> m_tweaks;
};

}  // namespace lf
