#include "lf/policy.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

namespace lf {
namespace {

std::string lower(std::string_view s) {
    std::string o(s);
    std::transform(o.begin(), o.end(), o.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return o;
}

std::vector<std::string> splitLower(std::string_view s) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t next = s.find('\\', pos);
        if (next == std::string_view::npos) next = s.size();
        out.push_back(lower(s.substr(pos, next - pos)));
        pos = next + 1;
    }
    return out;
}

std::vector<std::string> segmentsOf(const RegPath& p) {
    std::vector<std::string> v;
    v.push_back(lower(hiveName(p.hive)));
    for (auto& s : splitLower(p.subkey)) v.push_back(std::move(s));
    return v;
}

// 許可リストの接頭辞照合。セグメント "*" は「何かの 1 セグメント」(電源プランの GUID など実行時に決まる部分) に一致する。
// 空のセグメントや、セグメントが足りない場合には一致しない。
bool startsWith(const std::vector<std::string>& v, const std::vector<std::string>& prefix) {
    if (prefix.size() > v.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (prefix[i] == "*") {
            if (v[i].empty()) return false;
            continue;
        }
        if (prefix[i] != v[i]) return false;
    }
    return true;
}

// --- 拒否ルール -------------------------------------------------------------------------------

// セグメント名がこの部分文字列を含むキー配下は拒否 (Defender, Firewall 等の製品名系)。
constexpr std::array<std::string_view, 8> kDeniedSegmentSubstrings = {
    "defender", "firewall", "smartscreen", "deviceguard", "windowsupdate", "wuauserv", "securityhealth", "appcontrol"};

// セグメント名がこれに完全一致するキー配下は拒否。
constexpr std::array<std::string_view, 14> kDeniedSegments = {
    "lsa",                             // LSA 保護 (RunAsPPL)
    "sharedaccess",                    // Windows ファイアウォール サービス
    "mpssvc",                          // ファイアウォール
    "windefend",                       // Defender サービス
    "sense",                           // Defender for Endpoint
    "wdboot",    "wdfilter",
    "image file execution options",    // 緩和策の上書き経路
    "hypervisorenforcedcodeintegrity", // HVCI
    "code integrity",
    "ci",                              // ...\Control\CI (コード整合性)
    "securebootpolicy",
    "securityproviders",
    "kernel-dma protection"};

// 連続セグメント列として含まれたら拒否。
const std::vector<std::vector<std::string>>& deniedSequences() {
    static const std::vector<std::vector<std::string>> k = {
        {"policies", "system"},                   // UAC (EnableLUA, ConsentPrompt*)
        {"control", "deviceguard"},               // VBS
        {"control", "session manager", "memory management"},  // Spectre/Meltdown (FeatureSettingsOverride*)
    };
    return k;
}

// 値名による拒否 (許可されたキー内であっても書かせない)。
constexpr std::array<std::string_view, 21> kDeniedValueNames = {
    "smartscreenenabled",
    "disableexceptionchainvalidation",       // SEHOP 無効化
    "disablecontrolflowguardexportsuppression",  // CFG 弱体化
    "mitigationoptions",
    "mitigationauditoptions",
    "featuresettingsoverride",
    "featuresettingsoverridemask",
    "enablelua",
    "consentpromptbehavioradmin",
    "consentpromptbehavioruser",
    "enablevirtualizationbasedsecurity",
    "requireplatformsecurityfeatures",
    "hypervisorenforcedcodeintegrity",
    "runasppl",
    "enablesmartscreen",
    "disableantispyware",
    "disableantivirus",
    "enablefirewall",
    "disablerealtimemonitoring",
    "nowindowsupdate",
    "filteradministratortoken"};

}  // namespace

Policy Policy::standard() {
    return withAllowList({
        "HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\kernel",
        // 電源設定 (powrprof API): USB のサブグループ (GUID_USB_SETTINGS) だけ。"*" は電源プランの GUID。
        "POWER\\*\\2a737441-1930-4402-8d77-b2bebba308a3",
    });
}

Policy Policy::withAllowList(const std::vector<std::string>& prefixes) {
    Policy p;
    for (const auto& s : prefixes) p.m_allow.push_back(splitLower(s));
    return p;
}

Result<void> Policy::checkNotDenied(const RegPath& path) const {
    auto deny = [&](const std::string& why) {
        return Error{ErrorCode::PolicyDenied, "denied (" + why + "): " + path.display()};
    };
    const std::string val = lower(path.valueName);
    for (auto n : kDeniedValueNames)
        if (val == n) return deny("security-related value name");

    const auto segs = segmentsOf(path);
    for (size_t i = 1; i < segs.size(); ++i) {
        for (auto sub : kDeniedSegmentSubstrings)
            if (segs[i].find(sub) != std::string::npos) return deny("security-related key: " + segs[i]);
        for (auto exact : kDeniedSegments)
            if (segs[i] == exact) return deny("security-related key: " + segs[i]);
    }
    for (const auto& seq : deniedSequences()) {
        if (seq.size() > segs.size()) continue;
        for (size_t i = 0; i + seq.size() <= segs.size(); ++i)
            if (std::equal(seq.begin(), seq.end(), segs.begin() + static_cast<std::ptrdiff_t>(i)))
                return deny("security-related key path");
    }
    return {};
}

Result<void> Policy::check(const RegPath& path) const {
    if (auto r = checkNotDenied(path); !r.ok()) return r;
    const auto segs = segmentsOf(path);
    for (const auto& prefix : m_allow)
        if (startsWith(segs, prefix)) return {};
    return Error{ErrorCode::PolicyDenied, "denied (not in allow list): " + path.display()};
}

}  // namespace lf
