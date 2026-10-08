#include <gtest/gtest.h>

#include <algorithm>
#include <set>

#include "lf/sysinfo.hpp"
#include "lf/tweak.hpp"
#include "lf/win_probe.hpp"

namespace {

constexpr uint64_t MB = 1024ull * 1024ull;

lf::SystemInfo sample(const char* name) {
    auto probe = lf::makeSampleProbe(name);
    EXPECT_NE(probe, nullptr) << name;
    return probe ? lf::detectSystem(*probe) : lf::SystemInfo{};
}

}  // namespace

// ---- CPU トポロジー -------------------------------------------------------------------------------

TEST(Topology, IntelHybridHasPAndECoresSharingOneL3) {
    const auto cpu = sample("intel-hybrid").cpu;  // 13700K 相当: 8P(SMT) + 8E
    ASSERT_TRUE(cpu.known);
    EXPECT_EQ(cpu.vendor, lf::CpuVendor::Intel);
    EXPECT_EQ(cpu.physicalCores, 16);
    EXPECT_EQ(cpu.logicalProcessors, 24);
    EXPECT_TRUE(cpu.smt);
    EXPECT_TRUE(cpu.hybrid);
    EXPECT_EQ(cpu.performanceCores, 8);
    EXPECT_EQ(cpu.efficiencyCores, 8);
    EXPECT_EQ(cpu.processorGroups, 1);
    ASSERT_EQ(cpu.l3Groups.size(), 1u);
    EXPECT_EQ(cpu.l3Groups[0].sizeBytes, 30 * MB);
    EXPECT_EQ(cpu.l3Groups[0].physical, 16);
    EXPECT_EQ(cpu.l3Groups[0].performance, 8);
    EXPECT_EQ(cpu.l3Groups[0].efficiency, 8);
    EXPECT_FALSE(cpu.multiL3);
    EXPECT_FALSE(cpu.simple) << "hybrid CPUs are not 'simple': placement matters";

    // 個々のコア: 先頭 8 つが P (論理 2)、残りが E (論理 1)
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(cpu.cores[i].kind, lf::CoreKind::Performance) << i;
        EXPECT_EQ(cpu.cores[i].logical, 2) << i;
    }
    for (int i = 8; i < 16; ++i) {
        EXPECT_EQ(cpu.cores[i].kind, lf::CoreKind::Efficiency) << i;
        EXPECT_EQ(cpu.cores[i].logical, 1) << i;
    }
}

TEST(Topology, AmdDualCcdHasTwoAsymmetricL3Groups) {
    const auto cpu = sample("amd-dual-ccd").cpu;  // 7950X3D 相当
    ASSERT_TRUE(cpu.known);
    EXPECT_EQ(cpu.vendor, lf::CpuVendor::Amd);
    EXPECT_EQ(cpu.physicalCores, 16);
    EXPECT_EQ(cpu.logicalProcessors, 32);
    EXPECT_TRUE(cpu.smt);
    EXPECT_FALSE(cpu.hybrid);
    EXPECT_EQ(cpu.performanceCores + cpu.efficiencyCores, 0) << "P/E counts are only meaningful on hybrid CPUs";
    ASSERT_EQ(cpu.l3Groups.size(), 2u);
    EXPECT_TRUE(cpu.multiL3);
    EXPECT_TRUE(cpu.asymmetricL3) << "96 MB (3D V-Cache) vs 32 MB";
    EXPECT_FALSE(cpu.simple);
    EXPECT_EQ(cpu.l3Groups[0].sizeBytes, 96 * MB);
    EXPECT_EQ(cpu.l3Groups[1].sizeBytes, 32 * MB);
    for (const auto& g : cpu.l3Groups) {
        EXPECT_EQ(g.physical, 8);
        EXPECT_EQ(g.logical, 16);
    }
    // コア → L3 グループの割り当て
    for (int i = 0; i < 8; ++i) EXPECT_EQ(cpu.cores[i].l3Group, 0) << i;
    for (int i = 8; i < 16; ++i) EXPECT_EQ(cpu.cores[i].l3Group, 1) << i;
}

