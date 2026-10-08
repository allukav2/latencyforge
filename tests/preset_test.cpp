#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>

#include "lf/preset.hpp"
#include "lf/util.hpp"

namespace {

using nlohmann::json;

const char* kKernelKey = "HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\kernel";

json tweak(const std::string& id, const std::string& value, const char* risk = "low") {
    return {{"id", id},
            {"path", kKernelKey},
            {"value", value},
            {"type", "REG_DWORD"},
            {"data", 0},
            {"title", {{"ja", "題"}, {"en", "t"}}},
            {"summary", {{"ja", "概要"}, {"en", "s"}}},
            {"risk", risk},
            {"requiresReboot", true}};
}

// 小さなカタログ: a.one (low), a.two (low), a.risky (medium)
lf::TweakCatalog smallCatalog() {
    lf::TweakCatalog cat(lf::Policy::standard());
    std::vector<lf::DefinitionIssue> issues;
    json doc = {{"schema", 1},
                {"tweaks", json::array({tweak("a.one", "One"), tweak("a.two", "Two"), tweak("a.risky", "Risky", "medium")})}};
    EXPECT_TRUE(cat.loadString(doc.dump(), "cat.json", issues));
    return cat;
}

json preset(const std::string& id, std::vector<std::string> ids) {
    return {{"id", id},
            {"title", {{"ja", "題"}, {"en", "T"}}},
            {"description", {{"ja", "説明"}, {"en", "D"}}},
            {"tweaks", ids}};
}

std::string doc(std::initializer_list<json> presets) {
    json arr = json::array();
    for (const auto& p : presets) arr.push_back(p);
    return json{{"schema", 1}, {"presets", arr}}.dump();
}

struct Parsed {
    bool ok;
    std::vector<lf::PresetDef> presets;
    std::vector<lf::DefinitionIssue> issues;
};

Parsed parse(const std::string& text, const lf::TweakCatalog& cat) {
    Parsed p;
    p.ok = lf::parsePresetDocument(text, "presets.json", cat, p.presets, p.issues);
    return p;
}

}  // namespace

TEST(PresetSchema, AcceptsAValidDocumentAndResolvesTweaks) {
    auto cat = smallCatalog();
    auto p = parse(doc({preset("safe", {"a.one"}), preset("max", {"a.one", "a.two", "a.risky"})}), cat);
    ASSERT_TRUE(p.ok);
    ASSERT_EQ(p.presets.size(), 2u);
    auto tweaks = lf::resolvePreset(p.presets[1], cat);
    ASSERT_EQ(tweaks.size(), 3u);
    EXPECT_EQ(tweaks[2]->id, "a.risky");
    EXPECT_EQ(lf::presetRisk(p.presets[0], cat), lf::Risk::Low);
    EXPECT_EQ(lf::presetRisk(p.presets[1], cat), lf::Risk::Medium) << "one medium item makes the whole preset medium";
}

TEST(PresetSchema, RejectsUnknownTweakIds) {
    auto cat = smallCatalog();
    auto p = parse(doc({preset("safe", {"a.one", "a.nope"})}), cat);
    EXPECT_FALSE(p.ok);
    EXPECT_TRUE(p.presets.empty());
    ASSERT_FALSE(p.issues.empty());
    EXPECT_NE(p.issues[0].message.find("unknown tweak id"), std::string::npos);
}

TEST(PresetSchema, RejectsStructuralProblems) {
    auto cat = smallCatalog();
    EXPECT_FALSE(parse("{bad", cat).ok);
    EXPECT_FALSE(parse("[]", cat).ok);
    EXPECT_FALSE(parse(R"({"schema":2,"presets":[]})", cat).ok);
    EXPECT_FALSE(parse(R"({"schema":1})", cat).ok);
    EXPECT_FALSE(parse(R"({"schema":1,"presets":[],"extra":1})", cat).ok);
    EXPECT_FALSE(parse(doc({preset("safe", {})}), cat).ok) << "empty tweak list";
    EXPECT_FALSE(parse(doc({preset("safe", {"a.one", "a.one"})}), cat).ok) << "duplicate tweak id";
    EXPECT_FALSE(parse(doc({preset("Safe", {"a.one"})}), cat).ok) << "bad preset id";
    EXPECT_FALSE(parse(doc({preset("safe", {"a.one"}), preset("safe", {"a.two"})}), cat).ok) << "duplicate preset id";
    json bad = preset("safe", {"a.one"});
    bad["typo"] = 1;
    EXPECT_FALSE(parse(doc({bad}), cat).ok);
    json noEn = preset("safe", {"a.one"});
    noEn["title"] = {{"ja", "x"}};
    EXPECT_FALSE(parse(doc({noEn}), cat).ok);
}

TEST(PresetSchema, OneBadPresetRejectsTheWholeDocument) {
    auto cat = smallCatalog();
    auto p = parse(doc({preset("good", {"a.one"}), preset("bad", {"a.nope"})}), cat);
    EXPECT_FALSE(p.ok);
    EXPECT_TRUE(p.presets.empty());
}

