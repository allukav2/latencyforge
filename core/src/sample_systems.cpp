// 代表的な PC 構成の生データ (モック)。単体テストと、アプリの `--demo --sim <名前>` で共用する。
// 値は実機の一般的な報告内容に基づく見本で、特定の個体を再現するものではない。
#include <string>

#include "lf/sysinfo.hpp"

namespace lf {
namespace {

constexpr uint64_t MB = 1024ull * 1024ull;

// 論理プロセッサに連番を振ってコアを積み上げる。64 を超えたら次のプロセッサグループへ。
struct CpuBuilder {
    RawCpu cpu;
    int nextLogical = 0;

    CpuBuilder(std::string vendor, std::string brand) {
        cpu.ok = true;
        cpu.vendor = std::move(vendor);
        cpu.brand = std::move(brand);
    }

    // 論理プロセッサ [first, last) に対応するマスク群 (グループをまたぐ場合は複数)。
    static std::vector<GroupMask> maskRange(int first, int last) {
        std::vector<GroupMask> out;
        for (int i = first; i < last; ++i) {
            const uint16_t g = static_cast<uint16_t>(i / 64);
            if (out.empty() || out.back().group != g) out.push_back({g, 0});
            out.back().mask |= 1ull << (i % 64);
        }
        return out;
    }

    void addCore(bool smt, uint8_t effClass = 0) {
        const int n = smt ? 2 : 1;
        if (nextLogical % 64 + n > 64) nextLogical += 64 - nextLogical % 64;  // SMT の兄弟は同じグループに置く
        cpu.cores.push_back({maskRange(nextLogical, nextLogical + n), effClass, smt});
        nextLogical += n;
    }

    // 論理プロセッサ [first, last) を共有する L3 を追加。
    void addL3(int firstLogical, int lastLogical, uint64_t sizeBytes) {
        cpu.caches.push_back({3, sizeBytes, maskRange(firstLogical, lastLogical)});
    }

