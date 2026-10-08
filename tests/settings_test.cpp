#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "lf/settings.hpp"

namespace {
std::filesystem::path tempFile(const char* name) {
    auto dir = std::filesystem::temp_directory_path() / "lf_tests";
    std::filesystem::create_directories(dir);
    return dir / name;
}
}  // namespace

TEST(Settings, RoundTrip) {
    auto p = tempFile("settings_rt.json");
    lf::Settings a;
    a.language = "ja";
    a.accentRgb = 0x12AB34;
    a.reduceMotion = true;
    ASSERT_TRUE(a.saveFile(p));

    lf::Settings b;
    ASSERT_TRUE(b.loadFile(p));
    EXPECT_EQ(b.language, "ja");
    EXPECT_EQ(b.accentRgb, 0x12AB34u);
    EXPECT_TRUE(b.reduceMotion);
}

TEST(Settings, DisclaimerAcceptanceIsPersistedAndValidated) {
    lf::Settings fresh;
    EXPECT_EQ(fresh.acceptedDisclaimer, 0) << "first run: the wizard must be shown";

    auto p = tempFile("settings_disclaimer.json");
    lf::Settings a;
    a.acceptedDisclaimer = lf::kDisclaimerVersion;
    ASSERT_TRUE(a.saveFile(p));
    lf::Settings b;
    ASSERT_TRUE(b.loadFile(p));
    EXPECT_EQ(b.acceptedDisclaimer, lf::kDisclaimerVersion);

    std::ofstream(p) << R"({"acceptedDisclaimer":"yes"})";
    lf::Settings c;
    ASSERT_TRUE(c.loadFile(p));
    EXPECT_EQ(c.acceptedDisclaimer, 0) << "a non-integer value must not count as consent";
    std::ofstream(p) << R"({"acceptedDisclaimer":-5})";
    lf::Settings d;
    ASSERT_TRUE(d.loadFile(p));
    EXPECT_EQ(d.acceptedDisclaimer, 0);
}

TEST(Settings, MissingFileKeepsDefaults) {
    lf::Settings s;
    EXPECT_FALSE(s.loadFile(tempFile("does_not_exist.json")));
    EXPECT_EQ(s.language, "auto");
}

TEST(Settings, InvalidFieldsIgnored) {
    auto p = tempFile("settings_bad.json");
    std::ofstream(p) << R"({"language":"xx","accent":"red","reduceMotion":"yes"})";
    lf::Settings s;
    ASSERT_TRUE(s.loadFile(p));
    EXPECT_EQ(s.language, "auto");
    EXPECT_EQ(s.accentRgb, 0x5B8CFFu);
    EXPECT_FALSE(s.reduceMotion);
}

TEST(Settings, CorruptFileRejected) {
    auto p = tempFile("settings_corrupt.json");
    std::ofstream(p) << "{{{";
    lf::Settings s;
    EXPECT_FALSE(s.loadFile(p));
}
