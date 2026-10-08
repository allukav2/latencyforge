#pragma once
#include "lf/process_api.hpp"

namespace lf {

// 実機のプロセス API。プロセスを開く場所は 1 か所 (openMinimal) で、権限は下の定数だけ。
// 使わないもの: SeDebugPrivilege / PROCESS_ALL_ACCESS / プロセスメモリの読み書き / リモートスレッド / フック。
// (tests/forbidden_apis_test.cpp が、ソースにこれらが現れないことを検査する。)
extern const unsigned long kProcessAccessRights;  // PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION

class WinProcessApi final : public IProcessApi {
public:
    WinProcessApi();
    std::vector<ProcessEntry> enumerate() override;
    Result<ProcessState> query(uint32_t pid) override;
    Result<void> setAffinity(uint32_t pid, uint64_t startTime, const GroupMask& mask) override;
    Result<void> setPriority(uint32_t pid, uint64_t startTime, uint32_t priorityClass) override;
    uint32_t selfPid() const override;
    uint32_t currentSessionId() const override { return m_session; }

private:
    uint32_t m_session = 0;
};

}  // namespace lf
