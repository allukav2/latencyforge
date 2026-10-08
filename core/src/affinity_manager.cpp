#include "lf/affinity_manager.hpp"

#include <algorithm>
#include <cstdio>
#include <nlohmann/json.hpp>

#include "lf/util.hpp"

namespace lf {
namespace {

using nlohmann::json;
constexpr const char* kCat = "affinity";
constexpr int kMaxRestoreFailures = 3;

std::string hex(uint64_t v) {
    char buf[24];
    std::snprintf(buf, sizeof buf, "0x%llX", static_cast<unsigned long long>(v));
    return buf;
}

}  // namespace

AffinityManager::AffinityManager(IProcessApi& api, Logger& log, std::filesystem::path journalFile)
    : m_api(api), m_log(log), m_journal(std::move(journalFile)) {}

bool AffinityManager::needsPolling() const {
    if (!m_managed.empty()) return true;  // 復元が残っている間は止めない
    if (!m_cfg.enabled) return false;
    return std::any_of(m_cfg.profiles.begin(), m_cfg.profiles.end(), [](const AffinityProfile& p) { return p.enabled; });
}

const AffinityProfile* AffinityManager::findProfile(const std::string& id) const {
    for (const auto& p : m_cfg.profiles)
        if (p.id == id) return &p;
    return nullptr;
}

bool AffinityManager::isManaged(uint32_t pid) const {
    return std::any_of(m_managed.begin(), m_managed.end(), [&](const ManagedProcess& p) { return p.pid == pid; });
}

void AffinityManager::setStatus(AffinityStatus::State s, const std::string& note) {
    m_status.state = s;
    m_status.noteKey = note;
}

std::string AffinityManager::signature() const {
    std::string s = std::to_string(static_cast<int>(m_status.state)) + "|" + m_status.profileId + "|" +
                    std::to_string(m_status.gamePid) + "|" + std::to_string(m_managed.size()) + "|" + m_status.noteKey;
    return s;
}

// ------------------------------------------------------------------------------------------ 設定

void AffinityManager::setConfig(AffinityConfig config) {
    m_cfg = std::move(config);
    // 適用中のゲームのプロファイルが無効化/削除/exe 変更されたら、すぐ元に戻す。
    bool stillValid = false;
    if (m_active) {
        if (const AffinityProfile* p = findProfile(m_active->profileId)) {
            stillValid = m_cfg.enabled && p->enabled &&
                         std::find(p->exeNames.begin(), p->exeNames.end(), lowerAscii(m_active->exeName)) != p->exeNames.end();
        }
    }
    if (!stillValid) {
        if (!m_managed.empty()) restoreAll();
        m_active.reset();
        m_rejected.clear();
        m_status = AffinityStatus{};
        setStatus(m_cfg.enabled ? AffinityStatus::State::Idle : AffinityStatus::State::Disabled);
    }
}

// ------------------------------------------------------------------------------------------ ジャーナル

bool AffinityManager::persist() {
    json entries = json::array();
    for (const auto& p : m_managed) {
        entries.push_back({{"pid", p.pid},
                           {"startTime", p.startTime},
                           {"exe", p.exeName},
                           {"group", p.originalAffinity.group},
                           {"mask", p.originalAffinity.mask},
                           {"priority", p.originalPriority},
                           {"isGame", p.isGame},
                           {"affinityChanged", p.affinityChanged},
                           {"priorityChanged", p.priorityChanged}});
    }
    const json doc = {{"version", 1}, {"entries", entries}};
    auto r = atomicWriteFile(m_journal, doc.dump(2));
    if (!r.ok()) m_log.error(kCat, "could not write the restore journal", r.error().detail);
    return r.ok();
}

void AffinityManager::recoverFromJournal() {
    auto text = readFileText(m_journal);
    if (!text.ok()) return;  // 無ければ何もしない
    json doc = json::parse(text.value(), nullptr, false);
    if (doc.is_discarded() || !doc.is_object() || !doc.contains("entries") || !doc["entries"].is_array()) {
        m_log.warn(kCat, "restore journal is unreadable and was ignored");
        return;
    }
    for (const json& j : doc["entries"]) {
        if (!j.is_object()) continue;
        try {
            ManagedProcess p;
            p.pid = j.at("pid").get<uint32_t>();
            p.startTime = j.at("startTime").get<uint64_t>();
            p.exeName = j.at("exe").get<std::string>();
            p.originalAffinity.group = j.at("group").get<uint16_t>();
            p.originalAffinity.mask = j.at("mask").get<uint64_t>();
            p.originalPriority = j.at("priority").get<uint32_t>();
            p.isGame = j.value("isGame", false);
            p.affinityChanged = j.value("affinityChanged", true);
            p.priorityChanged = j.value("priorityChanged", false);
            if (p.originalAffinity.mask != 0) m_managed.push_back(std::move(p));
        } catch (const std::exception&) {
            m_log.warn(kCat, "a journal entry was malformed and was skipped");
        }
    }
    if (m_managed.empty()) return;
    m_log.warn(kCat, "restoring " + std::to_string(m_managed.size()) + " process(es) left modified by an earlier session");
    restoreAll();
}

// ------------------------------------------------------------------------------------------ 復元

bool AffinityManager::restoreOne(ManagedProcess& p) {
    bool done = true;
    if (p.affinityChanged) {
        auto r = m_api.setAffinity(p.pid, p.startTime, p.originalAffinity);
        if (r.ok()) {
            p.affinityChanged = false;
            m_log.info(kCat, "restored " + p.exeName + " (pid " + std::to_string(p.pid) + ")", "affinity " + hex(p.originalAffinity.mask));
        } else if (r.error().code == ErrorCode::ProcessChanged || r.error().code == ErrorCode::NotFound) {
            return true;  // プロセスは既に終了している (または PID が別プロセスに再利用された): 触らない
        } else {
            done = false;
            m_log.warn(kCat, "could not restore " + p.exeName + " (pid " + std::to_string(p.pid) + ")", r.error().detail);
        }
    }
    if (p.priorityChanged) {
        auto r = m_api.setPriority(p.pid, p.startTime, p.originalPriority);
        if (r.ok()) {
            p.priorityChanged = false;
        } else if (r.error().code == ErrorCode::ProcessChanged || r.error().code == ErrorCode::NotFound) {
            return true;
        } else {
            done = false;
            m_log.warn(kCat, "could not restore the priority of " + p.exeName, r.error().detail);
        }
    }
    return done;
}

void AffinityManager::restoreEntries(bool gameOnly, bool backgroundOnly) {
    std::vector<ManagedProcess> keep;
    // バックグラウンド → ゲームの順に戻す (ゲームの設定は最後まで保つ)。
    std::stable_sort(m_managed.begin(), m_managed.end(), [](const ManagedProcess& a, const ManagedProcess& b) { return !a.isGame && b.isGame; });
    for (auto& p : m_managed) {
        const bool selected = (!gameOnly || p.isGame) && (!backgroundOnly || !p.isGame);
        if (!selected) {
            keep.push_back(std::move(p));
            continue;
        }
        if (!restoreOne(p)) {
            if (++p.restoreFailures >= kMaxRestoreFailures) {
                m_log.error(kCat, "giving up restoring " + p.exeName + " (pid " + std::to_string(p.pid) + ") after repeated failures");
            } else {
                keep.push_back(std::move(p));  // 次の tick で再試行
            }
        }
    }
    m_managed = std::move(keep);
    persist();
}

void AffinityManager::restoreAll() {
    restoreEntries(false, false);
    if (m_managed.empty()) {
        m_active.reset();
        m_rejected.clear();
        m_status = AffinityStatus{};
        setStatus(m_cfg.enabled ? AffinityStatus::State::Idle : AffinityStatus::State::Disabled);
    }
}

// ------------------------------------------------------------------------------------------ 適用

void AffinityManager::startGame(const AffinityProfile& profile, const ProcessEntry& entry, const std::vector<ProcessEntry>& procs) {
    m_active = Active{profile.id, entry.pid, 0, entry.exeName, false};
    m_status = AffinityStatus{};
    m_status.profileId = profile.id;
    m_status.gameExe = entry.exeName;
    m_status.gamePid = entry.pid;

    auto q = m_api.query(entry.pid);
    if (!q.ok()) {
        // 開けない = 保護されている / アンチチートが保護している / 権限不足。触らずにスキップする。
        m_log.warn(kCat, "cannot open " + entry.exeName + "; it was left untouched (it may be protected, e.g. by an anti-cheat)", q.error().detail);
        setStatus(AffinityStatus::State::GameBlocked, "affinity.note.blockedOpen");
        return;
    }
    const ProcessState& st = q.value();
    m_active->startTime = st.startTime;
    if (SkipReason why = checkTarget(st, m_api.selfPid(), m_api.currentSessionId()); why != SkipReason::None) {
        m_log.warn(kCat, std::string("skipping ") + entry.exeName + ": " + skipReasonName(why));
        setStatus(AffinityStatus::State::GameBlocked, "affinity.note.blockedProtected");
        return;
    }

    m_status.plan = planAffinity(m_topology, profile.options);
    if (!m_status.plan.effective()) {
        m_log.info(kCat, entry.exeName + " is running; no change is made for this CPU layout");
        setStatus(AffinityStatus::State::GameNoEffect, m_status.plan.reason == PlanReason::SimpleTopology ? "affinity.note.noEffectSimple" : "affinity.note.noEffectOther");
        return;
    }
    const AffinityPlan& plan = m_status.plan;
    if (st.affinity.group != plan.group) {
        m_log.warn(kCat, entry.exeName + " runs in a different processor group; skipped");
        setStatus(AffinityStatus::State::GameBlocked, "affinity.note.blockedGroup");
        return;
    }
    const uint64_t newMask = plan.gameMask & st.affinity.mask;  // 既存のアフィニティより広げない
    if (newMask == 0) {
        m_log.warn(kCat, entry.exeName + ": the planned cores do not overlap its current affinity; skipped");
        setStatus(AffinityStatus::State::GameBlocked, "affinity.note.blockedOverlap");
        return;
    }

    ManagedProcess mp;
    mp.pid = st.pid;
    mp.startTime = st.startTime;
    mp.exeName = st.exeName.empty() ? entry.exeName : st.exeName;
    mp.originalAffinity = st.affinity;
    mp.originalPriority = st.priorityClass;
    mp.isGame = true;
    mp.affinityChanged = true;
    const uint32_t wantPriority = priorityClassOf(profile.priority);
    mp.priorityChanged = profile.priority != GamePriority::Unchanged && st.priorityClass != wantPriority;

    m_managed.push_back(mp);
    if (!persist()) {  // 変更前の値を保存できないなら、変更しない
        m_managed.pop_back();
        setStatus(AffinityStatus::State::GameBlocked, "affinity.note.blockedJournal");
        return;
    }

    auto r = m_api.setAffinity(st.pid, st.startTime, GroupMask{plan.group, newMask});
    if (!r.ok()) {
        m_log.warn(kCat, "could not change the affinity of " + entry.exeName + "; skipped", r.error().detail);
        m_managed.pop_back();
        persist();
        setStatus(AffinityStatus::State::GameBlocked, "affinity.note.blockedOpen");
        return;
    }
    if (mp.priorityChanged) {
        auto pr = m_api.setPriority(st.pid, st.startTime, wantPriority);
        if (!pr.ok()) {
            m_log.warn(kCat, "could not change the priority of " + entry.exeName, pr.error().detail);
            m_managed.back().priorityChanged = false;
            persist();
        }
    }
    m_active->managed = true;
    m_log.info(kCat, "optimized " + entry.exeName + " (pid " + std::to_string(st.pid) + ")",
               "affinity " + hex(st.affinity.mask) + " -> " + hex(newMask) + ", background " + hex(plan.backgroundMask));
    setStatus(AffinityStatus::State::GameActive);
    steerBackground(profile, procs);
}

void AffinityManager::steerBackground(const AffinityProfile& profile, const std::vector<ProcessEntry>& procs) {
    const AffinityPlan& plan = m_status.plan;
    if (!profile.moveBackground || !plan.effective() || plan.backgroundMask == 0 || !m_active) return;

    struct Target {
        ProcessState st;
        uint64_t newMask;
    };
    std::vector<Target> targets;
    for (const ProcessEntry& e : procs) {
        if (e.pid == m_active->pid || e.pid == m_api.selfPid() || isManaged(e.pid)) continue;
        const std::string lower = lowerAscii(e.exeName);
        if (auto it = m_rejected.find(e.pid); it != m_rejected.end() && it->second == lower) continue;  // 評価済み
        auto reject = [&]() { m_rejected[e.pid] = lower; };

        if (isNeverTouchName(e.exeName)) {
            reject();
            continue;
        }
        auto q = m_api.query(e.pid);  // 開けない = 保護/他ユーザー/システム: 黙ってスキップ
        if (!q.ok()) {
            reject();
            continue;
        }
        const ProcessState& st = q.value();
        if (checkTarget(st, m_api.selfPid(), m_api.currentSessionId()) != SkipReason::None || st.affinity.group != plan.group) {
            reject();
            continue;
        }
        if ((st.affinity.mask & ~plan.backgroundMask) == 0) {  // 既にバックグラウンド用コアの範囲内
            reject();
            continue;
        }
        const uint64_t newMask = st.affinity.mask & plan.backgroundMask;
        if (newMask == 0) {
            reject();
            continue;
        }
        targets.push_back({st, newMask});
    }
    if (targets.empty()) return;

    // 先に変更前の値をまとめて保存してから、変更する。
    const size_t before = m_managed.size();
    for (const Target& t : targets) {
        ManagedProcess mp;
        mp.pid = t.st.pid;
        mp.startTime = t.st.startTime;
        mp.exeName = t.st.exeName;
        mp.originalAffinity = t.st.affinity;
        mp.originalPriority = t.st.priorityClass;
        mp.isGame = false;
        mp.affinityChanged = true;
        m_managed.push_back(std::move(mp));
    }
    if (!persist()) {
        m_managed.resize(before);
        return;
    }
    size_t failed = 0;
    for (size_t i = 0; i < targets.size(); ++i) {
        const Target& t = targets[i];
        auto r = m_api.setAffinity(t.st.pid, t.st.startTime, GroupMask{plan.group, t.newMask});
        if (!r.ok()) {
            ++failed;
            m_rejected[t.st.pid] = lowerAscii(t.st.exeName);
            m_log.debug(kCat, "skipped " + t.st.exeName + " (pid " + std::to_string(t.st.pid) + ")", r.error().detail);
            m_managed.erase(std::remove_if(m_managed.begin(), m_managed.end(), [&](const ManagedProcess& p) { return p.pid == t.st.pid; }), m_managed.end());
        }
    }
    persist();
    size_t bg = 0;
    for (const auto& p : m_managed)
        if (!p.isGame) ++bg;
    m_status.backgroundCount = bg;
    m_log.info(kCat, "moved " + std::to_string(targets.size() - failed) + " background process(es) to " + hex(plan.backgroundMask) +
                         (failed ? " (" + std::to_string(failed) + " skipped)" : ""));
}

std::vector<ProcessState> listTargetCandidates(IProcessApi& api) {
    std::vector<ProcessState> out;
    std::unordered_map<std::string, bool> seen;
    for (const ProcessEntry& e : api.enumerate()) {
        const std::string lower = lowerAscii(e.exeName);
        if (e.pid == api.selfPid() || isNeverTouchName(e.exeName) || seen.count(lower)) continue;
        auto q = api.query(e.pid);
        if (!q.ok()) continue;  // 開けない = 保護/他ユーザー等。候補に出さない
        if (checkTarget(q.value(), api.selfPid(), api.currentSessionId()) != SkipReason::None) continue;
        if (!isValidExeName(e.exeName)) continue;
        seen[lower] = true;
        ProcessState s = q.value();
        s.exeName = e.exeName;
        out.push_back(std::move(s));
    }
    std::sort(out.begin(), out.end(), [](const ProcessState& a, const ProcessState& b) { return lowerAscii(a.exeName) < lowerAscii(b.exeName); });
    return out;
}

// ------------------------------------------------------------------------------------------ ポーリング

bool AffinityManager::tick() {
    const std::string before = signature();

    if (!m_cfg.enabled) {
        if (!m_managed.empty()) restoreAll();
        m_active.reset();
        if (m_status.state != AffinityStatus::State::Disabled) m_status = AffinityStatus{};
        return before != signature();
    }
    if (m_status.state == AffinityStatus::State::Disabled) setStatus(AffinityStatus::State::Idle);

    const std::vector<ProcessEntry> procs = m_api.enumerate();
    std::unordered_map<uint32_t, const ProcessEntry*> alive;
    alive.reserve(procs.size());
    for (const auto& e : procs) alive.emplace(e.pid, &e);

    // 終了したプロセスを管理対象から外す (終わったものは元に戻す必要が無い)。PID が別プロセスに再利用されていたら、名前で見分ける。
    auto gone = [&](uint32_t pid, const std::string& exe) {
        auto it = alive.find(pid);
        return it == alive.end() || lowerAscii(it->second->exeName) != lowerAscii(exe);
    };
    bool removedAny = false;
    {
        std::vector<ManagedProcess> keep;
        for (auto& p : m_managed) {
            if (gone(p.pid, p.exeName)) {
                removedAny = true;
                continue;
            }
            keep.push_back(std::move(p));
        }
        m_managed = std::move(keep);
    }
    for (auto it = m_rejected.begin(); it != m_rejected.end();) it = alive.count(it->first) ? std::next(it) : m_rejected.erase(it);

    // ゲームが終わった → バックグラウンドを復元して待機に戻る
    bool restoredThisTick = false;
    if (m_active && gone(m_active->pid, m_active->exeName)) {
        m_log.info(kCat, m_active->exeName + " exited; restoring the background processes");
        restoreAll();  // 残っているのはバックグラウンドだけ (ゲームは終了済み)
        restoredThisTick = true;
        m_active.reset();
        m_rejected.clear();
        m_status = AffinityStatus{};
        setStatus(AffinityStatus::State::Idle);
    } else if (removedAny) {
        persist();
    }

    // 保持している復元の再試行 (前回失敗したもの。1 tick につき 1 回まで)
    if (!m_active && !m_managed.empty() && !restoredThisTick) restoreAll();

    if (!m_active) {
        for (const AffinityProfile& p : m_cfg.profiles) {
            if (!p.enabled) continue;
            const ProcessEntry* found = nullptr;
            for (const ProcessEntry& e : procs) {
                if (e.pid == m_api.selfPid()) continue;
                if (std::find(p.exeNames.begin(), p.exeNames.end(), lowerAscii(e.exeName)) != p.exeNames.end()) {
                    found = &e;
                    break;
                }
            }
            if (found) {
                startGame(p, *found, procs);
                break;
            }
        }
    } else if (m_active->managed) {
        if (const AffinityProfile* p = findProfile(m_active->profileId)) steerBackground(*p, procs);  // 後から起動したプロセス
    }
    return before != signature();
}

}  // namespace lf