TEST(Topology, AmdSingleCcdIsSimple) {
    const auto cpu = sample("amd-single-ccd").cpu;
    EXPECT_EQ(cpu.physicalCores, 8);
    EXPECT_EQ(cpu.l3Groups.size(), 1u);
    EXPECT_FALSE(cpu.multiL3);
    EXPECT_FALSE(cpu.asymmetricL3);
    EXPECT_TRUE(cpu.simple);
}

TEST(Topology, Zen2CcxLayoutGivesFourL3GroupsOfThreeCores) {
    const auto cpu = sample("amd-zen2-multi-ccx").cpu;  // 3900X 相当
    EXPECT_EQ(cpu.physicalCores, 12);
    EXPECT_EQ(cpu.logicalProcessors, 24);
    ASSERT_EQ(cpu.l3Groups.size(), 4u);
    EXPECT_TRUE(cpu.multiL3);
    EXPECT_FALSE(cpu.asymmetricL3) << "all CCX have 16 MB";
    for (const auto& g : cpu.l3Groups) {
        EXPECT_EQ(g.physical, 3);
        EXPECT_EQ(g.logical, 6);
    }
}

TEST(Topology, SimpleSixCoreWithSmt) {
    const auto cpu = sample("simple").cpu;
    EXPECT_EQ(cpu.physicalCores, 6);
    EXPECT_EQ(cpu.logicalProcessors, 12);
    EXPECT_TRUE(cpu.smt);
    EXPECT_FALSE(cpu.hybrid);
    EXPECT_EQ(cpu.l3Groups.size(), 1u);
    EXPECT_TRUE(cpu.simple);
    for (const auto& c : cpu.cores) EXPECT_EQ(c.kind, lf::CoreKind::Uniform);
}

TEST(Topology, QuadCoreWithoutSmt) {
    const auto cpu = sample("quad-nosmt").cpu;
    EXPECT_EQ(cpu.physicalCores, 4);
    EXPECT_EQ(cpu.logicalProcessors, 4);
    EXPECT_FALSE(cpu.smt);
    EXPECT_TRUE(cpu.simple);
}

TEST(Topology, VirtualMachineWithoutL3Information) {
    const auto sys = sample("vm");
    ASSERT_TRUE(sys.cpu.known);
    EXPECT_EQ(sys.cpu.physicalCores, 4);
    EXPECT_FALSE(sys.cpu.l3Known);
    EXPECT_TRUE(sys.cpu.l3Groups.empty());
    EXPECT_FALSE(sys.cpu.multiL3);
    for (const auto& c : sys.cpu.cores) EXPECT_EQ(c.l3Group, -1);
}

TEST(Topology, MoreThan64LogicalProcessorsSpanTwoProcessorGroups) {
    const auto cpu = sample("threadripper").cpu;  // 3990X 相当: 64 コア / 128 スレッド
    EXPECT_EQ(cpu.physicalCores, 64);
    EXPECT_EQ(cpu.logicalProcessors, 128);
    EXPECT_EQ(cpu.processorGroups, 2);
    EXPECT_EQ(cpu.l3Groups.size(), 16u);
    EXPECT_EQ(cpu.cores.front().group, 0);
    EXPECT_EQ(cpu.cores.back().group, 1);
    for (const auto& c : cpu.cores) EXPECT_GE(c.l3Group, 0) << "core " << c.index << " must belong to an L3 group";
    for (const auto& g : cpu.l3Groups) EXPECT_EQ(g.physical, 4);
}

TEST(Topology, UnknownWhenTheOsCallFails) {
    const auto sys = sample("cpu-unknown");
    EXPECT_FALSE(sys.cpu.known);
    EXPECT_EQ(sys.cpu.physicalCores, 0);
    EXPECT_TRUE(sys.cpu.cores.empty());
}

TEST(Topology, ThreeEfficiencyClassesTreatOnlyTheHighestAsPerformance) {
    lf::RawCpu raw;
    raw.ok = true;
    raw.vendor = "GenuineIntel";
    // Core Ultra 風: P=2, E=1, LP-E=0
    raw.cores.push_back({{{0, 0b11}}, 2, true});
    raw.cores.push_back({{{0, 0b100}}, 1, false});
    raw.cores.push_back({{{0, 0b1000}}, 0, false});
    raw.caches.push_back({3, 12 * MB, {{0, 0b1111}}});
    const auto cpu = lf::analyzeCpu(raw);
    EXPECT_TRUE(cpu.hybrid);
    EXPECT_EQ(cpu.performanceCores, 1);
    EXPECT_EQ(cpu.efficiencyCores, 2);
    EXPECT_EQ(cpu.logicalProcessors, 4);
}

