#include <gtest/gtest.h>

#include <algorithm>
#include <nlohmann/json.hpp>
#include <string>

#include "lf/tweak.hpp"
#include "lf/util.hpp"

namespace {

using nlohmann::json;

const char* kKernelKey = "HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\kernel";

json validTweak(const std::string& id = "kernel.example") {
    return {{"id", id},
            {"path", kKernelKey},
            {"value", "TimerCheckFlags"},
            {"type", "REG_DWORD"},
            {"data", 0},
            {"title", {{"ja", "題"}, {"en", "Title"}}},
            {"summary", {{"ja", "概要"}, {"en", "Summary"}}},
            {"risk", "low"},
            {"requiresReboot", true}};
}

std::string doc(std::initializer_list<json> tweaks) {
    json t = json::array();
    for (const auto& x : tweaks) t.push_back(x);
    return json{{"schema", 1}, {"tweaks", t}}.dump();
}

struct Parsed {
    bool ok;
    std::vector<lf::TweakDef> defs;
    std::vector<lf::DefinitionIssue> issues;
    std::string allMessages() const {
        std::string s;
        for (const auto& i : issues) s += i.message + "\n";
        return s;
    }
};

Parsed parse(const std::string& text, const lf::Policy& policy = lf::Policy::standard()) {
    Parsed p;
    p.ok = lf::parseTweakDocument(text, "test.json", policy, p.defs, p.issues);
    return p;
}

// 1 つのフィールドを壊した tweak が拒否されることを確認する補助。
Parsed parseWith(const std::function<void(json&)>& mutate) {
    json t = validTweak();
    mutate(t);
    return parse(doc({t}));
}

}  // namespace

TEST(TweakSchema, AcceptsAValidDefinition) {
    auto p = parse(doc({validTweak()}));
    ASSERT_TRUE(p.ok) << p.allMessages();
    ASSERT_EQ(p.defs.size(), 1u);
    const auto& d = p.defs[0];
    EXPECT_EQ(d.id, "kernel.example");
    EXPECT_EQ(d.category, "general");
    EXPECT_EQ(d.target.valueName, "TimerCheckFlags");
    EXPECT_EQ(d.data, lf::RegValue::dword(0));
    EXPECT_EQ(d.title.ja, "題");
    EXPECT_TRUE(d.requiresReboot);
    EXPECT_EQ(d.minBuild, lf::kMinSupportedBuild);
    EXPECT_TRUE(d.supportsBuild(22631));
}

TEST(TweakSchema, ParsesStringAndQwordTypes) {
    json s = validTweak("kernel.str");
    s["type"] = "REG_SZ";
    s["data"] = "hello";
    json q = validTweak("kernel.qw");
    q["type"] = "REG_QWORD";
    q["data"] = 4294967296ull;
    auto p = parse(doc({s, q}));
    ASSERT_TRUE(p.ok) << p.allMessages();
    EXPECT_EQ(p.defs[0].data, lf::RegValue::string("hello"));
    EXPECT_EQ(p.defs[1].data, lf::RegValue::qword(4294967296ull));
}

TEST(TweakSchema, RejectsStructuralProblems) {
    EXPECT_FALSE(parse("{not json").ok);
    EXPECT_FALSE(parse("[]").ok);
    EXPECT_FALSE(parse(R"({"schema":2,"tweaks":[]})").ok);
    EXPECT_FALSE(parse(R"({"schema":1})").ok);
    EXPECT_FALSE(parse(R"({"schema":1,"tweaks":{}})").ok);
    EXPECT_FALSE(parse(R"({"schema":1,"tweaks":[],"extra":1})").ok);
}

TEST(TweakSchema, RejectsMissingRequiredFields) {
    for (const char* field : {"id", "path", "value", "type", "data", "title", "summary", "risk", "requiresReboot"}) {
        auto p = parseWith([&](json& t) { t.erase(field); });
        EXPECT_FALSE(p.ok) << "missing " << field;
    }
}

TEST(TweakSchema, RejectsUnknownFields) {
    auto p = parseWith([](json& t) { t["tpyo"] = 1; });
    EXPECT_FALSE(p.ok);
    EXPECT_NE(p.allMessages().find("unknown field 'tpyo'"), std::string::npos);
}