// ---- 同梱の data/presets.json: 承認された内容どおりであること ---------------------------------------------

namespace {

struct Shipped {
    lf::TweakCatalog cat{lf::Policy::standard()};
    std::vector<lf::PresetDef> presets;
    std::vector<lf::DefinitionIssue> issues;
    Shipped() {
        const auto root = std::filesystem::path(LF_SOURCE_DIR) / "data";
        issues = cat.loadDirectory(root / "tweaks");
        auto text = lf::readFileText(root / "presets.json");
        EXPECT_TRUE(text.ok());
        if (text.ok()) EXPECT_TRUE(lf::parsePresetDocument(text.value(), "presets.json", cat, presets, issues));
    }
    const lf::PresetDef* find(const std::string& id) const {
        for (const auto& p : presets)
            if (p.id == id) return &p;
        return nullptr;
    }
    static bool has(const lf::PresetDef& p, const std::string& id) {
        return std::find(p.tweakIds.begin(), p.tweakIds.end(), id) != p.tweakIds.end();
    }
};

}  // namespace

TEST(ShippedPresets, LoadWithoutIssuesAndHaveTheThreeApprovedPresets) {
    Shipped s;
    for (const auto& i : s.issues) ADD_FAILURE() << i.source << " " << i.tweakId << ": " << i.message;
    ASSERT_EQ(s.presets.size(), 3u);
    ASSERT_NE(s.find("safe"), nullptr);
    ASSERT_NE(s.find("balanced"), nullptr);
    ASSERT_NE(s.find("max"), nullptr);
}

TEST(ShippedPresets, ContentsMatchTheApprovedProposal) {
    Shipped s;
    ASSERT_EQ(s.presets.size(), 3u);
    const auto& safe = *s.find("safe");
    const auto& balanced = *s.find("balanced");
    const auto& mx = *s.find("max");
    EXPECT_EQ(safe.tweakIds.size(), 2u);
    EXPECT_TRUE(Shipped::has(safe, "kernel.global_timer_resolution_requests"));
    EXPECT_TRUE(Shipped::has(safe, "kernel.serialize_timer_expiration"));
    EXPECT_EQ(balanced.tweakIds.size(), 4u);
    EXPECT_TRUE(Shipped::has(balanced, "kernel.timer_check_flags"));
    EXPECT_TRUE(Shipped::has(balanced, "kernel.per_cpu_clock_tick_scheduling"));
    EXPECT_EQ(mx.tweakIds.size(), 5u);
    EXPECT_TRUE(Shipped::has(mx, "kernel.drive_remapping_mitigation"));

    // 包含関係: 安全 ⊂ バランス ⊂ 最大
    for (const auto& id : safe.tweakIds) EXPECT_TRUE(Shipped::has(balanced, id)) << id;
    for (const auto& id : balanced.tweakIds) EXPECT_TRUE(Shipped::has(mx, id)) << id;
}

// 承認事項: wer_user_reporting はレイテンシへの効果が期待できないため、どのプリセットにも含めない。
TEST(ShippedPresets, WerUserReportingIsInNoPreset) {
    Shipped s;
    for (const auto& p : s.presets) EXPECT_FALSE(Shipped::has(p, "kernel.wer_user_reporting")) << p.id;
    EXPECT_NE(s.cat.find("kernel.wer_user_reporting"), nullptr) << "it must stay available as an individual toggle";
}

// 承認事項: リスク「中」を含むのは「最大」だけ (差分プレビューで強調表示される)。
TEST(ShippedPresets, OnlyMaxContainsAMediumRiskItem) {
    Shipped s;
    EXPECT_EQ(lf::presetRisk(*s.find("safe"), s.cat), lf::Risk::Low);
    EXPECT_EQ(lf::presetRisk(*s.find("balanced"), s.cat), lf::Risk::Low);
    EXPECT_EQ(lf::presetRisk(*s.find("max"), s.cat), lf::Risk::Medium);
}

// 承認事項: 各説明文に「効果はビルド依存」を明記し、「最速」と誤解させる表現を使わない。
TEST(ShippedPresets, DescriptionsStateBuildDependenceAndDoNotPromiseSpeed) {
    Shipped s;
    for (const auto& p : s.presets) {
        EXPECT_NE(p.description.ja.find("ビルド依存"), std::string::npos) << p.id << " (ja)";
        EXPECT_NE(p.description.en.find("build-dependent"), std::string::npos) << p.id << " (en)";
        for (const char* banned : {"最速", "最高速", "FPS", "fastest", "best performance", "maximum speed", "boost"}) {
            EXPECT_EQ(p.description.ja.find(banned), std::string::npos) << p.id << " ja contains " << banned;
            EXPECT_EQ(p.description.en.find(banned), std::string::npos) << p.id << " en contains " << banned;
        }
    }
    // 「最大」は、速度が最大という意味ではないことを明示する。
    const auto& mx = *s.find("max");
    EXPECT_NE(mx.description.ja.find("速度が最大になるという意味ではなく"), std::string::npos);
    EXPECT_NE(mx.description.en.find("does not mean the highest speed"), std::string::npos);
}
