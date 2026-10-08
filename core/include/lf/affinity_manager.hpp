#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "lf/affinity_plan.hpp"
#include "lf/affinity_profile.hpp"
#include "lf/affinity_safety.hpp"
#include "lf/log.hpp"
#include "lf/process_api.hpp"

namespace lf {

// 変更したプロセス 1 件。original が復元の正本 (変更前のアフィニティ/優先度)。
struct ManagedProcess {
    uint32_t pid = 0;
    uint64_t startTime = 0;
    std::string exeName;
    GroupMask originalAffinity;
    uint32_t originalPriority = 0;
    bool isGame = false;
    bool affinityChanged = false;
    bool priorityChanged = false;
    int restoreFailures = 0;
};

struct AffinityStatus {
    enum class State {
        Disabled,      // 自動適用がオフ
        Idle,          // 有効。対象ゲームの起動待ち
        GameActive,    // ゲームに適用中
        GameNoEffect,  // ゲームは起動中だが、この CPU 構成では変更しない (単純な構成など)
        GameBlocked,   // ゲームを検出したが触れない (保護/アンチチート/権限不足など)
    } state = State::Disabled;
    std::string profileId;
    std::string gameExe;
    uint32_t gamePid = 0;
    AffinityPlan plan;
    size_t backgroundCount = 0;
    std::string noteKey;  // GameBlocked / GameNoEffect の理由 (data/lang のキー)
};

// プロセスの起動/終了を軽量にポーリングして、ゲームにアフィニティ/優先度を適用し、終了時に必ず復元する。
//
// 安全性の不変条件:
//  - 触る前に、変更前の値をジャーナル (JSON) へ永続化する。保存できなければ変更しない。
//  - 変更・復元のたびに、作成時刻で PID の再利用を確認する (別プロセスを誤って触らない)。
//  - 保護/重要/他ユーザー/Windows フォルダ配下/名前による除外 (checkTarget) には触れない。開けない・失敗は
//    スキップしてログに残す (アンチチートが保護しているプロセスは OpenProcess が失敗する)。
//  - 既存のアフィニティより広げない (新しいマスク = 計画 AND 元のマスク)。
// スレッド: 単一スレッド専用 (アプリのタイマーから tick() を呼ぶ)。
class AffinityManager {
public:
    AffinityManager(IProcessApi& api, Logger& log, std::filesystem::path journalFile);

    void setTopology(const CpuTopology& topology) { m_topology = topology; }
    void setConfig(AffinityConfig config);  // 無効化/プロファイル削除で適用中のものがあれば、すぐ復元する
    const AffinityConfig& config() const { return m_cfg; }

    // 起動時に 1 回: 前回の異常終了で変更されたまま残っているプロセスを元に戻す。
    void recoverFromJournal();

    // 1 回のポーリング。状態 (status / managed) が変わったら true。
    bool tick();

    // すべて元に戻す (アプリ終了時、手動、無効化)。戻せなかったものはジャーナルに残り、tick が再試行する。
    void restoreAll();

    const AffinityStatus& status() const { return m_status; }
    const std::vector<ManagedProcess>& managed() const { return m_managed; }
    bool needsPolling() const;  // ポーリングが必要か (無効なら false = 何も動かさない)

private:
    struct Active {
        std::string profileId;
        uint32_t pid = 0;
        uint64_t startTime = 0;
        std::string exeName;
        bool managed = false;  // false = 検出したが何も変更していない (NoEffect / Blocked)
    };

    const AffinityProfile* findProfile(const std::string& id) const;
    void startGame(const AffinityProfile& profile, const ProcessEntry& entry, const std::vector<ProcessEntry>& procs);
    void steerBackground(const AffinityProfile& profile, const std::vector<ProcessEntry>& procs);
    bool restoreOne(ManagedProcess& p);  // true = 片付いた (復元できた / プロセスが既に無い)
    void restoreEntries(bool gameOnly, bool backgroundOnly);
    bool persist();
    bool isManaged(uint32_t pid) const;
    void setStatus(AffinityStatus::State s, const std::string& note = {});
    std::string signature() const;

    IProcessApi& m_api;
    Logger& m_log;
    std::filesystem::path m_journal;
    CpuTopology m_topology;
    AffinityConfig m_cfg;
    AffinityStatus m_status;
    std::vector<ManagedProcess> m_managed;
    std::optional<Active> m_active;
    std::unordered_map<uint32_t, std::string> m_rejected;  // pid → 小文字の exe 名 (再評価しない)
};

// 「実行中のプロセスから選択」用の候補: 安全に対象にできる (checkTarget が None の) プロセスを、実行ファイル名ごとに 1 件、名前順で返す。
// 何も変更しない (読み取りのみ)。
std::vector<ProcessState> listTargetCandidates(IProcessApi& api);

}  // namespace lf
