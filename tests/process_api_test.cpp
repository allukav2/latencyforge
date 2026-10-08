// 実機のプロセス API の検証。触るのは「このテストプロセス自身」と「このテストが起動した子プロセス」だけ。
// 他のプロセスには一切触れない。権限は PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION だけで足りることも確認する。
#include <gtest/gtest.h>
#include <windows.h>

#include <algorithm>
#include <bit>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "lf/affinity_manager.hpp"
#include "lf/win_process_api.hpp"

namespace {

namespace fs = std::filesystem;

// 元のアフィニティ/優先度を必ず戻すガード
struct RestoreGuard {
    lf::WinProcessApi& api;
    lf::ProcessState original;
    ~RestoreGuard() {
        (void)api.setAffinity(original.pid, original.startTime, original.affinity);
        (void)api.setPriority(original.pid, original.startTime, original.priorityClass);
    }
};

uint64_t lowestBit(uint64_t m) { return m & (~m + 1); }

// 一時フォルダ。破棄時に必ず削除する (使用中で消せない場合は、少し待って再試行する)。
struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("lf_tests_ping_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(GetTickCount64()));
        std::error_code ec;
        fs::create_directories(path, ec);
    }
    ~TempDir() {
        for (int i = 0; i < 20; ++i) {
            std::error_code ec;
            fs::remove_all(path, ec);
            if (!ec && !fs::exists(path, ec)) return;
            Sleep(100);
        }
    }
};

// 数秒だけ生きる子プロセス (コンソールなし)
struct Child {
    PROCESS_INFORMATION pi{};
    bool started = false;
    explicit Child(std::wstring cmd = L"cmd.exe /c ping -n 20 127.0.0.1 >nul") {
        STARTUPINFOW si{sizeof si};
        started = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi) != FALSE;
    }
    ~Child() {
        if (!started) return;
        TerminateProcess(pi.hProcess, 0);  // このテストが起動した子プロセスだけを終了する
        WaitForSingleObject(pi.hProcess, 2000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    uint32_t pid() const { return pi.dwProcessId; }
};

}  // namespace

TEST(WinProcessApi, UsesExactlyTheMinimalAccessRights) {
    constexpr unsigned long allowed = PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION;
    EXPECT_EQ(lf::kProcessAccessRights, allowed);
    // 余計な権限 (メモリ読み書き、スレッド作成、終了、ハンドル複製、完全な問い合わせ、一時停止) を 1 つも含まない
    EXPECT_EQ(lf::kProcessAccessRights & ~allowed, 0ul);
    for (unsigned long extra : {PROCESS_VM_READ, PROCESS_VM_WRITE, PROCESS_VM_OPERATION, PROCESS_CREATE_THREAD, PROCESS_TERMINATE,
                                PROCESS_DUP_HANDLE, PROCESS_QUERY_INFORMATION, PROCESS_SUSPEND_RESUME})
        EXPECT_EQ(lf::kProcessAccessRights & extra, 0ul);
}

TEST(WinProcessApi, EnumeratesAndDescribesTheCurrentProcess) {
    lf::WinProcessApi api;
    const auto procs = api.enumerate();
    EXPECT_TRUE(std::any_of(procs.begin(), procs.end(), [&](const lf::ProcessEntry& e) { return e.pid == api.selfPid(); }));

    auto q = api.query(api.selfPid());
    ASSERT_TRUE(q.ok()) << q.error().detail;
    const auto& s = q.value();
    EXPECT_GT(s.startTime, 0u);
    EXPECT_FALSE(s.exeName.empty());
    EXPECT_NE(s.affinity.mask, 0u);
    EXPECT_TRUE(s.sameUser);
    EXPECT_FALSE(s.isProtected);
    EXPECT_FALSE(s.multiGroup);
    EXPECT_EQ(s.sessionId, api.currentSessionId());
    EXPECT_EQ(lf::checkTarget(s, api.selfPid(), api.currentSessionId()), lf::SkipReason::Self) << "the app never touches itself";
}

TEST(WinProcessApi, ChangesAndRestoresTheAffinityAndPriorityOfTheTestProcessItself) {
    lf::WinProcessApi api;
    auto q = api.query(api.selfPid());
    ASSERT_TRUE(q.ok());
    const auto original = q.value();
    if (std::popcount(original.affinity.mask) < 2) GTEST_SKIP() << "needs at least two logical processors";
    RestoreGuard guard{api, original};

    const uint64_t one = lowestBit(original.affinity.mask);
    ASSERT_TRUE(api.setAffinity(original.pid, original.startTime, {original.affinity.group, one}).ok());
    EXPECT_EQ(api.query(original.pid).value().affinity.mask, one);
    ASSERT_TRUE(api.setPriority(original.pid, original.startTime, static_cast<uint32_t>(lf::PriorityClass::BelowNormal)).ok());
    EXPECT_EQ(api.query(original.pid).value().priorityClass, static_cast<uint32_t>(lf::PriorityClass::BelowNormal));

    ASSERT_TRUE(api.setAffinity(original.pid, original.startTime, original.affinity).ok());
    ASSERT_TRUE(api.setPriority(original.pid, original.startTime, original.priorityClass).ok());
    EXPECT_EQ(api.query(original.pid).value().affinity.mask, original.affinity.mask);
    EXPECT_EQ(api.query(original.pid).value().priorityClass, original.priorityClass);
}

