#include "lf/win_process_api.hpp"

#include <windows.h>
#include <tlhelp32.h>

#include <cstring>
#include <string>
#include <vector>

#include "lf/util.hpp"

namespace lf {

const unsigned long kProcessAccessRights = PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION;

namespace {

class Handle {
public:
    explicit Handle(HANDLE h = nullptr) : m_h(h) {}
    ~Handle() {
        if (m_h) CloseHandle(m_h);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return m_h; }
    explicit operator bool() const { return m_h != nullptr; }

private:
    HANDLE m_h;
};

// プロセスを開く唯一の場所。最小権限のみ。
Handle openMinimal(uint32_t pid, DWORD* err) {
    HANDLE h = OpenProcess(kProcessAccessRights, FALSE, pid);
    if (!h && err) *err = GetLastError();
    return Handle(h);
}

Error openError(DWORD err, uint32_t pid) {
    const ErrorCode code = err == ERROR_ACCESS_DENIED ? ErrorCode::AccessDenied
                           : err == ERROR_INVALID_PARAMETER ? ErrorCode::NotFound
                                                            : ErrorCode::Io;
    return Error{code, "OpenProcess failed for pid " + std::to_string(pid), err};
}

uint64_t creationTime(HANDLE h) {
    FILETIME c{}, e{}, k{}, u{};
    if (!GetProcessTimes(h, &c, &e, &k, &u)) return 0;
    return (static_cast<uint64_t>(c.dwHighDateTime) << 32) | c.dwLowDateTime;
}

// 自分のトークンのユーザー SID (1 回だけ取得)。
const std::vector<BYTE>& selfUserSid() {
    static const std::vector<BYTE> sid = [] {
        std::vector<BYTE> out;
        HANDLE tok = nullptr;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
            DWORD len = 0;
            GetTokenInformation(tok, TokenUser, nullptr, 0, &len);
            std::vector<BYTE> buf(len);
            if (len && GetTokenInformation(tok, TokenUser, buf.data(), len, &len)) {
                auto* tu = reinterpret_cast<TOKEN_USER*>(buf.data());
                out.assign(static_cast<BYTE*>(static_cast<void*>(tu->User.Sid)),
                           static_cast<BYTE*>(static_cast<void*>(tu->User.Sid)) + GetLengthSid(tu->User.Sid));
            }
            CloseHandle(tok);
        }
        return out;
    }();
    return sid;
}

bool sameUser(HANDLE process) {
    const auto& mine = selfUserSid();
    if (mine.empty()) return false;
    HANDLE tok = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &tok)) return false;  // 読めない (SYSTEM/サービス/他ユーザー) = 対象外
    bool same = false;
    DWORD len = 0;
    GetTokenInformation(tok, TokenUser, nullptr, 0, &len);
    std::vector<BYTE> buf(len);
    if (len && GetTokenInformation(tok, TokenUser, buf.data(), len, &len)) {
        auto* tu = reinterpret_cast<TOKEN_USER*>(buf.data());
        same = EqualSid(tu->User.Sid, const_cast<BYTE*>(mine.data())) != FALSE;
    }
    CloseHandle(tok);
    return same;
}

}  // namespace

WinProcessApi::WinProcessApi() {
    DWORD sid = 0;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &sid)) m_session = sid;
}

uint32_t WinProcessApi::selfPid() const { return GetCurrentProcessId(); }

std::vector<ProcessEntry> WinProcessApi::enumerate() {
    std::vector<ProcessEntry> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);  // 軽量: 名前と PID の一覧だけ
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W e{};
    e.dwSize = sizeof e;
    if (Process32FirstW(snap, &e)) {
        do {
            out.push_back({e.th32ProcessID, e.th32ParentProcessID, narrow(e.szExeFile)});
        } while (Process32NextW(snap, &e));
    }
    CloseHandle(snap);
    return out;
}

