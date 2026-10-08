#include "lf/sysinfo.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <set>

namespace lf {

namespace {

int popcount(const std::vector<GroupMask>& masks) {
    int n = 0;
    for (const auto& m : masks) n += std::popcount(m.mask);
    return n;
}

bool overlaps(const std::vector<GroupMask>& a, const std::vector<GroupMask>& b) {
    for (const auto& x : a)
        for (const auto& y : b)
            if (x.group == y.group && (x.mask & y.mask) != 0) return true;
    return false;
}

bool contains(std::string_view hay, std::string_view needle) { return hay.find(needle) != std::string_view::npos; }

bool startsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

}  // namespace

// ---------------------------------------------------------------------------------------------- OS

OsInfo analyzeOs(const RawOs& r) {
    OsInfo o;
    o.major = r.major;
    o.minor = r.minor;
    o.build = r.build;
    o.edition = r.editionId;
    o.displayVersion = r.displayVersion;
    o.arch = r.nativeArch;
    o.isServer = r.productType != 1 || startsWith(r.editionId, "Server");
    // LTSC/LTSB は EditionID が "EnterpriseS" / "IoTEnterpriseS"、または製品名に LTSC/LTSB を含む。
    o.isLtsc = contains(r.editionId, "EnterpriseS") || contains(r.productName, "LTSC") || contains(r.productName, "LTSB");

    if (r.major >= 10) {
        o.name = o.isServer ? "Windows Server" : (r.build >= 22000 ? "Windows 11" : "Windows 10");
    } else if (r.major == 6 && r.minor == 3) {
        o.name = o.isServer ? "Windows Server 2012 R2" : "Windows 8.1";
    } else if (r.major == 6 && r.minor == 2) {
        o.name = o.isServer ? "Windows Server 2012" : "Windows 8";
    } else if (r.major == 6 && r.minor == 1) {
        o.name = o.isServer ? "Windows Server 2008 R2" : "Windows 7";
    } else if (r.major == 0) {
        o.name = "Windows";
    } else {
        o.name = o.isServer ? "Windows Server" : "Windows";
    }
    return o;
}

// ---------------------------------------------------------------------------------------------- CPU

CpuTopology analyzeCpu(const RawCpu& raw) {
    CpuTopology t;
    t.brand = raw.brand;
    if (raw.vendor == "GenuineIntel")
        t.vendor = CpuVendor::Intel;
    else if (raw.vendor == "AuthenticAMD")
        t.vendor = CpuVendor::Amd;
    else
        t.vendor = raw.vendor.empty() ? CpuVendor::Unknown : CpuVendor::Other;

    if (!raw.ok || raw.cores.empty()) return t;  // known=false
    t.known = true;
    t.packages = static_cast<int>(std::max<uint32_t>(1, raw.packages));

    uint8_t maxClass = 0, minClass = 255;
    uint16_t maxGroup = 0;
    for (const RawCore& c : raw.cores) {
        maxClass = std::max(maxClass, c.efficiencyClass);
        minClass = std::min(minClass, c.efficiencyClass);
        for (const auto& m : c.masks) maxGroup = std::max(maxGroup, m.group);
    }
    t.hybrid = maxClass != minClass;  // 最大クラス = P コア、それ以外 = E コア (LP-E 含む)
    t.processorGroups = std::max<int>(raw.groupCount, maxGroup + 1);

    for (size_t i = 0; i < raw.cores.size(); ++i) {
        const RawCore& rc = raw.cores[i];
        CoreInfo ci;
        ci.index = static_cast<int>(i);
        ci.group = rc.masks.empty() ? 0 : rc.masks[0].group;
        ci.mask = rc.masks.empty() ? 0 : rc.masks[0].mask;
        ci.logical = std::max(1, popcount(rc.masks));
        ci.efficiencyClass = rc.efficiencyClass;
        ci.kind = !t.hybrid ? CoreKind::Uniform : (rc.efficiencyClass == maxClass ? CoreKind::Performance : CoreKind::Efficiency);
        if (ci.logical > 1 || rc.smt) t.smt = true;
        t.logicalProcessors += ci.logical;
        if (ci.kind == CoreKind::Performance) ++t.performanceCores;
        if (ci.kind == CoreKind::Efficiency) ++t.efficiencyCores;
        t.cores.push_back(ci);
    }
    t.physicalCores = static_cast<int>(t.cores.size());

    // L3 共有グループ: 同一 mask の重複は 1 つにまとめ、コアを mask の重なりで割り当てる。
    std::vector<const RawCache*> l3;
    for (const RawCache& c : raw.caches) {
        if (c.level != 3) continue;
        const bool dup = std::any_of(l3.begin(), l3.end(), [&](const RawCache* o) { return o->masks == c.masks; });
        if (!dup) l3.push_back(&c);
    }
    for (const RawCache* c : l3) {
        L3Group g;
        g.index = static_cast<int>(t.l3Groups.size());
        g.sizeBytes = c->sizeBytes;
        for (size_t i = 0; i < raw.cores.size(); ++i) {
            if (t.cores[i].l3Group != -1 || !overlaps(raw.cores[i].masks, c->masks)) continue;
            t.cores[i].l3Group = g.index;
            g.cores.push_back(static_cast<int>(i));
            ++g.physical;
            g.logical += t.cores[i].logical;
            if (t.cores[i].kind == CoreKind::Performance) ++g.performance;
            if (t.cores[i].kind == CoreKind::Efficiency) ++g.efficiency;
        }
        t.l3Groups.push_back(std::move(g));
    }
    t.l3Known = !t.l3Groups.empty();
    t.multiL3 = t.l3Groups.size() > 1;
    if (t.multiL3) {
        const uint64_t first = t.l3Groups.front().sizeBytes;
        t.asymmetricL3 = std::any_of(t.l3Groups.begin(), t.l3Groups.end(), [&](const L3Group& g) { return g.sizeBytes != first; });
    }
    t.simple = !t.hybrid && !t.multiL3;
    return t;
}

// ---------------------------------------------------------------------------------------------- GPU

const char* gpuVendorName(GpuVendor v) {
    switch (v) {
        case GpuVendor::Nvidia: return "NVIDIA";
        case GpuVendor::Amd: return "AMD";
        case GpuVendor::Intel: return "Intel";
        case GpuVendor::Microsoft: return "Microsoft";
        case GpuVendor::Other: return "Other";
        default: return "Unknown";
    }
}

std::vector<GpuInfo> analyzeGpus(const std::vector<RawGpu>& raw) {
    std::vector<GpuInfo> out;
    for (const RawGpu& r : raw) {
        GpuInfo g;
        g.name = r.name;
        g.vendorId = r.vendorId;
        g.deviceId = r.deviceId;
        g.vramBytes = r.dedicatedVideoMemory;
        g.driverVersion = r.driverVersion;
        switch (r.vendorId) {  // PCI ベンダー ID
            case 0x10DE: g.vendor = GpuVendor::Nvidia; break;
            case 0x1002:
            case 0x1022: g.vendor = GpuVendor::Amd; break;
            case 0x8086:
            case 0x8087: g.vendor = GpuVendor::Intel; break;
            case 0x1414: g.vendor = GpuVendor::Microsoft; break;  // WARP / Basic Render Driver
            case 0: g.vendor = GpuVendor::Unknown; break;
            default: g.vendor = GpuVendor::Other; break;
        }
        g.software = r.software || g.vendor == GpuVendor::Microsoft;
        out.push_back(std::move(g));
    }
    return out;
}

std::string formatDriverVersion(uint64_t v) {
    if (v == 0) return {};
    const unsigned a = static_cast<unsigned>((v >> 48) & 0xFFFF), b = static_cast<unsigned>((v >> 32) & 0xFFFF);
    const unsigned c = static_cast<unsigned>((v >> 16) & 0xFFFF), d = static_cast<unsigned>(v & 0xFFFF);
    return std::to_string(a) + "." + std::to_string(b) + "." + std::to_string(c) + "." + std::to_string(d);
}

std::string nvidiaDriverVersion(uint64_t v) {
    if (v == 0) return {};
    const unsigned c = static_cast<unsigned>((v >> 16) & 0xFFFF), d = static_cast<unsigned>(v & 0xFFFF);
    char digits[16];
    std::snprintf(digits, sizeof digits, "%u%04u", c, d);  // 例: 15 と 6094 → "156094"
    const std::string s = digits;
    if (s.size() < 5) return {};
    const std::string last5 = s.substr(s.size() - 5);  // "56094" → "560.94"
    return last5.substr(0, 3) + "." + last5.substr(3);
}

bool SystemInfo::hasNvidia() const {
    return std::any_of(gpus.begin(), gpus.end(), [](const GpuInfo& g) { return g.vendor == GpuVendor::Nvidia && !g.software; });
}

bool SystemInfo::hasHardwareGpu() const {
    return std::any_of(gpus.begin(), gpus.end(), [](const GpuInfo& g) { return !g.software; });
}

std::vector<std::string> SystemInfo::otherGpuVendorNames() const {
    std::vector<std::string> out;
    for (const GpuInfo& g : gpus) {
        if (g.software || g.vendor == GpuVendor::Nvidia) continue;
        std::string n = g.vendor == GpuVendor::Other || g.vendor == GpuVendor::Unknown ? g.name : gpuVendorName(g.vendor);
        if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(std::move(n));
    }
    return out;
}

SystemInfo detectSystem(ISystemProbe& probe) {
    SystemInfo s;
    s.os = analyzeOs(probe.os());
    s.cpu = analyzeCpu(probe.cpu());
    s.gpus = analyzeGpus(probe.gpus());
    return s;
}

// ---------------------------------------------------------------------------------------------- 互換性

bool CompatReport::has(CompatIssue i) const { return std::find(issues.begin(), issues.end(), i) != issues.end(); }

CompatReport evaluateCompat(const SystemInfo& sys) {
    CompatReport r;
    const OsInfo& os = sys.os;
    if (os.major < 10 || os.build < kMinSupportedWindowsBuild) r.issues.push_back(CompatIssue::WindowsTooOld);
    if (os.isServer) r.issues.push_back(CompatIssue::ServerEdition);
    if (os.isLtsc) r.issues.push_back(CompatIssue::LtscEdition);
    if (os.arch == Arch::Arm64) r.issues.push_back(CompatIssue::Arm64);
    return r;
}

const char* compatIssueKey(CompatIssue i) {
    switch (i) {
        case CompatIssue::WindowsTooOld: return "compat.issue.windowsTooOld";
        case CompatIssue::ServerEdition: return "compat.issue.serverEdition";
        case CompatIssue::LtscEdition: return "compat.issue.ltscEdition";
        case CompatIssue::Arm64: return "compat.issue.arm64";
    }
    return "compat.issue.windowsTooOld";
}

namespace {

constexpr const char* kReasonBuildTooOld = "feature.reason.buildTooOld";
constexpr const char* kReasonArm64 = "feature.reason.arm64";
constexpr const char* kReasonNoNvidia = "feature.reason.noNvidia";
constexpr const char* kReasonNoNvidiaOthers = "feature.reason.noNvidiaOthers";
constexpr const char* kReasonSoftwareOnly = "feature.reason.softwareOnly";
constexpr const char* kReasonTopologyUnknown = "feature.reason.topologyUnknown";
constexpr const char* kReasonSingleProcessor = "feature.reason.singleProcessor";
constexpr const char* kNoteAffinitySmall = "feature.note.affinitySmall";

FeatureStatus unavailable(const char* key, std::vector<std::string> args = {}) {
    FeatureStatus s;
    s.available = false;
    s.reasonKey = key;
    s.reasonArgs = std::move(args);
    return s;
}

// OS 起因で使えない機能 (レジストリ tweak / USB / Affinity / GPU) に共通の判定。
bool osBlocks(const SystemInfo& sys, FeatureStatus& out) {
    if (sys.os.major < 10 || sys.os.build < kMinSupportedWindowsBuild) {
        out = unavailable(kReasonBuildTooOld, {std::to_string(sys.os.build), std::to_string(kMinSupportedWindowsBuild)});
        return true;
    }
    if (sys.os.arch == Arch::Arm64) {
        out = unavailable(kReasonArm64);
        return true;
    }
    return false;
}

}  // namespace

FeatureStatus featureStatus(Feature f, const SystemInfo& sys) {
    FeatureStatus blocked;
    switch (f) {
        case Feature::Kernel:
        case Feature::Usb:
            if (osBlocks(sys, blocked)) return blocked;
            return {};
        case Feature::Affinity: {
            if (osBlocks(sys, blocked)) return blocked;
            if (!sys.cpu.known) return unavailable(kReasonTopologyUnknown);
            if (sys.cpu.logicalProcessors < 2) return unavailable(kReasonSingleProcessor);
            FeatureStatus s;
            if (sys.cpu.simple) s.noteKey = kNoteAffinitySmall;  // 使えるが、効果は小さい構成
            return s;
        }
        case Feature::GpuNvidia: {
            if (osBlocks(sys, blocked)) return blocked;
            if (sys.hasNvidia()) return {};
            if (!sys.hasHardwareGpu()) return unavailable(kReasonSoftwareOnly);
            const auto others = sys.otherGpuVendorNames();
            if (others.empty()) return unavailable(kReasonNoNvidia);
            std::string list;
            for (size_t i = 0; i < others.size(); ++i) list += (i ? ", " : "") + others[i];
            return unavailable(kReasonNoNvidiaOthers, {list});
        }
        default:  // Home / Benchmark / Backup / Log / Settings
            return {};
    }
}

std::vector<std::string> allSystemMessageKeys() {
    std::vector<std::string> k = {kReasonBuildTooOld, kReasonArm64,           kReasonNoNvidia,       kReasonNoNvidiaOthers,
                                  kReasonSoftwareOnly, kReasonTopologyUnknown, kReasonSingleProcessor, kNoteAffinitySmall};
    for (CompatIssue i : {CompatIssue::WindowsTooOld, CompatIssue::ServerEdition, CompatIssue::LtscEdition, CompatIssue::Arm64})
        k.push_back(compatIssueKey(i));
    return k;
}

}  // namespace lf
