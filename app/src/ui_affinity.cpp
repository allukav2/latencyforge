// Affinity 自動最適化ページ: 対象ゲームの登録、実行中プロセスからの選択、状態表示、計画のプレビュー。
#include <imgui_internal.h>

#include <algorithm>
#include <bit>
#include <cstring>

#include "lf/affinity_safety.hpp"
#include "ui.hpp"

namespace lfapp {

namespace {

constexpr const char* kStrategyKeys[] = {"affinity.strategy.auto", "affinity.strategy.performance", "affinity.strategy.largest",
                                         "affinity.strategy.reserve"};
constexpr lf::AffinityStrategy kStrategies[] = {lf::AffinityStrategy::Auto, lf::AffinityStrategy::PerformanceCores,
                                                lf::AffinityStrategy::LargestL3, lf::AffinityStrategy::ReserveOneCore};
constexpr const char* kPriorityKeys[] = {"affinity.priority.unchanged", "affinity.priority.aboveNormal", "affinity.priority.high"};
constexpr lf::GamePriority kPriorities[] = {lf::GamePriority::Unchanged, lf::GamePriority::AboveNormal, lf::GamePriority::High};

int indexOf(lf::AffinityStrategy s) {
    for (int i = 0; i < 4; ++i)
        if (kStrategies[i] == s) return i;
    return 0;
}
int indexOf(lf::GamePriority p) {
    for (int i = 0; i < 3; ++i)
        if (kPriorities[i] == p) return i;
    return 0;
}

}  // namespace

// ---------------------------------------------------------------- メインループ連携

int Ui::pollIntervalMs() const {
    // 2 秒ごとのポーリング。無効 (または登録ゲームなし) の間は 0 = タイマー自体を止める (何も動かさない)。
    return (m_be && m_be->affinity && m_be->affinity->needsPolling()) ? 2000 : 0;
}

bool Ui::pollTimer() { return m_be && m_be->affinity && m_be->affinity->tick(); }

void Ui::shutdown() {
    if (m_be && m_be->affinity) {
        m_be->affinity->restoreAll();  // ゲーム終了時だけでなく、アプリ終了時にも必ず元に戻す
        m_be->log.info("app", "shutdown: affinity restored");
    }
}

// ---------------------------------------------------------------- 設定の変更

void Ui::commitAffinityConfig(const lf::AffinityConfig& cfg) {
    m_be->affinity->setConfig(cfg);  // 無効化/削除なら、適用中のものはここで元に戻る
    m_be->saveAffinityConfig();
}

bool Ui::addAffinityProfile(const std::string& exeName) {
    m_addErrorKey.clear();
    if (!lf::isValidExeName(exeName)) {
        m_addErrorKey = "affinity.err.invalid";
        return false;
    }
    const std::string lower = lf::lowerAscii(exeName);
    if (lf::isNeverTouchName(lower)) {
        m_addErrorKey = "affinity.err.protected";
        return false;
    }
    lf::AffinityConfig cfg = m_be->affinity->config();
    for (const auto& p : cfg.profiles)
        if (std::find(p.exeNames.begin(), p.exeNames.end(), lower) != p.exeNames.end()) {
            m_addErrorKey = "affinity.err.duplicate";
            return false;
        }
    int n = 1;
    auto idUsed = [&](const std::string& id) {
        return std::any_of(cfg.profiles.begin(), cfg.profiles.end(), [&](const lf::AffinityProfile& p) { return p.id == id; });
    };
    while (idUsed("p" + std::to_string(n))) ++n;
    lf::AffinityProfile p;
    p.id = "p" + std::to_string(n);
    p.name = exeName;
    p.exeNames = {lower};
    cfg.profiles.push_back(std::move(p));
    commitAffinityConfig(cfg);
    return true;
}

// ---------------------------------------------------------------- ページ

std::string Ui::planSummary(const lf::AffinityPlan& plan) const {
    auto threads = [](uint64_t m) { return std::to_string(std::popcount(m)); };
    switch (plan.kind) {
        case lf::PlanKind::PerformanceCores:
            return fmt("affinity.plan.perf", {std::to_string(plan.gameCores.size()), threads(plan.gameMask),
                                              std::to_string(plan.backgroundCores.size()), threads(plan.backgroundMask)});
        case lf::PlanKind::L3Groups:
            return fmt("affinity.plan.l3", {std::to_string(plan.gameCores.size()), threads(plan.gameMask),
                                            std::to_string(plan.backgroundCores.size()), threads(plan.backgroundMask)});
        case lf::PlanKind::ReserveCore:
            return fmt("affinity.plan.reserve", {std::to_string(plan.backgroundCores.size()), std::to_string(plan.gameCores.size())});
        default:
            break;
    }
    switch (plan.reason) {
        case lf::PlanReason::SimpleTopology: return t("affinity.plan.noneSimple");
        case lf::PlanReason::UnknownTopology: return t("sys.topologyUnknown");
        case lf::PlanReason::TooFewCores: return t("affinity.plan.noneTooFew");
        default: return t("affinity.plan.noneNotApplicable");
    }
}

void Ui::pageAffinity() {
    if (m_demo && !m_demoShowDone && m_demoShow == "affinity-picker") {  // 開発用: スクリーンショット
        m_demoShowDone = true;
        m_pickerList = lf::listTargetCandidates(*m_be->procApi);
        m_pickerOpen = true;
    }
    pageHeader(t("nav.affinity"), t("affinity.subtitle"));
    drawFeatureBanner(Page::Affinity);
    drawAntiCheatCard();
    if (!m_feat[static_cast<size_t>(Page::Affinity)].available) return;
    drawAffinityStatusCard();
    drawAffinityProfiles();
}

// アンチチートに関する注意は常に表示する。
void Ui::drawAntiCheatCard() {
    if (!beginCard("##aff_anticheat")) {
        endCard();
        return;
    }
    const ImVec2 p = ImGui::GetWindowPos();
    const ImVec2 s = ImGui::GetWindowSize();
    ImGui::GetWindowDrawList()->AddRect(p + ImVec2(0.5f, 0.5f), p + s - ImVec2(0.5f, 0.5f), u32(withAlpha(m_pal.warn, 0.55f)), S(12));
    pushBold();
    coloredText(m_pal.warn, t("affinity.antiCheatTitle"));
    popFont();
    dimText(t("affinity.antiCheatBody"));
    endCard();
}

void Ui::drawAffinityStatusCard() {
    lf::AffinityManager& mgr = *m_be->affinity;
    const lf::AffinityStatus& st = mgr.status();
    lf::AffinityConfig cfg = mgr.config();

    if (!beginCard("##aff_status")) {
        endCard();
        return;
    }
    ImGui::AlignTextToFramePadding();
    pushBold();
    ImGui::TextUnformatted(t("affinity.enable"));
    popFont();
    ImGui::SameLine(ImGui::GetContentRegionMax().x - S(44));
    if (toggleRaw("aff_enable", cfg.enabled, true, nullptr)) {
        cfg.enabled = !cfg.enabled;
        commitAffinityConfig(cfg);
    }
    dimText(t("affinity.enableDesc"));
    ImGui::Dummy(ImVec2(0, S(4)));

    // 状態
    const char* label = "affinity.state.disabled";
    ImVec4 color = m_pal.textDim;
    switch (st.state) {
        case lf::AffinityStatus::State::Idle: label = "affinity.state.idle"; color = m_pal.accentHover; break;
        case lf::AffinityStatus::State::GameActive: label = "affinity.state.active"; color = m_pal.ok; break;
        case lf::AffinityStatus::State::GameNoEffect: label = "affinity.state.noEffect"; color = m_pal.warn; break;
        case lf::AffinityStatus::State::GameBlocked: label = "affinity.state.blocked"; color = m_pal.danger; break;
        default: break;
    }
    badge(t(label), color);
    if (st.state == lf::AffinityStatus::State::GameActive) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(fmt("affinity.state.activeFmt", {st.gameExe, std::to_string(st.gamePid)}).c_str());
        dimText(fmt("affinity.managedFmt", {std::to_string(mgr.managed().size())}).c_str());
    } else if (st.state == lf::AffinityStatus::State::GameNoEffect || st.state == lf::AffinityStatus::State::GameBlocked) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(st.gameExe.c_str());
        if (!st.noteKey.empty()) coloredText(color, t(st.noteKey.c_str()));
    } else if (st.state == lf::AffinityStatus::State::Idle) {
        dimText(t("affinity.state.idleDesc"));
    }

    // 計画のプレビュー (適用中はその計画、そうでなければ先頭プロファイルの設定で計算)
    lf::AffinityOptions opts;
    for (const auto& p : cfg.profiles)
        if (p.id == st.profileId) opts = p.options;
    if (st.profileId.empty() && !cfg.profiles.empty()) opts = cfg.profiles.front().options;
    const lf::AffinityPlan plan = st.state == lf::AffinityStatus::State::GameActive ? st.plan : lf::planAffinity(m_sys.cpu, opts);

    ImGui::Dummy(ImVec2(0, S(6)));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, S(2)));
    pushBold();
    ImGui::TextUnformatted(t("affinity.planTitle"));
    popFont();
    dimText(planSummary(plan).c_str());
    drawTopologyMap(&plan);

    ImGui::Dummy(ImVec2(0, S(4)));
    if (secondaryButton(t("affinity.restoreNow"), 0, !mgr.managed().empty(), t("affinity.nothingToRestore"))) mgr.restoreAll();
    if (m_demo) {
        ImGui::SameLine();
        if (secondaryButton(m_be->demoGameRunning() ? t("affinity.demoStop") : t("affinity.demoStart"))) {
            m_be->demoToggleGame();
            mgr.tick();
        }
    }
    endCard();
}