TEST(Topology, DuplicateL3EntriesAreMerged) {
    lf::RawCpu raw;
    raw.ok = true;
    raw.cores.push_back({{{0, 0b11}}, 0, true});
    raw.cores.push_back({{{0, 0b1100}}, 0, true});
    raw.caches.push_back({3, 8 * MB, {{0, 0b1111}}});
    raw.caches.push_back({3, 8 * MB, {{0, 0b1111}}});  // 同じ mask の重複
    raw.caches.push_back({2, 1 * MB, {{0, 0b11}}});    // L2 は無視される
    const auto cpu = lf::analyzeCpu(raw);
    EXPECT_EQ(cpu.l3Groups.size(), 1u);
    EXPECT_EQ(cpu.l3Groups[0].physical, 2);
}

TEST(Topology, VendorDetection) {
    lf::RawCpu raw;
    raw.ok = true;
    raw.cores.push_back({{{0, 1}}, 0, false});
    raw.vendor = "GenuineIntel";
    EXPECT_EQ(lf::analyzeCpu(raw).vendor, lf::CpuVendor::Intel);
    raw.vendor = "AuthenticAMD";
    EXPECT_EQ(lf::analyzeCpu(raw).vendor, lf::CpuVendor::Amd);
    raw.vendor = "HygonGenuine";
    EXPECT_EQ(lf::analyzeCpu(raw).vendor, lf::CpuVendor::Other);
    raw.vendor = "";
    EXPECT_EQ(lf::analyzeCpu(raw).vendor, lf::CpuVendor::Unknown);
}

// ---- OS / 互換性 ----------------------------------------------------------------------------------

TEST(Os, Windows11And10AreToldApartByBuild) {
    EXPECT_EQ(sample("intel-hybrid").os.name, "Windows 11");
    EXPECT_EQ(sample("ltsc").os.name, "Windows 10");
    EXPECT_EQ(sample("win10-1803").os.name, "Windows 10");
    EXPECT_EQ(sample("win7").os.name, "Windows 7");
    EXPECT_EQ(sample("server").os.name, "Windows Server");
}

TEST(Compat, SupportedWindows11X64HasNoIssues) {
    for (const char* n : {"intel-hybrid", "amd-dual-ccd", "simple", "vm", "threadripper", "no-nvidia"}) {
        const auto rep = lf::evaluateCompat(sample(n));
        EXPECT_TRUE(rep.supported()) << n;
    }
}

TEST(Compat, OldWindowsIsFlaggedIncludingWindows7And10Before1809) {
    EXPECT_TRUE(lf::evaluateCompat(sample("win7")).has(lf::CompatIssue::WindowsTooOld));
    EXPECT_TRUE(lf::evaluateCompat(sample("win10-1803")).has(lf::CompatIssue::WindowsTooOld));
}

TEST(Compat, Windows8And81AreFlagged) {
    for (uint32_t minor : {2u, 3u}) {
        lf::RawOs o;
        o.major = 6;
        o.minor = minor;
        o.build = minor == 2 ? 9200 : 9600;
        o.nativeArch = lf::Arch::X64;
        lf::SystemInfo s;
        s.os = lf::analyzeOs(o);
        EXPECT_TRUE(lf::evaluateCompat(s).has(lf::CompatIssue::WindowsTooOld)) << minor;
        EXPECT_EQ(s.os.name, minor == 2 ? "Windows 8" : "Windows 8.1");
    }
}

TEST(Compat, Build17763IsTheFirstSupportedBuild) {
    lf::SystemInfo s = sample("intel-hybrid");
    s.os.build = 17762;
    EXPECT_TRUE(lf::evaluateCompat(s).has(lf::CompatIssue::WindowsTooOld));
    s.os.build = 17763;
    EXPECT_FALSE(lf::evaluateCompat(s).has(lf::CompatIssue::WindowsTooOld));
    EXPECT_EQ(lf::kMinSupportedWindowsBuild, lf::kMinSupportedBuild) << "the two constants must stay in sync";
}

