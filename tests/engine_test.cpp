#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>

#include "lf/engine.hpp"
#include "lf/util.hpp"
#include "mock_registry.hpp"

namespace {

namespace fs = std::filesystem;
using lf::ItemStatus;
using lf::RegValue;
using lftest::MockRegistry;

const char* kKernelKey = "HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\kernel";

lf::RegPath kernelPath(const std::string& value) { return lf::parseRegPath(kKernelKey, value).value(); }

lf::TweakDef makeTweak(const std::string& id, const std::string& value, RegValue data, bool reboot = true) {
    lf::TweakDef t;
    t.id = id;
    t.category = "kernel";
    t.target = kernelPath(value);
    t.data = std::move(data);
    t.title = t.summary = {"題", "title"};
    t.requiresReboot = reboot;
    return t;
}

// 各テスト用の作業フォルダ + エンジン一式。
struct Env {
    fs::path dir;
    lf::Logger log{500, [] { return std::string("t"); }};
    MockRegistry reg;
    lf::Policy policy = lf::Policy::standard();
    int tick = 0;
    std::unique_ptr<lf::Engine> eng;
    uint32_t build = 26100;
    int hour = 0;  // テスト内で時刻を進めるため (再起動待ちの判定用)

    explicit Env(const std::string& name) {
        dir = fs::temp_directory_path() / "lf_tests_engine" / name;
        fs::remove_all(dir);
        fs::create_directories(dir);
        eng = makeEngine();
    }
    ~Env() {
        eng.reset();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    fs::path stateFile() const { return dir / "state.json"; }
    fs::path historyFile() const { return dir / "history.jsonl"; }

    std::unique_ptr<lf::Engine> makeEngine() {
        lf::EngineConfig cfg{stateFile(), historyFile(), build};
        return std::make_unique<lf::Engine>(reg, policy, log, cfg, [this] {
            char buf[40];
            std::snprintf(buf, sizeof buf, "2026-01-01T%02d:00:%02d", hour, ++tick % 60);
            return std::string(buf);
        });
    }
    // 「再起動」: 同じファイルから新しい Engine を作る。
    void restart() {
        eng = makeEngine();
        ASSERT_TRUE(eng->load().ok());
    }
};

}  // namespace

#define ENV(name) Env env(name); ASSERT_TRUE(env.eng->load().ok())

// ---- 基本: バックアップ → 適用 → 復元 ----------------------------------------------------------------

TEST(Engine, ApplyBacksUpTheOriginalAndWrites) {
    ENV("basic");
    auto t = makeTweak("kernel.a", "ValueA", RegValue::dword(0));
    env.reg.set(t.target, RegValue::dword(5));

    auto rep = env.eng->apply({&t});
    ASSERT_TRUE(rep.ok);
    ASSERT_EQ(rep.items.size(), 1u);
    EXPECT_EQ(rep.items[0].status, ItemStatus::Applied);
    EXPECT_EQ(rep.items[0].before, std::optional<RegValue>(RegValue::dword(5)));
    EXPECT_TRUE(rep.rebootRequired);
    EXPECT_EQ(env.reg.get(t.target), std::optional<RegValue>(RegValue::dword(0)));

    ASSERT_TRUE(env.eng->isApplied("kernel.a"));
    EXPECT_EQ(env.eng->applied().at("kernel.a").original, std::optional<RegValue>(RegValue::dword(5)));
    EXPECT_FALSE(env.eng->pending().has_value());
    EXPECT_TRUE(fs::exists(env.stateFile()));

    // バックアップは永続化されている (別 Engine で復元できる)
    env.restart();
    auto rev = env.eng->revert({"kernel.a"});
    ASSERT_TRUE(rev.ok);
    EXPECT_EQ(rev.items[0].status, ItemStatus::Reverted);
    EXPECT_EQ(env.reg.get(t.target), std::optional<RegValue>(RegValue::dword(5)));
    EXPECT_FALSE(env.eng->isApplied("kernel.a"));
}

TEST(Engine, OriginalThatDidNotExistIsRestoredByDeleting) {
    ENV("absent");
    auto t = makeTweak("kernel.a", "ValueA", RegValue::dword(1));
    ASSERT_FALSE(env.reg.has(t.target));
    ASSERT_TRUE(env.eng->apply({&t}).ok);
    ASSERT_TRUE(env.reg.has(t.target));
    EXPECT_EQ(env.eng->applied().at("kernel.a").original, std::nullopt);

    auto rev = env.eng->revert({"kernel.a"});
    ASSERT_TRUE(rev.ok);
    EXPECT_FALSE(env.reg.has(t.target)) << "a value that did not exist must be removed again";
}

TEST(Engine, ReapplyingKeepsTheFirstOriginal) {
    ENV("reapply");
    auto v1 = makeTweak("kernel.a", "ValueA", RegValue::dword(1));
    auto v2 = makeTweak("kernel.a", "ValueA", RegValue::dword(2));
    env.reg.set(v1.target, RegValue::dword(9));

    ASSERT_TRUE(env.eng->apply({&v1}).ok);
    ASSERT_TRUE(env.eng->apply({&v2}).ok);
    EXPECT_EQ(env.reg.get(v1.target), std::optional<RegValue>(RegValue::dword(2)));
    EXPECT_EQ(env.eng->applied().at("kernel.a").original, std::optional<RegValue>(RegValue::dword(9)));

    ASSERT_TRUE(env.eng->revert({"kernel.a"}).ok);
    EXPECT_EQ(env.reg.get(v1.target), std::optional<RegValue>(RegValue::dword(9)));
}

TEST(Engine, ApplyIsIdempotentWhenAlreadyAtTarget) {
    ENV("idem_apply");
    auto t = makeTweak("kernel.a", "ValueA", RegValue::dword(0));
    env.reg.set(t.target, RegValue::dword(0));  // 元から目標値

    auto rep = env.eng->apply({&t});
    ASSERT_TRUE(rep.ok);
    EXPECT_EQ(rep.items[0].status, ItemStatus::AlreadyApplied);
    EXPECT_EQ(env.reg.writes, 0);
    EXPECT_FALSE(env.eng->isApplied("kernel.a")) << "we changed nothing, so there is nothing to restore";
    EXPECT_EQ(env.eng->revert({"kernel.a"}).items[0].status, ItemStatus::NotApplied);
    EXPECT_EQ(env.reg.get(t.target), std::optional<RegValue>(RegValue::dword(0)));  // 触っていない
}

TEST(Engine, RevertIsIdempotent) {
    ENV("idem_revert");
    auto t = makeTweak("kernel.a", "ValueA", RegValue::dword(0));
    env.reg.set(t.target, RegValue::dword(5));
    ASSERT_TRUE(env.eng->apply({&t}).ok);

    EXPECT_EQ(env.eng->revert({"kernel.a"}).items[0].status, ItemStatus::Reverted);
    auto again = env.eng->revert({"kernel.a"});
    EXPECT_TRUE(again.ok);
    EXPECT_EQ(again.items[0].status, ItemStatus::NotApplied);
    EXPECT_TRUE(env.eng->revert({"never.applied.thing"}).ok);
    EXPECT_EQ(env.reg.get(t.target), std::optional<RegValue>(RegValue::dword(5)));
}

TEST(Engine, RevertRestoresEvenIfSomeoneChangedTheValueMeanwhile) {
    ENV("drift");
    auto t = makeTweak("kernel.a", "ValueA", RegValue::dword(0));
    env.reg.set(t.target, RegValue::dword(5));
    ASSERT_TRUE(env.eng->apply({&t}).ok);
    env.reg.set(t.target, RegValue::dword(7));  // 外部で変更された
    ASSERT_TRUE(env.eng->revert({"kernel.a"}).ok);
    EXPECT_EQ(env.reg.get(t.target), std::optional<RegValue>(RegValue::dword(5)));
}

TEST(Engine, RevertAllRestoresEverythingAndEmptiesState) {
    ENV("revert_all");
    auto a = makeTweak("kernel.a", "A", RegValue::dword(0));
    auto b = makeTweak("kernel.b", "B", RegValue::dword(1));
    env.reg.set(a.target, RegValue::dword(10));
    ASSERT_TRUE(env.eng->apply({&a, &b}).ok);

    auto rep = env.eng->revertAll();
    ASSERT_TRUE(rep.ok);
    EXPECT_EQ(rep.items.size(), 2u);
    EXPECT_EQ(env.reg.get(a.target), std::optional<RegValue>(RegValue::dword(10)));
    EXPECT_FALSE(env.reg.has(b.target));
    EXPECT_TRUE(env.eng->applied().empty());
    EXPECT_TRUE(env.eng->revertAll().ok);  // 2 回目も成功
}

// ---- Dry-run / 互換性 ---------------------------------------------------------------------------------

TEST(Engine, DryRunWritesNothingAndShowsTheDiff) {
    ENV("dryrun");
    auto t = makeTweak("kernel.a", "ValueA", RegValue::dword(0));
    env.reg.set(t.target, RegValue::dword(5));

    auto rep = env.eng->apply({&t}, {.dryRun = true});
    ASSERT_TRUE(rep.ok);
    EXPECT_TRUE(rep.dryRun);
    EXPECT_EQ(rep.items[0].status, ItemStatus::WouldChange);
    EXPECT_EQ(rep.items[0].before, std::optional<RegValue>(RegValue::dword(5)));
    EXPECT_EQ(rep.items[0].after, std::optional<RegValue>(RegValue::dword(0)));
    EXPECT_TRUE(rep.rebootRequired);
    EXPECT_EQ(env.reg.writes, 0);
    EXPECT_EQ(env.reg.get(t.target), std::optional<RegValue>(RegValue::dword(5)));
    EXPECT_FALSE(fs::exists(env.stateFile())) << "dry-run must not create a backup or state";
    EXPECT_TRUE(env.eng->history().empty());
}

TEST(Engine, RevertDryRunChangesNothing) {
    ENV("dryrun_revert");
    auto t = makeTweak("kernel.a", "ValueA", RegValue::dword(0));
    env.reg.set(t.target, RegValue::dword(5));
    ASSERT_TRUE(env.eng->apply({&t}).ok);
    const int writes = env.reg.writes;
    auto rep = env.eng->revert({"kernel.a"}, /*dryRun=*/true);
    EXPECT_EQ(rep.items[0].status, ItemStatus::WouldRevert);
    EXPECT_EQ(env.reg.writes, writes);
    EXPECT_TRUE(env.eng->isApplied("kernel.a"));
}

TEST(Engine, TweaksOutsideTheBuildRangeAreSkipped) {
    ENV("build");
    env.build = 17763;
    env.restart();
    auto t = makeTweak("kernel.a", "ValueA", RegValue::dword(0));
    t.minBuild = 19041;
    auto rep = env.eng->apply({&t});
    EXPECT_TRUE(rep.ok);
    EXPECT_EQ(rep.items[0].status, ItemStatus::Skipped);
    ASSERT_TRUE(rep.items[0].error.has_value());
    EXPECT_EQ(rep.items[0].error->code, lf::ErrorCode::UnsupportedBuild);
    EXPECT_EQ(env.reg.writes, 0);
}

// ---- 事前検査で拒否 -----------------------------------------------------------------------------------

TEST(Engine, PolicyViolationRefusesTheWholeApplyBeforeAnyWrite) {
    ENV("blocked");
    auto good = makeTweak("kernel.good", "Good", RegValue::dword(1));
    lf::TweakDef evil = good;
    evil.id = "kernel.evil";
    evil.target = lf::parseRegPath("HKLM\\SOFTWARE\\Microsoft\\Windows Defender", "DisableAntiSpyware").value();

    auto rep = env.eng->apply({&good, &evil});
    EXPECT_FALSE(rep.ok);
    ASSERT_TRUE(rep.error.has_value());
    EXPECT_EQ(rep.error->code, lf::ErrorCode::PolicyDenied);
    EXPECT_EQ(rep.items[1].status, ItemStatus::Blocked);
    EXPECT_EQ(env.reg.writes, 0) << "no partial application";
    EXPECT_FALSE(env.reg.has(good.target));
    EXPECT_FALSE(fs::exists(env.stateFile()));
}

TEST(Engine, ExistingValueOfAnUnsupportedTypeIsNotOverwritten) {
    ENV("unsupported_type");
    auto t = makeTweak("kernel.a", "ValueA", RegValue::dword(0));
    env.reg.unsupportedRead.insert(MockRegistry::key(t.target));  // 例: 既存が REG_BINARY でバックアップ不能
    auto rep = env.eng->apply({&t});
    EXPECT_FALSE(rep.ok);
    EXPECT_EQ(rep.items[0].status, ItemStatus::Blocked);
    EXPECT_EQ(rep.error->code, lf::ErrorCode::UnsupportedValueType);
    EXPECT_EQ(env.reg.writes, 0);
}

TEST(Engine, NothingIsWrittenIfTheBackupCannotBeSaved) {
    Env env("no_backup");
    // 親ディレクトリのつもりのパスが通常ファイル → 状態ファイルを保存できない
    { std::ofstream(env.dir / "blocker") << "x"; }
    lf::EngineConfig cfg{env.dir / "blocker" / "state.json", env.dir / "history.jsonl", env.build};
    lf::Engine eng(env.reg, env.policy, env.log, cfg);
    ASSERT_TRUE(eng.load().ok());  // 状態ファイル無し = 空
    auto t = makeTweak("kernel.a", "ValueA", RegValue::dword(0));
    auto rep = eng.apply({&t});
    EXPECT_FALSE(rep.ok);
    EXPECT_EQ(rep.error->code, lf::ErrorCode::StateWrite);
    EXPECT_EQ(env.reg.writes, 0) << "no backup, no change";
}

// ---- ロールバック -------------------------------------------------------------------------------------

TEST(Engine, MidwayFailureRollsBackEverythingWrittenSoFar) {
    ENV("rollback");
    auto a = makeTweak("kernel.a", "A", RegValue::dword(1));
    auto b = makeTweak("kernel.b", "B", RegValue::dword(1));
    auto c = makeTweak("kernel.c", "C", RegValue::dword(1));
    env.reg.set(a.target, RegValue::dword(100));  // A は元の値あり、B/C は存在しない
    env.reg.hook = [&](const char* op, const lf::RegPath& p) -> std::optional<lf::Error> {
        if (std::string(op) == "write" && p.valueName == "C") return lf::Error{lf::ErrorCode::RegistryWrite, "injected", 5};
        return std::nullopt;
    };

    auto rep = env.eng->apply({&a, &b, &c});
    EXPECT_FALSE(rep.ok);
    EXPECT_FALSE(rep.rollbackIncomplete);
    ASSERT_EQ(rep.items.size(), 3u);
    EXPECT_EQ(rep.items[0].status, ItemStatus::RolledBack);
    EXPECT_EQ(rep.items[1].status, ItemStatus::RolledBack);
    EXPECT_EQ(rep.items[2].status, ItemStatus::Failed);

    EXPECT_EQ(env.reg.get(a.target), std::optional<RegValue>(RegValue::dword(100)));
    EXPECT_FALSE(env.reg.has(b.target));
    EXPECT_FALSE(env.reg.has(c.target));
    EXPECT_TRUE(env.eng->applied().empty());
    EXPECT_FALSE(env.eng->pending().has_value());

    // 永続化された状態もきれい (再起動後も pending/applied なし)
    env.restart();
    EXPECT_TRUE(env.eng->applied().empty());
    EXPECT_FALSE(env.eng->pending().has_value());

    // 履歴に失敗とロールバックが残る
    int failed = 0, rolledBack = 0;
    for (const auto& h : env.eng->history()) {
        if (h.action == "apply" && h.result == "failed") ++failed;
        if (h.action == "rollback" && h.result == "ok") ++rolledBack;
    }
    EXPECT_EQ(failed, 1);
    EXPECT_EQ(rolledBack, 2);
}

TEST(Engine, RollbackThatItselfFailsKeepsThePendingTransactionForRecovery) {
    ENV("rollback_fail");
    auto a = makeTweak("kernel.a", "A", RegValue::dword(1));
    auto b = makeTweak("kernel.b", "B", RegValue::dword(1));
    env.reg.set(a.target, RegValue::dword(100));
    int writesToA = 0;
    bool injectionActive = true;
    env.reg.hook = [&](const char* op, const lf::RegPath& p) -> std::optional<lf::Error> {
        if (!injectionActive || std::string(op) != "write") return std::nullopt;
        if (p.valueName == "B") return lf::Error{lf::ErrorCode::RegistryWrite, "injected B", 5};
        if (p.valueName == "A" && ++writesToA == 2) return lf::Error{lf::ErrorCode::AccessDenied, "injected A restore", 5};
        return std::nullopt;
    };

    auto rep = env.eng->apply({&a, &b});
    EXPECT_FALSE(rep.ok);
    EXPECT_TRUE(rep.rollbackIncomplete);
    EXPECT_EQ(rep.items[0].status, ItemStatus::RollbackFailed);
    EXPECT_EQ(env.reg.get(a.target), std::optional<RegValue>(RegValue::dword(1))) << "A is still modified";
    ASSERT_TRUE(env.eng->pending().has_value());

    // 次回起動: 未完了を検出 → 新しい適用は拒否 → 回復で元に戻る
    injectionActive = false;
    env.restart();
    ASSERT_TRUE(env.eng->pending().has_value());
    EXPECT_EQ(env.eng->pending()->ops.size(), 2u);
    auto blocked = env.eng->apply({&a});
    EXPECT_FALSE(blocked.ok);
    EXPECT_EQ(blocked.error->code, lf::ErrorCode::PendingTransaction);

    auto rec = env.eng->resolvePending();
    ASSERT_TRUE(rec.ok);
    EXPECT_EQ(env.reg.get(a.target), std::optional<RegValue>(RegValue::dword(100)));
    EXPECT_FALSE(env.reg.has(b.target));
    EXPECT_FALSE(env.eng->pending().has_value());
    EXPECT_TRUE(env.eng->applied().empty());
    EXPECT_TRUE(env.eng->resolvePending().ok);  // 冪等
}

TEST(Engine, CrashMidApplyIsDetectedAndRecoveredOnNextStart) {
    ENV("crash");
    auto a = makeTweak("kernel.a", "A", RegValue::dword(1));
    auto b = makeTweak("kernel.b", "B", RegValue::dword(1));
    env.reg.set(a.target, RegValue::dword(100));
    env.reg.crashAfterWrites = 1;  // A を書いた直後、B を書く前にプロセスが落ちる想定

    EXPECT_THROW((void)env.eng->apply({&a, &b}), lftest::SimulatedCrash);
    EXPECT_EQ(env.reg.get(a.target), std::optional<RegValue>(RegValue::dword(1)));  // 中途半端な状態

    env.reg.crashAfterWrites = -1;
    env.restart();  // 新しいプロセス
    ASSERT_TRUE(env.eng->pending().has_value()) << "unfinished transaction must be detected";
    EXPECT_EQ(env.eng->pending()->ops.size(), 2u);

    auto rec = env.eng->resolvePending();
    ASSERT_TRUE(rec.ok);
    EXPECT_EQ(env.reg.get(a.target), std::optional<RegValue>(RegValue::dword(100)));
    EXPECT_FALSE(env.reg.has(b.target));
    EXPECT_FALSE(env.eng->pending().has_value());
    EXPECT_TRUE(env.eng->applied().empty());

    env.restart();
    EXPECT_FALSE(env.eng->pending().has_value());
}

TEST(Engine, RecoveryRestoresThePreTransactionValueOfAnAlreadyAppliedTweak) {
    ENV("crash_tracked");
    auto v1 = makeTweak("kernel.a", "A", RegValue::dword(1));
    auto v2 = makeTweak("kernel.a", "A", RegValue::dword(2));
    env.reg.set(v1.target, RegValue::dword(100));
    ASSERT_TRUE(env.eng->apply({&v1}).ok);  // 100 -> 1 (確定済み)
    env.reg.crashAfterWrites = env.reg.writes;  // 次の write で落ちる (書く前)
    EXPECT_THROW((void)env.eng->apply({&v2}), lftest::SimulatedCrash);

    env.reg.crashAfterWrites = -1;
    env.restart();
    ASSERT_TRUE(env.eng->resolvePending().ok);
    EXPECT_EQ(env.reg.get(v1.target), std::optional<RegValue>(RegValue::dword(1))) << "back to the confirmed state, not the original";
    EXPECT_TRUE(env.eng->isApplied("kernel.a")) << "the earlier, confirmed apply is still tracked";
    EXPECT_EQ(env.eng->applied().at("kernel.a").original, std::optional<RegValue>(RegValue::dword(100)));
}

TEST(Engine, FailedRevertKeepsTheBackupAndCanBeRetried) {
    ENV("revert_fail");
    auto a = makeTweak("kernel.a", "A", RegValue::dword(0));
    auto b = makeTweak("kernel.b", "B", RegValue::dword(0));
    env.reg.set(a.target, RegValue::dword(5));
    env.reg.set(b.target, RegValue::dword(6));
    ASSERT_TRUE(env.eng->apply({&a, &b}).ok);

    bool inject = true;
    env.reg.hook = [&](const char* op, const lf::RegPath& p) -> std::optional<lf::Error> {
        if (inject && std::string(op) == "write" && p.valueName == "A") return lf::Error{lf::ErrorCode::AccessDenied, "injected", 5};
        return std::nullopt;
    };
    auto rep = env.eng->revertAll();
    EXPECT_FALSE(rep.ok);
    EXPECT_EQ(env.reg.get(b.target), std::optional<RegValue>(RegValue::dword(6))) << "B is still reverted despite A failing";
    EXPECT_TRUE(env.eng->isApplied("kernel.a")) << "A's backup must be kept";
    EXPECT_FALSE(env.eng->isApplied("kernel.b"));

    inject = false;
    EXPECT_TRUE(env.eng->revertAll().ok);
    EXPECT_EQ(env.reg.get(a.target), std::optional<RegValue>(RegValue::dword(5)));
}

// ---- 状態ファイルの保護 -------------------------------------------------------------------------------

TEST(Engine, TamperedStateTargetingASecurityKeyIsRefused) {
    Env env("tampered");
    {
        std::ofstream f(env.stateFile());
        f << R"({"version":1,"applied":{"kernel.a":{"key":"HKLM\\SOFTWARE\\Microsoft\\Windows Defender",
            "value":"DisableAntiSpyware","original":{"type":"REG_DWORD","data":0},
            "applied":{"type":"REG_DWORD","data":1},"time":"t","requiresReboot":false}},"pending":null})";
    }
    auto r = env.eng->load();
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, lf::ErrorCode::StateCorrupt);
    EXPECT_FALSE(env.eng->loaded());
    auto rep = env.eng->revertAll();
    EXPECT_FALSE(rep.ok);
    EXPECT_EQ(env.reg.writes + env.reg.deletes, 0) << "a tampered state file must never cause a registry write";
}

