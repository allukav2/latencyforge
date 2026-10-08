#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>

#include "lf/affinity_manager.hpp"
#include "lf/fake_process_api.hpp"
#include "lf/util.hpp"

namespace {

namespace fs = std::filesystem;
using lf::AffinityStatus;
using lf::FakeProcessApi;

constexpr uint64_t kAll = 0xFFFFFFull;   // 論理 0-23 (intel-hybrid サンプル)
constexpr uint64_t kGame = 0x00FFFFull;  // P コア (論理 0-15)
constexpr uint64_t kBg = 0xFF0000ull;    // E コア (論理 16-23)

lf::CpuTopology cpuOf(const char* name) {
    auto probe = lf::makeSampleProbe(name);
    return lf::detectSystem(*probe).cpu;
}

lf::AffinityProfile profile(const std::string& id, const std::string& exe) {
    lf::AffinityProfile p;
    p.id = id;
    p.name = exe;
    p.exeNames = {lf::lowerAscii(exe)};
    return p;
}

struct Env {
    fs::path dir;
    FakeProcessApi api;
    lf::Logger log{1000, [] { return std::string("t"); }};
    std::unique_ptr<lf::AffinityManager> mgr;

    explicit Env(const std::string& name, const char* sample = "intel-hybrid") {
        dir = fs::temp_directory_path() / "lf_tests_affinity" / name;
        fs::remove_all(dir);
        fs::create_directories(dir);
        api.self = 1000;
        mgr = makeManager(sample);
        lf::AffinityConfig cfg;
        cfg.enabled = true;
        cfg.profiles = {profile("p1", "game.exe")};
        mgr->setConfig(cfg);
    }
    ~Env() {
        mgr.reset();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    std::unique_ptr<lf::AffinityManager> makeManager(const char* sample = "intel-hybrid") {
        auto m = std::make_unique<lf::AffinityManager>(api, log, dir / "journal.json");
        m->setTopology(cpuOf(sample));
        return m;
    }
    void configure(const std::function<void(lf::AffinityConfig&)>& edit) {
        lf::AffinityConfig cfg = mgr->config();
        edit(cfg);
        mgr->setConfig(cfg);
    }
    uint64_t mask(uint32_t pid) { return api.find(pid)->st.affinity.mask; }
    size_t journalEntries() {
        auto text = lf::readFileText(dir / "journal.json");
        if (!text.ok()) return 0;
        const size_t n = std::count(text.value().begin(), text.value().end(), '{');
        return n > 0 ? n - 1 : 0;  // ルートの '{' を除く (各エントリは平坦なオブジェクト)
    }
    bool touched(uint32_t pid) {
        const std::string prefix = "setAffinity " + std::to_string(pid) + " ";
        return std::any_of(api.calls.begin(), api.calls.end(), [&](const std::string& c) { return c.rfind(prefix, 0) == 0; });
    }
};

FakeProcessApi::Options opt(uint64_t mask = kAll) {
    FakeProcessApi::Options o;
    o.mask = mask;
    return o;
}

}  // namespace

// ---- 適用 → 復元 ----------------------------------------------------------------------------------

TEST(AffinityManager, AppliesToTheGameAndMovesBackgroundProcessesWhenTheGameStarts) {
    Env e("apply");
    const auto chrome = e.api.add("chrome.exe", opt());
    const auto discord = e.api.add("Discord.exe", opt());
    EXPECT_FALSE(e.mgr->tick()) << "no game yet: nothing changes";
    EXPECT_EQ(e.mgr->status().state, AffinityStatus::State::Idle);

    const auto game = e.api.add("game.exe", opt());
    EXPECT_TRUE(e.mgr->tick());
    EXPECT_EQ(e.mgr->status().state, AffinityStatus::State::GameActive);
    EXPECT_EQ(e.mgr->status().gamePid, game);
    EXPECT_EQ(e.mask(game), kGame);
    EXPECT_EQ(e.mask(chrome), kBg);
    EXPECT_EQ(e.mask(discord), kBg);
    EXPECT_EQ(e.mgr->managed().size(), 3u);
    EXPECT_EQ(e.mgr->status().backgroundCount, 2u);
    EXPECT_EQ(e.journalEntries(), 3u) << "the originals are persisted";
    EXPECT_FALSE(e.mgr->tick()) << "a second tick with no change reports no change";
}

TEST(AffinityManager, RestoresEverythingWhenTheGameExits) {
    Env e("exit");
    const auto chrome = e.api.add("chrome.exe", opt());
    const auto game = e.api.add("game.exe", opt());
    ASSERT_TRUE(e.mgr->tick());
    ASSERT_EQ(e.mask(chrome), kBg);

    e.api.kill(game);
    EXPECT_TRUE(e.mgr->tick());
    EXPECT_EQ(e.mask(chrome), kAll) << "background restored to its original affinity";
    EXPECT_TRUE(e.mgr->managed().empty());
    EXPECT_EQ(e.mgr->status().state, AffinityStatus::State::Idle);
    EXPECT_EQ(e.journalEntries(), 0u);
}

TEST(AffinityManager, RestoreAllOnAppExitRestoresTheGameAndBackgroundWhileTheGameIsStillRunning) {
    Env e("appexit");
    const auto chrome = e.api.add("chrome.exe", opt());
    const auto game = e.api.add("game.exe", opt(0xFFFFF0ull));  // 元のアフィニティは特殊
    ASSERT_TRUE(e.mgr->tick());
    ASSERT_NE(e.mask(game), 0xFFFFF0ull);

    e.mgr->restoreAll();
    EXPECT_EQ(e.mask(game), 0xFFFFF0ull) << "the game gets back exactly its original mask";
    EXPECT_EQ(e.mask(chrome), kAll);
    EXPECT_TRUE(e.mgr->managed().empty());
    EXPECT_EQ(e.journalEntries(), 0u);
}

TEST(AffinityManager, PriorityIsChangedOnlyWhenRequestedAndRestoredAfterwards) {
    Env e("priority");
    e.configure([](lf::AffinityConfig& c) { c.profiles[0].priority = lf::GamePriority::High; });
    const auto game = e.api.add("game.exe", opt());
    ASSERT_TRUE(e.mgr->tick());
    EXPECT_EQ(e.api.find(game)->st.priorityClass, static_cast<uint32_t>(lf::PriorityClass::High));
    e.mgr->restoreAll();
    EXPECT_EQ(e.api.find(game)->st.priorityClass, static_cast<uint32_t>(lf::PriorityClass::Normal));
}

TEST(AffinityManager, PriorityIsLeftAloneWhenUnchangedOrAlreadyAtTheTarget) {
    {
        Env e("priority_none");
        const auto game = e.api.add("game.exe", opt());
        ASSERT_TRUE(e.mgr->tick());
        EXPECT_TRUE(std::none_of(e.api.calls.begin(), e.api.calls.end(), [](const std::string& c) { return c.rfind("setPriority", 0) == 0; }));
        (void)game;
    }
    {
        Env e("priority_already");
        e.configure([](lf::AffinityConfig& c) { c.profiles[0].priority = lf::GamePriority::High; });
        FakeProcessApi::Options o = opt();
        o.priority = static_cast<uint32_t>(lf::PriorityClass::High);
        e.api.add("game.exe", o);
        ASSERT_TRUE(e.mgr->tick());
        EXPECT_TRUE(std::none_of(e.api.calls.begin(), e.api.calls.end(), [](const std::string& c) { return c.rfind("setPriority", 0) == 0; }));
    }
}

TEST(AffinityManager, ProfileMatchingIsCaseInsensitive) {
    Env e("case");
    const auto game = e.api.add("GAME.EXE", opt());
    ASSERT_TRUE(e.mgr->tick());
    EXPECT_EQ(e.mask(game), kGame);
}

TEST(AffinityManager, BackgroundIsLeftAloneWhenTheProfileSaysSo) {
    Env e("nobg");
    e.configure([](lf::AffinityConfig& c) { c.profiles[0].moveBackground = false; });
    const auto chrome = e.api.add("chrome.exe", opt());
    const auto game = e.api.add("game.exe", opt());
    ASSERT_TRUE(e.mgr->tick());
    EXPECT_EQ(e.mask(game), kGame);
    EXPECT_EQ(e.mask(chrome), kAll);
    EXPECT_EQ(e.mgr->managed().size(), 1u);
}

// ---- 触ってはいけないプロセス ---------------------------------------------------------------------

TEST(AffinityManager, NeverTouchesProtectedSystemOtherUserOrAntiCheatProcesses) {
    Env e("untouchable");
    FakeProcessApi::Options o = opt();
    const auto normal = e.api.add("chrome.exe", o);

    FakeProcessApi::Options prot = o;
    prot.isProtected = true;
    const auto pProtected = e.api.add("protected_thing.exe", prot);
    FakeProcessApi::Options crit = o;
    crit.isCritical = true;
    const auto pCritical = e.api.add("critical_thing.exe", crit);
    FakeProcessApi::Options other = o;
    other.sameUser = false;
    const auto pOtherUser = e.api.add("service_thing.exe", other);
    FakeProcessApi::Options win = o;
    win.inWindowsDir = true;
    const auto pWindows = e.api.add("tool_in_windows.exe", win);
    FakeProcessApi::Options sess = o;
    sess.sessionId = 0;
    const auto pSession0 = e.api.add("session0_thing.exe", sess);
    FakeProcessApi::Options multi = o;
    multi.multiGroup = true;
    const auto pMulti = e.api.add("multigroup.exe", multi);
    FakeProcessApi::Options deny = o;
    deny.denyOpen = true;
    const auto pDenied = e.api.add("guarded.exe", deny);
    const auto pExplorer = e.api.add("explorer.exe", o);  // 名前による除外
    const auto pAudio = e.api.add("audiodg.exe", o);
    const auto pVgc = e.api.add("vgc.exe", o);
    const auto pEac = e.api.add("EasyAntiCheat.exe", o);
    const auto pSelf = e.api.add("selflike.exe", [&] { auto x = o; x.pid = e.api.self; return x; }());

    const auto game = e.api.add("game.exe", o);
    ASSERT_TRUE(e.mgr->tick());
    EXPECT_EQ(e.mask(game), kGame);
    EXPECT_EQ(e.mask(normal), kBg);

    for (auto pid : {pProtected, pCritical, pOtherUser, pWindows, pSession0, pMulti, pDenied, pExplorer, pAudio, pVgc, pEac, pSelf}) {
        EXPECT_FALSE(e.touched(pid)) << "pid " << pid << " must never be modified";
        EXPECT_EQ(e.mask(pid), kAll) << "pid " << pid;
    }
}

TEST(AffinityManager, AGameThatCannotBeOpenedIsSkippedAndNothingElseIsTouched) {
    Env e("blocked_open");
    const auto chrome = e.api.add("chrome.exe", opt());
    FakeProcessApi::Options guarded = opt();
    guarded.denyOpen = true;  // アンチチートが OpenProcess を拒否する想定
    const auto game = e.api.add("game.exe", guarded);

    EXPECT_TRUE(e.mgr->tick());
    EXPECT_EQ(e.mgr->status().state, AffinityStatus::State::GameBlocked);
    EXPECT_EQ(e.mgr->status().noteKey, "affinity.note.blockedOpen");
    EXPECT_TRUE(e.api.calls.empty()) << "no write attempts at all";
    EXPECT_EQ(e.mask(chrome), kAll) << "do not shuffle background processes for a game we cannot optimize";
    EXPECT_EQ(e.mask(game), kAll);
    EXPECT_FALSE(e.mgr->tick()) << "the blocked game is not re-evaluated every tick";
    const int queries = e.api.queryCount;
    e.mgr->tick();
    EXPECT_EQ(e.api.queryCount, queries);

    // ログに残る
    const std::string log = e.log.exportText();
    EXPECT_NE(log.find("cannot open game.exe"), std::string::npos);

    // ゲームが終わったら待機に戻る
    e.api.kill(game);
    e.mgr->tick();
    EXPECT_EQ(e.mgr->status().state, AffinityStatus::State::Idle);
}

TEST(AffinityManager, AProtectedGameIsSkipped) {
    Env e("blocked_protected");
    FakeProcessApi::Options prot = opt();
    prot.isProtected = true;
    e.api.add("game.exe", prot);
    e.mgr->tick();
    EXPECT_EQ(e.mgr->status().state, AffinityStatus::State::GameBlocked);
    EXPECT_EQ(e.mgr->status().noteKey, "affinity.note.blockedProtected");
    EXPECT_TRUE(e.api.calls.empty());
}

TEST(AffinityManager, ARejectedWriteIsSkippedAndLeavesNoBackup) {
    Env e("write_fail");
    e.api.hook = [&](const char* op, uint32_t) -> std::optional<lf::Error> {
        if (std::string(op) == "setAffinity") return lf::Error{lf::ErrorCode::AccessDenied, "injected", 5};
        return std::nullopt;
    };
    const auto game = e.api.add("game.exe", opt());
    e.mgr->tick();
    EXPECT_EQ(e.mgr->status().state, AffinityStatus::State::GameBlocked);
    EXPECT_TRUE(e.mgr->managed().empty()) << "nothing was changed, so nothing needs restoring";
    EXPECT_EQ(e.mask(game), kAll);
}

TEST(AffinityManager, NothingIsChangedIfTheRestoreJournalCannotBeWritten) {
    Env e("journal_fail");
    { std::ofstream(e.dir / "blocker") << "x"; }
    e.mgr = std::make_unique<lf::AffinityManager>(e.api, e.log, e.dir / "blocker" / "journal.json");  // 親が通常ファイル
    e.mgr->setTopology(cpuOf("intel-hybrid"));
    lf::AffinityConfig cfg;
    cfg.enabled = true;
    cfg.profiles = {profile("p1", "game.exe")};
    e.mgr->setConfig(cfg);

    const auto game = e.api.add("game.exe", opt());
    e.mgr->tick();
    EXPECT_EQ(e.mgr->status().state, AffinityStatus::State::GameBlocked);
    EXPECT_EQ(e.mgr->status().noteKey, "affinity.note.blockedJournal");
    EXPECT_TRUE(e.api.calls.empty()) << "no backup, no change";
    EXPECT_EQ(e.mask(game), kAll);
}

// ---- 既存のアフィニティを広げない -----------------------------------------------------------------

TEST(AffinityManager, NeverWidensAnExistingAffinity) {
    Env e("intersect");
    const auto game = e.api.add("game.exe", opt(0x0000FFull));        // 論理 0-7 に制限済み
    const auto inner = e.api.add("inner.exe", opt(0x000F00ull));       // バックグラウンド用コアと重ならない
    const auto mixed = e.api.add("mixed.exe", opt(0xF0FF00ull));       // 一部だけ重なる
    const auto already = e.api.add("already.exe", opt(0x300000ull));   // 既にバックグラウンド用コアの範囲内
    ASSERT_TRUE(e.mgr->tick());
    EXPECT_EQ(e.mask(game), 0x0000FFull) << "game: plan AND original";
    EXPECT_EQ(e.mask(inner), 0x000F00ull) << "no overlap with the background cores: left alone";
    EXPECT_EQ(e.mask(mixed), 0xF00000ull);
    EXPECT_EQ(e.mask(already), 0x300000ull);
    EXPECT_FALSE(e.touched(inner));
    EXPECT_FALSE(e.touched(already));
}

TEST(AffinityManager, ProcessesInAnotherProcessorGroupAreSkipped) {
    Env e("group");
    FakeProcessApi::Options g1 = opt();
    g1.group = 1;
    const auto other = e.api.add("other_group.exe", g1);
    e.api.add("game.exe", opt());
    ASSERT_TRUE(e.mgr->tick());
    EXPECT_FALSE(e.touched(other));
}

// ---- 単純な構成 / 無効 ----------------------------------------------------------------------------

TEST(AffinityManager, SimpleCpuLayoutsChangeNothing) {
    Env e("simple", "simple");
    const auto chrome = e.api.add("chrome.exe", opt(0xFFF));
    const auto game = e.api.add("game.exe", opt(0xFFF));
    e.mgr->tick();
    EXPECT_EQ(e.mgr->status().state, AffinityStatus::State::GameNoEffect);
    EXPECT_EQ(e.mgr->status().noteKey, "affinity.note.noEffectSimple");
    EXPECT_TRUE(e.api.calls.empty());
    EXPECT_EQ(e.mask(chrome), 0xFFFu);
    EXPECT_EQ(e.mask(game), 0xFFFu);
    EXPECT_TRUE(e.mgr->managed().empty());

    e.api.kill(game);
    e.mgr->tick();
    EXPECT_EQ(e.mgr->status().state, AffinityStatus::State::Idle);
}

TEST(AffinityManager, DisablingRestoresImmediatelyAndStopsPolling) {
    Env e("disable");
    const auto chrome = e.api.add("chrome.exe", opt());
    e.api.add("game.exe", opt());
    ASSERT_TRUE(e.mgr->tick());
    ASSERT_EQ(e.mask(chrome), kBg);
    EXPECT_TRUE(e.mgr->needsPolling());

    e.configure([](lf::AffinityConfig& c) { c.enabled = false; });
    EXPECT_EQ(e.mask(chrome), kAll);
    EXPECT_TRUE(e.mgr->managed().empty());
    EXPECT_EQ(e.mgr->status().state, AffinityStatus::State::Disabled);
    EXPECT_FALSE(e.mgr->needsPolling()) << "disabled: no polling at all";
    const int queries = e.api.queryCount;
    EXPECT_FALSE(e.mgr->tick());
    EXPECT_EQ(e.api.queryCount, queries) << "a disabled manager does not even look at processes";
}

TEST(AffinityManager, RemovingTheActiveProfileRestoresImmediately) {
    Env e("remove");
    const auto chrome = e.api.add("chrome.exe", opt());
    e.api.add("game.exe", opt());
    ASSERT_TRUE(e.mgr->tick());
    e.configure([](lf::AffinityConfig& c) { c.profiles.clear(); });
    EXPECT_EQ(e.mask(chrome), kAll);
    EXPECT_TRUE(e.mgr->managed().empty());
}

TEST(AffinityManager, PollingIsOnlyNeededWhenSomethingIsEnabled) {
    Env e("polling");
    EXPECT_TRUE(e.mgr->needsPolling());
    e.configure([](lf::AffinityConfig& c) { c.profiles[0].enabled = false; });
    EXPECT_FALSE(e.mgr->needsPolling()) << "enabled globally but no enabled profile";
    e.configure([](lf::AffinityConfig& c) { c.profiles.clear(); });
    EXPECT_FALSE(e.mgr->needsPolling());
}

// ---- 複数ゲーム / 後から起動 / PID 再利用 ----------------------------------------------------------

TEST(AffinityManager, FirstMatchingProfileWinsAndASecondGameIsIgnoredWhileOneIsActive) {
    Env e("two_games");
    e.configure([](lf::AffinityConfig& c) { c.profiles.push_back(profile("p2", "other_game.exe")); });
    const auto g2 = e.api.add("other_game.exe", opt());
    const auto g1 = e.api.add("game.exe", opt());
    ASSERT_TRUE(e.mgr->tick());
    EXPECT_EQ(e.mgr->status().profileId, "p1") << "profile order decides";
    EXPECT_EQ(e.mask(g1), kGame);
    EXPECT_EQ(e.mask(g2), kBg) << "the second game is just another background process";
}

TEST(AffinityManager, ProcessesStartedDuringTheGameAreMovedAndEvaluatedOnlyOnce) {
    Env e("late");
    e.api.add("game.exe", opt());
    FakeProcessApi::Options guarded = opt();
    guarded.denyOpen = true;
    const auto denied = e.api.add("guarded.exe", guarded);
    ASSERT_TRUE(e.mgr->tick());

    const auto obs = e.api.add("obs64.exe", opt());
    EXPECT_TRUE(e.mgr->tick());
    EXPECT_EQ(e.mask(obs), kBg);
    EXPECT_EQ(e.mgr->status().backgroundCount, 1u);

    const int queries = e.api.queryCount;
    e.mgr->tick();
    e.mgr->tick();
    EXPECT_EQ(e.api.queryCount, queries) << "managed and rejected processes are not queried again";
    EXPECT_FALSE(e.touched(denied));
}

TEST(AffinityManager, ARecycledPidIsNeverRestoredOntoANewProcess) {
    Env e("pid_reuse");
    const auto chrome = e.api.add("chrome.exe", opt());
    const auto game = e.api.add("game.exe", opt());
    ASSERT_TRUE(e.mgr->tick());
    ASSERT_EQ(e.mask(chrome), kBg);

    // chrome が終了し、同じ PID を別のプロセス (同名でも作成時刻が違う) が再利用した
    e.api.reusePid(chrome, "chrome.exe", opt(0x00F0F0ull));
    e.api.kill(game);
    e.mgr->tick();
    EXPECT_EQ(e.mask(chrome), 0x00F0F0ull) << "the new process keeps whatever affinity it has";
    EXPECT_TRUE(e.mgr->managed().empty());
}

TEST(AffinityManager, ARecycledPidWithADifferentNameIsDroppedWithoutAnyWrite) {
    Env e("pid_reuse2");
    const auto chrome = e.api.add("chrome.exe", opt());
    const auto game = e.api.add("game.exe", opt());
    ASSERT_TRUE(e.mgr->tick());
    e.api.reusePid(chrome, "notepad.exe", opt());
    e.api.calls.clear();
    e.api.kill(game);
    e.mgr->tick();
    EXPECT_FALSE(e.touched(chrome));
}

// ---- 復元の再試行 / 異常終了からの回復 --------------------------------------------------------------

TEST(AffinityManager, FailedRestoresAreRetriedOnTheNextTicks) {
    Env e("retry");
    const auto chrome = e.api.add("chrome.exe", opt());
    const auto game = e.api.add("game.exe", opt());
    ASSERT_TRUE(e.mgr->tick());
    int failures = 2;  // 最初の 2 回の復元だけ失敗させる
    e.api.hook = [&](const char* op, uint32_t pid) -> std::optional<lf::Error> {
        if (std::string(op) == "setAffinity" && pid == chrome && failures > 0) {
            --failures;
            return lf::Error{lf::ErrorCode::AccessDenied, "injected", 5};
        }
        return std::nullopt;
    };
    e.api.kill(game);
    e.mgr->tick();  // 失敗 1 回目
    EXPECT_EQ(e.mgr->managed().size(), 1u) << "the backup is kept";
    EXPECT_EQ(e.journalEntries(), 1u);
    EXPECT_EQ(e.mask(chrome), kBg);
    e.mgr->tick();  // 失敗 2 回目
    EXPECT_EQ(e.mgr->managed().size(), 1u);
    e.mgr->tick();  // 成功
    EXPECT_TRUE(e.mgr->managed().empty());
    EXPECT_EQ(e.mask(chrome), kAll);
    EXPECT_EQ(e.journalEntries(), 0u);
}

TEST(AffinityManager, GivesUpAfterThreeFailedRestoreAttempts) {
    Env e("giveup");
    const auto chrome = e.api.add("chrome.exe", opt());
    const auto game = e.api.add("game.exe", opt());
    ASSERT_TRUE(e.mgr->tick());
    e.api.hook = [&](const char* op, uint32_t pid) -> std::optional<lf::Error> {
        if (std::string(op) == "setAffinity" && pid == chrome) return lf::Error{lf::ErrorCode::AccessDenied, "always", 5};
        return std::nullopt;
    };
    e.api.kill(game);
    for (int i = 0; i < 3; ++i) e.mgr->tick();
    EXPECT_TRUE(e.mgr->managed().empty());
    EXPECT_NE(e.log.exportText().find("giving up restoring chrome.exe"), std::string::npos);
}

TEST(AffinityManager, ARestartRestoresProcessesLeftModifiedByACrashedSession) {
    Env e("crash");
    const auto chrome = e.api.add("chrome.exe", opt());
    const auto game = e.api.add("game.exe", opt(0xFFFFF0ull));
    ASSERT_TRUE(e.mgr->tick());
    ASSERT_EQ(e.mask(chrome), kBg);
    ASSERT_EQ(e.journalEntries(), 2u);

    // アプリが異常終了 (restoreAll が呼ばれない)。ゲームも chrome も変更されたまま。新しいマネージャで再起動する。
    e.mgr.reset();
    e.mgr = e.makeManager();
    e.mgr->recoverFromJournal();
    EXPECT_EQ(e.mask(chrome), kAll);
    EXPECT_EQ(e.mask(game), 0xFFFFF0ull);
    EXPECT_TRUE(e.mgr->managed().empty());
    EXPECT_EQ(e.journalEntries(), 0u);
}

TEST(AffinityManager, RecoveryDoesNotTouchProcessesThatNoLongerExistOrWereRecycled) {
    Env e("crash2");
    const auto chrome = e.api.add("chrome.exe", opt());
    const auto game = e.api.add("game.exe", opt());
    ASSERT_TRUE(e.mgr->tick());
    e.mgr.reset();
    e.api.reusePid(chrome, "chrome.exe", opt(0x00AAAAull));  // 再起動の間に PID が再利用された
    e.api.kill(game);
    e.mgr = e.makeManager();
    e.mgr->recoverFromJournal();
    EXPECT_EQ(e.mask(chrome), 0x00AAAAull);
    EXPECT_TRUE(e.mgr->managed().empty());
}

TEST(AffinityManager, ACorruptJournalIsIgnoredWithoutCrashing) {
    Env e("corrupt_journal");
    { std::ofstream(e.dir / "journal.json") << "{{{ not json"; }
    e.mgr = e.makeManager();
    EXPECT_NO_THROW(e.mgr->recoverFromJournal());
    EXPECT_TRUE(e.mgr->managed().empty());
}

// ---- 候補一覧 -------------------------------------------------------------------------------------

TEST(AffinityManager, CandidateListContainsOnlySafeUserProcessesOncePerName) {
    FakeProcessApi api;
    api.self = 1000;
    api.add("zeta.exe");
    api.add("Alpha.exe");
    api.add("alpha.exe");  // 同名は 1 件にまとめる
    api.add("explorer.exe");
    FakeProcessApi::Options prot;
    prot.isProtected = true;
    api.add("protected.exe", prot);
    FakeProcessApi::Options other;
    other.sameUser = false;
    api.add("svc.exe", other);
    FakeProcessApi::Options deny;
    deny.denyOpen = true;
    api.add("guarded.exe", deny);
    FakeProcessApi::Options notExe;
    api.add("README", notExe);

    const auto list = lf::listTargetCandidates(api);
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(lf::lowerAscii(list[0].exeName), "alpha.exe");
    EXPECT_EQ(list[1].exeName, "zeta.exe");
    EXPECT_TRUE(api.calls.empty()) << "listing is read-only";
}