TEST(Compat, ServerLtscAndArm64AreWarnedAboutButOnlyForTheirOwnReason) {
    const auto server = lf::evaluateCompat(sample("server"));
    EXPECT_TRUE(server.has(lf::CompatIssue::ServerEdition));
    EXPECT_FALSE(server.has(lf::CompatIssue::WindowsTooOld)) << "Server 2022 is new enough";
    EXPECT_FALSE(server.has(lf::CompatIssue::LtscEdition));

    const auto ltsc = lf::evaluateCompat(sample("ltsc"));
    EXPECT_TRUE(ltsc.has(lf::CompatIssue::LtscEdition));
    EXPECT_FALSE(ltsc.has(lf::CompatIssue::ServerEdition));
    EXPECT_EQ(ltsc.issues.size(), 1u);

    const auto arm = lf::evaluateCompat(sample("arm64"));
    EXPECT_TRUE(arm.has(lf::CompatIssue::Arm64));
    EXPECT_EQ(arm.issues.size(), 1u);
}

TEST(Compat, LtscIsRecognizedFromTheProductNameToo) {
    lf::RawOs o;
    o.major = 10;
    o.build = 19044;
    o.editionId = "Enterprise";  // EditionID だけでは判別できない場合
    o.productName = "Windows 10 Enterprise LTSC";
    EXPECT_TRUE(lf::analyzeOs(o).isLtsc);
    o.productName = "Windows 10 Enterprise";
    EXPECT_FALSE(lf::analyzeOs(o).isLtsc);
}

TEST(Compat, ServerIsRecognizedByProductTypeOrEditionId) {
    lf::RawOs o;
    o.major = 10;
    o.build = 20348;
    o.productType = 3;
    EXPECT_TRUE(lf::analyzeOs(o).isServer);
    o.productType = 1;
    o.editionId = "ServerStandard";
    EXPECT_TRUE(lf::analyzeOs(o).isServer);
    o.editionId = "Professional";
    EXPECT_FALSE(lf::analyzeOs(o).isServer);
}

TEST(Compat, SeveralIssuesAreReportedTogether) {
    lf::SystemInfo s = sample("ltsc");
    s.os.isServer = true;
    s.os.arch = lf::Arch::Arm64;
    const auto rep = lf::evaluateCompat(s);
    EXPECT_EQ(rep.issues.size(), 3u);
}

// ---- GPU ------------------------------------------------------------------------------------------

TEST(Gpu, VendorIdsAreClassified) {
    const auto gpus = lf::analyzeGpus({{"a", 0x10DE, 1, 0, false},
                                       {"b", 0x1002, 1, 0, false},
                                       {"c", 0x8086, 1, 0, false},
                                       {"d", 0x1414, 1, 0, true},
                                       {"e", 0x5143, 1, 0, false},
                                       {"f", 0, 1, 0, false}});
    ASSERT_EQ(gpus.size(), 6u);
    EXPECT_EQ(gpus[0].vendor, lf::GpuVendor::Nvidia);
    EXPECT_EQ(gpus[1].vendor, lf::GpuVendor::Amd);
    EXPECT_EQ(gpus[2].vendor, lf::GpuVendor::Intel);
    EXPECT_EQ(gpus[3].vendor, lf::GpuVendor::Microsoft);
    EXPECT_TRUE(gpus[3].software);
    EXPECT_EQ(gpus[4].vendor, lf::GpuVendor::Other);
    EXPECT_EQ(gpus[5].vendor, lf::GpuVendor::Unknown);
}

TEST(Gpu, MicrosoftBasicRenderIsSoftwareEvenWithoutTheDxgiFlag) {
    const auto gpus = lf::analyzeGpus({{"Microsoft Basic Render Driver", 0x1414, 0x8C, 0, false}});
    EXPECT_TRUE(gpus[0].software);
}

TEST(Gpu, HybridGraphicsLaptopHasNvidia) {
    const auto sys = sample("laptop-hybrid");
    ASSERT_EQ(sys.gpus.size(), 2u);
    EXPECT_TRUE(sys.hasNvidia());
    EXPECT_TRUE(sys.hasHardwareGpu());
}

