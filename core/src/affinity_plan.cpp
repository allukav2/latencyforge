#include "lf/affinity_plan.hpp"

#include <algorithm>
#include <numeric>
#include <string_view>

namespace lf {
namespace {

uint64_t lowestBit(uint64_t m) { return m & (~m + 1); }

AffinityPlan noChange(PlanReason why) {
    AffinityPlan p;
    p.kind = PlanKind::NoChange;
    p.reason = why;
    return p;
}

// L3 グループを大きい順 (同サイズなら元の順) に足し、物理コアが kMinGameCores 以上になるまで集める。
// すべてのグループを使い切ってしまう (= バックグラウンドに残らない) 場合は空を返す。
std::vector<int> pickL3Groups(const CpuTopology& t) {
    std::vector<int> order(t.l3Groups.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return t.l3Groups[static_cast<size_t>(a)].sizeBytes > t.l3Groups[static_cast<size_t>(b)].sizeBytes; });
    std::vector<int> chosen;
    int physical = 0;
    for (int idx : order) {
        chosen.push_back(idx);
        physical += t.l3Groups[static_cast<size_t>(idx)].physical;
        if (physical >= kMinGameCores) break;
    }
    if (chosen.size() >= t.l3Groups.size()) return {};
    return chosen;
}

// 割り当て済みのコア群を、プロセッサグループ 1 つに揃えてマスクを作る。
AffinityPlan finish(const CpuTopology& t, PlanKind kind, std::vector<int> game, std::vector<int> bg, const AffinityOptions& opt) {
    if (game.empty()) return noChange(PlanReason::NotApplicable);

    const uint16_t group = t.cores[static_cast<size_t>(game.front())].group;
    auto keep = [&](std::vector<int>& v) {
        v.erase(std::remove_if(v.begin(), v.end(), [&](int i) { return t.cores[static_cast<size_t>(i)].group != group; }), v.end());
    };
    keep(game);
    keep(bg);
    if (game.empty()) return noChange(PlanReason::NotApplicable);

    AffinityPlan p;
    p.kind = kind;
    p.group = group;
    for (int i : game) {
        const uint64_t m = t.cores[static_cast<size_t>(i)].mask;
        p.gameMask |= opt.gameUsesSmtSiblings ? m : lowestBit(m);  // 兄弟スレッドを使わない場合は 1 スレッド目のみ
    }
    for (int i : bg) p.backgroundMask |= t.cores[static_cast<size_t>(i)].mask;
    p.gameCores = std::move(game);
    p.backgroundCores = std::move(bg);
    return p;
}

}  // namespace

AffinityPlan planAffinity(const CpuTopology& t, const AffinityOptions& opt) {
    if (!t.known || t.cores.empty()) return noChange(PlanReason::UnknownTopology);

    AffinityStrategy s = opt.strategy;
    if (s == AffinityStrategy::Auto) {
        if (t.hybrid)
            s = AffinityStrategy::PerformanceCores;
        else if (t.multiL3)
            s = AffinityStrategy::LargestL3;
        else
            return noChange(PlanReason::SimpleTopology);  // 効果が小さい構成: 何も変更しない
    }

    switch (s) {
        case AffinityStrategy::PerformanceCores: {
            if (!t.hybrid) return noChange(PlanReason::NotApplicable);
            std::vector<int> allowed;  // ハイブリッドかつマルチ L3 の場合は、選んだ L3 グループ内の P コアに限る
            if (t.multiL3) {
                const auto chosen = pickL3Groups(t);
                for (int g : chosen)
                    for (int c : t.l3Groups[static_cast<size_t>(g)].cores) allowed.push_back(c);
            }
            std::vector<int> game, bg;
            for (const CoreInfo& c : t.cores) {
                const bool inAllowed = allowed.empty() || std::find(allowed.begin(), allowed.end(), c.index) != allowed.end();
                if (c.kind == CoreKind::Performance && inAllowed)
                    game.push_back(c.index);
                else
                    bg.push_back(c.index);
            }
            return finish(t, PlanKind::PerformanceCores, std::move(game), std::move(bg), opt);
        }
        case AffinityStrategy::LargestL3: {
            if (!t.multiL3) return noChange(PlanReason::NotApplicable);
            const auto chosen = pickL3Groups(t);
            if (chosen.empty()) return noChange(PlanReason::TooFewCores);
            std::vector<int> game, bg;
            for (const CoreInfo& c : t.cores) {
                const bool isGame = std::find(chosen.begin(), chosen.end(), c.l3Group) != chosen.end();
                (isGame ? game : bg).push_back(c.index);
            }
            return finish(t, PlanKind::L3Groups, std::move(game), std::move(bg), opt);
        }
        case AffinityStrategy::ReserveOneCore: {
            if (t.physicalCores < 4) return noChange(PlanReason::TooFewCores);
            // コア 0 は割り込み/DPC が集まりやすいので、ここをバックグラウンド用に空け、ゲームは 1.. に置く。
            std::vector<int> game, bg{t.cores.front().index};
            for (size_t i = 1; i < t.cores.size(); ++i) game.push_back(t.cores[i].index);
            // finish() は game の先頭コアのグループに揃える。バックグラウンドも同じグループのものだけ残る。
            return finish(t, PlanKind::ReserveCore, std::move(game), std::move(bg), opt);
        }
        case AffinityStrategy::Auto: break;
    }
    return noChange(PlanReason::NotApplicable);
}

const char* strategyKey(AffinityStrategy s) {
    switch (s) {
        case AffinityStrategy::Auto: return "auto";
        case AffinityStrategy::PerformanceCores: return "performance_cores";
        case AffinityStrategy::LargestL3: return "largest_l3";
        case AffinityStrategy::ReserveOneCore: return "reserve_one_core";
    }
    return "auto";
}

bool parseStrategy(std::string_view text, AffinityStrategy& out) {
    for (AffinityStrategy s : {AffinityStrategy::Auto, AffinityStrategy::PerformanceCores, AffinityStrategy::LargestL3,
                               AffinityStrategy::ReserveOneCore}) {
        if (text == strategyKey(s)) {
            out = s;
            return true;
        }
    }
    return false;
}

}  // namespace lf
