// 実機レジストリ統合テスト。
// 安全策: 触るのは HKCU\Software\LatencyForgeTest\<一意なサブキー> 配下だけ。HKLM には一切触れない。
// 終了時にそのサブキーを丸ごと削除する。
#include <gtest/gtest.h>
#include <windows.h>

#include <filesystem>

#include "lf/engine.hpp"
#include "lf/util.hpp"
#include "lf/win_registry.hpp"

namespace {

namespace fs = std::filesystem;
using lf::RegValue;

class RegistryIntegration : public ::testing::Test {
protected:
    void SetUp() override {
        const std::string unique = std::to_string(GetCurrentProcessId()) + "_" + std::to_string(GetTickCount64());
        m_subkey = "Software\\LatencyForgeTest\\" + unique;
        m_key = "HKCU\\" + m_subkey;
        // ガード: テストが HKCU 以外を指していないこと。
        ASSERT_EQ(m_key.rfind("HKCU\\Software\\LatencyForgeTest\\", 0), 0u);
        m_dir = fs::temp_directory_path() / ("lf_tests_integration_" + unique);
        fs::create_directories(m_dir);
    }
    void TearDown() override {
        RegDeleteTreeW(HKEY_CURRENT_USER, lf::widen(m_subkey).c_str());
        std::error_code ec;
        fs::remove_all(m_dir, ec);
    }
    lf::RegPath path(const std::string& value, const std::string& child = "") const {
        auto p = lf::parseRegPath(child.empty() ? m_key : m_key + "\\" + child, value);
        return p.value();
    }

    std::string m_subkey, m_key;
    fs::path m_dir;
};

}  // namespace

TEST_F(RegistryIntegration, MissingKeyAndValueReadAsAbsent) {
    lf::WinRegistry reg;
    auto r = reg.read(path("Nope"));
    ASSERT_TRUE(r.ok());
    EXPECT_FALSE(r.value().has_value());
}

TEST_F(RegistryIntegration, RoundTripsAllSupportedTypes) {
    lf::WinRegistry reg;
    const std::vector<std::pair<std::string, RegValue>> cases = {
        {"D", RegValue::dword(0xDEADBEEF)},
        {"Q", RegValue::qword(0x1122334455667788ull)},
        {"S", RegValue::string("日本語 text")},
        {"E", RegValue::expand("%SystemRoot%\\foo")},
        {"Empty", RegValue::string("")},
    };
    for (const auto& [name, v] : cases) {
        ASSERT_TRUE(reg.write(path(name), v).ok()) << name;
        auto r = reg.read(path(name));
        ASSERT_TRUE(r.ok()) << name;
        ASSERT_TRUE(r.value().has_value()) << name;
        EXPECT_EQ(*r.value(), v) << name;
    }
}

TEST_F(RegistryIntegration, OverwriteAndIdempotentDelete) {
    lf::WinRegistry reg;
    ASSERT_TRUE(reg.write(path("V"), RegValue::dword(1)).ok());
    ASSERT_TRUE(reg.write(path("V"), RegValue::dword(2)).ok());
    EXPECT_EQ(*reg.read(path("V")).value(), RegValue::dword(2));
    ASSERT_TRUE(reg.deleteValue(path("V")).ok());
    EXPECT_FALSE(reg.read(path("V")).value().has_value());
    EXPECT_TRUE(reg.deleteValue(path("V")).ok());                    // 値が無くても成功
    EXPECT_TRUE(reg.deleteValue(path("V", "no\\such\\key")).ok());   // キーが無くても成功
}

TEST_F(RegistryIntegration, CreatesIntermediateKeysOnWrite) {
    lf::WinRegistry reg;
    ASSERT_TRUE(reg.write(path("V", "a\\b\\c"), RegValue::dword(7)).ok());
    EXPECT_EQ(*reg.read(path("V", "a\\b\\c")).value(), RegValue::dword(7));
}

TEST_F(RegistryIntegration, UnsupportedExistingTypesAreReportedNotMangled) {
    lf::WinRegistry reg;
    ASSERT_TRUE(reg.write(path("Seed"), RegValue::dword(1)).ok());  // キー作成
    HKEY h = nullptr;
    ASSERT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, lf::widen(m_subkey).c_str(), 0, KEY_SET_VALUE, &h), ERROR_SUCCESS);
    const BYTE bin[3] = {1, 2, 3};
    ASSERT_EQ(RegSetValueExW(h, L"Bin", 0, REG_BINARY, bin, sizeof bin), ERROR_SUCCESS);
    RegCloseKey(h);

    auto r = reg.read(path("Bin"));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, lf::ErrorCode::UnsupportedValueType);
}

TEST_F(RegistryIntegration, EngineApplyAndRevertAgainstTheRealRegistry) {
    lf::WinRegistry reg;
    lf::Logger log;
    // テスト用の許可リスト = HKCU のテストキーのみ (拒否リストは常に有効)。
    auto policy = lf::Policy::withAllowList({m_key});
    ASSERT_FALSE(lf::Policy::standard().check(path("X")).ok()) << "the production policy must not allow the test key";

    lf::EngineConfig cfg{m_dir / "state.json", m_dir / "history.jsonl", 26100};
    lf::Engine eng(reg, policy, log, cfg);
    ASSERT_TRUE(eng.load().ok());

    lf::TweakDef a, b;
    a.id = "test.a";
    a.target = path("A");
    a.data = RegValue::dword(0);
    a.title = a.summary = {"a", "a"};
    b = a;
    b.id = "test.b";
    b.target = path("B");
    b.data = RegValue::string("new");
    ASSERT_TRUE(reg.write(a.target, RegValue::dword(42)).ok());  // A には元の値、B は存在しない

    auto rep = eng.apply({&a, &b});
    ASSERT_TRUE(rep.ok) << (rep.error ? rep.error->detail : "");
    EXPECT_EQ(*reg.read(a.target).value(), RegValue::dword(0));
    EXPECT_EQ(*reg.read(b.target).value(), RegValue::string("new"));

    // 「再起動」して復元
    lf::Engine eng2(reg, policy, log, cfg);
    ASSERT_TRUE(eng2.load().ok());
    ASSERT_TRUE(eng2.isApplied("test.a"));
    auto rev = eng2.revertAll();
    ASSERT_TRUE(rev.ok);
    EXPECT_EQ(*reg.read(a.target).value(), RegValue::dword(42));
    EXPECT_FALSE(reg.read(b.target).value().has_value());
    EXPECT_TRUE(eng2.revertAll().ok);  // 冪等
    EXPECT_EQ(eng2.history().size(), 4u);
}

TEST_F(RegistryIntegration, EngineStopsAtAPolicyViolationWithoutTouchingTheRegistry) {
    lf::WinRegistry reg;
    lf::Logger log;
    auto policy = lf::Policy::withAllowList({m_key});
    lf::EngineConfig cfg{m_dir / "state.json", m_dir / "history.jsonl", 26100};
    lf::Engine eng(reg, policy, log, cfg);
    ASSERT_TRUE(eng.load().ok());

    // 許可キー配下だが "Windows Defender" という名前のサブキー → 拒否リストに該当
    lf::TweakDef t;
    t.id = "test.defender";
    t.target = path("V", "Windows Defender");
    t.data = RegValue::dword(1);
    t.title = t.summary = {"x", "x"};
    auto rep = eng.apply({&t});
    EXPECT_FALSE(rep.ok);
    EXPECT_EQ(rep.error->code, lf::ErrorCode::PolicyDenied);
    EXPECT_FALSE(reg.read(t.target).value().has_value());
}