TEST(Gpu, SoftwareAdapterNeverCountsAsNvidia) {
    lf::SystemInfo s;
    s.gpus = lf::analyzeGpus({{"NVIDIA something", 0x10DE, 1, 0, true}});  // ソフトウェア扱い
    EXPECT_FALSE(s.hasNvidia());
    EXPECT_FALSE(s.hasHardwareGpu());
}

// ---- 機能の有効/無効 ------------------------------------------------------------------------------

TEST(Features, NvidiaPageNeedsAnNvidiaGpu) {
    EXPECT_TRUE(lf::featureStatus(lf::Feature::GpuNvidia, sample("intel-hybrid")).available);
    EXPECT_TRUE(lf::featureStatus(lf::Feature::GpuNvidia, sample("laptop-hybrid")).available);

    const auto amd = lf::featureStatus(lf::Feature::GpuNvidia, sample("no-nvidia"));
    EXPECT_FALSE(amd.available);
    EXPECT_EQ(amd.reasonKey, "feature.reason.noNvidiaOthers");
    ASSERT_EQ(amd.reasonArgs.size(), 1u);
    EXPECT_EQ(amd.reasonArgs[0], "AMD") << "the detected vendor is shown in the tooltip";

    const auto vm = lf::featureStatus(lf::Feature::GpuNvidia, sample("vm"));
    EXPECT_FALSE(vm.available);
    EXPECT_EQ(vm.reasonKey, "feature.reason.softwareOnly");

    lf::SystemInfo none = sample("intel-hybrid");
    none.gpus.clear();
    EXPECT_EQ(lf::featureStatus(lf::Feature::GpuNvidia, none).reasonKey, "feature.reason.softwareOnly");
}

TEST(Features, KernelAndUsbAreDisabledBelowTheSupportedBuildWithTheBuildInTheReason) {
    for (auto f : {lf::Feature::Kernel, lf::Feature::Usb}) {
        EXPECT_TRUE(lf::featureStatus(f, sample("intel-hybrid")).available);
        const auto old = lf::featureStatus(f, sample("win10-1803"));
        EXPECT_FALSE(old.available);
        EXPECT_EQ(old.reasonKey, "feature.reason.buildTooOld");
        ASSERT_EQ(old.reasonArgs.size(), 2u);
        EXPECT_EQ(old.reasonArgs[0], "17134");
        EXPECT_EQ(old.reasonArgs[1], "17763");
        EXPECT_FALSE(lf::featureStatus(f, sample("win7")).available);
    }
}

TEST(Features, Arm64DisablesSystemTweaksButNotTheBenchmarkOrSettings) {
    const auto arm = sample("arm64");
    for (auto f : {lf::Feature::Kernel, lf::Feature::Usb, lf::Feature::Affinity, lf::Feature::GpuNvidia}) {
        const auto s = lf::featureStatus(f, arm);
        EXPECT_FALSE(s.available);
        EXPECT_EQ(s.reasonKey, "feature.reason.arm64");
    }
    EXPECT_TRUE(lf::featureStatus(lf::Feature::Benchmark, arm).available);
    EXPECT_TRUE(lf::featureStatus(lf::Feature::Settings, arm).available);
    EXPECT_TRUE(lf::featureStatus(lf::Feature::Home, arm).available);
}

TEST(Features, ServerAndLtscStayUsableBecauseTheyOnlyGetAWarning) {
    EXPECT_TRUE(lf::featureStatus(lf::Feature::Kernel, sample("server")).available);
    EXPECT_TRUE(lf::featureStatus(lf::Feature::Kernel, sample("ltsc")).available);
}

TEST(Features, AffinityNoteAppearsOnlyOnSimpleTopologies) {
    EXPECT_EQ(lf::featureStatus(lf::Feature::Affinity, sample("simple")).noteKey, "feature.note.affinitySmall");
    EXPECT_EQ(lf::featureStatus(lf::Feature::Affinity, sample("amd-single-ccd")).noteKey, "feature.note.affinitySmall");
    EXPECT_EQ(lf::featureStatus(lf::Feature::Affinity, sample("quad-nosmt")).noteKey, "feature.note.affinitySmall");
    EXPECT_TRUE(lf::featureStatus(lf::Feature::Affinity, sample("intel-hybrid")).noteKey.empty());
    EXPECT_TRUE(lf::featureStatus(lf::Feature::Affinity, sample("amd-dual-ccd")).noteKey.empty());
    EXPECT_TRUE(lf::featureStatus(lf::Feature::Affinity, sample("threadripper")).noteKey.empty());
    EXPECT_TRUE(lf::featureStatus(lf::Feature::Affinity, sample("simple")).available) << "still usable";
}

