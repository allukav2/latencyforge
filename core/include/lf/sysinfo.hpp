#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace lf {

// ================================================================================================
// 層 1: OS API の生データ。ISystemProbe の実装 (win_probe.cpp) だけが Windows API を呼ぶ。
//       以降の解析 (analyze*, evaluateCompat, featureStatus) は純粋関数で、モックデータでテストできる。
// ================================================================================================

struct GroupMask {
    uint16_t group = 0;  // プロセッサグループ (論理プロセッサが 64 を超える機種では複数)
    uint64_t mask = 0;
    bool operator==(const GroupMask&) const = default;
};

struct RawCore {  // RelationProcessorCore
    std::vector<GroupMask> masks;
    uint8_t efficiencyClass = 0;  // 大きいほど高性能 (非ハイブリッドでは全コア同値)
    bool smt = false;
};

struct RawCache {  // RelationCache (L3 のみ使う)
    uint8_t level = 0;
    uint64_t sizeBytes = 0;
    std::vector<GroupMask> masks;
};

struct RawCpu {
    bool ok = false;  // 取得に失敗したら false (トポロジー不明として扱う)
    std::string vendor;  // CPUID の 12 文字 ("GenuineIntel" / "AuthenticAMD" ...)
    std::string brand;
    std::vector<RawCore> cores;
    std::vector<RawCache> caches;
    uint32_t packages = 1;
    uint16_t groupCount = 1;
};

enum class Arch { Unknown, X64, Arm64, X86 };

struct RawOs {
    uint32_t major = 0, minor = 0, build = 0;
    uint32_t productType = 1;  // VER_NT_WORKSTATION=1, DOMAIN_CONTROLLER=2, SERVER=3
    std::string editionId;     // 例: "Professional", "EnterpriseS", "ServerStandard"
    std::string productName;
    std::string displayVersion;  // 例: "24H2"
    Arch nativeArch = Arch::Unknown;  // OS 自体のアーキテクチャ (エミュレーション下でも実機側)
};

struct RawGpu {
    std::string name;  // UTF-8
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
    uint64_t dedicatedVideoMemory = 0;
    bool software = false;  // DXGI_ADAPTER_FLAG_SOFTWARE
};

class ISystemProbe {
public:
    virtual ~ISystemProbe() = default;
    virtual RawOs os() = 0;
    virtual RawCpu cpu() = 0;
    virtual std::vector<RawGpu> gpus() = 0;
};

// ================================================================================================
// 層 2: 解析結果
// ================================================================================================

constexpr uint32_t kMinSupportedWindowsBuild = 17763;  // Windows 10 1809 (tweak.hpp の kMinSupportedBuild と同値)

struct OsInfo {
    uint32_t major = 0, minor = 0, build = 0;
    std::string name;  // "Windows 11" / "Windows 10" / "Windows Server" / "Windows 7" ...
    std::string edition, displayVersion;
    bool isServer = false;
    bool isLtsc = false;
    Arch arch = Arch::Unknown;
};

enum class CoreKind { Uniform, Performance, Efficiency };

struct CoreInfo {
    int index = 0;
    uint16_t group = 0;
    uint64_t mask = 0;
    int logical = 1;
    uint8_t efficiencyClass = 0;
    CoreKind kind = CoreKind::Uniform;
    int l3Group = -1;  // L3Groups の添字。不明なら -1
};

struct L3Group {
    int index = 0;
    uint64_t sizeBytes = 0;
    std::vector<int> cores;  // CoreInfo::index
    int physical = 0, logical = 0;
    int performance = 0, efficiency = 0;
};

enum class CpuVendor { Unknown, Intel, Amd, Other };

struct CpuTopology {
    bool known = false;
    CpuVendor vendor = CpuVendor::Unknown;
    std::string brand;
    int packages = 1;
    int physicalCores = 0, logicalProcessors = 0, processorGroups = 1;
    bool smt = false;
    bool hybrid = false;  // 効率クラスが 2 種類以上 (Intel P/E コア)
    int performanceCores = 0, efficiencyCores = 0;  // hybrid のときだけ意味を持つ
    std::vector<CoreInfo> cores;
    std::vector<L3Group> l3Groups;  // マルチ CCD (AMD) では CCD/CCX ごと
    bool l3Known = false;
    bool multiL3 = false;       // L3 が複数 (AMD のマルチ CCD/CCX など)
    bool asymmetricL3 = false;  // L3 サイズが不揃い (3D V-Cache のデュアル CCD など)
    bool simple = false;        // ハイブリッドでも複数 L3 でもない = Affinity の効果は小さい構成
};

enum class GpuVendor { Unknown, Nvidia, Amd, Intel, Microsoft, Other };

struct GpuInfo {
    std::string name;
    GpuVendor vendor = GpuVendor::Unknown;
    uint32_t vendorId = 0, deviceId = 0;
    uint64_t vramBytes = 0;
    bool software = false;  // WARP / Microsoft Basic Render Driver など
};

struct SystemInfo {
    OsInfo os;
    CpuTopology cpu;
    std::vector<GpuInfo> gpus;

    bool hasNvidia() const;        // ソフトウェアアダプタは除く
    bool hasHardwareGpu() const;
    std::vector<std::string> otherGpuVendorNames() const;  // "AMD" "Intel" ... (重複なし)
};

OsInfo analyzeOs(const RawOs& raw);
CpuTopology analyzeCpu(const RawCpu& raw);
std::vector<GpuInfo> analyzeGpus(const std::vector<RawGpu>& raw);
SystemInfo detectSystem(ISystemProbe& probe);

const char* gpuVendorName(GpuVendor v);  // "NVIDIA" / "AMD" / "Intel" / "Microsoft" / "Other"

// ================================================================================================
// 層 3: 互換性判定と機能の有効/無効
// ================================================================================================

// 起動時に警告する「動作保証外」の環境 (動作を止めはしない)。
enum class CompatIssue { WindowsTooOld, ServerEdition, LtscEdition, Arm64 };

struct CompatReport {
    std::vector<CompatIssue> issues;
    bool supported() const { return issues.empty(); }
    bool has(CompatIssue i) const;
};

CompatReport evaluateCompat(const SystemInfo& sys);
const char* compatIssueKey(CompatIssue i);  // "compat.issue.xxx" (data/lang のキー)

enum class Feature { Home, Affinity, Usb, GpuNvidia, Kernel, Benchmark, Backup, Log, Settings };

struct FeatureStatus {
    bool available = true;
    std::string reasonKey;  // available=false のとき: 無効の理由 (data/lang のキー。{0} {1} は args で置換)
    std::vector<std::string> reasonArgs;
    std::string noteKey;    // 利用はできるが伝えたい注意 (例: 単純な構成のため効果が小さい)
};

FeatureStatus featureStatus(Feature f, const SystemInfo& sys);

// data/lang に両言語で存在しなければならないキー (単体テストで検証する)。
std::vector<std::string> allSystemMessageKeys();

// ================================================================================================
// サンプル構成 (単体テストと、アプリの --demo --sim <名前> で共用)
// ================================================================================================

std::vector<std::string> sampleSystemNames();
// 未知の名前なら nullptr。
std::unique_ptr<ISystemProbe> makeSampleProbe(std::string_view name);

}  // namespace lf
