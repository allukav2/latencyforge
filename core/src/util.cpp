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

std::optional<std::chrono::system_clock::time_point> parseIso8601Utc(std::string_view s) {
    // 厳密な形式のみ: "YYYY-MM-DDTHH:MM:SSZ" (末尾の Z 必須)。前後の空白・余計な文字・符号は拒否する。
    // (sscanf は先頭の空白を読み飛ばし、末尾の余りも黙って無視するため、文字種の検査と %n による消費長の確認が必要。)
    if (s.empty()) return std::nullopt;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || c == '-' || c == ':' || c == 'T' || c == 'Z')) return std::nullopt;
    const std::string str(s);
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
    int consumed = -1;  // 'Z' まで一致したときだけ %n が代入される
    const int n = sscanf_s(str.c_str(), "%d-%d-%dT%d:%d:%dZ%n", &y, &mo, &d, &h, &mi, &sec, &consumed);
    if (n != 6 || consumed != static_cast<int>(str.size())) return std::nullopt;
    if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 || sec < 0 || sec > 61)
        return std::nullopt;
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = sec;
    const std::time_t t = _mkgmtime(&tm);
    if (t == static_cast<std::time_t>(-1)) return std::nullopt;
    return std::chrono::system_clock::from_time_t(t);
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
