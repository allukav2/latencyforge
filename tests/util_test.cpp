#include <gtest/gtest.h>

#include "lf/memory_registry.hpp"
#include "lf/util.hpp"

TEST(Iso8601, ParsesAndRoundTrips) {
    auto t = lf::parseIso8601Utc("2026-10-08T12:34:56Z");
    ASSERT_TRUE(t.has_value());
    const auto later = lf::parseIso8601Utc("2026-10-08T12:34:57Z");
    ASSERT_TRUE(later.has_value());
    EXPECT_EQ(std::chrono::duration_cast<std::chrono::seconds>(*later - *t).count(), 1);

    auto now = lf::parseIso8601Utc(lf::nowIso8601Utc());
    ASSERT_TRUE(now.has_value());
    EXPECT_LT(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - *now).count(), 5);
}

TEST(Iso8601, RejectsGarbage) {
    for (const char* s : {"", "yesterday", "2026-13-01T00:00:00Z", "2026-01-01", "2026-01-01T25:00:00Z", "1969-01-01T00:00:00Z",
                          "2026-01-01T00:00:00Zjunk"})
        EXPECT_FALSE(lf::parseIso8601Utc(s).has_value()) << s;
}

// 厳密な形式 "YYYY-MM-DDTHH:MM:SSZ" だけを受理する (状態ファイルの改変・破損を見逃さないため)。
TEST(Iso8601, RejectsTrailingCharactersAfterTheZ) {
    ASSERT_TRUE(lf::parseIso8601Utc("2026-01-01T00:00:00Z").has_value());
    EXPECT_FALSE(lf::parseIso8601Utc("2026-01-01T00:00:00Zjunk").has_value());
    EXPECT_FALSE(lf::parseIso8601Utc("2026-01-01T00:00:00ZZ").has_value());
    EXPECT_FALSE(lf::parseIso8601Utc("2026-01-01T00:00:00Z0").has_value());
}

TEST(Iso8601, RequiresTheTrailingZ) {
    EXPECT_FALSE(lf::parseIso8601Utc("2026-01-01T00:00:00").has_value());
    EXPECT_FALSE(lf::parseIso8601Utc("2026-01-01T00:00:00z").has_value()) << "lower-case z is not accepted";
    EXPECT_FALSE(lf::parseIso8601Utc("2026-01-01T00:00:00+09:00").has_value()) << "offsets are not accepted";
}

TEST(Iso8601, RejectsEmptyString) {
    EXPECT_FALSE(lf::parseIso8601Utc("").has_value());
    EXPECT_FALSE(lf::parseIso8601Utc("Z").has_value());
}

TEST(Iso8601, RejectsSurroundingWhitespace) {
    EXPECT_FALSE(lf::parseIso8601Utc(" 2026-01-01T00:00:00Z").has_value()) << "leading space";
    EXPECT_FALSE(lf::parseIso8601Utc("2026-01-01T00:00:00Z ").has_value()) << "trailing space";
    EXPECT_FALSE(lf::parseIso8601Utc("\t2026-01-01T00:00:00Z").has_value()) << "leading tab";
    EXPECT_FALSE(lf::parseIso8601Utc("2026-01-01T00:00:00Z\n").has_value()) << "trailing newline";
    EXPECT_FALSE(lf::parseIso8601Utc("2026-01-01 T00:00:00Z").has_value()) << "inner space";
    EXPECT_FALSE(lf::parseIso8601Utc("2026- 1-01T00:00:00Z").has_value()) << "space inside a number";
}

TEST(Iso8601, RejectsSignsAndMalformedNumbers) {
    EXPECT_FALSE(lf::parseIso8601Utc("+2026-01-01T00:00:00Z").has_value());
    EXPECT_FALSE(lf::parseIso8601Utc("2026-+1-01T00:00:00Z").has_value());
    EXPECT_FALSE(lf::parseIso8601Utc("2026-01-01T00:00:-5Z").has_value());
}

TEST(MemoryRegistry, BehavesLikeTheRealOne) {
    lf::MemoryRegistry reg;
    auto p = lf::parseRegPath("HKLM\\SYSTEM\\Foo", "Bar").value();
    auto same = lf::parseRegPath("hklm\\system\\FOO", "bar");  // ハイブは大文字のみ受理される
    EXPECT_FALSE(same.ok());

    EXPECT_FALSE(reg.read(p).value().has_value());
    ASSERT_TRUE(reg.write(p, lf::RegValue::dword(3)).ok());
    EXPECT_EQ(*reg.read(p).value(), lf::RegValue::dword(3));
    auto p2 = lf::parseRegPath("HKLM\\SYSTEM\\FOO", "BAR").value();  // キー/値名は大文字小文字無視
    EXPECT_EQ(*reg.read(p2).value(), lf::RegValue::dword(3));
    ASSERT_TRUE(reg.deleteValue(p).ok());
    EXPECT_TRUE(reg.deleteValue(p).ok());
    EXPECT_FALSE(reg.read(p).value().has_value());
}
