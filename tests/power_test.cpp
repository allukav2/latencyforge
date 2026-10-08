#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>

#include "lf/engine.hpp"
#include "lf/memory_registry.hpp"
#include "lf/power_api.hpp"
#include "lf/sysinfo.hpp"
#include "lf/tweak.hpp"
#include "lf/util.hpp"
#include "lf/win_power_api.hpp"

namespace {

namespace fs = std::filesystem;
using nlohmann::json;

const char* kUsbSub = "2a737441-1930-4402-8d77-b2bebba308a3";
const char* kSuspend = "48e6b7a6-50f5-4782-a5d4-53bb8f07e226";
const char* kSchemeA = "381b4222-f694-41f0-9685-ff5bb260df2e";  // Balanced
const char* kSchemeB = "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c";  // High performance

lf::RegPath powerPath(const std::string& scheme, const std::string& sub, const std::string& setting, const char* mode) {
    return lf::parseRegPath("POWER\\" + scheme + "\\" + sub, setting + ":" + mode).value();
}

std::vector<lf::TweakDef> shippedUsb() {
    lf::TweakCatalog cat(lf::Policy::standard());
    auto issues = cat.loadDirectory(fs::path(LF_SOURCE_DIR) / "data" / "tweaks");
    for (const auto& i : issues) ADD_FAILURE() << i.source << ": " << i.message;
    std::vector<lf::TweakDef> out;
    for (const auto& t : cat.all())
        if (t.category == "usb") out.push_back(t);
    return out;
}

// Fake の電源プランと Engine/レジストリ一式
struct Env {
    fs::path dir;
    lf::FakePowerApi power;
    lf::Logger log{200, [] { return std::string("t"); }};
    lf::Policy policy = lf::Policy::standard();
    std::unique_ptr<lf::RoutingRegistry> reg;
    std::unique_ptr<lf::Engine> eng;
    int tick = 0;

