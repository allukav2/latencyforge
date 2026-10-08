#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "lf/log.hpp"

namespace {
int g_tick = 0;
std::string fakeClock() { return "T" + std::to_string(++g_tick); }
}  // namespace

TEST(Logger, KeepsOnlyTheNewestEntries) {
    lf::Logger log(3, fakeClock);
    for (int i = 0; i < 5; ++i) log.info("c", "m" + std::to_string(i));
    auto s = log.snapshot();
    ASSERT_EQ(s.size(), 3u);
    EXPECT_EQ(s.front().message, "m2");
    EXPECT_EQ(s.back().message, "m4");
}

TEST(Logger, MinLevelFiltersLowerLevels) {
    lf::Logger log(10, fakeClock);
    log.setMinLevel(lf::LogLevel::Warn);
    log.debug("c", "d");
    log.info("c", "i");
    log.warn("c", "w");
    log.error("c", "e");
    EXPECT_EQ(log.snapshot().size(), 2u);
}

TEST(Logger, ExportContainsLevelCategoryMessageAndDetail) {
    lf::Logger log(10, [] { return std::string("2026-01-01T00:00:00Z"); });
    log.error("engine", "boom", "HRESULT 5");
    const std::string text = log.exportText();
    EXPECT_NE(text.find("2026-01-01T00:00:00Z [ERROR] engine: boom | HRESULT 5"), std::string::npos);
}

TEST(Logger, WritesToFileAndRotates) {
    auto dir = std::filesystem::temp_directory_path() / "lf_tests_log";
    std::filesystem::remove_all(dir);
    lf::Logger log(10, fakeClock);
    log.setFile(dir / "app.log", /*maxBytes=*/200);
    for (int i = 0; i < 20; ++i) log.info("cat", "message number " + std::to_string(i));
    EXPECT_TRUE(std::filesystem::exists(dir / "app.log"));
    EXPECT_TRUE(std::filesystem::exists(dir / "app.log.1"));
    std::ifstream f(dir / "app.log");
    std::stringstream ss;
    ss << f.rdbuf();
    EXPECT_NE(ss.str().find("message number 19"), std::string::npos);
}

TEST(Logger, ClearEmptiesBuffer) {
    lf::Logger log(10, fakeClock);
    log.info("c", "x");
    log.clear();
    EXPECT_TRUE(log.snapshot().empty());
}
