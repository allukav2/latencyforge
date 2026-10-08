#include "lf/system.hpp"

#include <windows.h>

#include <srrestoreptapi.h>

#include <string>

#include "lf/util.hpp"

namespace lf {

OsVersion detectOsVersion() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW vi{};
    vi.dwOSVersionInfoSize = sizeof vi;
    if (HMODULE nt = GetModuleHandleW(L"ntdll.dll")) {
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(nt, "RtlGetVersion"))) fn(&vi);
    }
    return {vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber};
}

std::chrono::system_clock::time_point systemBootTime() {
    return std::chrono::system_clock::now() - std::chrono::milliseconds(static_cast<long long>(GetTickCount64()));
}

Result<void> createRestorePoint(std::string_view description) {
    HMODULE lib = LoadLibraryExW(L"srclient.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!lib) return Error{ErrorCode::RestorePointFailed, "srclient.dll could not be loaded", GetLastError()};
    using SetFn = BOOL(WINAPI*)(PRESTOREPOINTINFOW, PSTATEMGRSTATUS);
    auto set = reinterpret_cast<SetFn>(GetProcAddress(lib, "SRSetRestorePointW"));
    if (!set) {
        FreeLibrary(lib);
        return Error{ErrorCode::RestorePointFailed, "SRSetRestorePointW not found", GetLastError()};
    }

    RESTOREPOINTINFOW info{};
    info.dwEventType = BEGIN_SYSTEM_CHANGE;
    info.dwRestorePtType = MODIFY_SETTINGS;
    info.llSequenceNumber = 0;
    const std::wstring w = widen(description.substr(0, MAX_DESC_W - 1));
    wcsncpy_s(info.szDescription, w.c_str(), _TRUNCATE);

    STATEMGRSTATUS st{};
    const BOOL ok = set(&info, &st);
    Result<void> result;
    if (!ok || st.nStatus != ERROR_SUCCESS) {
        result = Error{ErrorCode::RestorePointFailed, "SRSetRestorePointW(BEGIN) failed", st.nStatus ? st.nStatus : GetLastError()};
    } else {
        // 開始したら必ず終了を通知する (これで復元ポイントが確定する)。
        RESTOREPOINTINFOW end{};
        end.dwEventType = END_SYSTEM_CHANGE;
        end.dwRestorePtType = MODIFY_SETTINGS;
        end.llSequenceNumber = st.llSequenceNumber;
        STATEMGRSTATUS st2{};
        set(&end, &st2);
    }
    FreeLibrary(lib);
    return result;
}

}  // namespace lf