    explicit Env(const std::string& name) {
        dir = fs::temp_directory_path() / "lf_tests_power" / name;
        fs::remove_all(dir);
        fs::create_directories(dir);
        power.addScheme(kSchemeA, "Balanced");
        power.addScheme(kSchemeB, "High performance");
        power.active = kSchemeA;
        for (const char* s : {kSchemeA, kSchemeB})
            for (bool ac : {true, false}) power.setValue(s, kUsbSub, kSuspend, ac, 1);  // 既定: 有効
        reg = std::make_unique<lf::RoutingRegistry>(std::make_unique<lf::MemoryRegistry>(), std::make_unique<lf::PowerRegistry>(power));
        eng = makeEngine();
        EXPECT_TRUE(eng->load().ok());
    }
    ~Env() {
        eng.reset();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    std::unique_ptr<lf::Engine> makeEngine() {
        lf::EngineConfig cfg{dir / "state.json", dir / "history.jsonl", 26100};
        return std::make_unique<lf::Engine>(*reg, policy, log, cfg, [this] {
            char buf[40];
            std::snprintf(buf, sizeof buf, "2026-01-01T00:00:%02dZ", ++tick % 60);
            return std::string(buf);
        });
    }
    // 現在アクティブな電源プランに解決した USB の 2 つの tweak (UI と同じ手順)
    std::vector<lf::TweakDef> resolvedUsb() {
        std::vector<lf::TweakDef> out;
        for (const auto& t : shippedUsb()) out.push_back(lf::resolveForScheme(t, power.active));
        return out;
    }
    static std::vector<const lf::TweakDef*> ptrs(const std::vector<lf::TweakDef>& v) {
        std::vector<const lf::TweakDef*> p;
        for (const auto& t : v) p.push_back(&t);
        return p;
    }
};

}  // namespace

// ---- GUID / パス ----------------------------------------------------------------------------------

TEST(PowerPath, NormalizeGuidAcceptsBracesAndAnyCaseAndRejectsGarbage) {
    EXPECT_EQ(lf::normalizeGuid("{2A737441-1930-4402-8D77-B2BEBBA308A3}"), kUsbSub);
    EXPECT_EQ(lf::normalizeGuid("2A737441-1930-4402-8D77-B2BEBBA308A3"), kUsbSub);
    for (const char* bad : {"", "ACTIVE", "2a737441-1930-4402-8d77-b2bebba308a", "2a737441_1930_4402_8d77_b2bebba308a3",
                            "2a737441-1930-4402-8d77-b2bebba308aZ", "{2a737441-1930-4402-8d77-b2bebba308a3", " 2a737441-1930-4402-8d77-b2bebba308a3"})
        EXPECT_TRUE(lf::normalizeGuid(bad).empty()) << bad;
}

TEST(PowerPath, ParsesAndRoundTripsThePowerHive) {
    auto p = lf::parseRegPath(std::string("POWER\\ACTIVE\\") + kUsbSub, std::string(kSuspend) + ":ac");
    ASSERT_TRUE(p.ok());
    EXPECT_EQ(p.value().hive, lf::RegHive::Power);
    EXPECT_EQ(p.value().keyString(), std::string("POWER\\ACTIVE\\") + kUsbSub);
    EXPECT_FALSE(lf::parseRegPath("POWER\\", "x").ok());
    EXPECT_FALSE(lf::parseRegPath("power\\a\\b", "x").ok()) << "the hive name is case-sensitive like HKLM/HKCU";
}

// ---- tweak 定義 (data/tweaks/usb.json) -------------------------------------------------------------

TEST(PowerTweaks, ShippedUsbDefinitionsLoadAndUseTheDocumentedSetting) {
    const auto usb = shippedUsb();
    ASSERT_EQ(usb.size(), 2u);
    for (const auto& t : usb) {
        EXPECT_TRUE(t.isPowerTemplate()) << t.id;
        EXPECT_EQ(t.data, lf::RegValue::dword(0)) << "disabled";
        EXPECT_FALSE(t.requiresReboot) << "power settings apply immediately";
        EXPECT_EQ(t.risk, lf::Risk::Low);
        EXPECT_FALSE(t.note.ja.empty());
        EXPECT_NE(t.note.ja.find("システム依存"), std::string::npos) << t.id << ": effect must be described as system-dependent";
        EXPECT_NE(t.note.en.find("system-dependent"), std::string::npos) << t.id;
        EXPECT_EQ(t.target.subkey, std::string("ACTIVE\\") + kUsbSub);
    }
    EXPECT_EQ(usb[0].target.valueName, std::string(kSuspend) + ":ac");
    EXPECT_EQ(usb[1].target.valueName, std::string(kSuspend) + ":dc");
}

namespace {
json powerTweak() {
    return {{"id", "usb.example"},
            {"path", std::string("POWER\\ACTIVE\\") + kUsbSub},
            {"value", std::string(kSuspend) + ":ac"},
            {"type", "REG_DWORD"},
            {"data", 0},
            {"title", {{"ja", "題"}, {"en", "t"}}},
            {"summary", {{"ja", "概要"}, {"en", "s"}}},
            {"risk", "low"},
            {"requiresReboot", false}};
}
bool loads(const json& tweak) {
    std::vector<lf::TweakDef> out;
    std::vector<lf::DefinitionIssue> issues;
    return lf::parseTweakDocument(json{{"schema", 1}, {"tweaks", json::array({tweak})}}.dump(), "t.json", lf::Policy::standard(), out, issues);
}
}  // namespace

TEST(PowerTweaks, ValidationRejectsMalformedPowerDefinitions) {
    EXPECT_TRUE(loads(powerTweak()));
    json t = powerTweak();
    t["type"] = "REG_SZ";
    t["data"] = "0";
    EXPECT_FALSE(loads(t)) << "power settings are DWORD";
    t = powerTweak();
    t["path"] = std::string("POWER\\") + kSchemeA + "\\" + kUsbSub;
    EXPECT_FALSE(loads(t)) << "definitions are templates: the scheme must be the ACTIVE alias";
    t = powerTweak();
    t["value"] = kSuspend;
    EXPECT_FALSE(loads(t)) << "missing :ac/:dc";
    t = powerTweak();
    t["value"] = std::string(kSuspend) + ":both";
    EXPECT_FALSE(loads(t));
    t = powerTweak();
    t["value"] = "not-a-guid:ac";
    EXPECT_FALSE(loads(t));
    t = powerTweak();
    t["path"] = "POWER\\ACTIVE\\not-a-guid";
    EXPECT_FALSE(loads(t));
}

TEST(PowerTweaks, OnlyTheUsbSubgroupIsAllowedByThePolicy) {
    json t = powerTweak();
    t["path"] = "POWER\\ACTIVE\\54533251-82be-4824-96c1-47b60b740d00";  // プロセッサの電源管理: 許可リストに無い
    EXPECT_FALSE(loads(t));
    t["path"] = "POWER\\ACTIVE\\e73a048d-bf27-4f12-9731-8b2076e8891f";  // バッテリー: 許可リストに無い
    EXPECT_FALSE(loads(t));

    const auto p = lf::Policy::standard();
    EXPECT_TRUE(p.check(powerPath(kSchemeA, kUsbSub, kSuspend, "dc")).ok());
    EXPECT_TRUE(p.check(powerPath("ACTIVE", kUsbSub, kSuspend, "ac")).ok());
    EXPECT_FALSE(p.check(powerPath(kSchemeA, "54533251-82be-4824-96c1-47b60b740d00", kSuspend, "ac")).ok());
    auto kernelOnly = lf::Policy::withAllowList({"HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\kernel"});
    EXPECT_FALSE(kernelOnly.check(powerPath(kSchemeA, kUsbSub, kSuspend, "ac")).ok());
}

TEST(PowerTweaks, ResolveForSchemeReplacesTheAliasAndMakesAPerSchemeId) {
    const auto usb = shippedUsb();
    ASSERT_FALSE(usb.empty());
    const auto r = lf::resolveForScheme(usb[0], kSchemeA);
    EXPECT_EQ(r.target.subkey, std::string(kSchemeA) + "\\" + kUsbSub);
    EXPECT_EQ(r.id, usb[0].id + "@" + kSchemeA);
    EXPECT_EQ(lf::baseTweakId(r.id), usb[0].id);
    EXPECT_EQ(lf::baseTweakId("kernel.timer_check_flags"), "kernel.timer_check_flags");
    EXPECT_NE(lf::resolveForScheme(usb[0], kSchemeB).id, r.id) << "each power plan has its own backup record";
    // レジストリの tweak は変わらない
    lf::TweakDef reg;
    reg.id = "x.y";
    reg.target = lf::parseRegPath("HKLM\\SYSTEM\\Foo", "V").value();
    EXPECT_EQ(lf::resolveForScheme(reg, kSchemeA).id, "x.y");
}

// ---- PowerRegistry (Fake の電源プラン) -------------------------------------------------------------

TEST(PowerRegistry, ReadsAndWritesDwordValuesPerAcAndDc) {
    lf::FakePowerApi api;
    api.addScheme(kSchemeA, "Balanced");
    api.active = kSchemeA;
    api.setValue(kSchemeA, kUsbSub, kSuspend, true, 1);
    api.setValue(kSchemeA, kUsbSub, kSuspend, false, 1);
    lf::PowerRegistry reg(api);

    auto r = reg.read(powerPath(kSchemeA, kUsbSub, kSuspend, "ac"));
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(*r.value(), lf::RegValue::dword(1));
    ASSERT_TRUE(reg.write(powerPath(kSchemeA, kUsbSub, kSuspend, "ac"), lf::RegValue::dword(0)).ok());
    EXPECT_EQ(api.value(kSchemeA, kUsbSub, kSuspend, true), std::optional<uint32_t>(0));
    EXPECT_EQ(api.value(kSchemeA, kUsbSub, kSuspend, false), std::optional<uint32_t>(1)) << "DC is independent of AC";
}

TEST(PowerRegistry, ReappliesTheSchemeOnlyWhenItIsTheActiveOne) {
    lf::FakePowerApi api;
    api.addScheme(kSchemeA, "Balanced");
    api.addScheme(kSchemeB, "High performance");
    api.active = kSchemeA;
    api.setValue(kSchemeA, kUsbSub, kSuspend, true, 1);
    api.setValue(kSchemeB, kUsbSub, kSuspend, true, 1);
    lf::PowerRegistry reg(api);

    ASSERT_TRUE(reg.write(powerPath(kSchemeA, kUsbSub, kSuspend, "ac"), lf::RegValue::dword(0)).ok());
    EXPECT_EQ(api.calls.back(), std::string("apply ") + kSchemeA) << "the active plan must be re-applied for the change to take effect";
    api.calls.clear();
    ASSERT_TRUE(reg.write(powerPath(kSchemeB, kUsbSub, kSuspend, "ac"), lf::RegValue::dword(0)).ok());
    for (const auto& c : api.calls) EXPECT_EQ(c.rfind("apply", 0), std::string::npos) << "an inactive plan is only written, never activated: " << c;
}

TEST(PowerRegistry, ASettingMissingFromThePlanIsUnsupportedAndNeverWritten) {
    lf::FakePowerApi api;
    api.addScheme(kSchemeA, "Balanced");
    api.active = kSchemeA;
    lf::PowerRegistry reg(api);  // 値を設定していない = そのプランに無い
    auto r = reg.read(powerPath(kSchemeA, kUsbSub, kSuspend, "ac"));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, lf::ErrorCode::UnsupportedValueType);
    EXPECT_TRUE(api.calls.empty());
}