TEST(Engine, TamperedPendingOperationIsRefusedToo) {
    Env env("tampered_pending");
    {
        std::ofstream f(env.stateFile());
        f << R"({"version":1,"applied":{},"pending":{"id":"x","startedAt":"x","ops":[{"tweakId":"a",
            "key":"HKLM\\SYSTEM\\CurrentControlSet\\Control\\Lsa","value":"RunAsPPL","before":null,"wasTracked":false}]}})";
    }
    EXPECT_EQ(env.eng->load().error().code, lf::ErrorCode::StateCorrupt);
    EXPECT_FALSE(env.eng->resolvePending().ok);
    EXPECT_EQ(env.reg.writes + env.reg.deletes, 0);
}

TEST(Engine, CorruptStateFileIsReportedAndLeftUntouched) {
    Env env("corrupt");
    { std::ofstream(env.stateFile()) << "{{{ not json"; }
    auto r = env.eng->load();
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, lf::ErrorCode::StateCorrupt);
    auto t = makeTweak("kernel.a", "A", RegValue::dword(0));
    EXPECT_FALSE(env.eng->apply({&t}).ok);
    EXPECT_EQ(env.reg.writes, 0);
    std::ifstream f(env.stateFile());
    std::string content((std::istreambuf_iterator<char>(f)), {});
    EXPECT_EQ(content, "{{{ not json") << "the damaged file must be preserved for diagnosis";
}