Result<ProcessState> WinProcessApi::query(uint32_t pid) {
    DWORD err = 0;
    Handle h = openMinimal(pid, &err);
    if (!h) return openError(err, pid);

    DWORD exitCode = 0;
    if (!GetExitCodeProcess(h.get(), &exitCode) || exitCode != STILL_ACTIVE)
        return Error{ErrorCode::NotFound, "process has exited (pid " + std::to_string(pid) + ")"};

    ProcessState s;
    s.pid = pid;
    s.startTime = creationTime(h.get());
    s.priorityClass = GetPriorityClass(h.get());

    wchar_t path[MAX_PATH * 2]{};
    DWORD plen = static_cast<DWORD>(std::size(path));
    if (QueryFullProcessImageNameW(h.get(), 0, path, &plen)) {
        s.imagePath = narrow(std::wstring_view(path, plen));
        const size_t slash = s.imagePath.find_last_of("\\/");
        s.exeName = slash == std::string::npos ? s.imagePath : s.imagePath.substr(slash + 1);
        wchar_t win[MAX_PATH]{};
        const UINT wl = GetSystemWindowsDirectoryW(win, MAX_PATH);
        if (wl > 0 && wl < MAX_PATH) {
            const std::string winDir = narrow(std::wstring_view(win, wl)) + "\\";
            s.inWindowsDir = _strnicmp(s.imagePath.c_str(), winDir.c_str(), winDir.size()) == 0;
        }
    }

    WORD groups[8]{};
    USHORT groupCount = static_cast<USHORT>(std::size(groups));
    if (GetProcessGroupAffinity(h.get(), &groupCount, groups)) {
        s.multiGroup = groupCount > 1;
        s.affinity.group = groupCount ? groups[0] : 0;
    } else if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        s.multiGroup = true;
    }
    DWORD_PTR procMask = 0, sysMask = 0;
    if (GetProcessAffinityMask(h.get(), &procMask, &sysMask)) s.affinity.mask = procMask;
    else s.multiGroup = true;  // 読めない = 触らない

    PROCESS_PROTECTION_LEVEL_INFORMATION prot{};
    if (GetProcessInformation(h.get(), ProcessProtectionLevelInfo, &prot, sizeof prot))
        s.isProtected = prot.ProtectionLevel != PROTECTION_LEVEL_NONE;
    BOOL critical = FALSE;
    if (IsProcessCritical(h.get(), &critical)) s.isCritical = critical != FALSE;

    s.sameUser = sameUser(h.get());
    DWORD sid = 0;
    if (ProcessIdToSessionId(pid, &sid)) s.sessionId = sid;
    return s;
}

Result<void> WinProcessApi::setAffinity(uint32_t pid, uint64_t startTime, const GroupMask& mask) {
    DWORD err = 0;
    Handle h = openMinimal(pid, &err);
    if (!h) return openError(err, pid);
    if (creationTime(h.get()) != startTime) return Error{ErrorCode::ProcessChanged, "the PID now belongs to a different process"};
    if (!SetProcessAffinityMask(h.get(), static_cast<DWORD_PTR>(mask.mask)))
        return Error{GetLastError() == ERROR_ACCESS_DENIED ? ErrorCode::AccessDenied : ErrorCode::Io,
                     "SetProcessAffinityMask failed for pid " + std::to_string(pid), GetLastError()};
    return {};
}

Result<void> WinProcessApi::setPriority(uint32_t pid, uint64_t startTime, uint32_t priorityClass) {
    DWORD err = 0;
    Handle h = openMinimal(pid, &err);
    if (!h) return openError(err, pid);
    if (creationTime(h.get()) != startTime) return Error{ErrorCode::ProcessChanged, "the PID now belongs to a different process"};
    if (!SetPriorityClass(h.get(), priorityClass))
        return Error{GetLastError() == ERROR_ACCESS_DENIED ? ErrorCode::AccessDenied : ErrorCode::Io,
                     "SetPriorityClass failed for pid " + std::to_string(pid), GetLastError()};
    return {};
}

}  // namespace lf
