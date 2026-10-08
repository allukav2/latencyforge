#include <gtest/gtest.h>

#include <algorithm>
#include <bit>

#include "lf/affinity_plan.hpp"
#include "lf/sysinfo.hpp"

namespace {

lf::CpuTopology cpuOf(const char* name) {
    auto probe = lf::makeSampleProbe(name);
    EXPECT_NE(probe, nullptr) << name;
    return probe ? lf::detectSystem(*probe).cpu : lf::CpuTopology{};
}

constexpr uint64_t bits(int first, int count) { return ((count >= 64 ? ~0ull : ((1ull << count) - 1ull))) << first; }

// 計画の不変条件 (すべての構成・すべての戦略で成り立つこと)
void expectSane(const lf::CpuTopology& t, const lf::AffinityPlan& p, const std::string& label) {
    if (!p.effective()) {
        EXPECT_EQ(p.gameMask, 0u) << label;
        EXPECT_EQ(p.backgroundMask, 0u) << label;
        return;
    }
    EXPECT_NE(p.gameMask, 0u) << label;
    EXPECT_EQ(p.gameMask & p.backgroundMask, 0u) << label << ": game and background must not overlap";
    EXPECT_FALSE(p.gameCores.empty()) << label;
    // すべてのコアが、計画のプロセッサグループに属し、マスクはそのコアの論理プロセッサだけを含む
    uint64_t allowed = 0;
    for (int i : p.gameCores) {
        EXPECT_EQ(t.cores[static_cast<size_t>(i)].group, p.group) << label;
        allowed |= t.cores[static_cast<size_t>(i)].mask;
    }
    for (int i : p.backgroundCores) {
        EXPECT_EQ(t.cores[static_cast<size_t>(i)].group, p.group) << label;
        allowed |= t.cores[static_cast<size_t>(i)].mask;
    }
    EXPECT_EQ(p.gameMask & ~allowed, 0u) << label;
    EXPECT_EQ(p.backgroundMask & ~allowed, 0u) << label;
    // ゲームのコアとバックグラウンドのコアは重ならない
    for (int g : p.gameCores) EXPECT_EQ(std::count(p.backgroundCores.begin(), p.backgroundCores.end(), g), 0) << label;
}

}  // namespace

// ---- Intel ハイブリッド ---------------------------------------------------------------------------

TEST(AffinityPlan, IntelHybridPutsTheGameOnPCoresAndBackgroundOnECores) {
    const auto t = cpuOf("intel-hybrid");  // 8P(SMT, 論理 0-15) + 8E (論理 16-23)
    const auto p = lf::planAffinity(t, {});
    ASSERT_TRUE(p.effective());
    EXPECT_EQ(p.kind, lf::PlanKind::PerformanceCores);
    EXPECT_EQ(p.group, 0);
    EXPECT_EQ(p.gameMask, bits(0, 16));
    EXPECT_EQ(p.backgroundMask, bits(16, 8));
    EXPECT_EQ(p.gameCores.size(), 8u);
    EXPECT_EQ(p.backgroundCores.size(), 8u);
    expectSane(t, p, "intel-hybrid");
}

TEST(AffinityPlan, LaptopHybridSixPAndEightE) {
    const auto t = cpuOf("laptop-hybrid");  // 6P(論理 0-11) + 8E (論理 12-19)
    const auto p = lf::planAffinity(t, {});
    EXPECT_EQ(p.gameMask, bits(0, 12));
    EXPECT_EQ(p.backgroundMask, bits(12, 8));
    expectSane(t, p, "laptop-hybrid");
}

TEST(AffinityPlan, HybridGameCanSkipSmtSiblings) {
    const auto t = cpuOf("intel-hybrid");
    lf::AffinityOptions o;
    o.gameUsesSmtSiblings = false;
    const auto p = lf::planAffinity(t, o);
    ASSERT_TRUE(p.effective());
    EXPECT_EQ(p.gameMask, 0x5555u) << "only the first thread of each P-core: bits 0,2,4,...,14";
    EXPECT_EQ(std::popcount(p.gameMask), 8);
    EXPECT_EQ(p.backgroundMask, bits(16, 8)) << "background is unaffected";
    EXPECT_EQ(p.gameMask & p.backgroundMask, 0u);
}

