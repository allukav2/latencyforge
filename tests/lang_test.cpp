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

// すべての ErrorCode に、ユーザー向けメッセージ (error.*) が両言語で用意されていること。
TEST(Lang, EveryErrorCodeHasAUserMessage) {
    const auto ja = load("ja.json");
    const auto en = load("en.json");
    for (int i = 0; i <= static_cast<int>(lf::ErrorCode::RestorePointFailed); ++i) {
        const char* key = lf::errorKey(static_cast<lf::ErrorCode>(i));
        EXPECT_TRUE(ja.has(key)) << "ja missing " << key;
        EXPECT_TRUE(en.has(key)) << "en missing " << key;
    }
}
