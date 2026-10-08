#include "lf/engine.hpp"

#include <algorithm>
#include <set>

namespace lf {

namespace {

constexpr const char* kCat = "engine";

std::string cause(const std::optional<Error>& e) { return e ? e->detail : std::string(); }

}  // namespace

Engine::Engine(IRegistry& registry, const Policy& policy, Logger& logger, EngineConfig config, Clock clock)
    : m_reg(registry),
      m_policy(policy),
      m_log(logger),
      m_cfg(std::move(config)),
      m_clock(std::move(clock)),
      m_history(m_cfg.historyFile) {}

Result<void> Engine::load() {
    m_loaded = false;
    auto st = loadState(m_cfg.stateFile, m_policy);
    if (!st.ok()) {
        m_log.error(kCat, "state file could not be loaded; all changes are refused until it is fixed", st.error().detail);
        return st.error();
    }
    m_state = std::move(st.value());
    m_loaded = true;
    m_log.info(kCat, "state loaded: " + std::to_string(m_state.applied.size()) + " applied tweak(s)" +
                         (m_state.pending ? ", UNFINISHED TRANSACTION FOUND" : ""));
    if (m_state.pending)
        m_log.warn(kCat, "an earlier operation did not finish; restoring is recommended", "tx " + m_state.pending->id);
    return {};
}

Report Engine::fail(Report r, Error e) {
    r.ok = false;
    m_log.error(kCat, "operation refused", e.detail);
    r.error = std::move(e);
    return r;
}

void Engine::record(const char* action, const std::string& id, const RegPath& path, const std::optional<RegValue>& oldV,
                    const std::optional<RegValue>& newV, const char* result, const std::string& message) {
    HistoryEntry e{m_clock(), action, id, path.display(), oldV, newV, result, message};
    if (auto r = m_history.append(e); !r.ok()) m_log.warn(kCat, "could not write history entry", r.error().detail);
}

Result<void> Engine::restoreValue(const RegPath& path, const std::optional<RegValue>& value) {
    if (auto pr = m_policy.check(path); !pr.ok()) return pr.error();
    auto cur = m_reg.read(path);
    if (cur.ok() && cur.value() == value) return {};  // 既に目的の状態 (冪等)
    if (value) return m_reg.write(path, *value);
    return m_reg.deleteValue(path);
}

TweakStatus Engine::status(const TweakDef& t) const {
    TweakStatus s;
    if (!t.supportsBuild(m_cfg.osBuild)) {
        s.state = TweakState::Unsupported;
        s.error = Error{ErrorCode::UnsupportedBuild, "Windows build " + std::to_string(m_cfg.osBuild) + " is outside " +
                                                         std::to_string(t.minBuild) + ".." + std::to_string(t.maxBuild)};
        return s;
    }
    if (auto pr = m_policy.check(t.target); !pr.ok()) {
        s.state = TweakState::Blocked;
        s.error = pr.error();
        return s;
    }
    auto cur = m_reg.read(t.target);
    if (!cur.ok()) {
        s.state = TweakState::Blocked;
        s.error = cur.error();
        return s;
    }
    s.current = cur.value();
    auto rec = m_state.applied.find(t.id);
    s.tracked = rec != m_state.applied.end();
    if (s.tracked)
        s.state = (s.current && *s.current == rec->second.applied) ? TweakState::Applied : TweakState::Drifted;
    else
        s.state = (s.current && *s.current == t.data) ? TweakState::AlreadyAtTarget : TweakState::NotApplied;
    return s;
}

bool Engine::rebootPending(std::chrono::system_clock::time_point bootTime) const {
    if (!m_state.rebootMark) return false;
    auto t = parseIso8601Utc(*m_state.rebootMark);
    return t && *t > bootTime;
}

// ------------------------------------------------------------------------------------------------ apply

Report Engine::apply(const std::vector<const TweakDef*>& input, ApplyOptions opt) {
    Report rep;
    rep.dryRun = opt.dryRun;
    if (!m_loaded) return fail(rep, {ErrorCode::NotLoaded, "state not loaded"});
    if (m_state.pending)
        return fail(rep, {ErrorCode::PendingTransaction, "an unfinished transaction must be resolved first"});

    std::vector<const TweakDef*> tweaks;
    {
        std::set<std::string> seen;
        for (const TweakDef* t : input)
            if (t && seen.insert(t->id).second) tweaks.push_back(t);
    }
    m_log.info(kCat, std::string(opt.dryRun ? "dry-run" : "apply") + " requested for " + std::to_string(tweaks.size()) +
                         " tweak(s)");

    // --- 事前検査 (ここまでは何も書き込まない)
    struct Change {
        const TweakDef* t;
        size_t item;
    };
    std::vector<Change> changes;
    bool preflightFailed = false;
    for (const TweakDef* t : tweaks) {
        ItemResult it;
        it.tweakId = t->id;
        it.after = t->data;
        it.requiresReboot = t->requiresReboot;
        if (!t->supportsBuild(m_cfg.osBuild)) {
            it.status = ItemStatus::Skipped;
            it.error = Error{ErrorCode::UnsupportedBuild, "Windows build " + std::to_string(m_cfg.osBuild) +
                                                              " is outside the supported range of " + t->id};
            rep.items.push_back(std::move(it));
            continue;
        }
        if (auto pr = m_policy.check(t->target); !pr.ok()) {
            it.status = ItemStatus::Blocked;
            it.error = pr.error();
            preflightFailed = true;
            rep.items.push_back(std::move(it));
            continue;
        }
        auto cur = m_reg.read(t->target);
        if (!cur.ok()) {
            it.status = cur.error().code == ErrorCode::UnsupportedValueType ? ItemStatus::Blocked : ItemStatus::Failed;
            it.error = cur.error();
            preflightFailed = true;
            rep.items.push_back(std::move(it));
            continue;
        }
        it.before = cur.value();
        if (it.before && *it.before == t->data) {
            it.status = ItemStatus::AlreadyApplied;
        } else {
            it.status = ItemStatus::WouldChange;
            changes.push_back({t, rep.items.size()});
        }
        rep.items.push_back(std::move(it));
    }

    if (preflightFailed) {
        // 1 件でも不可なら全体を拒否 (部分適用しない)。WouldChange のまま残る項目は「書き込まれていない」の意味。
        rep.ok = false;
        for (const auto& it : rep.items)
            if (it.error && !rep.error && (it.status == ItemStatus::Blocked || it.status == ItemStatus::Failed)) rep.error = it.error;
        m_log.error(kCat, "pre-flight check failed; nothing was written", cause(rep.error));
        return rep;
    }
    if (opt.dryRun || changes.empty()) {
        for (const auto& it : rep.items) rep.rebootRequired |= (it.status == ItemStatus::WouldChange && it.requiresReboot);
        return rep;
    }

    // --- 書き込み前に pending (適用前の値) とバックアップを永続化
    const State prev = m_state;
    State next = m_state;
    PendingTx tx;
    tx.id = tx.startedAt = m_clock();
    for (const Change& c : changes) {
        const bool tracked = next.applied.count(c.t->id) != 0;
        const auto& before = rep.items[c.item].before;
        tx.ops.push_back({c.t->id, c.t->target, before, tracked});
        if (!tracked) next.applied[c.t->id] = {c.t->id, c.t->target, before, c.t->data, tx.startedAt, c.t->requiresReboot};
    }
    next.pending = tx;
    if (auto s = saveState(m_cfg.stateFile, next); !s.ok()) {
        rep.ok = false;
        rep.error = s.error();
        m_log.error(kCat, "could not save the backup; nothing was written", s.error().detail);
        return rep;
    }
    m_state = std::move(next);

    // --- 適用
    std::vector<size_t> written;  // changes のインデックス
    std::optional<Error> writeError;
    size_t failedAt = 0;
    for (size_t i = 0; i < changes.size(); ++i) {
        auto r = m_reg.write(changes[i].t->target, changes[i].t->data);
        if (!r.ok()) {
            writeError = r.error();
            failedAt = i;
            break;
        }
        written.push_back(i);
        rep.items[changes[i].item].status = ItemStatus::Applied;
    }

    if (writeError) {
        m_log.error(kCat, "write failed; rolling back this transaction", writeError->detail);
        rep.ok = false;
        rep.error = writeError;
        ItemResult& bad = rep.items[changes[failedAt].item];
        bad.status = ItemStatus::Failed;
        bad.error = writeError;
        record("apply", bad.tweakId, changes[failedAt].t->target, bad.before, bad.after, "failed", writeError->detail);
        for (size_t i = failedAt + 1; i < changes.size(); ++i) rep.items[changes[i].item].status = ItemStatus::NotApplied;

        bool allRestored = true;
        for (auto rit = written.rbegin(); rit != written.rend(); ++rit) {
            const Change& c = changes[*rit];
            ItemResult& it = rep.items[c.item];
            auto rr = restoreValue(c.t->target, it.before);
            if (rr.ok()) {
                it.status = ItemStatus::RolledBack;
                record("rollback", it.tweakId, c.t->target, it.after, it.before, "ok", "rolled back after a later write failed");
            } else {
                it.status = ItemStatus::RollbackFailed;
                it.error = rr.error();
                allRestored = false;
                record("rollback", it.tweakId, c.t->target, it.after, it.before, "failed", rr.error().detail);
                m_log.error(kCat, "rollback of " + it.tweakId + " failed", rr.error().detail);
            }
        }
        if (allRestored) {
            m_state = prev;
            if (auto s = saveState(m_cfg.stateFile, m_state); !s.ok()) {
                rep.stateSaveFailed = true;  // ファイルには pending が残るが、復元は冪等なので次回起動時の回復で無害に収束する
                m_log.warn(kCat, "could not clear the pending marker after rollback", s.error().detail);
            }
            m_log.info(kCat, "rollback complete; the system is back to its previous state");
        } else {
            rep.rollbackIncomplete = true;  // pending を残す → 次回起動時に復元を提案
            m_log.error(kCat, "rollback incomplete; the pending transaction is kept for recovery");
        }
        return rep;
    }

    // --- 確定
    const std::string now = m_clock();
    for (const Change& c : changes) {
        AppliedRecord& rec = m_state.applied[c.t->id];
        rec.applied = c.t->data;
        rec.time = now;
        rec.requiresReboot = c.t->requiresReboot;
        rep.rebootRequired |= c.t->requiresReboot;
        const ItemResult& it = rep.items[c.item];
        record("apply", it.tweakId, c.t->target, it.before, it.after, "ok", "");
        m_log.info(kCat, "applied " + it.tweakId, c.t->target.display() + " = " + c.t->data.display());
    }
    m_state.pending.reset();
    if (rep.rebootRequired) m_state.rebootMark = now;
    if (auto s = saveState(m_cfg.stateFile, m_state); !s.ok()) {
        rep.stateSaveFailed = true;
        m_log.error(kCat, "changes were applied but the state file could not be finalized", s.error().detail);
    }
    return rep;
}

// ------------------------------------------------------------------------------------------------ revert

Report Engine::revert(const std::vector<std::string>& ids, bool dryRun) {
    Report rep;
    rep.dryRun = dryRun;
    if (!m_loaded) return fail(rep, {ErrorCode::NotLoaded, "state not loaded"});
    if (m_state.pending)
        return fail(rep, {ErrorCode::PendingTransaction, "an unfinished transaction must be resolved first"});

    std::set<std::string> seen;
    bool changed = false;
    for (const std::string& id : ids) {
        if (!seen.insert(id).second) continue;
        ItemResult it;
        it.tweakId = id;
        auto found = m_state.applied.find(id);
        if (found == m_state.applied.end()) {
            it.status = ItemStatus::NotApplied;  // 冪等: 適用されていないものの復元は成功扱い
            rep.items.push_back(std::move(it));
            continue;
        }
        const AppliedRecord rec = found->second;
        it.requiresReboot = rec.requiresReboot;
        it.after = rec.original;
        auto cur = m_reg.read(rec.target);
        if (cur.ok()) it.before = cur.value();

        if (dryRun) {
            it.status = ItemStatus::WouldRevert;
            rep.rebootRequired |= rec.requiresReboot;
            rep.items.push_back(std::move(it));
            continue;
        }
        const bool alreadyThere = cur.ok() && cur.value() == rec.original;
        auto rr = restoreValue(rec.target, rec.original);
        if (rr.ok()) {
            it.status = ItemStatus::Reverted;
            m_state.applied.erase(id);
            changed = true;
            rep.rebootRequired |= rec.requiresReboot;
            record("revert", id, rec.target, it.before, rec.original, alreadyThere ? "noop" : "ok",
                   alreadyThere ? "value was already at its original state" : "");
            m_log.info(kCat, "reverted " + id, rec.target.display());
        } else {
            it.status = ItemStatus::Failed;
            it.error = rr.error();
            rep.ok = false;
            if (!rep.error) rep.error = rr.error();
            record("revert", id, rec.target, it.before, rec.original, "failed", rr.error().detail);
            m_log.error(kCat, "revert of " + id + " failed (the backup is kept)", rr.error().detail);
        }
        rep.items.push_back(std::move(it));
    }
    if (changed) {
        if (rep.rebootRequired) m_state.rebootMark = m_clock();
        if (auto s = saveState(m_cfg.stateFile, m_state); !s.ok()) {
            rep.stateSaveFailed = true;
            m_log.error(kCat, "reverted, but the state file could not be updated", s.error().detail);
        }
    }
    return rep;
}

Report Engine::revertAll(bool dryRun) {
    std::vector<std::string> ids;
    for (const auto& [id, rec] : m_state.applied) ids.push_back(id);
    return revert(ids, dryRun);
}

// ------------------------------------------------------------------------------------------------ recovery

Report Engine::resolvePending() {
    Report rep;
    if (!m_loaded) return fail(rep, {ErrorCode::NotLoaded, "state not loaded"});
    if (!m_state.pending) return rep;  // 何も無ければ成功

    const PendingTx tx = *m_state.pending;
    m_log.warn(kCat, "restoring the state from before the unfinished transaction", "tx " + tx.id);
    State next = m_state;
    bool allOk = true;
    for (auto it = tx.ops.rbegin(); it != tx.ops.rend(); ++it) {
        ItemResult r;
        r.tweakId = it->tweakId;
        r.after = it->before;
        auto cur = m_reg.read(it->target);
        if (cur.ok()) r.before = cur.value();
        auto rr = restoreValue(it->target, it->before);
        if (rr.ok()) {
            r.status = ItemStatus::RolledBack;
            if (!it->wasTracked) next.applied.erase(it->tweakId);
            record("recover", r.tweakId, it->target, r.before, it->before, "ok", "restored state from before the interrupted operation");
        } else {
            r.status = ItemStatus::RollbackFailed;
            r.error = rr.error();
            allOk = false;
            if (!rep.error) rep.error = rr.error();
            record("recover", r.tweakId, it->target, r.before, it->before, "failed", rr.error().detail);
        }
        rep.items.push_back(std::move(r));
    }
    if (!allOk) {
        rep.ok = false;
        rep.rollbackIncomplete = true;
        m_log.error(kCat, "recovery incomplete; it can be retried (restores are idempotent)", cause(rep.error));
        return rep;
    }
    next.pending.reset();
    next.rebootMark = m_clock();  // 中断された書き込みが再起動を要する値だったか分からないので、安全側に倒す
    if (auto s = saveState(m_cfg.stateFile, next); !s.ok()) {
        rep.ok = false;
        rep.error = s.error();
        rep.stateSaveFailed = true;
        return rep;
    }
    m_state = std::move(next);
    m_log.info(kCat, "recovery complete");
    return rep;
}

}  // namespace lf
