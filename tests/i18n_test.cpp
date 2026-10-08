#include <gtest/gtest.h>

#include <string>

#include "lf/i18n.hpp"

TEST(Translator, FlattensNestedKeys) {
    lf::Translator t;
    ASSERT_TRUE(t.loadString(R"({"nav":{"home":"Home","sub":{"x":"X"}},"top":"T"})"));
    EXPECT_STREQ(t.tr("nav.home"), "Home");
    EXPECT_STREQ(t.tr("nav.sub.x"), "X");
    EXPECT_STREQ(t.tr("top"), "T");
}

TEST(Translator, MissingKeyReturnsKey) {
    lf::Translator t;
    EXPECT_STREQ(t.tr("no.such.key"), "no.such.key");
    EXPECT_FALSE(t.has("no.such.key"));
}

TEST(Translator, LaterFileOverridesButKeepsFallback) {
    lf::Translator t;
    ASSERT_TRUE(t.loadString(R"({"a":"en-a","b":"en-b"})"));
    ASSERT_TRUE(t.loadString(R"({"a":"ja-a"})"));
    EXPECT_STREQ(t.tr("a"), "ja-a");
    EXPECT_STREQ(t.tr("b"), "en-b");
}

TEST(Translator, RejectsInvalidAndKeepsTable) {
    lf::Translator t;
    ASSERT_TRUE(t.loadString(R"({"a":"1"})"));
    std::string err;
    EXPECT_FALSE(t.loadString("{not json", &err));
    EXPECT_FALSE(err.empty());
    EXPECT_FALSE(t.loadString(R"({"a":{"b":5}})", &err));
    EXPECT_FALSE(t.loadString("[1,2]", &err));
    EXPECT_STREQ(t.tr("a"), "1");
}

TEST(Translator, Utf8Roundtrip) {
    lf::Translator t;
    // /utf-8 でコンパイルされるため通常リテラルが UTF-8 になる。
    ASSERT_TRUE(t.loadString(R"({"nav":{"home":"ホーム"}})"));
    EXPECT_STREQ(t.tr("nav.home"), "ホーム");
}

TEST(Translator, MissingFile) {
    lf::Translator t;
    std::string err;
    EXPECT_FALSE(t.loadFile("Z:/definitely/not/here.json", &err));
    EXPECT_FALSE(err.empty());
}
