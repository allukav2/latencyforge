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