TEST(AffinityPlan, ThreeEfficiencyClassesTreatLowPowerCoresAsBackground) {
    lf::RawCpu raw;
    raw.ok = true;
    raw.vendor = "GenuineIntel";
    raw.cores.push_back({{{0, 0b11}}, 2, true});  // P
    raw.cores.push_back({{{0, 0b100}}, 1, false});  // E
    raw.cores.push_back({{{0, 0b1000}}, 0, false});  // LP-E
    raw.caches.push_back({3, 8ull << 20, {{0, 0b1111}}});
    const auto t = lf::analyzeCpu(raw);
    const auto p = lf::planAffinity(t, {});
    EXPECT_EQ(p.gameMask, 0b11u);
    EXPECT_EQ(p.backgroundMask, 0b1100u);
}

// ---- AMD マルチ CCD / CCX -------------------------------------------------------------------------

TEST(AffinityPlan, DualCcdWithVCachePutsTheGameOnTheLargerCacheCcd) {
    const auto t = cpuOf("amd-dual-ccd");  // CCD0=96MB (論理 0-15)、CCD1=32MB (論理 16-31)
    const auto p = lf::planAffinity(t, {});
    ASSERT_TRUE(p.effective());
    EXPECT_EQ(p.kind, lf::PlanKind::L3Groups);
    EXPECT_EQ(p.gameMask, bits(0, 16));
    EXPECT_EQ(p.backgroundMask, bits(16, 16));
    expectSane(t, p, "amd-dual-ccd");
}

TEST(AffinityPlan, DualCcdWhereTheSecondCcdHasTheLargerCachePicksTheSecondCcd) {
    auto t = cpuOf("amd-dual-ccd");
    std::swap(t.l3Groups[0].sizeBytes, t.l3Groups[1].sizeBytes);  // CCD0=32MB、CCD1=96MB にする
    const auto p = lf::planAffinity(t, {});
    ASSERT_TRUE(p.effective());
    EXPECT_EQ(p.gameMask, bits(16, 16)) << "the game goes to the CCD with the larger L3 (logical processors 16-31)";
    EXPECT_EQ(p.backgroundMask, bits(0, 16));
    expectSane(t, p, "swapped dual-ccd");
}

TEST(AffinityPlan, SymmetricDualCcdUsesTheFirstCcd) {
    const auto t = cpuOf("no-nvidia");  // 5900X 相当: 2 CCD × 6 コア、L3 は両方 32MB
    ASSERT_TRUE(t.multiL3);
    ASSERT_FALSE(t.asymmetricL3);
    const auto p = lf::planAffinity(t, {});
    ASSERT_TRUE(p.effective());
    // 1 つ目の CCD は 6 コア (>= kMinGameCores) なので、それだけで十分
    EXPECT_EQ(p.gameCores.size(), 6u);
    EXPECT_EQ(p.gameMask, bits(0, 12));
    EXPECT_EQ(p.backgroundMask, bits(12, 12));
    expectSane(t, p, "no-nvidia");
}

TEST(AffinityPlan, Zen2CcxGroupsAreMergedUntilEnoughCoresForTheGame) {
    const auto t = cpuOf("amd-zen2-multi-ccx");  // 4 CCX × 3 コア
    const auto p = lf::planAffinity(t, {});
    ASSERT_TRUE(p.effective());
    EXPECT_EQ(p.gameCores.size(), 6u) << "two 3-core CCX are merged to reach kMinGameCores";
    EXPECT_EQ(p.gameMask, bits(0, 12));
    EXPECT_EQ(p.backgroundMask, bits(12, 12));
    expectSane(t, p, "zen2");
}

TEST(AffinityPlan, ThreadripperPlansWithinASingleProcessorGroup) {
    const auto t = cpuOf("threadripper");  // 128 スレッド = 2 グループ、CCX 16 個 (4 コアずつ)
    ASSERT_EQ(t.processorGroups, 2);
    const auto p = lf::planAffinity(t, {});
    ASSERT_TRUE(p.effective());
    EXPECT_EQ(p.group, 0);
    EXPECT_EQ(p.gameCores.size(), 8u) << "two 4-core CCX";
    EXPECT_EQ(p.gameMask, bits(0, 16));
    // バックグラウンドは同じグループ内の残り (論理 16-63) だけ。グループ 1 のコアは含めない。
    EXPECT_EQ(p.backgroundMask, bits(16, 48));
    expectSane(t, p, "threadripper");
}

// ---- 単純な構成 -----------------------------------------------------------------------------------

TEST(AffinityPlan, SimpleTopologiesAreLeftAloneByAuto) {
    for (const char* n : {"simple", "amd-single-ccd", "quad-nosmt", "vm", "ltsc", "server", "win7"}) {
        const auto t = cpuOf(n);
        const auto p = lf::planAffinity(t, {});
        EXPECT_FALSE(p.effective()) << n;
        EXPECT_EQ(p.reason, lf::PlanReason::SimpleTopology) << n;
        EXPECT_EQ(p.gameMask, 0u) << n;
    }
}

