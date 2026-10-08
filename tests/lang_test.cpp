#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <iterator>

#include "lf/i18n.hpp"
#include "lf/result.hpp"
#include "lf/sysinfo.hpp"

namespace {

lf::Translator load(const char* name) {
    lf::Translator t;
    std::string err;
    const auto path = std::filesystem::path(LF_SOURCE_DIR) / "data" / "lang" / name;
    EXPECT_TRUE(t.loadFile(path, &err)) << path.string() << ": " << err;
    return t;
}

}  // namespace

// 翻訳漏れ (片方の言語にだけキーがある) を CI で検出する。
TEST(Lang, JapaneseAndEnglishHaveTheSameKeys) {
    const auto ja = load("ja.json").keys();
    const auto en = load("en.json").keys();
    std::vector<std::string> onlyJa, onlyEn;
    std::set_difference(ja.begin(), ja.end(), en.begin(), en.end(), std::back_inserter(onlyJa));
    std::set_difference(en.begin(), en.end(), ja.begin(), ja.end(), std::back_inserter(onlyEn));
    for (const auto& k : onlyJa) ADD_FAILURE() << "key only in ja.json: " << k;
    for (const auto& k : onlyEn) ADD_FAILURE() << "key only in en.json: " << k;
    EXPECT_GT(ja.size(), 50u);
}

TEST(Lang, NoValueIsEmpty) {
    for (const char* file : {"ja.json", "en.json"}) {
        auto t = load(file);
        for (const auto& k : t.keys()) EXPECT_STRNE(t.tr(k), "") << file << ": " << k;
    }
}

// 機能の無効理由・互換性警告のキーが、両言語に存在すること (ツールチップが生のキーで表示されないように)。
TEST(Lang, EverySystemMessageKeyExistsInBothLanguages) {
    const auto ja = load("ja.json");
    const auto en = load("en.json");
    for (const auto& key : lf::allSystemMessageKeys()) {
        EXPECT_TRUE(ja.has(key)) << "ja missing " << key;
        EXPECT_TRUE(en.has(key)) << "en missing " << key;
    }
    // 理由文は、フォーマット引数 ({0}, {1}) を使うキーに対して、両言語で同じ数の引数を持つこと。
    for (const auto& key : lf::allSystemMessageKeys()) {
        for (const char* ph : {"{0}", "{1}"})
            EXPECT_EQ(std::string(ja.tr(key)).find(ph) != std::string::npos, std::string(en.tr(key)).find(ph) != std::string::npos)
                << key << " " << ph;
    }
}

// 準備中ページ (Affinity / USB / GPU / ベンチ / バックアップ / ログ) の文言キーが揃っていること。
// (キーの組み立てを誤ると、画面に "placeholder.nav.gpu.subtitle" のような生のキーが出る。)
TEST(Lang, EveryPlaceholderPageHasItsTexts) {
    for (const char* file : {"ja.json", "en.json"}) {
        const auto tr = load(file);
        for (const char* page : {"affinity", "usb", "gpu", "bench", "backup", "log"}) {
            EXPECT_TRUE(tr.has(std::string("nav.") + page)) << file << " nav." << page;
            EXPECT_TRUE(tr.has(std::string("placeholder.") + page + ".subtitle")) << file << " " << page;
            EXPECT_TRUE(tr.has(std::string("placeholder.") + page + ".desc")) << file << " " << page;
        }
    }
}

// Affinity ページと AffinityManager が使うキー (note キーは Manager が状態として返す) が両言語にあること。
TEST(Lang, EveryAffinityKeyUsedByTheCodeExistsInBothLanguages) {
    const char* keys[] = {
        "affinity.subtitle", "affinity.antiCheatTitle", "affinity.antiCheatBody", "affinity.enable", "affinity.enableDesc",
        "affinity.state.disabled", "affinity.state.idle", "affinity.state.idleDesc", "affinity.state.active", "affinity.state.activeFmt",
        "affinity.state.noEffect", "affinity.state.blocked", "affinity.managedFmt", "affinity.planTitle", "affinity.plan.perf",
        "affinity.plan.l3", "affinity.plan.reserve", "affinity.plan.noneSimple", "affinity.plan.noneTooFew",
        "affinity.plan.noneNotApplicable", "affinity.legendGame", "affinity.legendBackground", "affinity.restoreNow",
        "affinity.nothingToRestore", "affinity.demoStart", "affinity.demoStop", "affinity.profiles", "affinity.noProfiles",
        "affinity.strategyLabel", "affinity.strategy.auto", "affinity.strategy.performance", "affinity.strategy.largest",
        "affinity.strategy.reserve", "affinity.priority.unchanged", "affinity.priority.aboveNormal", "affinity.priority.high",
        "affinity.priorityNote", "affinity.smtSiblings", "affinity.moveBackground", "affinity.remove", "affinity.addRunning",
        "affinity.addManual", "affinity.exeHint", "affinity.add", "affinity.pickerTitle", "affinity.pickerHint",
        "affinity.pickerFilter", "affinity.pickerEmpty", "affinity.pickerNone", "affinity.err.invalid", "affinity.err.protected",
        "affinity.err.duplicate", "affinity.note.blockedOpen", "affinity.note.blockedProtected", "affinity.note.blockedGroup",
        "affinity.note.blockedOverlap", "affinity.note.blockedJournal", "affinity.note.noEffectSimple", "affinity.note.noEffectOther"};
    for (const char* file : {"ja.json", "en.json"}) {
        const auto tr = load(file);
        for (const char* k : keys) EXPECT_TRUE(tr.has(k)) << file << " missing " << k;
    }
}

// アンチチートに関する注意が、ページに必ず書かれていること (UI の明記要件)。
TEST(Lang, TheAntiCheatWarningNamesTheCommonAntiCheatSystems) {
    for (const char* file : {"ja.json", "en.json"}) {
        const auto tr = load(file);
        const std::string body = tr.tr("affinity.antiCheatBody");
        EXPECT_NE(body.find("Vanguard"), std::string::npos) << file;
        EXPECT_NE(body.find("EasyAntiCheat"), std::string::npos) << file;
        EXPECT_NE(body.find("BattlEye"), std::string::npos) << file;
    }
}

// すべての ErrorCode に、ユーザー向けメッセージ (error.*) が両言語で用意されていること。
TEST(Lang, EveryErrorCodeHasAUserMessage) {
    const auto ja = load("ja.json");
    const auto en = load("en.json");
    for (int i = 0; i <= static_cast<int>(lf::ErrorCode::ProcessChanged); ++i) {  // ProcessChanged = 最後のコード
        const char* key = lf::errorKey(static_cast<lf::ErrorCode>(i));
        EXPECT_TRUE(ja.has(key)) << "ja missing " << key;
        EXPECT_TRUE(en.has(key)) << "en missing " << key;
    }
}