TEST(PowerRegistry, RejectsTheActiveAliasBadPathsNonDwordAndDeletes) {
    lf::FakePowerApi api;
    api.addScheme(kSchemeA, "Balanced");
    api.active = kSchemeA;
    api.setValue(kSchemeA, kUsbSub, kSuspend, true, 1);
    lf::PowerRegistry reg(api);

    auto alias = reg.write(powerPath("ACTIVE", kUsbSub, kSuspend, "ac"), lf::RegValue::dword(0));
    ASSERT_FALSE(alias.ok());
    EXPECT_EQ(alias.error().code, lf::ErrorCode::InvalidPath) << "the alias must be resolved by the caller so the backup names a concrete plan";
    EXPECT_FALSE(reg.read(powerPath("ACTIVE", kUsbSub, kSuspend, "ac")).ok());

    EXPECT_FALSE(reg.write(powerPath(kSchemeA, kUsbSub, kSuspend, "ac"), lf::RegValue::string("0")).ok());
    EXPECT_FALSE(reg.read(lf::parseRegPath("HKLM\\SYSTEM\\Foo", "V").value()).ok()) << "not a power path";
    auto del = reg.deleteValue(powerPath(kSchemeA, kUsbSub, kSuspend, "ac"));
    ASSERT_FALSE(del.ok());
    EXPECT_EQ(del.error().code, lf::ErrorCode::RegistryDelete);
    EXPECT_FALSE(reg.read(powerPath("00000000-0000-0000-0000-000000000000", kUsbSub, kSuspend, "ac")).ok()) << "unknown plan";
    EXPECT_EQ(api.value(kSchemeA, kUsbSub, kSuspend, true), std::optional<uint32_t>(1)) << "nothing was changed by the rejected calls";
}

