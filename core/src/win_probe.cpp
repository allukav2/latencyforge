#include "lf/win_probe.hpp"

#include <windows.h>
#include <dxgi.h>
#include <intrin.h>

#include <cstring>

#include "lf/registry.hpp"
#include "lf/util.hpp"
#include "lf/win_registry.hpp"

namespace lf {
namespace {

std::string readCurrentVersionString(WinRegistry& reg, const char* name) {
    auto p = parseRegPath("HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", name);
    if (!p.ok()) return {};
    auto v = reg.read(p.value());
    if (!v.ok() || !v.value() || v.value()->isNumeric()) return {};
    return v.value()->text;
}

Arch nativeArch() {
    using Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
    if (HMODULE k = GetModuleHandleW(L"kernel32.dll")) {
        if (auto fn = reinterpret_cast<Fn>(GetProcAddress(k, "IsWow64Process2"))) {  // Windows 10 1511+
            USHORT process = 0, native = 0;
            if (fn(GetCurrentProcess(), &process, &native)) {
                switch (native) {
                    case 0xAA64: return Arch::Arm64;   // IMAGE_FILE_MACHINE_ARM64
                    case 0x8664: return Arch::X64;     // IMAGE_FILE_MACHINE_AMD64
                    case 0x014C: return Arch::X86;     // IMAGE_FILE_MACHINE_I386
                    default: break;
                }
            }
        }
    }
    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: return Arch::X64;
        case PROCESSOR_ARCHITECTURE_ARM64: return Arch::Arm64;
        case PROCESSOR_ARCHITECTURE_INTEL: return Arch::X86;
        default: return Arch::Unknown;
    }
}

std::string cpuidVendor() {
    int r[4] = {};
    __cpuid(r, 0);
    char v[13] = {};
    std::memcpy(v + 0, &r[1], 4);  // EBX
    std::memcpy(v + 4, &r[3], 4);  // EDX
    std::memcpy(v + 8, &r[2], 4);  // ECX
    return v;
}

std::string cpuidBrand() {
    int r[4] = {};
    __cpuid(r, static_cast<int>(0x80000000));
    if (static_cast<unsigned>(r[0]) < 0x80000004u) return {};
    char b[49] = {};
    for (int i = 0; i < 3; ++i) {
        __cpuid(r, static_cast<int>(0x80000002u + static_cast<unsigned>(i)));
        std::memcpy(b + i * 16, r, 16);
    }
    std::string s = b;
    const size_t first = s.find_first_not_of(' ');
    return first == std::string::npos ? std::string() : s.substr(first);
}

}  // namespace

RawOs WinSystemProbe::os() {
    RawOs o;
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOEXW*);
    OSVERSIONINFOEXW vi{};
    vi.dwOSVersionInfoSize = sizeof vi;
    if (HMODULE nt = GetModuleHandleW(L"ntdll.dll")) {
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(nt, "RtlGetVersion"))) fn(&vi);
    }
    o.major = vi.dwMajorVersion;
    o.minor = vi.dwMinorVersion;
    o.build = vi.dwBuildNumber;
    o.productType = vi.wProductType ? vi.wProductType : 1;
    o.nativeArch = nativeArch();

    WinRegistry reg;  // 読み取りのみ
    o.editionId = readCurrentVersionString(reg, "EditionID");
    o.productName = readCurrentVersionString(reg, "ProductName");
    o.displayVersion = readCurrentVersionString(reg, "DisplayVersion");
    if (o.displayVersion.empty()) o.displayVersion = readCurrentVersionString(reg, "ReleaseId");
    return o;
}

RawCpu WinSystemProbe::cpu() {
    RawCpu c;
    c.vendor = cpuidVendor();
    c.brand = cpuidBrand();

    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationAll, nullptr, &len);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || len == 0) return c;  // ok=false のまま
    std::vector<BYTE> buf(len);
    if (!GetLogicalProcessorInformationEx(RelationAll, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data()), &len))
        return c;

    uint32_t packages = 0;
    for (DWORD off = 0; off < len;) {
        auto* e = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data() + off);
        if (e->Size == 0) break;
        switch (e->Relationship) {
            case RelationProcessorCore: {
                RawCore core;
                core.efficiencyClass = e->Processor.EfficiencyClass;
                core.smt = (e->Processor.Flags & LTP_PC_SMT) != 0;
                for (WORD i = 0; i < e->Processor.GroupCount; ++i)
                    core.masks.push_back({e->Processor.GroupMask[i].Group, static_cast<uint64_t>(e->Processor.GroupMask[i].Mask)});
                c.cores.push_back(std::move(core));
                break;
            }
            case RelationCache: {
                if (e->Cache.Level != 3) break;
                if (e->Cache.Type != CacheUnified && e->Cache.Type != CacheData) break;
                RawCache cache;
                cache.level = e->Cache.Level;
                cache.sizeBytes = e->Cache.CacheSize;
                const WORD n = e->Cache.GroupCount > 0 ? e->Cache.GroupCount : 1;  // Windows 10 では常に 1
                for (WORD i = 0; i < n; ++i) {
                    const GROUP_AFFINITY& ga = n == 1 && e->Cache.GroupCount <= 1 ? e->Cache.GroupMask : e->Cache.GroupMasks[i];
                    cache.masks.push_back({ga.Group, static_cast<uint64_t>(ga.Mask)});
                }
                c.caches.push_back(std::move(cache));
                break;
            }
            case RelationProcessorPackage: ++packages; break;
            case RelationGroup: c.groupCount = static_cast<uint16_t>(e->Group.ActiveGroupCount); break;
            default: break;
        }
        off += e->Size;
    }
    c.packages = packages ? packages : 1;
    c.ok = !c.cores.empty();
    return c;
}

std::vector<RawGpu> WinSystemProbe::gpus() {
    std::vector<RawGpu> out;
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) || !factory) return out;
    for (UINT i = 0;; ++i) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 d{};
        if (SUCCEEDED(adapter->GetDesc1(&d))) {
            RawGpu g;
            g.name = narrow(d.Description);
            g.vendorId = d.VendorId;
            g.deviceId = d.DeviceId;
            g.dedicatedVideoMemory = d.DedicatedVideoMemory;
            g.software = (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
            out.push_back(std::move(g));
        }
        adapter->Release();
    }
    factory->Release();
    return out;
}

}  // namespace lf