TEST(Features, AffinityNeedsKnownTopologyAndAtLeastTwoProcessors) {
    const auto unknown = lf::featureStatus(lf::Feature::Affinity, sample("cpu-unknown"));
    EXPECT_FALSE(unknown.available);
    EXPECT_EQ(unknown.reasonKey, "feature.reason.topologyUnknown");

    lf::SystemInfo one = sample("quad-nosmt");
    one.cpu.logicalProcessors = 1;
    const auto s = lf::featureStatus(lf::Feature::Affinity, one);
    EXPECT_FALSE(s.available);
    EXPECT_EQ(s.reasonKey, "feature.reason.singleProcessor");
}

// ---- サンプル / 実機プローブ ----------------------------------------------------------------------

TEST(Samples, EveryNamedSampleCanBeAnalyzed) {
    const auto names = lf::sampleSystemNames();
    EXPECT_GE(names.size(), 10u);
    std::set<std::string> seen;
    for (const auto& n : names) {
        EXPECT_TRUE(seen.insert(n).second) << "duplicate sample name " << n;
        auto probe = lf::makeSampleProbe(n);
        ASSERT_NE(probe, nullptr) << n;
        const auto sys = lf::detectSystem(*probe);
        EXPECT_FALSE(sys.os.name.empty()) << n;
        (void)lf::evaluateCompat(sys);
        for (auto f : {lf::Feature::Home, lf::Feature::Affinity, lf::Feature::Usb, lf::Feature::GpuNvidia, lf::Feature::Kernel})
            (void)lf::featureStatus(f, sys);
    }
    EXPECT_EQ(lf::makeSampleProbe("no-such-sample"), nullptr);
}

TEST(Samples, TheLogicalProcessorCountEqualsTheSumOfCoreThreads) {
    for (const auto& n : lf::sampleSystemNames()) {
        const auto sys = sample(n.c_str());
        if (!sys.cpu.known) continue;
        int sum = 0;
        for (const auto& c : sys.cpu.cores) sum += c.logical;
        EXPECT_EQ(sum, sys.cpu.logicalProcessors) << n;
        int l3Cores = 0;
        for (const auto& g : sys.cpu.l3Groups) l3Cores += g.physical;
        if (sys.cpu.l3Known) EXPECT_EQ(l3Cores, sys.cpu.physicalCores) << n << ": every core must belong to exactly one L3 group";
    }
}

// 実機の API を呼ぶ読み取り専用の煙テスト (CI のランナーで動作)。値そのものではなく、不変条件だけを確認する。
TEST(WinProbe, RealSystemSatisfiesBasicInvariants) {
    lf::WinSystemProbe probe;
    const auto sys = lf::detectSystem(probe);
    EXPECT_GE(sys.os.major, 10u);
    EXPECT_GT(sys.os.build, 0u);
    EXPECT_FALSE(sys.os.name.empty());
    EXPECT_EQ(sys.os.arch == lf::Arch::X64 || sys.os.arch == lf::Arch::Arm64, true);
    ASSERT_TRUE(sys.cpu.known);
    EXPECT_GE(sys.cpu.physicalCores, 1);
    EXPECT_GE(sys.cpu.logicalProcessors, sys.cpu.physicalCores);
    EXPECT_GE(sys.cpu.processorGroups, 1);
    EXPECT_FALSE(sys.cpu.brand.empty());
    if (sys.cpu.l3Known) {
        int cores = 0;
        for (const auto& g : sys.cpu.l3Groups) cores += g.physical;
        EXPECT_LE(cores, sys.cpu.physicalCores);
    }
    // GPU はヘッドレスの CI では 0 件のこともあるので、件数は問わない。
    for (const auto& g : sys.gpus) EXPECT_FALSE(g.name.empty());
}
