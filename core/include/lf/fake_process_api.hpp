#pragma once
#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "lf/process_api.hpp"

namespace lf {

// メモリ上の模擬プロセス一覧。単体テストと、アプリの --demo (実機のプロセスには一切触れない) で使う。
class FakeProcessApi final : public IProcessApi {
public:
    struct Options {
        uint16_t group = 0;
        uint64_t mask = 0xFFFFFFFFull;
        uint32_t priority = static_cast<uint32_t>(PriorityClass::Normal);
        bool sameUser = true;
        bool isProtected = false;
        bool isCritical = false;
        bool inWindowsDir = false;
        bool multiGroup = false;
        bool denyOpen = false;  // OpenProcess が失敗する (保護/アンチチートのつもり)
        uint32_t sessionId = 1;
        uint32_t pid = 0;       // 0 = 自動採番
    };
    struct Proc {
        ProcessState st;
        bool denyOpen = false;
    };

    uint32_t self = 1000;
    uint32_t session = 1;
    std::vector<std::string> calls;  // "setAffinity <pid> <mask>" など (安全性のテスト用)
    int queryCount = 0;
    // 操作ごとのフック。Error を返すとその操作が失敗する。op: "query" / "setAffinity" / "setPriority"。
    std::function<std::optional<Error>(const char* op, uint32_t pid)> hook;

    uint32_t add(const std::string& exe, const Options& o = Options()) {
        Proc p;
        p.st.pid = o.pid ? o.pid : m_nextPid++;
        p.st.startTime = m_nextStart++;
        p.st.exeName = exe;
        p.st.imagePath = (o.inWindowsDir ? "C:\\Windows\\System32\\" : "C:\\Program Files\\App\\") + exe;
        p.st.affinity = {o.group, o.mask};
        p.st.priorityClass = o.priority;
        p.st.multiGroup = o.multiGroup;
        p.st.isProtected = o.isProtected;
        p.st.isCritical = o.isCritical;
        p.st.sameUser = o.sameUser;
        p.st.inWindowsDir = o.inWindowsDir;
        p.st.sessionId = o.sessionId;
        p.denyOpen = o.denyOpen;
        m_procs.push_back(std::move(p));
        return m_procs.back().st.pid;
    }
    void kill(uint32_t pid) {
        m_procs.erase(std::remove_if(m_procs.begin(), m_procs.end(), [&](const Proc& p) { return p.st.pid == pid; }), m_procs.end());
    }
    // 同じ PID を、別のプロセス (別の作成時刻) が再利用した状態にする。
    uint32_t reusePid(uint32_t pid, const std::string& newExe, const Options& o = Options()) {
        kill(pid);
        Options oo = o;
        oo.pid = pid;
        return add(newExe, oo);
    }
    Proc* find(uint32_t pid) {
        for (auto& p : m_procs)
            if (p.st.pid == pid) return &p;
        return nullptr;
    }
    const std::vector<Proc>& procs() const { return m_procs; }

    // --- IProcessApi
    std::vector<ProcessEntry> enumerate() override {
        std::vector<ProcessEntry> out;
        for (const auto& p : m_procs) out.push_back({p.st.pid, 0, p.st.exeName});
        return out;
    }
    Result<ProcessState> query(uint32_t pid) override {
        ++queryCount;
        if (auto e = failure("query", pid)) return *e;
        Proc* p = find(pid);
        if (!p) return Error{ErrorCode::NotFound, "no such process"};
        if (p->denyOpen) return Error{ErrorCode::AccessDenied, "OpenProcess failed (access denied)", 5};
        return p->st;
    }
    Result<void> setAffinity(uint32_t pid, uint64_t startTime, const GroupMask& mask) override {
        calls.push_back("setAffinity " + std::to_string(pid) + " " + std::to_string(mask.mask));
        auto r = check("setAffinity", pid, startTime);
        if (!r.ok()) return r;
        find(pid)->st.affinity = mask;
        return {};
    }
    Result<void> setPriority(uint32_t pid, uint64_t startTime, uint32_t cls) override {
        calls.push_back("setPriority " + std::to_string(pid) + " " + std::to_string(cls));
        auto r = check("setPriority", pid, startTime);
        if (!r.ok()) return r;
        find(pid)->st.priorityClass = cls;
        return {};
    }
    uint32_t selfPid() const override { return self; }
    uint32_t currentSessionId() const override { return session; }

private:
    std::optional<Error> failure(const char* op, uint32_t pid) { return hook ? hook(op, pid) : std::nullopt; }
    Result<void> check(const char* op, uint32_t pid, uint64_t startTime) {
        if (auto e = failure(op, pid)) return *e;
        Proc* p = find(pid);
        if (!p) return Error{ErrorCode::NotFound, "no such process"};
        if (p->st.startTime != startTime) return Error{ErrorCode::ProcessChanged, "the PID now belongs to a different process"};
        if (p->denyOpen) return Error{ErrorCode::AccessDenied, "OpenProcess failed (access denied)", 5};
        return {};
    }

    std::vector<Proc> m_procs;
    uint32_t m_nextPid = 4000;
    uint64_t m_nextStart = 1000;
};

}  // namespace lf
