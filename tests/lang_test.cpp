#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <iterator>

#include "lf/i18n.hpp"
#include "lf/result.hpp"

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