    RawCpu finish() {
        cpu.groupCount = static_cast<uint16_t>((nextLogical + 63) / 64);
        return cpu;
    }
};

RawCpu intelHybrid(const char* brand, int pCores, int eCores, uint64_t l3) {
    CpuBuilder b("GenuineIntel", brand);
    for (int i = 0; i < pCores; ++i) b.addCore(true, 1);   // P コア: SMT あり、効率クラス 1
    for (int i = 0; i < eCores; ++i) b.addCore(false, 0);  // E コア: SMT なし、効率クラス 0
    b.addL3(0, b.nextLogical, l3);                         // L3 は全コアで共有
    return b.finish();
}

RawCpu uniformSingleL3(const char* vendor, const char* brand, int cores, bool smt, uint64_t l3) {
    CpuBuilder b(vendor, brand);
    for (int i = 0; i < cores; ++i) b.addCore(smt);
    b.addL3(0, b.nextLogical, l3);
    return b.finish();
}

// L3 を cacheSizes.size() 個 (CCD または CCX) に等分したコア群で持つ AMD。
RawCpu amdMulti(const char* brand, int coresPerL3, const std::vector<uint64_t>& cacheSizes, bool smt = true) {
    CpuBuilder b("AuthenticAMD", brand);
    for (uint64_t size : cacheSizes) {
        const int first = b.nextLogical;
        for (int i = 0; i < coresPerL3; ++i) b.addCore(smt);
        b.addL3(first, b.nextLogical, size);
    }
    return b.finish();
}

RawOs win11() {
    RawOs o;
    o.major = 10;
    o.build = 26100;
    o.productType = 1;
    o.editionId = "Professional";
    o.productName = "Windows 11 Pro";
    o.displayVersion = "24H2";
    o.nativeArch = Arch::X64;
    return o;
}

RawGpu nvidia(const char* name, uint64_t vramMb) { return {name, 0x10DE, 0x2504, vramMb * MB, false}; }
RawGpu amdGpu(const char* name, uint64_t vramMb) { return {name, 0x1002, 0x744C, vramMb * MB, false}; }
RawGpu intelGpu(const char* name, uint64_t vramMb) { return {name, 0x8086, 0xA7A0, vramMb * MB, false}; }
RawGpu warp() { return {"Microsoft Basic Render Driver", 0x1414, 0x8C, 0, true}; }

class SampleProbe final : public ISystemProbe {
public:
    SampleProbe(RawOs os, RawCpu cpu, std::vector<RawGpu> gpus) : m_os(std::move(os)), m_cpu(std::move(cpu)), m_gpus(std::move(gpus)) {}
    RawOs os() override { return m_os; }
    RawCpu cpu() override { return m_cpu; }
    std::vector<RawGpu> gpus() override { return m_gpus; }

private:
    RawOs m_os;
    RawCpu m_cpu;
    std::vector<RawGpu> m_gpus;
};

}  // namespace

std::vector<std::string> sampleSystemNames() {
    return {"intel-hybrid", "laptop-hybrid", "amd-dual-ccd", "amd-single-ccd", "amd-zen2-multi-ccx", "simple",
            "quad-nosmt",   "vm",            "threadripper",  "server",         "ltsc",               "arm64",
            "win7",         "win10-1803",    "no-nvidia",     "cpu-unknown"};
}

std::unique_ptr<ISystemProbe> makeSampleProbe(std::string_view name) {
    auto make = [](RawOs os, RawCpu cpu, std::vector<RawGpu> gpus) {
        return std::make_unique<SampleProbe>(std::move(os), std::move(cpu), std::move(gpus));
    };

    if (name == "intel-hybrid")  // i7-13700K: 8P(SMT) + 8E = 16 コア / 24 スレッド、L3 30 MB 共有
        return make(win11(), intelHybrid("13th Gen Intel(R) Core(TM) i7-13700K", 8, 8, 30 * MB), {nvidia("NVIDIA GeForce RTX 4070", 12288)});
    if (name == "laptop-hybrid")  // i7-12700H: 6P + 8E、iGPU と NVIDIA dGPU
        return make(win11(), intelHybrid("12th Gen Intel(R) Core(TM) i7-12700H", 6, 8, 24 * MB),
                    {intelGpu("Intel(R) Iris(R) Xe Graphics", 128), nvidia("NVIDIA GeForce RTX 3060 Laptop GPU", 6144)});
    if (name == "amd-dual-ccd")  // 7950X3D: 2 CCD × 8 コア、L3 は 96 MB (3D V-Cache) と 32 MB
        return make(win11(), amdMulti("AMD Ryzen 9 7950X3D 16-Core Processor", 8, {96 * MB, 32 * MB}),
                    {nvidia("NVIDIA GeForce RTX 4080", 16384)});
    if (name == "amd-single-ccd")  // 7800X3D
        return make(win11(), amdMulti("AMD Ryzen 7 7800X3D 8-Core Processor", 8, {96 * MB}), {amdGpu("AMD Radeon RX 7800 XT", 16384)});
    if (name == "amd-zen2-multi-ccx")  // 3900X: 4 CCX × 3 コア、L3 は CCX ごと 16 MB
        return make(win11(), amdMulti("AMD Ryzen 9 3900X 12-Core Processor", 3, {16 * MB, 16 * MB, 16 * MB, 16 * MB}),
                    {nvidia("NVIDIA GeForce GTX 1080", 8192)});
    if (name == "simple")  // i5-10400: 6 コア / 12 スレッド、L3 12 MB
        return make(win11(), uniformSingleL3("GenuineIntel", "Intel(R) Core(TM) i5-10400 CPU @ 2.90GHz", 6, true, 12 * MB),
                    {nvidia("NVIDIA GeForce GTX 1660 SUPER", 6144)});
    if (name == "quad-nosmt")  // i3-8100: 4 コア SMT なし
        return make(win11(), uniformSingleL3("GenuineIntel", "Intel(R) Core(TM) i3-8100 CPU @ 3.60GHz", 4, false, 6 * MB),
                    {intelGpu("Intel(R) UHD Graphics 630", 128)});
    if (name == "vm") {  // 仮想マシン: L3 情報なし、ソフトウェア描画のみ
        CpuBuilder b("GenuineIntel", "Intel(R) Xeon(R) Platinum 8272CL CPU @ 2.60GHz");
        for (int i = 0; i < 4; ++i) b.addCore(false);
        return make(win11(), b.finish(), {warp()});
    }
    if (name == "threadripper") {  // 3990X: 64 コア / 128 スレッド = 2 プロセッサグループ、CCX 16 個
        std::vector<uint64_t> l3(16, 16 * MB);
        return make(win11(), amdMulti("AMD Ryzen Threadripper 3990X 64-Core Processor", 4, l3), {nvidia("NVIDIA RTX A4000", 16384)});
    }
    if (name == "server") {
        RawOs o = win11();
        o.build = 20348;
        o.productType = 3;
        o.editionId = "ServerStandard";
        o.productName = "Windows Server 2022 Standard";
        o.displayVersion = "21H2";
        return make(o, uniformSingleL3("GenuineIntel", "Intel(R) Xeon(R) Silver 4310", 12, true, 18 * MB), {warp()});
    }
    if (name == "ltsc") {
        RawOs o = win11();
        o.build = 19044;
        o.editionId = "IoTEnterpriseS";
        o.productName = "Windows 10 Enterprise LTSC 2021";
        o.displayVersion = "21H2";
        return make(o, uniformSingleL3("GenuineIntel", "Intel(R) Core(TM) i5-9500", 6, false, 9 * MB), {intelGpu("Intel(R) UHD Graphics 630", 128)});
    }
    if (name == "arm64") {
        RawOs o = win11();
        o.nativeArch = Arch::Arm64;
        RawCpu c = uniformSingleL3("", "Snapdragon(R) X Elite - X1E78100 - Qualcomm(R) Oryon(TM) CPU", 12, false, 42 * MB);
        return make(o, c, {{"Qualcomm(R) Adreno(TM) X1-85 GPU", 0x5143, 0x4, 0, false}});
    }
    if (name == "win7") {
        RawOs o;
        o.major = 6;
        o.minor = 1;
        o.build = 7601;
        o.editionId = "Professional";
        o.productName = "Windows 7 Professional";
        o.nativeArch = Arch::X64;
        return make(o, uniformSingleL3("GenuineIntel", "Intel(R) Core(TM) i5-2500K CPU @ 3.30GHz", 4, false, 6 * MB),
                    {nvidia("NVIDIA GeForce GTX 970", 4096)});
    }
    if (name == "win10-1803") {
        RawOs o = win11();
        o.build = 17134;
        o.productName = "Windows 10 Pro";
        o.displayVersion = "1803";
        return make(o, uniformSingleL3("GenuineIntel", "Intel(R) Core(TM) i7-7700K CPU @ 4.20GHz", 4, true, 8 * MB), {nvidia("NVIDIA GeForce GTX 1070", 8192)});
    }
    if (name == "no-nvidia")  // AMD CPU + AMD GPU + Intel iGPU なし
        return make(win11(), amdMulti("AMD Ryzen 9 5900X 12-Core Processor", 6, {32 * MB, 32 * MB}), {amdGpu("AMD Radeon RX 6800 XT", 16384)});
    if (name == "cpu-unknown") {  // トポロジー取得に失敗した場合
        RawCpu c;
        c.ok = false;
        return make(win11(), c, {nvidia("NVIDIA GeForce RTX 3070", 8192)});
    }
    return nullptr;
}

}  // namespace lf