void Ui::drawAffinityProfiles() {
    lf::AffinityConfig cfg = m_be->affinity->config();
    bool dirty = false;
    int removeIndex = -1;

    pushBold();
    ImGui::TextUnformatted(t("affinity.profiles"));
    popFont();
    if (m_be->affinityConfigError) coloredText(m_pal.danger, t("error.definitionInvalid"));
    if (cfg.profiles.empty()) dimText(t("affinity.noProfiles"));

    for (size_t i = 0; i < cfg.profiles.size(); ++i) {
        lf::AffinityProfile& p = cfg.profiles[i];
        if (!beginCard(("##aff_prof_" + p.id).c_str())) {
            endCard();
            continue;
        }
        ImGui::AlignTextToFramePadding();
        pushBold();
        ImGui::TextUnformatted(p.name.c_str());
        popFont();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, m_pal.textDim);
        ImGui::TextUnformatted(p.exeNames.empty() ? "" : p.exeNames.front().c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine(ImGui::GetContentRegionMax().x - S(44));
        if (toggleRaw(("prof_en_" + p.id).c_str(), p.enabled, true, nullptr)) {
            p.enabled = !p.enabled;
            dirty = true;
        }
        ImGui::Dummy(ImVec2(0, S(4)));

        ImGui::PushID(static_cast<int>(i));
        // 戦略
        dimText(t("affinity.strategyLabel"));
        ImGui::SetNextItemWidth(S(300));
        if (ImGui::BeginCombo("##strategy", t(kStrategyKeys[indexOf(p.options.strategy)]))) {
            for (int k = 0; k < 4; ++k)
                if (ImGui::Selectable(t(kStrategyKeys[k]), indexOf(p.options.strategy) == k)) {
                    p.options.strategy = kStrategies[k];
                    dirty = true;
                }
            ImGui::EndCombo();
        }
        // 優先度
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(240));
        if (ImGui::BeginCombo("##priority", t(kPriorityKeys[indexOf(p.priority)]))) {
            for (int k = 0; k < 3; ++k)
                if (ImGui::Selectable(t(kPriorityKeys[k]), indexOf(p.priority) == k)) {
                    p.priority = kPriorities[k];
                    dirty = true;
                }
            ImGui::EndCombo();
        }
        ImGui::Dummy(ImVec2(0, S(4)));

        // オプション
        ImGui::AlignTextToFramePadding();
        if (toggleRaw("smt", p.options.gameUsesSmtSiblings, true, nullptr)) {
            p.options.gameUsesSmtSiblings = !p.options.gameUsesSmtSiblings;
            dirty = true;
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(t("affinity.smtSiblings"));
        ImGui::SameLine(0, S(28));
        if (toggleRaw("bg", p.moveBackground, true, nullptr)) {
            p.moveBackground = !p.moveBackground;
            dirty = true;
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(t("affinity.moveBackground"));
        ImGui::Dummy(ImVec2(0, S(4)));
        if (p.priority != lf::GamePriority::Unchanged) dimText(t("affinity.priorityNote"));
        if (secondaryButton(t("affinity.remove"))) removeIndex = static_cast<int>(i);
        ImGui::PopID();
        endCard();
    }
    if (removeIndex >= 0) {
        cfg.profiles.erase(cfg.profiles.begin() + removeIndex);
        dirty = true;
    }

    // 追加
    if (beginCard("##aff_add")) {
        if (primaryButton(t("affinity.addRunning"))) {
            m_pickerList = lf::listTargetCandidates(*m_be->procApi);
            m_pickerSel = -1;
            m_pickerFilter[0] = 0;
            m_addErrorKey.clear();
            m_pickerOpen = true;
        }
        ImGui::Dummy(ImVec2(0, S(4)));
        dimText(t("affinity.addManual"));
        ImGui::SetNextItemWidth(S(320));
        ImGui::InputTextWithHint("##manual", t("affinity.exeHint"), m_manualExe, sizeof m_manualExe);
        ImGui::SameLine();
        if (secondaryButton(t("affinity.add"), 0, m_manualExe[0] != 0, t("affinity.exeHint"))) {
            if (addAffinityProfile(m_manualExe)) m_manualExe[0] = 0;
        }
        if (!m_addErrorKey.empty()) coloredText(m_pal.danger, t(m_addErrorKey.c_str()));
    }
    endCard();  // BeginChild は戻り値に関係なく EndChild が必要 (画面外でクリップされて false になっても対にする)

    if (dirty) commitAffinityConfig(cfg);
}

// 実行中のプロセスから選ぶ。安全に対象にできるもの (保護/システム/他ユーザー/アンチチートを除く) だけが並ぶ。
void Ui::drawProcessPicker() {
    if (!beginModalWindow("##picker", 640)) return;
    ImGui::PushFont(m_fontBold, 22.0f);
    ImGui::TextUnformatted(t("affinity.pickerTitle"));
    ImGui::PopFont();
    dimText(t("affinity.pickerHint"));
    ImGui::Dummy(ImVec2(0, S(4)));

    ImGui::SetNextItemWidth(S(360));
    ImGui::InputTextWithHint("##filter", t("affinity.pickerFilter"), m_pickerFilter, sizeof m_pickerFilter);
    ImGui::SameLine();
    if (secondaryButton(t("common.reload"))) {
        m_pickerList = lf::listTargetCandidates(*m_be->procApi);
        m_pickerSel = -1;
    }
    ImGui::Dummy(ImVec2(0, S(4)));

    const std::string filter = lf::lowerAscii(m_pickerFilter);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, m_pal.panel);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(10));
    ImGui::BeginChild("##pickerlist", ImVec2(0, S(300)), ImGuiChildFlags_Borders);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    int shown = 0;
    for (size_t i = 0; i < m_pickerList.size(); ++i) {
        const lf::ProcessState& c = m_pickerList[i];
        if (!filter.empty() && lf::lowerAscii(c.exeName).find(filter) == std::string::npos) continue;
        ++shown;
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(c.exeName.c_str(), m_pickerSel == static_cast<int>(i), ImGuiSelectableFlags_AllowDoubleClick)) {
            m_pickerSel = static_cast<int>(i);
            if (ImGui::IsMouseDoubleClicked(0) && addAffinityProfile(c.exeName)) {
                m_pickerOpen = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::PopID();
    }
    if (shown == 0) dimText(t("affinity.pickerEmpty"));
    ImGui::EndChild();
    if (!m_addErrorKey.empty()) coloredText(m_pal.danger, t(m_addErrorKey.c_str()));

    ImGui::Dummy(ImVec2(0, S(8)));
    const float w1 = S(120), w2 = S(160), gap = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - (w1 + gap + w2));
    if (secondaryButton(t("common.cancel"), w1) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        m_pickerOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    const bool canAdd = m_pickerSel >= 0 && m_pickerSel < static_cast<int>(m_pickerList.size());
    if (primaryButton(t("affinity.add"), w2, canAdd, t("affinity.pickerNone"))) {
        if (addAffinityProfile(m_pickerList[static_cast<size_t>(m_pickerSel)].exeName)) {
            m_pickerOpen = false;
            ImGui::CloseCurrentPopup();
        }
    }
    endModalWindow();
}

}  // namespace lfapp