// ---- 履歴 ---------------------------------------------------------------------------------------------

TEST(Engine, HistoryRecordsTimeTargetOldNewAndResult) {
    ENV("history");
    auto t = makeTweak("kernel.a", "ValueA", RegValue::dword(0));
    env.reg.set(t.target, RegValue::dword(5));
    ASSERT_TRUE(env.eng->apply({&t}).ok);
    ASSERT_TRUE(env.eng->revert({"kernel.a"}).ok);

    env.restart();  // 履歴はファイルから読み戻せる
    auto h = env.eng->history();
    ASSERT_EQ(h.size(), 2u);
    EXPECT_EQ(h[0].action, "apply");
    EXPECT_EQ(h[0].tweakId, "kernel.a");
    EXPECT_FALSE(h[0].time.empty());
    EXPECT_NE(h[0].target.find("ValueA"), std::string::npos);
    EXPECT_EQ(h[0].oldValue, std::optional<RegValue>(RegValue::dword(5)));
    EXPECT_EQ(h[0].newValue, std::optional<RegValue>(RegValue::dword(0)));
    EXPECT_EQ(h[0].result, "ok");
    EXPECT_EQ(h[1].action, "revert");
    EXPECT_EQ(h[1].oldValue, std::optional<RegValue>(RegValue::dword(0)));
    EXPECT_EQ(h[1].newValue, std::optional<RegValue>(RegValue::dword(5)));
}

