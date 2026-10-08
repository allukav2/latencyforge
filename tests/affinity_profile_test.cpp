#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "lf/affinity_profile.hpp"
#include "lf/affinity_safety.hpp"

namespace {

using nlohmann::json;
namespace fs = std::filesystem;

json prof(const std::string& id, const std::string& exe) {
    return {{"id", id}, {"name", exe}, {"exeNames", json::array({exe})}};
}

std::string doc(std::initializer_list<json> profiles, bool enabled = true) {
    json arr = json::array();
    for (const auto& p : profiles) arr.push_back(p);
    return json{{"version", 1}, {"enabled", enabled}, {"profiles", arr}}.dump();
}

}  // namespace

// ---- 設定 -----------------------------------------------------------------------------------------

TEST(AffinityConfig, DefaultsAreOffAndEmpty) {
    lf::AffinityConfig c;
    EXPECT_FALSE(c.enabled) << "nothing may be changed until the user turns it on";
    EXPECT_TRUE(c.profiles.empty());
    lf::AffinityProfile p;
    EXPECT_TRUE(p.moveBackground);
    EXPECT_EQ(p.priority, lf::GamePriority::Unchanged);
    EXPECT_EQ(p.options.strategy, lf::AffinityStrategy::Auto);
}

TEST(AffinityConfig, ParsesAndNormalizesNames) {
    auto r = lf::parseAffinityConfig(doc({prof("p1", "MyGame.EXE")}));
    ASSERT_TRUE(r.ok()) << (r.ok() ? "" : r.error().detail);
    const auto& cfg = r.value();
    EXPECT_TRUE(cfg.enabled);
    ASSERT_EQ(cfg.profiles.size(), 1u);
    EXPECT_EQ(cfg.profiles[0].exeNames[0], "mygame.exe") << "names are stored lower-case";
    EXPECT_TRUE(cfg.profiles[0].enabled);
}

TEST(AffinityConfig, RoundTripsEveryField) {
    lf::AffinityConfig c;
    c.enabled = true;
    lf::AffinityProfile p;
    p.id = "p7";
    p.name = "Game Seven";
    p.exeNames = {"seven.exe", "seven_launcher.exe"};
    p.options.strategy = lf::AffinityStrategy::ReserveOneCore;
    p.options.gameUsesSmtSiblings = false;
    p.moveBackground = false;
    p.priority = lf::GamePriority::AboveNormal;
    p.enabled = false;
    c.profiles = {p};

    auto back = lf::parseAffinityConfig(lf::serializeAffinityConfig(c));
    ASSERT_TRUE(back.ok()) << (back.ok() ? "" : back.error().detail);
    const auto& q = back.value().profiles.at(0);
    EXPECT_EQ(q.id, "p7");
    EXPECT_EQ(q.name, "Game Seven");
    EXPECT_EQ(q.exeNames, p.exeNames);
    EXPECT_EQ(q.options.strategy, lf::AffinityStrategy::ReserveOneCore);
    EXPECT_FALSE(q.options.gameUsesSmtSiblings);
    EXPECT_FALSE(q.moveBackground);
    EXPECT_EQ(q.priority, lf::GamePriority::AboveNormal);
    EXPECT_FALSE(q.enabled);
}

TEST(AffinityConfig, RejectsMalformedDocuments) {
    EXPECT_FALSE(lf::parseAffinityConfig("{bad").ok());
    EXPECT_FALSE(lf::parseAffinityConfig("[]").ok());
    EXPECT_FALSE(lf::parseAffinityConfig(R"({"version":2,"profiles":[]})").ok());
    EXPECT_FALSE(lf::parseAffinityConfig(R"({"version":1})").ok());
    EXPECT_FALSE(lf::parseAffinityConfig(R"({"version":1,"profiles":[],"extra":1})").ok());
    EXPECT_FALSE(lf::parseAffinityConfig(R"({"version":1,"enabled":"yes","profiles":[]})").ok());
    json bad = prof("p1", "a.exe");
    bad["typo"] = 1;
    EXPECT_FALSE(lf::parseAffinityConfig(doc({bad})).ok());
    json strategy = prof("p1", "a.exe");
    strategy["strategy"] = "turbo";
    EXPECT_FALSE(lf::parseAffinityConfig(doc({strategy})).ok());
    json priority = prof("p1", "a.exe");
    priority["priority"] = "realtime";  // リアルタイムは選べない
    EXPECT_FALSE(lf::parseAffinityConfig(doc({priority})).ok());
}

TEST(AffinityConfig, RejectsBadIdsNamesAndDuplicates) {
    EXPECT_FALSE(lf::parseAffinityConfig(doc({prof("P1", "a.exe")})).ok());
    EXPECT_FALSE(lf::parseAffinityConfig(doc({prof("", "a.exe")})).ok());
    EXPECT_FALSE(lf::parseAffinityConfig(doc({prof("p1", "a.exe"), prof("p1", "b.exe")})).ok()) << "duplicate id";
    EXPECT_FALSE(lf::parseAffinityConfig(doc({prof("p1", "a.exe"), prof("p2", "A.EXE")})).ok()) << "same exe in two profiles";
    json noName = prof("p1", "a.exe");
    noName["name"] = "";
    EXPECT_FALSE(lf::parseAffinityConfig(doc({noName})).ok());
    json noExes = prof("p1", "a.exe");
    noExes["exeNames"] = json::array();
    EXPECT_FALSE(lf::parseAffinityConfig(doc({noExes})).ok());
}

