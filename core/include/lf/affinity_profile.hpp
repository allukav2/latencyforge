#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "lf/affinity_plan.hpp"
#include "lf/result.hpp"

namespace lf {

enum class GamePriority { Unchanged, AboveNormal, High };

// 対象ゲームごとのルール。
struct AffinityProfile {
    std::string id;                    // "p1" など (一意)
    std::string name;                  // 表示名
    std::vector<std::string> exeNames; // 小文字に正規化済み ("game.exe")
    AffinityOptions options;
    bool moveBackground = true;        // ゲーム以外のユーザープロセスを残りのコアへ寄せる
    GamePriority priority = GamePriority::Unchanged;
    bool enabled = true;
};

struct AffinityConfig {
    bool enabled = false;  // 自動適用の全体スイッチ。既定は OFF (ユーザーが明示的に有効にするまで何も変更しない)
    std::vector<AffinityProfile> profiles;
};

const char* priorityKey(GamePriority p);
bool parsePriority(std::string_view text, GamePriority& out);
uint32_t priorityClassOf(GamePriority p);  // Windows の優先度クラス値 (Unchanged は Normal)

// 厳密な検証つきで読み込む。不正なら DefinitionInvalid (何も取り込まない)。
// 検証: ID 形式/重複、exe 名 (isValidExeName)、名前による除外対象 (isNeverTouchName) は登録不可、重複 exe、未知フィールド。
Result<AffinityConfig> parseAffinityConfig(std::string_view json);
std::string serializeAffinityConfig(const AffinityConfig& config);

Result<AffinityConfig> loadAffinityConfig(const std::filesystem::path& file);  // ファイルが無ければ既定値
Result<void> saveAffinityConfig(const std::filesystem::path& file, const AffinityConfig& config);

}  // namespace lf