TEST(AffinityPlan, ReserveOneCoreLeavesCoreZeroToTheBackground) {
    const auto t = cpuOf("simple");  // 6 コア / 12 スレッド
    lf::AffinityOptions o;
    o.strategy = lf::AffinityStrategy::ReserveOneCore;
    const auto p = lf::planAffinity(t, o);
    ASSERT_TRUE(p.effective());
    EXPECT_EQ(p.kind, lf::PlanKind::ReserveCore);
    EXPECT_EQ(p.backgroundMask, 0b11u);
    EXPECT_EQ(p.gameMask, bits(2, 10));
    expectSane(t, p, "reserve-one");
}

TEST(AffinityPlan, ReserveOneCoreNeedsAtLeastFourPhysicalCores) {
    lf::RawCpu raw;
    raw.ok = true;
    raw.cores.push_back({{{0, 0b01}}, 0, false});
    raw.cores.push_back({{{0, 0b10}}, 0, false});
    raw.cores.push_back({{{0, 0b100}}, 0, false});
    raw.caches.push_back({3, 4ull << 20, {{0, 0b111}}});
    lf::AffinityOptions o;
    o.strategy = lf::AffinityStrategy::ReserveOneCore;
    const auto p = lf::planAffinity(lf::analyzeCpu(raw), o);
    EXPECT_FALSE(p.effective());
    EXPECT_EQ(p.reason, lf::PlanReason::TooFewCores);
}

TEST(AffinityPlan, ForcedStrategiesThatDoNotFitTheCpuDoNothing) {
    lf::AffinityOptions pc;
    pc.strategy = lf::AffinityStrategy::PerformanceCores;
    EXPECT_FALSE(lf::planAffinity(cpuOf("simple"), pc).effective());
    EXPECT_EQ(lf::planAffinity(cpuOf("simple"), pc).reason, lf::PlanReason::NotApplicable);
    EXPECT_FALSE(lf::planAffinity(cpuOf("amd-dual-ccd"), pc).effective());

    lf::AffinityOptions l3;
    l3.strategy = lf::AffinityStrategy::LargestL3;
    EXPECT_FALSE(lf::planAffinity(cpuOf("intel-hybrid"), l3).effective()) << "Intel hybrid has a single L3";
    EXPECT_FALSE(lf::planAffinity(cpuOf("simple"), l3).effective());
}

TEST(AffinityPlan, UnknownTopologyDoesNothing) {
    const auto p = lf::planAffinity(cpuOf("cpu-unknown"), {});
    EXPECT_FALSE(p.effective());
    EXPECT_EQ(p.reason, lf::PlanReason::UnknownTopology);
}

// ---- 全 16 構成 × 全戦略 の不変条件 ----------------------------------------------------------------

TEST(AffinityPlan, EverySampleSystemAndStrategyYieldsASanePlan) {
    const auto names = lf::sampleSystemNames();
    ASSERT_EQ(names.size(), 16u);
    int effective = 0;
    for (const auto& n : names) {
        const auto t = cpuOf(n.c_str());
        for (auto s : {lf::AffinityStrategy::Auto, lf::AffinityStrategy::PerformanceCores, lf::AffinityStrategy::LargestL3,
                       lf::AffinityStrategy::ReserveOneCore}) {
            for (bool smt : {true, false}) {
                lf::AffinityOptions o;
                o.strategy = s;
                o.gameUsesSmtSiblings = smt;
                const auto p = lf::planAffinity(t, o);
                expectSane(t, p, n + " / " + lf::strategyKey(s));
                if (p.effective()) ++effective;
            }
        }
    }
    EXPECT_GT(effective, 20) << "several samples must produce real plans";
}

TEST(AffinityPlan, StrategyNamesRoundTrip) {
    for (auto s : {lf::AffinityStrategy::Auto, lf::AffinityStrategy::PerformanceCores, lf::AffinityStrategy::LargestL3,
                   lf::AffinityStrategy::ReserveOneCore}) {
        lf::AffinityStrategy back{};
        ASSERT_TRUE(lf::parseStrategy(lf::strategyKey(s), back));
        EXPECT_EQ(back, s);
    }
    lf::AffinityStrategy x{};
    EXPECT_FALSE(lf::parseStrategy("turbo", x));
}