TEST(WinProcessApi, MinimalRightsAreEnoughToChangeAChildProcess) {
    Child child;
    ASSERT_TRUE(child.started);
    lf::WinProcessApi api;
    auto q = api.query(child.pid());
    ASSERT_TRUE(q.ok()) << q.error().detail;
    const auto original = q.value();
    EXPECT_TRUE(original.sameUser);
    // この子プロセスは cmd.exe で、C:\Windows\System32 配下にある。Windows フォルダ配下の実行ファイルは、
    // 安全判定 (checkTarget) が名前に関係なく拒否する設計なので、ここでの期待値は WindowsDir になる。
    // (ここで確認したいのは API 層の挙動 = 最小権限だけで別プロセスのアフィニティを変更・復元できること。)
    // 「安全判定が許可する子プロセス」の経路は、次のテスト (一時フォルダにコピーした ping.exe) で確認する。
    EXPECT_EQ(lf::checkTarget(original, api.selfPid(), api.currentSessionId()), lf::SkipReason::WindowsDir);
    if (std::popcount(original.affinity.mask) < 2) GTEST_SKIP() << "needs at least two logical processors";

    const uint64_t one = lowestBit(original.affinity.mask);
    ASSERT_TRUE(api.setAffinity(child.pid(), original.startTime, {original.affinity.group, one}).ok());
    EXPECT_EQ(api.query(child.pid()).value().affinity.mask, one);
    ASSERT_TRUE(api.setAffinity(child.pid(), original.startTime, original.affinity).ok());
    EXPECT_EQ(api.query(child.pid()).value().affinity.mask, original.affinity.mask);
}

// 一時フォルダにコピーした ping.exe (Windows フォルダの外) を起動し、安全判定が許可する対象として扱えることを確認する。
// 子プロセスと一時ファイルは、テストが成功・失敗のどちらで終わっても必ず片付ける。
TEST(WinProcessApi, ACopyOfPingOutsideTheWindowsFolderIsAValidTarget) {
    TempDir tmp;  // 先に宣言する = 子プロセスの終了後に破棄される (ファイルが使用中でなくなってから削除)
    wchar_t sysDir[MAX_PATH]{};
    ASSERT_GT(GetSystemDirectoryW(sysDir, MAX_PATH), 0u);
    const fs::path src = fs::path(sysDir) / L"PING.EXE";
    const fs::path exe = tmp.path / L"lf_ping_copy.exe";
    std::error_code ec;
    fs::copy_file(src, exe, fs::copy_options::overwrite_existing, ec);
    ASSERT_FALSE(ec) << "could not copy ping.exe: " << ec.message();

    Child child(L"\"" + exe.wstring() + L"\" -n 20 127.0.0.1");
    ASSERT_TRUE(child.started);
    lf::WinProcessApi api;
    auto q = api.query(child.pid());
    ASSERT_TRUE(q.ok()) << q.error().detail;
    const auto original = q.value();
    EXPECT_TRUE(original.sameUser);
    EXPECT_FALSE(original.inWindowsDir) << original.imagePath;
    EXPECT_EQ(lf::checkTarget(original, api.selfPid(), api.currentSessionId()), lf::SkipReason::None)
        << "a process outside the Windows folder, owned by us, in our session, is a valid target";
    if (std::popcount(original.affinity.mask) < 2) GTEST_SKIP() << "needs at least two logical processors";

    const uint64_t one = lowestBit(original.affinity.mask);
    ASSERT_TRUE(api.setAffinity(child.pid(), original.startTime, {original.affinity.group, one}).ok());
    EXPECT_EQ(api.query(child.pid()).value().affinity.mask, one);
    ASSERT_TRUE(api.setAffinity(child.pid(), original.startTime, original.affinity).ok());
    EXPECT_EQ(api.query(child.pid()).value().affinity.mask, original.affinity.mask);

    // 候補一覧にも載る (実機の一覧は他のプロセスも含むので、この名前だけを確認する)
    const auto list = lf::listTargetCandidates(api);
    EXPECT_TRUE(std::any_of(list.begin(), list.end(), [&](const lf::ProcessState& c) { return c.pid == child.pid(); }));
}

