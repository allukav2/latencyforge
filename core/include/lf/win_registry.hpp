#pragma once
#include "lf/registry.hpp"

namespace lf {

// Win32 レジストリ実装。常に 64bit ビュー (KEY_WOW64_64KEY) を使う。
// 注意: ここには Policy 検査は無い。書き込み経路は必ず Engine (Policy 検査済み) を通すこと。
class WinRegistry final : public IRegistry {
public:
    Result<std::optional<RegValue>> read(const RegPath& path) override;
    Result<void> write(const RegPath& path, const RegValue& value) override;
    Result<void> deleteValue(const RegPath& path) override;
};

}  // namespace lf
