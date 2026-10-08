#pragma once
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "lf/policy.hpp"
#include "lf/registry.hpp"

namespace lf {

// 適用済み tweak 1 件の記録。original が復元の正本 (nullopt = 適用前は値が存在しなかった)。
struct AppliedRecord {
    std::string tweakId;
    RegPath target;
    std::optional<RegValue> original;
    RegValue applied;
    std::string time;
    bool requiresReboot = false;
};

// 実行中トランザクションの 1 操作。トランザクション開始「前」の値を持つので、クラッシュ後でも正確に戻せる。
struct TxOp {
    std::string tweakId;
    RegPath target;
    std::optional<RegValue> before;
    bool wasTracked = false;  // トランザクション前から applied に記録があったか
};

struct PendingTx {
    std::string id;
    std::string startedAt;
    std::vector<TxOp> ops;
};

struct State {
    std::map<std::string, AppliedRecord> applied;
    std::optional<PendingTx> pending;
};

// ファイルが無ければ空の State。壊れている/ポリシー違反のパスを含む場合は StateCorrupt
// (ファイルは変更しない — 呼び出し側は操作を拒否すること)。
Result<State> loadState(const std::filesystem::path& file, const Policy& policy);
Result<void> saveState(const std::filesystem::path& file, const State& state);

}  // namespace lf