TEST(WinProcessApi, ARecycledPidIsRefusedBecauseTheStartTimeDiffers) {
    Child child;
    ASSERT_TRUE(child.started);
    lf::WinProcessApi api;
    auto q = api.query(child.pid());
    ASSERT_TRUE(q.ok());
    auto r = api.setAffinity(child.pid(), q.value().startTime + 1, q.value().affinity);  // 作成時刻が違う = 別のプロセスとみなす
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, lf::ErrorCode::ProcessChanged);
    auto p = api.setPriority(child.pid(), q.value().startTime + 1, q.value().priorityClass);
    ASSERT_FALSE(p.ok());
    EXPECT_EQ(p.error().code, lf::ErrorCode::ProcessChanged);
}

TEST(WinProcessApi, ANonexistentOrUnopenableProcessReturnsAnErrorInsteadOfCrashing) {
    lf::WinProcessApi api;
    EXPECT_FALSE(api.query(0xFFFFFFF0u).ok());
    // System (pid 4): 標準ユーザーでは開けない。管理者で開けた場合でも、安全判定で必ず対象外になる。
    if (auto sys = api.query(4); sys.ok()) {
        EXPECT_NE(lf::checkTarget(sys.value(), api.selfPid(), api.currentSessionId()), lf::SkipReason::None);
    }
    EXPECT_FALSE(api.setAffinity(0xFFFFFFF0u, 1, {0, 1}).ok());
}

TEST(WinProcessApi, CandidateListNeverContainsUnsafeProcesses) {
    lf::WinProcessApi api;
    const auto list = lf::listTargetCandidates(api);
    for (const auto& c : list) {
        EXPECT_NE(c.pid, api.selfPid());
        EXPECT_EQ(lf::checkTarget(c, api.selfPid(), api.currentSessionId()), lf::SkipReason::None) << c.exeName;
        EXPECT_FALSE(lf::isNeverTouchName(c.exeName)) << c.exeName;
    }
}

// ---- ソースの規約検査 (回帰防止) ---------------------------------------------------------------------
// 禁止されている API / 権限が、製品コード (core/ と app/) に現れないこと。コメントは除いて検査する。

namespace {

std::string stripComments(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s.compare(i, 2, "//") == 0) {
            while (i < s.size() && s[i] != '\n') ++i;
            out += '\n';
        } else if (s.compare(i, 2, "/*") == 0) {
            const size_t end = s.find("*/", i + 2);
            i = end == std::string::npos ? s.size() : end + 1;
        } else {
            out += s[i];
        }
    }
    return out;
}

size_t countOf(const std::string& hay, const std::string& needle) {
    size_t n = 0;
    for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + needle.size())) ++n;
    return n;
}

std::vector<std::pair<std::string, std::string>> productSources() {
    std::vector<std::pair<std::string, std::string>> out;
    for (const char* dir : {"core", "app"}) {
        for (const auto& e : fs::recursive_directory_iterator(fs::path(LF_SOURCE_DIR) / dir)) {
            const auto ext = e.path().extension().string();
            if (!e.is_regular_file() || (ext != ".cpp" && ext != ".hpp" && ext != ".h" && ext != ".c")) continue;
            std::ifstream f(e.path(), std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            out.emplace_back(e.path().filename().string(), stripComments(ss.str()));
        }
    }
    return out;
}

}  // namespace

TEST(SourceRules, ForbiddenProcessAndInjectionApisNeverAppearInProductCode) {
    const auto sources = productSources();
    ASSERT_GT(sources.size(), 20u) << "the scan must actually see the source tree";
    const char* forbidden[] = {"PROCESS_ALL_ACCESS", "SeDebugPrivilege", "SE_DEBUG_NAME", "AdjustTokenPrivileges", "LookupPrivilegeValue",
                               "WriteProcessMemory", "ReadProcessMemory", "CreateRemoteThread", "VirtualAllocEx", "VirtualProtectEx",
                               "SetWindowsHookEx", "NtSetInformationProcess", "QueueUserAPC", "DebugActiveProcess",
                               "PROCESS_VM_WRITE", "PROCESS_VM_READ", "PROCESS_VM_OPERATION", "PROCESS_CREATE_THREAD",
                               "PROCESS_TERMINATE", "TerminateProcess", "SuspendThread", "SetThreadContext", "MiniDumpWriteDump"};
    for (const auto& [name, text] : sources)
        for (const char* api : forbidden) EXPECT_EQ(text.find(api), std::string::npos) << name << " uses forbidden API " << api;
}

TEST(SourceRules, ProcessesAreOpenedInExactlyOnePlaceWithTheMinimalRights) {
    const auto sources = productSources();
    size_t total = 0;
    for (const auto& [name, text] : sources) {
        const size_t n = countOf(text, "OpenProcess(");
        total += n;
        if (n > 0) {
            EXPECT_EQ(name, "win_process_api.cpp") << "OpenProcess may only be called from WinProcessApi";
            EXPECT_NE(text.find("OpenProcess(kProcessAccessRights"), std::string::npos);
        }
    }
    EXPECT_EQ(total, 1u);
}