TEST(Engine, HistoryRepresentsAbsenceAsNull) {
    ENV("history_absent");
    auto t = makeTweak("kernel.a", "ValueA", RegValue::dword(1));
    ASSERT_TRUE(env.eng->apply({&t}).ok);
    auto h = env.eng->history();
    ASSERT_EQ(h.size(), 1u);
    EXPECT_EQ(h[0].oldValue, std::nullopt);
    EXPECT_EQ(h[0].newValue, std::optional<RegValue>(RegValue::dword(1)));
}

TEST(History, SkipsDamagedLines) {
    auto dir = fs::temp_directory_path() / "lf_tests_engine" / "history_damaged";
    fs::remove_all(dir);
    fs::create_directories(dir);
    lf::HistoryLog h(dir / "h.jsonl");
    ASSERT_TRUE(h.append({"t1", "apply", "x.y", "p", std::nullopt, RegValue::dword(1), "ok", ""}).ok());
    { std::ofstream(dir / "h.jsonl", std::ios::app) << "garbage line\n"; }
    ASSERT_TRUE(h.append({"t2", "revert", "x.y", "p", RegValue::dword(1), std::nullopt, "ok", ""}).ok());
    EXPECT_EQ(h.readAll().size(), 2u);
}

// ---- UI 用の状態判定 (status) ------------------------------------------------------------------------

