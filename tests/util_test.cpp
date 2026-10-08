#include <gtest/gtest.h>

#include "lf/memory_registry.hpp"
#include "lf/util.hpp"

TEST(Iso8601, ParsesAndRoundTrips) {
    auto t = lf::parseIso8601Utc("2026-10-08T12:34:56Z");
    ASSERT_TRUE(t.has_value());
    const auto later = lf::parseIso8601Utc("2026-10-08T12:34:57Z");
    ASSERT_TRUE(later.has_value());
    EXPECT_EQ(std::chrono::duration_cast<std::chrono::seconds>(*later - *t).count(), 1);
    EXPECT_TRUE(lf::parseIso8601Utc("2026-10-08T12:34:56").has_value()) << "trailing Z is optional";

    auto now = lf::parseIso8601Utc(lf::nowIso8601Utc());
    ASSERT_TRUE(now.has_value());
    EXPECT_LT(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - *now).count(), 5);
}

TEST(Iso8601, RejectsGarbage) {
    for (const char* s : {"", "yesterday", "2026-13-01T00:00:00Z", "2026-01-01", "2026-01-01T25:00:00Z", "1969-01-01T00:00:00Z",
                          "2026-01-01T00:00:00Zjunk"})
        EXPECT_FALSE(lf::parseIso8601Utc(s).has_value()) << s;
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