TEST(PowerRegistry, RoutingSendsPowerPathsToThePowerApiAndTheRestToTheRegistry) {
    lf::FakePowerApi api;
    api.addScheme(kSchemeA, "Balanced");
    api.active = kSchemeA;
    api.setValue(kSchemeA, kUsbSub, kSuspend, true, 1);
    auto mem = std::make_unique<lf::MemoryRegistry>();
    lf::MemoryRegistry* memPtr = mem.get();
    lf::RoutingRegistry router(std::move(mem), std::make_unique<lf::PowerRegistry>(api));

    auto hk = lf::parseRegPath("HKLM\\SYSTEM\\Foo", "V").value();
    ASSERT_TRUE(router.write(hk, lf::RegValue::dword(5)).ok());
    EXPECT_EQ(memPtr->get(hk), std::optional<lf::RegValue>(lf::RegValue::dword(5)));
    EXPECT_TRUE(api.calls.empty()) << "a registry write must not reach the power API";

    ASSERT_TRUE(router.write(powerPath(kSchemeA, kUsbSub, kSuspend, "ac"), lf::RegValue::dword(0)).ok());
    EXPECT_EQ(memPtr->size(), 1u) << "a power write must not reach the registry";
    EXPECT_EQ(api.value(kSchemeA, kUsbSub, kSuspend, true), std::optional<uint32_t>(0));
}