TEST(TweakSchema, RejectsBadIds) {
    for (const char* id : {"", "nodot", "Upper.case", "a..b", ".a.b", "a.b.", "a.1b", "kernel.has space", "kernel.x-y"}) {
        auto p = parseWith([&](json& t) { t["id"] = id; });
        EXPECT_FALSE(p.ok) << "id '" << id << "'";
    }
}

TEST(TweakSchema, RejectsDuplicateIdsInOneDocument) {
    auto p = parse(doc({validTweak("kernel.same"), validTweak("kernel.same")}));
    EXPECT_FALSE(p.ok);
    EXPECT_TRUE(p.defs.empty());
}

TEST(TweakSchema, RejectsBadTypesAndData) {
    EXPECT_FALSE(parseWith([](json& t) { t["type"] = "REG_BINARY"; }).ok);
    EXPECT_FALSE(parseWith([](json& t) { t["type"] = "reg_dword"; }).ok);
    EXPECT_FALSE(parseWith([](json& t) { t["data"] = -1; }).ok);
    EXPECT_FALSE(parseWith([](json& t) { t["data"] = 4294967296ull; }).ok);  // DWORD 範囲外
    EXPECT_FALSE(parseWith([](json& t) { t["data"] = 1.5; }).ok);
    EXPECT_FALSE(parseWith([](json& t) { t["data"] = "1"; }).ok);           // DWORD に文字列
    EXPECT_FALSE(parseWith([](json& t) { t["type"] = "REG_SZ"; t["data"] = 1; }).ok);
}

TEST(TweakSchema, RejectsBadRiskRebootAndBuilds) {
    EXPECT_FALSE(parseWith([](json& t) { t["risk"] = "high"; }).ok);
    EXPECT_FALSE(parseWith([](json& t) { t["risk"] = "Low"; }).ok);
    EXPECT_FALSE(parseWith([](json& t) { t["requiresReboot"] = "yes"; }).ok);
    EXPECT_FALSE(parseWith([](json& t) { t["minBuild"] = 10240; }).ok);                 // 1809 未満
    EXPECT_FALSE(parseWith([](json& t) { t["minBuild"] = 22000; t["maxBuild"] = 19041; }).ok);
    EXPECT_FALSE(parseWith([](json& t) { t["minBuild"] = "19041"; }).ok);
    EXPECT_TRUE(parseWith([](json& t) { t["minBuild"] = 19041; t["maxBuild"] = 26100; t["risk"] = "medium"; }).ok);
}

TEST(TweakSchema, RejectsIncompleteLocalizedText) {
    EXPECT_FALSE(parseWith([](json& t) { t["title"] = {{"ja", "x"}}; }).ok);             // en が無い
    EXPECT_FALSE(parseWith([](json& t) { t["title"] = {{"ja", ""}, {"en", "x"}}; }).ok);  // 空
    EXPECT_FALSE(parseWith([](json& t) { t["summary"] = "plain string"; }).ok);
    EXPECT_FALSE(parseWith([](json& t) { t["note"] = {{"ja", "a"}, {"en", "b"}, {"fr", "c"}}; }).ok);
}

TEST(TweakSchema, RejectsBadPathsAndValueNames) {
    EXPECT_FALSE(parseWith([](json& t) { t["path"] = "SYSTEM\\foo"; }).ok);
    EXPECT_FALSE(parseWith([](json& t) { t["path"] = std::string(kKernelKey) + "\\..\\Lsa"; }).ok);
    EXPECT_FALSE(parseWith([](json& t) { t["value"] = "a\\b"; }).ok);
    EXPECT_FALSE(parseWith([](json& t) { t["value"] = ""; }).ok);
}

// ---- ポリシー連動 ---------------------------------------------------------------------------------

TEST(TweakSchema, RejectsPathsOutsideTheAllowList) {
    auto p = parseWith([](json& t) { t["path"] = "HKLM\\SOFTWARE\\Some\\Other\\Key"; });
    EXPECT_FALSE(p.ok);
    EXPECT_NE(p.allMessages().find("not in allow list"), std::string::npos);
}

