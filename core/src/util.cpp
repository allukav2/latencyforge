#include "lf/util.hpp"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

namespace lf {

std::string nowIso8601Utc() {
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    gmtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(std::wstring_view w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

Result<void> atomicWriteFile(const std::filesystem::path& path, std::string_view data) {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);

    std::filesystem::path tmp = path;
    tmp += L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return Error{ErrorCode::Io, "cannot create " + narrow(tmp.wstring()), GetLastError()};

    bool ok = true;
    DWORD err = 0;
    size_t off = 0;
    while (off < data.size()) {
        DWORD wrote = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(data.size() - off, 1u << 20));
        if (!WriteFile(h, data.data() + off, chunk, &wrote, nullptr)) {
            ok = false;
            err = GetLastError();
            break;
        }
        off += wrote;
    }
    if (ok && !FlushFileBuffers(h)) {
        ok = false;
        err = GetLastError();
    }
    CloseHandle(h);
    if (ok && !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ok = false;
        err = GetLastError();
    }
    if (!ok) {
        DeleteFileW(tmp.c_str());
        return Error{ErrorCode::Io, "cannot write " + narrow(path.wstring()), err};
    }
    return {};
}

Result<std::string> readFileText(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) return Error{ErrorCode::NotFound, narrow(path.wstring())};
        return Error{ErrorCode::Io, "cannot open " + narrow(path.wstring())};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}  // namespace lf
