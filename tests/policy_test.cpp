#include <gtest/gtest.h>

#include "lf/policy.hpp"

namespace {

lf::RegPath P(const std::string& key, const std::string& value) {
    auto r = lf::parseRegPath(key, value);
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().detail);
    return r.ok() ? r.value() : lf::RegPath{};
}

const char* kKernel = "HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\kernel";

}  // namespace

// ---- parseRegPath ---------------------------------------------------------------------------------

TEST(RegPathParse, AcceptsHklmAndHkcu) {
    EXPECT_TRUE(lf::parseRegPath("HKLM\\SOFTWARE\\X", "V").ok());
    EXPECT_TRUE(lf::parseRegPath("HKCU\\Software\\X", "V").ok());
}

TEST(RegPathParse, RejectsMalformedPaths) {
    EXPECT_FALSE(lf::parseRegPath("HKEY_LOCAL_MACHINE\\SOFTWARE", "V").ok());
    EXPECT_FALSE(lf::parseRegPath("HKLM\\", "V").ok());
    EXPECT_FALSE(lf::parseRegPath("HKLM\\a\\\\b", "V").ok());           // 空セグメント
    EXPECT_FALSE(lf::parseRegPath("HKLM\\a\\b\\", "V").ok());           // 末尾 '\'
    EXPECT_FALSE(lf::parseRegPath("HKLM\\a\\..\\Lsa", "V").ok());       // 相対セグメント
    EXPECT_FALSE(lf::parseRegPath("HKLM\\a/b", "V").ok());
    EXPECT_FALSE(lf::parseRegPath("HKLM\\a\nb", "V").ok());
    EXPECT_FALSE(lf::parseRegPath("HKLM\\a", "").ok());                 // 既定値は扱わない
    EXPECT_FALSE(lf::parseRegPath("HKLM\\a", "x\\y").ok());             // 値名にバックスラッシュ
}

// ---- 許可 -----------------------------------------------------------------------------------------

TEST(Policy, AllowsKernelKeyValues) {
    auto p = lf::Policy::standard();
    EXPECT_TRUE(p.check(P(kKernel, "TimerCheckFlags")).ok());
    EXPECT_TRUE(p.check(P(kKernel, "SerializeTimerExpiration")).ok());
}

TEST(Policy, AllowListMatchesOnSegmentBoundaries) {
    auto p = lf::Policy::standard();
    EXPECT_FALSE(p.check(P("HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\kernelX", "V")).ok());
    EXPECT_FALSE(p.check(P("HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager", "V")).ok());  // 親は不可
    EXPECT_TRUE(p.check(P("HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\KERNEL", "V")).ok());  // 大文字小文字
}

TEST(Policy, StandardPolicyDoesNotAllowHkcuOrArbitraryKeys) {
    auto p = lf::Policy::standard();
    EXPECT_FALSE(p.check(P("HKCU\\Software\\Anything", "V")).ok());
    EXPECT_FALSE(p.check(P("HKLM\\SOFTWARE\\Anything", "V")).ok());
}

// ---- 拒否 (セキュリティ関連) ------------------------------------------------------------------------

struct DenyCase {
    const char* key;
    const char* value;
    const char* what;
};

class PolicyDeny : public ::testing::TestWithParam<DenyCase> {};

TEST_P(PolicyDeny, SecurityRelatedWritesAreRejectedEvenWithAWideAllowList) {
    const auto& c = GetParam();
    const auto path = P(c.key, c.value);
    // 標準ポリシー
    auto r1 = lf::Policy::standard().check(path);
    ASSERT_FALSE(r1.ok()) << c.what;
    EXPECT_EQ(r1.error().code, lf::ErrorCode::PolicyDenied);
    // 許可リストを広げても、組み込みの拒否リストは外れない
    auto wide = lf::Policy::withAllowList({"HKLM\\SOFTWARE", "HKLM\\SYSTEM", "HKCU\\Software"});
    auto r2 = wide.check(path);
    ASSERT_FALSE(r2.ok()) << c.what << " (wide allow list)";
    EXPECT_EQ(r2.error().code, lf::ErrorCode::PolicyDenied);
    EXPECT_FALSE(wide.checkNotDenied(path).ok()) << c.what;
}

INSTANTIATE_TEST_SUITE_P(
    SecurityFeatures, PolicyDeny,
    ::testing::Values(
        DenyCase{"HKLM\\SOFTWARE\\Microsoft\\Windows Defender", "DisableAntiSpyware", "Defender"},
        DenyCase{"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows Defender\\Real-Time Protection", "DisableRealtimeMonitoring", "Defender policy"},
        DenyCase{"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", "EnableLUA", "UAC"},
        DenyCase{"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", "ConsentPromptBehaviorAdmin", "UAC prompt"},
        DenyCase{"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate\\AU", "NoAutoUpdate", "Windows Update"},
        DenyCase{"HKLM\\SYSTEM\\CurrentControlSet\\Services\\wuauserv", "Start", "Windows Update service"},
        DenyCase{"HKLM\\SYSTEM\\CurrentControlSet\\Services\\SharedAccess\\Parameters\\FirewallPolicy\\StandardProfile", "EnableFirewall", "Firewall"},
        DenyCase{"HKLM\\SYSTEM\\CurrentControlSet\\Services\\MpsSvc", "Start", "Firewall service"},
        DenyCase{"HKLM\\SYSTEM\\CurrentControlSet\\Control\\DeviceGuard", "EnableVirtualizationBasedSecurity", "VBS"},
        DenyCase{"HKLM\\SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity", "Enabled", "HVCI"},
        DenyCase{"HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management", "FeatureSettingsOverride", "Spectre/Meltdown"},
        DenyCase{"HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management", "LargeSystemCache", "Memory Management key"},
        DenyCase{"HKLM\\SYSTEM\\CurrentControlSet\\Control\\Lsa", "RunAsPPL", "LSA protection"},
        DenyCase{"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\game.exe", "MitigationOptions", "mitigation override"},
        DenyCase{"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\System", "EnableSmartScreen", "SmartScreen"},
        DenyCase{"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer", "SmartScreenEnabled", "SmartScreen key name"}));

TEST(Policy, SehopAndCfgValuesAreDeniedInsideTheAllowedKernelKey) {
    auto p = lf::Policy::standard();
    auto sehop = p.check(P(kKernel, "DisableExceptionChainValidation"));
    ASSERT_FALSE(sehop.ok());
    EXPECT_EQ(sehop.error().code, lf::ErrorCode::PolicyDenied);
    auto cfg = p.check(P(kKernel, "DisableControlFlowGuardExportSuppression"));
    ASSERT_FALSE(cfg.ok());
    EXPECT_EQ(cfg.error().code, lf::ErrorCode::PolicyDenied);
    EXPECT_FALSE(p.check(P(kKernel, "mitigationoptions")).ok());                    // 大文字小文字無視
    EXPECT_FALSE(p.check(P(kKernel, "DISABLEEXCEPTIONCHAINVALIDATION")).ok());
}

TEST(Policy, TestAllowListCanOpenAScratchKeyButNotSecurityKeys) {
    auto p = lf::Policy::withAllowList({"HKCU\\Software\\LatencyForgeTest"});
    EXPECT_TRUE(p.check(P("HKCU\\Software\\LatencyForgeTest\\run1", "V")).ok());
    EXPECT_FALSE(p.check(P("HKCU\\Software\\LatencyForgeTest\\Windows Defender", "V")).ok());
    EXPECT_FALSE(p.check(P(kKernel, "TimerCheckFlags")).ok());  // 許可リストに無いので不可
}
