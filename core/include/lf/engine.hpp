#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "lf/history.hpp"
#include "lf/log.hpp"
#include "lf/policy.hpp"
#include "lf/registry.hpp"
#include "lf/state.hpp"
#include "lf/tweak.hpp"

namespace lf {

enum class ItemStatus {
    // dry-run
    WouldChange,
    WouldRevert,
    // apply
    Applied,
    AlreadyApplied,  // 既に目標値 (こちらでは何も変更せず、記録も作らない)
    Skipped,         // Windows ビルド範囲外
    Failed,
    Blocked,         // ポリシー拒否 / バックアップ不能な型 など。この場合は何も書き込まれない
    RolledBack,
    RollbackFailed,
    // revert
    Reverted,
    NotApplied,      // 元々適用されていない (冪等: 成功扱い)
};

struct ItemResult {
    std::string tweakId;
    ItemStatus status = ItemStatus::Failed;
    std::optional<Error> error;
    std::optional<RegValue> before;  // 変更前の現在値 (nullopt = 存在しない)
    std::optional<RegValue> after;   // 変更後の値
    bool requiresReboot = false;
};

struct Report {
    bool ok = true;
    bool dryRun = false;
    std::optional<Error> error;  // 全体のエラー (個別は items[].error)
    std::vector<ItemResult> items;
    bool rebootRequired = false;
    bool rollbackIncomplete = false;  // ロールバックに失敗した項目あり → pending が残り、次回起動時に復元を提案
    bool stateSaveFailed = false;     // レジストリは変更済みだが状態ファイルの確定書き込みに失敗
};

// UI 表示用: tweak 1 件の現在の状態 (レジストリを読むだけで、何も書かない)。
enum class TweakState {
    NotApplied,       // 目標値ではなく、こちらが変更した記録も無い
    Applied,          // こちらが適用し、現在も適用時の値のまま
    Drifted,          // こちらが適用した記録はあるが、現在値は適用時の値と違う (外部で変更された)
    AlreadyAtTarget,  // 元から目標値 (こちらの変更ではないので、復元する対象も無い)
    Unsupported,      // Windows ビルドが範囲外
    Blocked,          // ポリシー拒否 / 読み取り不能 / バックアップ不能な型
};

struct TweakStatus {
    TweakState state = TweakState::NotApplied;
    std::optional<RegValue> current;  // nullopt = 存在しない (Blocked / Unsupported では意味なし)
    bool tracked = false;             // 復元用バックアップがある
    std::optional<Error> error;       // Unsupported / Blocked の理由
};

struct EngineConfig {
    std::filesystem::path stateFile;
    std::filesystem::path historyFile;
    uint32_t osBuild = 0;
};

// 適用 / 復元 / ロールバック / 異常終了からの回復。レジストリ書き込みはすべてここを通る。
//
// 状態ファイルの不変条件:
//  - applied[id].original は「最初に適用する直前の値」。再適用しても上書きしない。
//  - 書き込み前に pending (各操作の適用前値を含む) を永続化し、全件成功後に消す。
//    途中でプロセスが落ちても pending から確実に元へ戻せる。
//  - 復元先は Policy で再検査する。
// 注意: スレッドセーフではない。複数プロセスの同時実行は想定しない (単一インスタンス制御は app 側)。
class Engine {
public:
    Engine(IRegistry& registry, const Policy& policy, Logger& logger, EngineConfig config, Clock clock = nowIso8601Utc);

    // 状態ファイルを読む。失敗 (StateCorrupt 等) の間は apply/revert を拒否する。
    Result<void> load();
    bool loaded() const { return m_loaded; }

    struct ApplyOptions {
        bool dryRun = false;
    };
    // all-or-nothing: 書き込み中に 1 件でも失敗したら、このトランザクションで書いた分をすべて元に戻す。
    Report apply(const std::vector<const TweakDef*>& tweaks, ApplyOptions opt = {});

    // 冪等。記録が無い id は NotApplied (成功扱い)。個々の失敗は他の項目を止めない。
    Report revert(const std::vector<std::string>& tweakIds, bool dryRun = false);
    Report revertAll(bool dryRun = false);

    // 異常終了などで未完了のトランザクションがあるか / それを適用前の状態へ戻す。
    const std::optional<PendingTx>& pending() const { return m_state.pending; }
    Report resolvePending();

    TweakStatus status(const TweakDef& tweak) const;

    // 再起動しないと反映されない変更を、bootTime (最後の起動時刻) より後に確定したか。
    bool rebootPending(std::chrono::system_clock::time_point bootTime) const;

    bool isApplied(const std::string& id) const { return m_state.applied.count(id) != 0; }
    const std::map<std::string, AppliedRecord>& applied() const { return m_state.applied; }
    std::vector<HistoryEntry> history() const { return m_history.readAll(); }

private:
    Result<void> restoreValue(const RegPath& path, const std::optional<RegValue>& value);
    void record(const char* action, const std::string& id, const RegPath& path, const std::optional<RegValue>& oldV,
                const std::optional<RegValue>& newV, const char* result, const std::string& message);
    Report fail(Report r, Error e);

    IRegistry& m_reg;
    Policy m_policy;
    Logger& m_log;
    EngineConfig m_cfg;
    Clock m_clock;
    HistoryLog m_history;
    State m_state;
    bool m_loaded = false;
};

}  // namespace lf