TEST(EngineStatus, ReportsEachState) {
    ENV("status");
    auto t = makeTweak("kernel.a", "A", RegValue::dword(0));

    auto s = env.eng->status(t);  // 値が存在しない
    EXPECT_EQ(s.state, lf::TweakState::NotApplied);
    EXPECT_FALSE(s.current.has_value());
    EXPECT_FALSE(s.tracked);

    env.reg.set(t.target, RegValue::dword(5));
    s = env.eng->status(t);
    EXPECT_EQ(s.state, lf::TweakState::NotApplied);
    EXPECT_EQ(s.current, std::optional<RegValue>(RegValue::dword(5)));

    ASSERT_TRUE(env.eng->apply({&t}).ok);
    s = env.eng->status(t);
    EXPECT_EQ(s.state, lf::TweakState::Applied);
    EXPECT_TRUE(s.tracked);

    env.reg.set(t.target, RegValue::dword(9));  // 外部で変更
    s = env.eng->status(t);
    EXPECT_EQ(s.state, lf::TweakState::Drifted);
    EXPECT_TRUE(s.tracked) << "a backup still exists, so it can be reverted";

    ASSERT_TRUE(env.eng->revert({"kernel.a"}).ok);
    EXPECT_EQ(env.eng->status(t).state, lf::TweakState::NotApplied);
}

