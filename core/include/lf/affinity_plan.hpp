#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

#include "lf/sysinfo.hpp"

namespace lf {

enum class AffinityStrategy {
    Auto,              // トポロジーから自動: ハイブリッド → P コア / マルチ L3 → L3 グループ / 単純 → 変更なし
    PerformanceCores,  // ハイブリッド CPU の P コアだけをゲームに
    LargestL3,         // マルチ L3 (マルチ CCD/CCX) のキャッシュが大きい側からゲームに
    ReserveOneCore,    // 単純な構成向け: コア 0 をバックグラウンド用に空け、残りをゲームに
};

struct AffinityOptions {
    AffinityStrategy strategy = AffinityStrategy::Auto;
    // false: ゲームには各 P/通常コアの 1 スレッド目だけを使い、SMT の兄弟スレッドはどちらにも割り当てない。
    bool gameUsesSmtSiblings = true;
};

enum class PlanKind { NoChange, PerformanceCores, L3Groups, ReserveCore };
enum class PlanReason { None, SimpleTopology, UnknownTopology, NotApplicable, TooFewCores };

// ゲームとバックグラウンドの割り当て。Windows のプロセス アフィニティは 1 つのプロセッサグループ内でのみ有効なので、
// すべてのコアは同一グループ (group) に揃える。
struct AffinityPlan {
    PlanKind kind = PlanKind::NoChange;
    PlanReason reason = PlanReason::None;
    uint16_t group = 0;
    uint64_t gameMask = 0;
    uint64_t backgroundMask = 0;
    std::vector<int> gameCores;        // CoreInfo::index
    std::vector<int> backgroundCores;  // CoreInfo::index

    bool effective() const { return kind != PlanKind::NoChange; }
};

constexpr int kMinGameCores = 6;  // L3 グループを足していって、ゲームに最低これだけの物理コアを確保する

// 純関数: OS API を呼ばない。
AffinityPlan planAffinity(const CpuTopology& topology, const AffinityOptions& options);

const char* strategyKey(AffinityStrategy s);  // JSON 用の識別子 ("auto" ...)
bool parseStrategy(std::string_view text, AffinityStrategy& out);

}  // namespace lf
