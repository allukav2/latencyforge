#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "lf/tweak.hpp"

namespace lf {

// プリセット = tweak ID の組。適用は通常の「差分プレビュー → 確認 → 適用」と全く同じ経路を通る。
struct PresetDef {
    std::string id;  // "safe" など
    LText title;
    LText description;
    std::vector<std::string> tweakIds;
};

// data/presets.json (schema 1) を検証してパースする。1 件でも不正なら out には何も入れず false (文書単位で拒否)。
// 検証: 構造/型/未知フィールド/ID 形式・重複、tweak ID がカタログに実在すること、空でないこと。
bool parsePresetDocument(std::string_view json, std::string_view source, const TweakCatalog& catalog,
                         std::vector<PresetDef>& out, std::vector<DefinitionIssue>& issues);

// カタログから tweak 定義を引く (parse 済みなので、すべて実在する)。
std::vector<const TweakDef*> resolvePreset(const PresetDef& preset, const TweakCatalog& catalog);

// プリセットに含まれる最大のリスク (1 つでも「中」があれば「中」)。
Risk presetRisk(const PresetDef& preset, const TweakCatalog& catalog);

}  // namespace lf