TEST(EngineStatus, ValueAlreadyAtTargetIsNotOursToRevert) {
    ENV("status_already");
    auto t = makeTweak("kernel.a", "A", RegValue::dword(0));
    env.reg.set(t.target, RegValue::dword(0));
    auto s = env.eng->status(t);
    EXPECT_EQ(s.state, lf::TweakState::AlreadyAtTarget);
    EXPECT_FALSE(s.tracked);
}

TEST(EngineStatus, UnsupportedBuildAndBlockedStates) {
    ENV("status_blocked");
    env.build = 17763;
    env.restart();
    auto old = makeTweak("kernel.old", "Old", RegValue::dword(0));
    old.minBuild = 19041;
    auto s = env.eng->status(old);
    EXPECT_EQ(s.state, lf::TweakState::Unsupported);
    ASSERT_TRUE(s.error.has_value());
    EXPECT_EQ(s.error->code, lf::ErrorCode::UnsupportedBuild);

    lf::TweakDef evil = makeTweak("kernel.evil", "X", RegValue::dword(1));
    evil.target = lf::parseRegPath("HKLM\\SOFTWARE\\Microsoft\\Windows Defender", "DisableAntiSpyware").value();
    s = env.eng->status(evil);
    EXPECT_EQ(s.state, lf::TweakState::Blocked);
    EXPECT_EQ(s.error->code, lf::ErrorCode::PolicyDenied);

    auto bin = makeTweak("kernel.bin", "Bin", RegValue::dword(1));
    env.reg.unsupportedRead.insert(MockRegistry::key(bin.target));
    s = env.eng->status(bin);
    EXPECT_EQ(s.state, lf::TweakState::Blocked);
    EXPECT_EQ(s.error->code, lf::ErrorCode::UnsupportedValueType);
}

