#include "lf/affinity_safety.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace lf {

std::string lowerAscii(std::string_view s) {
    std::string o(s);
    std::transform(o.begin(), o.end(), o.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return o;
}

namespace {

constexpr std::array<std::string_view, 48> kNeverTouch = {
    // Windows の中核
    "system", "registry", "smss.exe", "csrss.exe", "wininit.exe", "winlogon.exe", "services.exe", "lsass.exe",
    "svchost.exe", "dwm.exe", "fontdrvhost.exe", "sihost.exe", "taskhostw.exe", "explorer.exe", "ctfmon.exe",
    "runtimebroker.exe", "dllhost.exe", "conhost.exe", "wudfhost.exe", "spoolsv.exe", "memory compression",
    "audiodg.exe",  // 音声エンジン: レイテンシに直結するので触らない
    // セキュリティ
    "msmpeng.exe", "nissrv.exe", "securityhealthservice.exe", "securityhealthsystray.exe", "mpdefendercoreservice.exe",
    "smartscreen.exe", "mssense.exe", "sgrmbroker.exe",
    // 代表的なアンチチート (名前による除外はベストエフォート)
    "vgc.exe", "vgtray.exe", "easyanticheat.exe", "easyanticheat_eos.exe", "easyanticheat_launcher.exe", "beservice.exe",
    "beservice_x64.exe", "battleye.exe", "faceit.exe", "faceitclient.exe", "faceitservice.exe", "esportal.exe",
    "ricochet.exe", "xigncode.exe", "nprotect.exe", "gameguard.exe", "gamemon.exe", "equ8.exe"};

}  // namespace

bool isNeverTouchName(std::string_view exeName) {
    const std::string n = lowerAscii(exeName);
    return std::find(kNeverTouch.begin(), kNeverTouch.end(), n) != kNeverTouch.end();
}

const char* skipReasonName(SkipReason r) {
    switch (r) {
        case SkipReason::None: return "none";
        case SkipReason::Protected: return "protected";
        case SkipReason::Critical: return "critical";
        case SkipReason::OtherUser: return "other-user";
        case SkipReason::OtherSession: return "other-session";
        case SkipReason::WindowsDir: return "windows-dir";
        case SkipReason::MultiGroup: return "multi-group";
        case SkipReason::NeverTouchName: return "never-touch-name";
        case SkipReason::Self: return "self";
    }
    return "none";
}

SkipReason checkTarget(const ProcessState& p, uint32_t selfPid, uint32_t sessionId) {
    if (p.pid == selfPid) return SkipReason::Self;
    if (p.isProtected) return SkipReason::Protected;
    if (p.isCritical) return SkipReason::Critical;
    if (!p.sameUser) return SkipReason::OtherUser;
    if (p.sessionId != sessionId) return SkipReason::OtherSession;
    if (p.inWindowsDir) return SkipReason::WindowsDir;
    if (p.multiGroup) return SkipReason::MultiGroup;
    if (isNeverTouchName(p.exeName)) return SkipReason::NeverTouchName;
    return SkipReason::None;
}

bool isValidExeName(std::string_view name) {
    if (name.empty() || name.size() > 64) return false;
    for (unsigned char c : name) {
        if (c < 0x20 || c == 0x7F) return false;
        switch (c) {
            case '/': case '\\': case ':': case '*': case '?': case '"': case '<': case '>': case '|': return false;
            default: break;
        }
    }
    if (name.front() == '.' || name.front() == ' ' || name.back() == ' ') return false;
    const std::string lower = lowerAscii(name);
    if (lower.size() < 5 || lower.compare(lower.size() - 4, 4, ".exe") != 0) return false;
    return lower.find("..") == std::string::npos;
}

}  // namespace lf
