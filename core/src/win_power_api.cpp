#include "lf/win_power_api.hpp"

#include <windows.h>
#include <powrprof.h>

#include <cstdio>
#include <vector>

#include "lf/util.hpp"

namespace lf {
namespace {

bool parseGuid(const std::string& text, GUID& out) {
    const std::string g = normalizeGuid(text);
    if (g.empty()) return false;
    unsigned d1 = 0, d2 = 0, d3 = 0, b[8] = {};
    if (sscanf_s(g.c_str(), "%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x", &d1, &d2, &d3, &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6], &b[7]) != 11)
        return false;
    out.Data1 = d1;
    out.Data2 = static_cast<WORD>(d2);
    out.Data3 = static_cast<WORD>(d3);
    for (int i = 0; i < 8; ++i) out.Data4[i] = static_cast<BYTE>(b[i]);
    return true;
}

std::string formatGuid(const GUID& g) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1],
                  g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return buf;
}

Error winErr(ErrorCode code, DWORD rc, const char* what) {
    const ErrorCode c = rc == ERROR_ACCESS_DENIED ? ErrorCode::AccessDenied : code;
    return Error{c, std::string(what) + " failed", static_cast<unsigned long>(rc)};
}

}  // namespace

Result<std::string> WinPowerApi::activeSchemeGuid() {
    GUID* active = nullptr;
    const DWORD rc = PowerGetActiveScheme(nullptr, &active);
    if (rc != ERROR_SUCCESS || !active) return winErr(ErrorCode::NotFound, rc, "PowerGetActiveScheme");
    const std::string s = formatGuid(*active);
    LocalFree(active);
    return s;
}

Result<std::string> WinPowerApi::schemeName(const std::string& scheme) {
    GUID g{};
    if (!parseGuid(scheme, g)) return Error{ErrorCode::InvalidPath, "invalid power scheme GUID"};
    DWORD size = 0;
    DWORD rc = PowerReadFriendlyName(nullptr, &g, nullptr, nullptr, nullptr, &size);
    if (rc != ERROR_SUCCESS || size == 0) return winErr(ErrorCode::NotFound, rc, "PowerReadFriendlyName");
    std::vector<UCHAR> buf(size);
    rc = PowerReadFriendlyName(nullptr, &g, nullptr, nullptr, buf.data(), &size);
    if (rc != ERROR_SUCCESS) return winErr(ErrorCode::NotFound, rc, "PowerReadFriendlyName");
    std::wstring w(reinterpret_cast<const wchar_t*>(buf.data()));
    return narrow(w);
}

Result<std::optional<uint32_t>> WinPowerApi::readIndex(const std::string& scheme, const std::string& subgroup, const std::string& setting,
                                                       bool ac) {
    GUID s{}, sub{}, set{};
    if (!parseGuid(scheme, s) || !parseGuid(subgroup, sub) || !parseGuid(setting, set))
        return Error{ErrorCode::InvalidPath, "invalid GUID in power setting"};
    DWORD value = 0;
    const DWORD rc = ac ? PowerReadACValueIndex(nullptr, &s, &sub, &set, &value) : PowerReadDCValueIndex(nullptr, &s, &sub, &set, &value);
    if (rc == ERROR_SUCCESS) return std::optional<uint32_t>{value};
    if (rc == ERROR_FILE_NOT_FOUND || rc == ERROR_NOT_FOUND) return std::optional<uint32_t>{};
    return winErr(ErrorCode::RegistryRead, rc, ac ? "PowerReadACValueIndex" : "PowerReadDCValueIndex");
}

Result<void> WinPowerApi::writeIndex(const std::string& scheme, const std::string& subgroup, const std::string& setting, bool ac,
                                     uint32_t value) {
    GUID s{}, sub{}, set{};
    if (!parseGuid(scheme, s) || !parseGuid(subgroup, sub) || !parseGuid(setting, set))
        return Error{ErrorCode::InvalidPath, "invalid GUID in power setting"};
    const DWORD rc = ac ? PowerWriteACValueIndex(nullptr, &s, &sub, &set, value) : PowerWriteDCValueIndex(nullptr, &s, &sub, &set, value);
    if (rc != ERROR_SUCCESS) return winErr(ErrorCode::RegistryWrite, rc, ac ? "PowerWriteACValueIndex" : "PowerWriteDCValueIndex");
    return {};
}

Result<void> WinPowerApi::applyScheme(const std::string& scheme) {
    GUID s{};
    if (!parseGuid(scheme, s)) return Error{ErrorCode::InvalidPath, "invalid power scheme GUID"};
    const DWORD rc = PowerSetActiveScheme(nullptr, &s);
    if (rc != ERROR_SUCCESS) return winErr(ErrorCode::RegistryWrite, rc, "PowerSetActiveScheme");
    return {};
}

}  // namespace lf
