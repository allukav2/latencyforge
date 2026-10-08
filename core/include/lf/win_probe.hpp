#pragma once
#include "lf/sysinfo.hpp"

namespace lf {

// 実機の ISystemProbe。ここだけが Windows API (GetLogicalProcessorInformationEx / CPUID / DXGI /
// RtlGetVersion / レジストリの読み取り) を呼ぶ。すべて読み取り専用で、何も書き込まない。
class WinSystemProbe final : public ISystemProbe {
public:
    RawOs os() override;
    RawCpu cpu() override;
    std::vector<RawGpu> gpus() override;
};

}  // namespace lf