TEST(AffinityConfig, RejectsExeNamesThatAreNotBareFileNames) {
    for (const char* bad : {"C:\\Games\\a.exe", "dir/a.exe", "a", "a.dll", ".exe", "..\\a.exe", "a b|c.exe", "a*.exe", "", " a.exe", "a.exe "}) {
        EXPECT_FALSE(lf::parseAffinityConfig(doc({prof("p1", bad)})).ok()) << "'" << bad << "'";
    }
    EXPECT_FALSE(lf::parseAffinityConfig(doc({prof("p1", std::string(70, 'a') + ".exe")})).ok()) << "too long";
    EXPECT_TRUE(lf::parseAffinityConfig(doc({prof("p1", "My Game-2_x64.exe")})).ok());
}

TEST(AffinityConfig, ProtectedProcessesCannotBeRegisteredAsGames) {
    for (const char* name : {"explorer.exe", "svchost.exe", "csrss.exe", "audiodg.exe", "MsMpEng.exe", "vgc.exe", "EasyAntiCheat.exe", "BEService.exe"}) {
        auto r = lf::parseAffinityConfig(doc({prof("p1", name)}));
        EXPECT_FALSE(r.ok()) << name;
        if (!r.ok()) EXPECT_NE(r.error().detail.find("protected"), std::string::npos) << name;
    }
}

TEST(AffinityConfig, MissingFileGivesDefaultsAndSaveLoadWorks) {
    auto dir = fs::temp_directory_path() / "lf_tests_affinity_cfg";
    fs::remove_all(dir);
    auto missing = lf::loadAffinityConfig(dir / "affinity.json");
    ASSERT_TRUE(missing.ok());
    EXPECT_FALSE(missing.value().enabled);

    lf::AffinityConfig c;
    c.enabled = true;
    lf::AffinityProfile p;
    p.id = "p1";
    p.name = "G";
    p.exeNames = {"g.exe"};
    c.profiles = {p};
    ASSERT_TRUE(lf::saveAffinityConfig(dir / "affinity.json", c).ok());
    auto loaded = lf::loadAffinityConfig(dir / "affinity.json");
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.value().profiles.size(), 1u);

    std::ofstream(dir / "affinity.json") << "garbage";
    EXPECT_FALSE(lf::loadAffinityConfig(dir / "affinity.json").ok()) << "a damaged file is reported, not silently replaced";
    fs::remove_all(dir);
}

// ---- 安全判定 -------------------------------------------------------------------------------------

TEST(AffinitySafety, NeverTouchNamesAreCaseInsensitive) {
    for (const char* n : {"explorer.exe", "EXPLORER.EXE", "svchost.exe", "audiodg.exe", "vgc.exe", "EasyAntiCheat_EOS.exe", "lsass.exe"})
        EXPECT_TRUE(lf::isNeverTouchName(n)) << n;
    for (const char* n : {"game.exe", "chrome.exe", "discord.exe", "obs64.exe"}) EXPECT_FALSE(lf::isNeverTouchName(n)) << n;
}

TEST(AffinitySafety, CheckTargetReportsTheFirstReasonItFinds) {
    lf::ProcessState ok;
    ok.pid = 4242;
    ok.exeName = "game.exe";
    ok.sameUser = true;
    ok.sessionId = 1;
    EXPECT_EQ(lf::checkTarget(ok, 1000, 1), lf::SkipReason::None);

    auto with = [&](auto edit) {
        lf::ProcessState s = ok;
        edit(s);
        return lf::checkTarget(s, 1000, 1);
    };
    EXPECT_EQ(with([](auto& s) { s.pid = 1000; }), lf::SkipReason::Self);
    EXPECT_EQ(with([](auto& s) { s.isProtected = true; }), lf::SkipReason::Protected);
    EXPECT_EQ(with([](auto& s) { s.isCritical = true; }), lf::SkipReason::Critical);
    EXPECT_EQ(with([](auto& s) { s.sameUser = false; }), lf::SkipReason::OtherUser);
    EXPECT_EQ(with([](auto& s) { s.sessionId = 0; }), lf::SkipReason::OtherSession);
    EXPECT_EQ(with([](auto& s) { s.inWindowsDir = true; }), lf::SkipReason::WindowsDir);
    EXPECT_EQ(with([](auto& s) { s.multiGroup = true; }), lf::SkipReason::MultiGroup);
    EXPECT_EQ(with([](auto& s) { s.exeName = "explorer.exe"; }), lf::SkipReason::NeverTouchName);
}
