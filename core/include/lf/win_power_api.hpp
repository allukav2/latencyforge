#pragma once
#include "lf/power_api.hpp"

namespace lf {

// 実機の電源プラン API。文書化された powrprof の関数だけを使う:
//   PowerGetActiveScheme / PowerReadFriendlyName / PowerReadACValueIndex / PowerReadDCValueIndex /
//   PowerWriteACValueIndex / PowerWriteDCValueIndex / PowerSetActiveScheme
// 書き込み (writeIndex / applyScheme) は、Engine (Policy 検査・バックアップ済み) 経由でのみ呼ぶこと。
class WinPowerApi final : public IPowerApi {
public:
    Result<std::string> activeSchemeGuid() override;
    Result<std::string> schemeName(const std::string& scheme) override;
    Result<std::optional<uint32_t>> readIndex(const std::string& scheme, const std::string& subgroup, const std::string& setting,
                                              bool ac) override;
    Result<void> writeIndex(const std::string& scheme, const std::string& subgroup, const std::string& setting, bool ac,
                            uint32_t value) override;
    Result<void> applyScheme(const std::string& scheme) override;
};

}  // namespace lf
