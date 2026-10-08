#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "lf/engine.hpp"
#include "lf/log.hpp"
#include "lf/memory_registry.hpp"
#include "lf/policy.hpp"
#include "lf/preset.hpp"
#include "lf/tweak.hpp"

namespace lfapp {

struct BackendOptions {
    std::filesystem::path exeDir;
    std::filesystem::path configDir;
    bool demo = false;          // メモリ上のレジストリを使う。実機のレジストリには一切触れない。
    bool seedDemoPending = false;  // デモ: 未完了トランザクションを仕込む (回復ダイアログの確認用)
};

// UI の背後にある実体: レジストリ / ポリシー / tweak カタログ / エンジン。
// 宣言順 = 構築順 (Engine は Logger と IRegistry を参照するので、それらより後に宣言する)。
class Backend {
public:
    bool init(const BackendOptions& opt);

    lf::Logger log;
    std::unique_ptr<lf::IRegistry> registry;
    lf::Policy policy = lf::Policy::standard();
    std::unique_ptr<lf::TweakCatalog> catalog;
    std::vector<lf::PresetDef> presets;  // data/presets.json (検証済み)
    std::unique_ptr<lf::Engine> engine;

    bool demo = false;
    uint32_t osBuild = 0;
    std::vector<lf::DefinitionIssue> definitionIssues;  // 読み込めなかった tweak 定義
    std::optional<lf::Error> stateError;                // 状態ファイルが読めない/不正 (操作は拒否される)

    std::vector<const lf::TweakDef*> tweaksIn(const std::string& category) const;
};

}  // namespace lfapp