TEST(EngineStatus, NeverWrites) {
    ENV("status_readonly");
    auto t = makeTweak("kernel.a", "A", RegValue::dword(0));
    for (int i = 0; i < 3; ++i) (void)env.eng->status(t);
    EXPECT_EQ(env.reg.writes + env.reg.deletes, 0);
    EXPECT_FALSE(fs::exists(env.stateFile()));
}

// ---- 再起動が必要な表示 --------------------------------------------------------------------------------

TEST(EngineReboot, PendingUntilTheNextBootThenClears) {
    ENV("reboot");
    const auto bootBefore = *lf::parseIso8601Utc("2026-01-01T00:00:00Z");
    const auto bootAfter = *lf::parseIso8601Utc("2026-01-01T01:00:00Z");
    EXPECT_FALSE(env.eng->rebootPending(bootBefore)) << "nothing changed yet";

    auto t = makeTweak("kernel.a", "A", RegValue::dword(0), /*reboot=*/true);
    ASSERT_TRUE(env.eng->apply({&t}).ok);
    EXPECT_TRUE(env.eng->rebootPending(bootBefore));
    EXPECT_FALSE(env.eng->rebootPending(bootAfter)) << "the machine restarted after the change";

    env.restart();  // 永続化されている
    EXPECT_TRUE(env.eng->rebootPending(bootBefore));

    env.hour = 2;  // 再起動後 (01:00) にもう一度変更 → また再起動待ち
    ASSERT_TRUE(env.eng->revert({"kernel.a"}).ok);
    EXPECT_TRUE(env.eng->rebootPending(bootAfter)) << "reverting also needs a restart to take effect";
}