// ---- Engine 経由 (バックアップ / 復元 / ロールバック) ------------------------------------------------

TEST(PowerEngine, AppliesBacksUpAndRestoresBothUsbSettings) {
    Env e("apply");
    const auto defs = e.resolvedUsb();
    ASSERT_EQ(defs.size(), 2u);

    auto dry = e.eng->apply(Env::ptrs(defs), {.dryRun = true});
    ASSERT_TRUE(dry.ok);
    EXPECT_EQ(dry.items[0].status, lf::ItemStatus::WouldChange);
    EXPECT_EQ(dry.items[0].before, std::optional<lf::RegValue>(lf::RegValue::dword(1)));
    EXPECT_EQ(dry.items[0].after, std::optional<lf::RegValue>(lf::RegValue::dword(0)));
    EXPECT_TRUE(e.power.calls.empty()) << "dry-run must not touch the power plan";
    EXPECT_FALSE(dry.rebootRequired) << "power settings do not need a restart";

    auto rep = e.eng->apply(Env::ptrs(defs));
    ASSERT_TRUE(rep.ok) << (rep.error ? rep.error->detail : "");
    EXPECT_EQ(e.power.value(kSchemeA, kUsbSub, kSuspend, true), std::optional<uint32_t>(0));
    EXPECT_EQ(e.power.value(kSchemeA, kUsbSub, kSuspend, false), std::optional<uint32_t>(0));
    EXPECT_FALSE(rep.rebootRequired);
    EXPECT_EQ(e.eng->applied().size(), 2u);
    EXPECT_EQ(e.eng->applied().at(defs[0].id).original, std::optional<lf::RegValue>(lf::RegValue::dword(1)));

    // 「再起動」しても復元できる
    e.eng = e.makeEngine();
    ASSERT_TRUE(e.eng->load().ok());
    auto rev = e.eng->revertAll();
    ASSERT_TRUE(rev.ok);
    EXPECT_EQ(e.power.value(kSchemeA, kUsbSub, kSuspend, true), std::optional<uint32_t>(1));
    EXPECT_EQ(e.power.value(kSchemeA, kUsbSub, kSuspend, false), std::optional<uint32_t>(1));
    EXPECT_TRUE(e.eng->applied().empty());
    EXPECT_TRUE(e.eng->revertAll().ok) << "idempotent";
}

TEST(PowerEngine, AMidwayFailureRollsTheFirstSettingBack) {
    Env e("rollback");
    const auto defs = e.resolvedUsb();
    int writes = 0;
    e.power.hook = [&](const char* op) -> std::optional<lf::Error> {
        if (std::string(op) == "write" && ++writes == 2) return lf::Error{lf::ErrorCode::RegistryWrite, "injected", 5};
        return std::nullopt;
    };
    auto rep = e.eng->apply(Env::ptrs(defs));
    EXPECT_FALSE(rep.ok);
    EXPECT_EQ(rep.items[0].status, lf::ItemStatus::RolledBack);
    EXPECT_EQ(rep.items[1].status, lf::ItemStatus::Failed);
    EXPECT_EQ(e.power.value(kSchemeA, kUsbSub, kSuspend, true), std::optional<uint32_t>(1)) << "rolled back to the original";
    EXPECT_EQ(e.power.value(kSchemeA, kUsbSub, kSuspend, false), std::optional<uint32_t>(1));
    EXPECT_TRUE(e.eng->applied().empty());
    EXPECT_FALSE(e.eng->pending().has_value());
}