TEST(TweakSchema, RejectsSecurityKeysEvenIfSyntacticallyValid) {
    auto p = parseWith([](json& t) {
        t["path"] = "HKLM\\SOFTWARE\\Microsoft\\Windows Defender";
        t["value"] = "DisableAntiSpyware";
    });
    EXPECT_FALSE(p.ok);
    EXPECT_NE(p.allMessages().find("denied"), std::string::npos);
}

// 要件: SEHOP 無効化と CFG 弱体化の値は、定義ファイルに書いてもロードされない。
TEST(TweakSchema, RejectsSehopAndCfgWeakeningDefinitions) {
    for (const char* name : {"DisableExceptionChainValidation", "DisableControlFlowGuardExportSuppression"}) {
        json t = validTweak("kernel.forbidden");
        t["value"] = name;
        t["data"] = 1;
        auto p = parse(doc({t}));
        EXPECT_FALSE(p.ok) << name << " must never load";
        EXPECT_TRUE(p.defs.empty());
        EXPECT_NE(p.allMessages().find("denied"), std::string::npos) << p.allMessages();

        // 広い許可リストを与えても拒否される (組み込みの拒否リスト)。
        auto wide = parse(doc({t}), lf::Policy::withAllowList({"HKLM\\SYSTEM"}));
        EXPECT_FALSE(wide.ok) << name << " (wide allow list)";
    }
}

TEST(TweakSchema, OneBadEntryRejectsTheWholeDocument) {
    json bad = validTweak("kernel.bad");
    bad["value"] = "DisableExceptionChainValidation";
    auto p = parse(doc({validTweak("kernel.good"), bad}));
    EXPECT_FALSE(p.ok);
    EXPECT_TRUE(p.defs.empty());  // 良い方も取り込まれない
}

// ---- カタログ -------------------------------------------------------------------------------------

TEST(TweakCatalog, DuplicateIdAcrossDocumentsRejectsTheLaterDocument) {
    lf::TweakCatalog cat(lf::Policy::standard());
    std::vector<lf::DefinitionIssue> issues;
    EXPECT_TRUE(cat.loadString(doc({validTweak("kernel.a")}), "one.json", issues));
    EXPECT_FALSE(cat.loadString(doc({validTweak("kernel.b"), validTweak("kernel.a")}), "two.json", issues));
    EXPECT_EQ(cat.all().size(), 1u);
    EXPECT_EQ(cat.find("kernel.b"), nullptr);
    EXPECT_NE(cat.find("kernel.a"), nullptr);
}

// 同梱の data/tweaks/*.json: 6 値すべてが読み込め、禁止値は含まれない。
TEST(TweakCatalog, ShippedKernelDefinitionsLoad) {
    lf::TweakCatalog cat(lf::Policy::standard());
    auto issues = cat.loadDirectory(std::filesystem::path(LF_SOURCE_DIR) / "data" / "tweaks");
    for (const auto& i : issues) ADD_FAILURE() << i.source << " " << i.tweakId << ": " << i.message;

    const std::vector<std::pair<std::string, uint32_t>> expected = {{"TimerCheckFlags", 0},
                                                                    {"SerializeTimerExpiration", 1},
                                                                    {"EnablePerCpuClockTickScheduling", 1},
                                                                    {"GlobalTimerResolutionRequests", 0},
                                                                    {"DriveRemappingMitigation", 0},
                                                                    {"EnableWerUserReporting", 0}};
    ASSERT_EQ(cat.all().size(), expected.size());
    for (const auto& [name, data] : expected) {
        auto it = std::find_if(cat.all().begin(), cat.all().end(), [&](const lf::TweakDef& d) { return d.target.valueName == name; });
        ASSERT_NE(it, cat.all().end()) << name;
        EXPECT_EQ(it->data, lf::RegValue::dword(data)) << name;
        EXPECT_EQ(it->target.subkey, "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\kernel");
        EXPECT_TRUE(it->requiresReboot);
        EXPECT_FALSE(it->note.ja.empty());  // 「効果はビルド依存」の注記
        EXPECT_FALSE(it->note.en.empty());
    }
    for (const auto& d : cat.all()) {
        EXPECT_NE(d.target.valueName, "DisableExceptionChainValidation");
        EXPECT_NE(d.target.valueName, "DisableControlFlowGuardExportSuppression");
    }
}