TEST(EngineReboot, ChangesThatNeedNoRestartDoNotSetTheMark) {
    ENV("reboot_none");
    auto t = makeTweak("kernel.a", "A", RegValue::dword(0), /*reboot=*/false);
    ASSERT_TRUE(env.eng->apply({&t}).ok);
    EXPECT_FALSE(env.eng->rebootPending(*lf::parseIso8601Utc("2025-12-31T00:00:00Z")));
}

TEST(EngineReboot, DryRunAndRollbackDoNotSetTheMark) {
    ENV("reboot_dry");
    auto a = makeTweak("kernel.a", "A", RegValue::dword(0));
    auto b = makeTweak("kernel.b", "B", RegValue::dword(0));
    const auto longAgo = *lf::parseIso8601Utc("2025-12-31T00:00:00Z");
    (void)env.eng->apply({&a}, {.dryRun = true});
    EXPECT_FALSE(env.eng->rebootPending(longAgo));
    env.reg.hook = [](const char* op, const lf::RegPath& p) -> std::optional<lf::Error> {
        if (std::string(op) == "write" && p.valueName == "B") return lf::Error{lf::ErrorCode::RegistryWrite, "x", 5};
        return std::nullopt;
    };
    EXPECT_FALSE(env.eng->apply({&a, &b}).ok);
    EXPECT_FALSE(env.eng->rebootPending(longAgo)) << "a rolled-back transaction changed nothing";
}