TEST(PowerEngine, EachPowerPlanHasItsOwnBackupAndRevertAllRestoresBoth) {
    Env e("two_schemes");
    const auto onA = e.resolvedUsb();
    ASSERT_TRUE(e.eng->apply(Env::ptrs(onA)).ok);

    e.power.active = kSchemeB;  // ユーザーが電源プランを切り替えた
    const auto onB = e.resolvedUsb();
    EXPECT_EQ(e.eng->status(onB[0]).state, lf::TweakState::NotApplied) << "applied on A, not on B";
    EXPECT_EQ(e.eng->status(onA[0]).state, lf::TweakState::Applied) << "A is still tracked";
    ASSERT_TRUE(e.eng->apply(Env::ptrs(onB)).ok);
    EXPECT_EQ(e.eng->applied().size(), 4u);

    e.power.calls.clear();
    auto rev = e.eng->revert({onA[0].id, onA[1].id});  // 非アクティブな A だけ戻す
    ASSERT_TRUE(rev.ok);
    EXPECT_EQ(e.power.value(kSchemeA, kUsbSub, kSuspend, true), std::optional<uint32_t>(1));
    EXPECT_EQ(e.power.value(kSchemeB, kUsbSub, kSuspend, true), std::optional<uint32_t>(0)) << "B is untouched by reverting A";
    for (const auto& c : e.power.calls) EXPECT_EQ(c.rfind("apply", 0), std::string::npos) << "restoring an inactive plan never activates it: " << c;

    ASSERT_TRUE(e.eng->revertAll().ok);
    EXPECT_EQ(e.power.value(kSchemeB, kUsbSub, kSuspend, true), std::optional<uint32_t>(1));
    EXPECT_TRUE(e.eng->applied().empty());
}

TEST(PowerEngine, ASettingThatDoesNotExistInThePlanBlocksTheWholeApplyBeforeAnyWrite) {
    Env e("missing");
    e.power.schemes[kSchemeA].values.erase(lf::FakePowerApi::key(kUsbSub, kSuspend, false));  // DC だけ存在しない
    const auto defs = e.resolvedUsb();
    auto rep = e.eng->apply(Env::ptrs(defs));
    EXPECT_FALSE(rep.ok);
    EXPECT_EQ(rep.items[1].status, lf::ItemStatus::Blocked);
    EXPECT_EQ(rep.error->code, lf::ErrorCode::UnsupportedValueType);
    for (const auto& c : e.power.calls) EXPECT_EQ(c.rfind("write", 0), std::string::npos) << "nothing written";
    EXPECT_EQ(e.power.value(kSchemeA, kUsbSub, kSuspend, true), std::optional<uint32_t>(1));
}

TEST(PowerEngine, ADefinitionStillUsingTheActiveAliasIsRefusedWithoutAnyWrite) {
    Env e("alias");
    const auto usb = shippedUsb();  // 解決していないテンプレート
    auto rep = e.eng->apply({&usb[0]});
    EXPECT_FALSE(rep.ok);
    EXPECT_TRUE(e.power.calls.empty());
    EXPECT_EQ(e.power.value(kSchemeA, kUsbSub, kSuspend, true), std::optional<uint32_t>(1));
}

TEST(PowerEngine, APowerSettingOutsideTheUsbSubgroupIsRefusedByThePolicy) {
    Env e("policy");
    lf::TweakDef evil = e.resolvedUsb()[0];
    evil.id = "evil.cpu@" + std::string(kSchemeA);
    evil.target = powerPath(kSchemeA, "54533251-82be-4824-96c1-47b60b740d00", "45bcc044-d885-43e2-8605-ee0ec6e96b59", "ac");
    auto rep = e.eng->apply({&evil});
    EXPECT_FALSE(rep.ok);
    EXPECT_EQ(rep.error->code, lf::ErrorCode::PolicyDenied);
    EXPECT_TRUE(e.power.calls.empty());
}

TEST(PowerEngine, ATamperedStateTargetingAnotherPowerSubgroupIsRefused) {
    Env e("tamper");
    {
        std::ofstream f(e.dir / "state.json");
        f << R"({"version":1,"applied":{"x.y":{"key":"POWER\\381b4222-f694-41f0-9685-ff5bb260df2e\\54533251-82be-4824-96c1-47b60b740d00",
            "value":"45bcc044-d885-43e2-8605-ee0ec6e96b59:ac","original":{"type":"REG_DWORD","data":5},
            "applied":{"type":"REG_DWORD","data":100},"time":"t","requiresReboot":false}},"pending":null})";
    }
    e.eng = e.makeEngine();
    auto r = e.eng->load();
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, lf::ErrorCode::StateCorrupt);
    EXPECT_TRUE(e.eng->revertAll().ok == false);
    EXPECT_TRUE(e.power.calls.empty());
}

// ---- 実機の電源 API (読み取りのみ。書き込みはしない) ------------------------------------------------

TEST(WinPowerApi, ReadsTheActiveSchemeAndTheUsbSettingWithoutWriting) {
    lf::WinPowerApi api;
    auto active = api.activeSchemeGuid();
    ASSERT_TRUE(active.ok()) << active.error().detail;
    EXPECT_EQ(lf::normalizeGuid(active.value()), active.value()) << "a normalized GUID";
    auto name = api.schemeName(active.value());
    ASSERT_TRUE(name.ok()) << name.error().detail;
    EXPECT_FALSE(name.value().empty());
    for (bool ac : {true, false}) {
        auto v = api.readIndex(active.value(), kUsbSub, kSuspend, ac);
        ASSERT_TRUE(v.ok()) << v.error().detail;  // 値が無い環境でも「無い」として成功する
        if (v.value()) EXPECT_LE(*v.value(), 1u) << "USB selective suspend is 0 or 1";
    }
}

// ---- GPU ドライバー バージョン ----------------------------------------------------------------------

TEST(GpuDriver, FormatsTheRawVersionAndTheNvidiaMarketingVersion) {
    constexpr auto pack = [](uint64_t a, uint64_t b, uint64_t c, uint64_t d) { return (a << 48) | (b << 32) | (c << 16) | d; };
    EXPECT_EQ(lf::formatDriverVersion(pack(32, 0, 15, 6094)), "32.0.15.6094");
    EXPECT_EQ(lf::nvidiaDriverVersion(pack(32, 0, 15, 6094)), "560.94");
    EXPECT_EQ(lf::nvidiaDriverVersion(pack(31, 0, 15, 4601)), "546.01");
    EXPECT_EQ(lf::nvidiaDriverVersion(pack(30, 0, 14, 7111)), "471.11");
    EXPECT_EQ(lf::nvidiaDriverVersion(pack(32, 0, 15, 9)), "500.09");
    EXPECT_TRUE(lf::formatDriverVersion(0).empty());
    EXPECT_TRUE(lf::nvidiaDriverVersion(0).empty());
}

TEST(GpuDriver, SampleNvidiaSystemsCarryADriverVersion) {
    auto probe = lf::makeSampleProbe("intel-hybrid");
    const auto sys = lf::detectSystem(*probe);
    ASSERT_TRUE(sys.hasNvidia());
    for (const auto& g : sys.gpus)
        if (g.vendor == lf::GpuVendor::Nvidia) EXPECT_EQ(lf::nvidiaDriverVersion(g.driverVersion), "560.94");
}
