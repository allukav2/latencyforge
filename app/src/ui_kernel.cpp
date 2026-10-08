// カーネル/タイマーページ: 実データ (tweak 定義 + Engine) への接続、差分プレビュー/結果ダイアログ。
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>

#include "lf/system.hpp"
#include "ui.hpp"

namespace lfapp {

namespace {

const char* statusName(lf::ItemStatus s) {
    switch (s) {
        case lf::ItemStatus::WouldChange: return "WouldChange";
        case lf::ItemStatus::WouldRevert: return "WouldRevert";
        case lf::ItemStatus::Applied: return "Applied";
        case lf::ItemStatus::AlreadyApplied: return "AlreadyApplied";
        case lf::ItemStatus::Skipped: return "Skipped";
        case lf::ItemStatus::Failed: return "Failed";
        case lf::ItemStatus::Blocked: return "Blocked";
        case lf::ItemStatus::RolledBack: return "RolledBack";
        case lf::ItemStatus::RollbackFailed: return "RollbackFailed";
        case lf::ItemStatus::Reverted: return "Reverted";
        case lf::ItemStatus::NotApplied: return "NotApplied";
    }
    return "Failed";
}

bool wouldChange(lf::ItemStatus s) { return s == lf::ItemStatus::WouldChange || s == lf::ItemStatus::WouldRevert; }

}  // namespace

// ---------------------------------------------------------------- 値の表示 / 状態

std::string Ui::shortValue(const std::optional<lf::RegValue>& v) const {
    if (!v) return t("common.absent");
    if (v->isNumeric()) return std::to_string(v->number);
    return "\"" + v->text + "\"";
}

ImVec4 Ui::statusColor(lf::ItemStatus s) const {
    switch (s) {
        case lf::ItemStatus::WouldChange:
        case lf::ItemStatus::WouldRevert: return m_pal.accentHover;
        case lf::ItemStatus::Applied:
        case lf::ItemStatus::Reverted: return m_pal.ok;
        case lf::ItemStatus::RolledBack: return m_pal.warn;
        case lf::ItemStatus::Failed:
        case lf::ItemStatus::Blocked:
        case lf::ItemStatus::RollbackFailed: return m_pal.danger;
        default: return m_pal.textDim;
    }
}

const lf::TweakDef* Ui::findTweak(const std::string& anyId) const { return m_be->catalog->find(lf::baseTweakId(anyId)); }

const lf::TweakDef* Ui::resolvedDef(const lf::TweakDef& base) {
    if (!base.isPowerTemplate() || m_activeScheme.empty()) return &base;  // 解決できなければテンプレートのまま (Engine が拒否する)
    const std::string key = base.id + "@" + m_activeScheme;
    auto it = m_resolvedStore.find(key);
    if (it == m_resolvedStore.end()) it = m_resolvedStore.emplace(key, lf::resolveForScheme(base, m_activeScheme)).first;
    return &it->second;
}

// このページのカテゴリに属する、バックアップのある (= 元に戻せる) tweak の ID。他の電源プランで適用したものも含む。
std::vector<std::string> Ui::trackedIdsIn(const std::string& category) const {
    std::vector<std::string> ids;
    for (const auto& [id, rec] : m_be->engine->applied()) {
        const lf::TweakDef* d = findTweak(id);
        if (d && d->category == category) ids.push_back(id);
    }
    return ids;
}

bool Ui::opsEnabled(const char** reason, Page page) const {
    const size_t k = static_cast<size_t>(page);
    if (!m_feat[k].available) {  // このシステムでは対象外 (OS ビルド / ARM64 など)
        *reason = m_featReason[k].c_str();
        return false;
    }
    if (!m_be->engine->loaded()) {
        *reason = t("kernel.disabledState");
        return false;
    }
    if (m_be->engine->pending()) {
        *reason = t("kernel.disabledPending");
        return false;
    }
    return true;
}

void Ui::refreshRows() {
    // 現在の電源プラン (電源設定の tweak は、このプランに解決して扱う)。読めなければ空 = 解決されず、操作は拒否される。
    m_activeScheme.clear();
    m_activeSchemeName.clear();
    if (auto a = m_be->power().activeSchemeGuid(); a.ok()) {
        m_activeScheme = a.value();
        if (auto n = m_be->power().schemeName(a.value()); n.ok()) m_activeSchemeName = n.value();
    }

    // 全 tweak の状態を 1 回だけ読む (ページ表示・プリセットの件数表示で共用)。キーは解決済みの ID。
    std::unordered_map<std::string, lf::TweakStatus> cache;
    for (const auto& d : m_be->catalog->all()) {
        const lf::TweakDef* r = resolvedDef(d);
        cache.emplace(r->id, m_be->engine->status(*r));
    }

    m_rowsByCat.clear();
    for (const char* cat : {"kernel", "usb"}) {
        auto& rows = m_rowsByCat[cat];
        for (const lf::TweakDef* d : m_be->tweaksIn(cat)) {
            const lf::TweakDef* r = resolvedDef(*d);
            rows.push_back({r, cache[r->id]});
        }
    }

    m_presetInfo.clear();
    for (const lf::PresetDef& p : m_be->presets) {
        PresetInfo pi;
        for (const std::string& id : p.tweakIds) {
            ++pi.total;
            const lf::TweakDef* base = m_be->catalog->find(id);
            if (!base) continue;
            auto it = cache.find(resolvedDef(*base)->id);
            if (it == cache.end()) continue;
            const lf::TweakState s = it->second.state;
            if (s == lf::TweakState::Unsupported || s == lf::TweakState::Blocked)
                ++pi.unsupported;
            else if (s == lf::TweakState::NotApplied || s == lf::TweakState::Drifted)
                ++pi.pending;
        }
        m_presetInfo.push_back(pi);
    }
    m_rebootPending = m_be->engine->loaded() && m_be->engine->rebootPending(lf::systemBootTime());
    m_rowsDirty = false;
}

void Ui::requestApplyPreset(size_t index) {
    if (index >= m_be->presets.size()) return;
    const lf::PresetDef& p = m_be->presets[index];
    std::vector<const lf::TweakDef*> defs;
    for (const lf::TweakDef* b : lf::resolvePreset(p, *m_be->catalog)) defs.push_back(resolvedDef(*b));
    requestApply(std::move(defs));  // 通常の適用と同じ: 差分プレビュー → 確認 → 適用
    m_op.presetTitle = pick(p.title);
}

void Ui::drawPresetCards() {
    pushBold();
    ImGui::TextUnformatted(t("home.presets"));
    popFont();
    if (m_be->presets.empty()) {
        dimText(t("home.presetNone"));
        return;
    }
    if (m_rowsDirty) refreshRows();
    const char* why = nullptr;
    const bool ops = opsEnabled(&why);

    if (m_demo && !m_demoShowDone && m_demoShow.rfind("preset-", 0) == 0) {  // 開発用: スクリーンショット
        m_demoShowDone = true;
        for (size_t i = 0; i < m_be->presets.size(); ++i)
            if ("preset-" + m_be->presets[i].id == m_demoShow) requestApplyPreset(i);
    }

    const int n = static_cast<int>(m_be->presets.size());
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float cw = (ImGui::GetContentRegionAvail().x - gap * (n - 1)) / static_cast<float>(n);
    for (int i = 0; i < n; ++i) {
        const lf::PresetDef& p = m_be->presets[static_cast<size_t>(i)];
        const PresetInfo pi = i < static_cast<int>(m_presetInfo.size()) ? m_presetInfo[static_cast<size_t>(i)] : PresetInfo{};
        const bool medium = lf::presetRisk(p, *m_be->catalog) == lf::Risk::Medium;
        if (i) ImGui::SameLine();
        if (beginCard(("##preset_" + p.id).c_str(), S(290), cw)) {
            pushBold();
            ImGui::TextUnformatted(pick(p.title).c_str());
            popFont();
            badge(medium ? t("common.riskMid") : t("common.riskLow"), medium ? m_pal.warn : m_pal.ok);
            ImGui::Dummy(ImVec2(0, S(2)));
            dimText(pick(p.description).c_str());
            ImGui::Dummy(ImVec2(0, S(2)));
            ImGui::TextUnformatted(fmt("home.presetPending", {std::to_string(pi.pending), std::to_string(pi.total)}).c_str());
            if (pi.unsupported > 0)
                coloredText(m_pal.textDim, fmt("home.presetUnsupported", {std::to_string(pi.unsupported)}).c_str());

            ImGui::SetCursorPosY(ImGui::GetWindowHeight() - S(16) - ImGui::GetFrameHeight());
            const char* btnWhy = !ops ? why : t("home.presetNothing");
            if (secondaryButton(t("home.previewApply"), -FLT_MIN, ops && pi.pending > 0, btnWhy)) requestApplyPreset(static_cast<size_t>(i));
        }
        endCard();
    }
}

// ---------------------------------------------------------------- 操作の流れ

void Ui::requestApply(std::vector<const lf::TweakDef*> tweaks) {
    m_op = {};
    m_op.kind = OpKind::Apply;
    m_op.tweaks = std::move(tweaks);
    m_op.dryRun = m_dryRun;
    m_op.preview = m_be->engine->apply(m_op.tweaks, {.dryRun = true});  // 先に必ず差分を計算 (何も書かない)
    m_modal = Modal::Preview;
}

void Ui::requestRevert(std::vector<std::string> ids) {
    m_op = {};
    m_op.kind = OpKind::Revert;
    m_op.ids = std::move(ids);
    m_op.dryRun = m_dryRun;
    m_op.preview = m_be->engine->revert(m_op.ids, /*dryRun=*/true);
    m_modal = Modal::Preview;
}

void Ui::confirmOperation() {
    // プレビューで「確認」された場合にのみここへ来る。
    if (m_op.kind == OpKind::Apply) {
        m_result = m_be->engine->apply(m_op.tweaks);
        m_resultKind = ResultKind::Apply;
    } else {
        m_result = m_be->engine->revert(m_op.ids);
        m_resultKind = ResultKind::Revert;
    }
    m_rowsDirty = true;
    m_modal = Modal::Result;
}

// ---------------------------------------------------------------- ページ

void Ui::pageTweaks(const char* category, Page page) {
    if (m_rowsDirty) refreshRows();
    m_curCat = category;
    const bool isUsb = m_curCat == "usb";

    // 開発用 (--demo): --show preview / result / expanded (カーネル)、usb-preview / usb-result / usb-expanded (USB)
    if (m_demo && !m_demoShowDone) {  // 開発用: スクリーンショットのため、ダイアログ等を自動で開く
        const std::string show = m_demoShow;
        const bool forThisPage = isUsb ? show.rfind("usb-", 0) == 0 : (show == "expanded" || show == "preview" || show == "result");
        if (forThisPage) {
            m_demoShowDone = true;
            const std::string what = isUsb ? show.substr(4) : show;
            if (what == "expanded" && !rows().empty()) m_expanded[rows().front().def->id] = true;
            if (what == "preview" || what == "result") {
                std::vector<const lf::TweakDef*> todo;
                for (const Row& r : rows())
                    if (r.st.state == lf::TweakState::NotApplied || r.st.state == lf::TweakState::Drifted) todo.push_back(r.def);
                requestApply(std::move(todo));
                if (what == "result") confirmOperation();
            }
        }
    }

    pageHeader(t(isUsb ? "nav.usb" : "nav.kernel"), t(isUsb ? "usb.subtitle" : "kernel.subtitle"));
    drawFeatureBanner(page);
    if (isUsb) drawUsbInfoCard();
    drawStatusBanners();

    const char* why = nullptr;
    bool ops = opsEnabled(&why, page);
    if (ops && isUsb && m_activeScheme.empty()) {  // 電源プランを読めなければ操作しない (適用先を特定できない)
        ops = false;
        why = t("usb.noScheme");
    }
    drawToolbar(ops, why);
    for (const Row& r : rows()) tweakCard(r, ops, why);
    if (rows().empty()) dimText(t("kernel.empty"));
}

// USB ページ: 現在の電源プランと、適用先についての説明。
void Ui::drawUsbInfoCard() {
    if (beginCard("##usb_info")) {
        pushBold();
        ImGui::TextUnformatted(t("usb.planTitle"));
        popFont();
        if (m_activeScheme.empty()) {
            coloredText(m_pal.danger, t("usb.noScheme"));
        } else {
            ImGui::TextUnformatted((m_activeSchemeName.empty() ? m_activeScheme : m_activeSchemeName).c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, m_pal.textDim);
            ImGui::TextUnformatted(m_activeScheme.c_str());
            ImGui::PopStyleColor();
        }
        dimText(t("usb.planNote"));
        // 他の電源プランで適用した項目の数 (これらも「すべて元に戻す」で戻る)
        int others = 0;
        for (const auto& id : trackedIdsIn("usb"))
            if (id.find('@') != std::string::npos && id.find(m_activeScheme) == std::string::npos) ++others;
        if (others > 0) coloredText(m_pal.warn, fmt("usb.otherPlans", {std::to_string(others)}).c_str());
    }
    endCard();
}

void Ui::drawStatusBanners() {
    auto bannerBegin = [&](const char* id, const ImVec4& color) {
        const bool open = beginCard(id);
        if (open) {
            const ImVec2 p = ImGui::GetWindowPos();
            const ImVec2 s = ImGui::GetWindowSize();
            ImGui::GetWindowDrawList()->AddRect(p + ImVec2(0.5f, 0.5f), p + s - ImVec2(0.5f, 0.5f), u32(withAlpha(color, 0.55f)),
                                                S(12));
        }
        return open;
    };

    if (m_demo) {
        if (bannerBegin("##b_demo", m_pal.warn)) {
            badge(t("common.demo"), m_pal.warn);
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextWrapped("%s", t("kernel.demoBanner"));
        }
        endCard();
    }
    if (m_be->stateError) {
        if (bannerBegin("##b_state", m_pal.danger)) {
            pushBold();
            coloredText(m_pal.danger, t("kernel.stateErrorTitle"));
            popFont();
            dimText(t("kernel.stateErrorBody"));
            dimText(t(lf::errorKey(m_be->stateError->code)));
        }
        endCard();
    }
    if (m_be->engine->pending()) {
        if (bannerBegin("##b_pending", m_pal.warn)) {
            pushBold();
            coloredText(m_pal.warn, t("kernel.pendingTitle"));
            popFont();
            dimText(t("kernel.pendingBody"));
            if (primaryButton(t("kernel.pendingButton"))) m_recoveryDeferred = false;
        }
        endCard();
    }
    if (m_rebootPending && m_curCat == "kernel") {  // 再起動が必要なのはレジストリの tweak だけ (電源設定は即時に反映される)
        if (bannerBegin("##b_reboot", m_pal.warn)) {
            pushBold();
            coloredText(m_pal.warn, t("kernel.rebootTitle"));
            popFont();
            dimText(t("kernel.rebootBody"));
        }
        endCard();
    }
    if (!m_be->definitionIssues.empty()) {
        if (bannerBegin("##b_defs", m_pal.danger)) {
            pushBold();
            coloredText(m_pal.danger, fmt("kernel.defIssuesTitle", {std::to_string(m_be->definitionIssues.size())}).c_str());
            popFont();
            const auto& i = m_be->definitionIssues.front();
            dimText((i.source + (i.tweakId.empty() ? "" : " [" + i.tweakId + "]") + ": " + i.message).c_str());
        }
        endCard();
    }
}

void Ui::drawToolbar(bool ops, const char* why) {
    int applicable = 0;
    std::vector<const lf::TweakDef*> todo;
    for (const Row& r : rows()) {
        if (r.st.state == lf::TweakState::NotApplied || r.st.state == lf::TweakState::Drifted) {
            ++applicable;
            todo.push_back(r.def);
        }
    }
    const std::vector<std::string> trackedIds = trackedIdsIn(m_curCat);  // 他の電源プランで適用したものも含む
    const size_t tracked = trackedIds.size();

    if (!beginCard("##toolbar")) {
        endCard();
        return;
    }
    ImGui::AlignTextToFramePadding();
    pushBold();
    ImGui::TextUnformatted(t("kernel.dryRun"));
    popFont();
    ImGui::SameLine(ImGui::GetContentRegionMax().x - S(44));
    toggle("dryrun", &m_dryRun);
    dimText(t("kernel.dryRunDesc"));
    ImGui::Dummy(ImVec2(0, S(2)));

    const std::string applyLabel = fmt("kernel.applyAll", {std::to_string(applicable)});
    const char* applyWhy = !ops ? why : t("kernel.nothingToApply");
    if (primaryButton(applyLabel.c_str(), 0, ops && applicable > 0, applyWhy)) requestApply(todo);
    ImGui::SameLine();
    const char* revertWhy = !ops ? why : t("common.nothingToRevert");
    if (secondaryButton(t("kernel.revertAll"), 0, ops && tracked > 0, revertWhy)) requestRevert(trackedIds);
    ImGui::SameLine();
    if (secondaryButton(t("common.reload"))) m_rowsDirty = true;
    endCard();
}

void Ui::tweakCard(const Row& r, bool ops, const char* why) {
    const lf::TweakDef& d = *r.def;
    const lf::TweakStatus& st = r.st;
    const bool unavailable = st.state == lf::TweakState::Unsupported || st.state == lf::TweakState::Blocked;
    if (unavailable) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.62f);  // グレーアウト

    if (beginCard(d.id.c_str())) {
        ImGui::AlignTextToFramePadding();
        pushBold();
        ImGui::TextUnformatted(pick(d.title).c_str());
        popFont();
        ImGui::SameLine();
        badge(d.risk == lf::Risk::Low ? t("common.riskLow") : t("common.riskMid"),
              d.risk == lf::Risk::Low ? m_pal.ok : m_pal.warn);
        ImGui::SameLine();
        const char* stateKey = "state.notApplied";
        ImVec4 stateColor = m_pal.textDim;
        switch (st.state) {
            case lf::TweakState::Applied: stateKey = "state.applied"; stateColor = m_pal.accentHover; break;
            case lf::TweakState::Drifted: stateKey = "state.drifted"; stateColor = m_pal.warn; break;
            case lf::TweakState::AlreadyAtTarget: stateKey = "state.alreadyAtTarget"; stateColor = m_pal.ok; break;
            case lf::TweakState::Unsupported: stateKey = "state.unsupported"; break;
            case lf::TweakState::Blocked: stateKey = "state.blocked"; stateColor = m_pal.danger; break;
            default: break;
        }
        badge(t(stateKey), stateColor);
        if (d.requiresReboot) {
            ImGui::SameLine();
            badge(t("common.restartRequired"), m_pal.warn);
        }

        // トグル: ON = 目標値になっている。クリックすると、必ず差分プレビューを経由する。
        const bool on = st.state == lf::TweakState::Applied || st.state == lf::TweakState::AlreadyAtTarget;
        const bool canToggle = ops && (st.state == lf::TweakState::NotApplied || st.state == lf::TweakState::Applied ||
                                       st.state == lf::TweakState::Drifted);
        std::string reason;
        if (!ops) {
            reason = why ? why : "";
        } else if (st.state == lf::TweakState::AlreadyAtTarget) {
            reason = t("tweak.alreadyAtTarget");
        } else if (st.state == lf::TweakState::Unsupported) {
            const std::string range = d.maxBuild == 0xFFFFFFFFu ? std::to_string(d.minBuild) + "+"
                                                                : std::to_string(d.minBuild) + "-" + std::to_string(d.maxBuild);
            reason = fmt("tweak.unsupportedBuild", {std::to_string(m_be->osBuild), range});
        } else if (st.state == lf::TweakState::Blocked && st.error) {
            // 電源設定が、現在の電源プランに存在しない場合は専用の説明にする
            reason = (d.isPowerTemplate() || d.id.find('@') != std::string::npos) && st.error->code == lf::ErrorCode::UnsupportedValueType
                         ? t("usb.settingMissing")
                         : t(lf::errorKey(st.error->code));
        }
        ImGui::SameLine(ImGui::GetContentRegionMax().x - S(44));
        if (toggleRaw(d.id.c_str(), on, canToggle, reason.empty() ? nullptr : reason.c_str())) {
            if (on)
                requestRevert({d.id});
            else
                requestApply({&d});
        }

        dimText(pick(d.summary).c_str());
        ImGui::Dummy(ImVec2(0, S(2)));

        // 現在値 → 変更後
        const lf::AppliedRecord* rec = nullptr;
        if (st.tracked) {
            auto it = m_be->engine->applied().find(d.id);
            if (it != m_be->engine->applied().end()) rec = &it->second;
        }
        auto dim = [&](const char* s) { ImGui::PushStyleColor(ImGuiCol_Text, m_pal.textDim); ImGui::TextUnformatted(s); ImGui::PopStyleColor(); };
        dim(t("common.currentValue"));
        ImGui::SameLine();
        ImGui::TextUnformatted(unavailable ? "-" : shortValue(st.current).c_str());
        if (st.state == lf::TweakState::NotApplied || st.state == lf::TweakState::Drifted) {
            ImGui::SameLine();
            dim("→");
            ImGui::SameLine();
            dim(t("common.afterValue"));
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, m_pal.accentHover);
            ImGui::TextUnformatted(shortValue(d.data).c_str());
            ImGui::PopStyleColor();
        }
        if (rec) {
            dim(t("common.originalValue"));
            ImGui::SameLine();
            ImGui::TextUnformatted(shortValue(rec->original).c_str());
        }

        // 詳細 (展開)
        ImGui::Dummy(ImVec2(0, S(2)));
        bool& open = m_expanded[d.id];
        const float o = expander(("exp_" + d.id).c_str(), t("common.details"), &open);
        if (o > 0.01f) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * o);
            ImGui::Separator();
            if (!pick(d.details).empty()) dimText(pick(d.details).c_str());
            dimText(d.target.display().c_str());
            coloredText(m_pal.warn, pick(d.note).empty() ? t("common.buildDependent") : pick(d.note).c_str());
            ImGui::PopStyleVar();
        }

        ImGui::Dummy(ImVec2(0, S(4)));
        const bool canRevert = ops && st.tracked;
        const char* revertWhy = !ops ? why : t("common.nothingToRevert");
        if (secondaryButton(t("common.revert"), 0, canRevert, revertWhy)) requestRevert({d.id});
    }
    endCard();
    if (unavailable) ImGui::PopStyleVar();
}

// ---------------------------------------------------------------- ダイアログ共通

bool Ui::beginModalWindow(const char* id, float width) {
    if (!ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(S(width), 0), ImVec2(S(width), vp->Size.y * 0.92f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(16));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(26), S(22)));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, m_pal.card);
    const bool open = ImGui::BeginPopupModal(id, nullptr,
                                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    return open;
}

void Ui::endModalWindow() { ImGui::EndPopup(); }

void Ui::drawModals() {
    bool any = true;
    if (!m_compat.supported() && !m_compatAck)
        drawCompatModal();  // 動作保証外の環境: 何よりも先に伝える
    else if (needWizard())
        drawWizardModal();
    else if (m_modal == Modal::Preview)
        drawPreviewModal();
    else if (m_modal == Modal::Result)
        drawResultModal();
    else if (m_pickerOpen)
        drawProcessPicker();
    else if (m_be->engine->pending() && !m_recoveryDeferred)
        drawRecoveryModal();
    else if (m_be->stateError && !m_stateErrorDismissed)
        drawStateErrorModal();
    else
        any = false;

    if (any) {
        m_modalT += std::min(ImGui::GetIO().DeltaTime, 0.05f);
        if (m_modalT < 0.6f) m_animatingNext = true;  // 背景のディム演出が終わるまで描画を続ける
    } else {
        m_modalT = 0.0f;
    }
}

void Ui::drawReportItems(const lf::Report& rep, bool revertWording) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, S(340)));
    ImGui::BeginChild("##items", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_None);
    ImGui::PopStyleColor();

    // リスク「中」の項目を先頭に出す (スクロールの下に隠れて見落とされないように)。それ以外は元の順序のまま。
    std::vector<size_t> order(rep.items.size());
    for (size_t k = 0; k < order.size(); ++k) order[k] = k;
    std::stable_partition(order.begin(), order.end(), [&](size_t k) {
        const lf::TweakDef* d = findTweak(rep.items[k].tweakId);
        return d && d->risk == lf::Risk::Medium;
    });

    for (size_t pos = 0; pos < order.size(); ++pos) {
        const size_t i = order[pos];
        const lf::ItemResult& it = rep.items[i];
        const lf::TweakDef* d = findTweak(it.tweakId);
        ImGui::PushID(static_cast<int>(i));

        // リスク「中」の項目は背景と枠で強調する (描画順の都合で、背景は別チャンネルに後から描く)。
        const bool medium = d && d->risk == lf::Risk::Medium;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 rowStart = ImGui::GetCursorScreenPos();
        const float rowWidth = ImGui::GetContentRegionAvail().x;
        if (medium) {
            dl->ChannelsSplit(2);
            dl->ChannelsSetCurrent(1);
            ImGui::Dummy(ImVec2(0, S(6)));
        }

        ImGui::AlignTextToFramePadding();
        pushBold();
        ImGui::TextUnformatted(d ? pick(d->title).c_str() : it.tweakId.c_str());
        popFont();
        if (d) {
            ImGui::SameLine();
            badge(d->risk == lf::Risk::Low ? t("common.riskLow") : t("common.riskMid"),
                  d->risk == lf::Risk::Low ? m_pal.ok : m_pal.warn);
        }
        ImGui::SameLine();
        badge(t((std::string("status.item.") + statusName(it.status)).c_str()), statusColor(it.status));

        const bool showValues = it.status != lf::ItemStatus::Skipped && it.status != lf::ItemStatus::Blocked &&
                                it.status != lf::ItemStatus::NotApplied;
        if (showValues) {
            auto dim = [&](const char* s) { ImGui::PushStyleColor(ImGuiCol_Text, m_pal.textDim); ImGui::TextUnformatted(s); ImGui::PopStyleColor(); };
            dim(wouldChange(it.status) ? t("common.currentValue") : t("common.beforeValue"));
            ImGui::SameLine();
            ImGui::TextUnformatted(shortValue(it.before).c_str());
            ImGui::SameLine();
            dim("→");
            ImGui::SameLine();
            dim(revertWording ? t("common.restoredValue") : t("common.afterValue"));
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, m_pal.accentHover);
            ImGui::TextUnformatted(shortValue(it.after).c_str());
            ImGui::PopStyleColor();
        }
        if (it.error) {
            const bool soft = it.status == lf::ItemStatus::Skipped;
            coloredText(soft ? m_pal.textDim : m_pal.danger, t(lf::errorKey(it.error->code)));
        }
        if (medium) {
            if (it.status == lf::ItemStatus::WouldChange || it.status == lf::ItemStatus::Applied)
                coloredText(m_pal.warn, t("preview.mediumRiskItem"));
            ImGui::Dummy(ImVec2(0, S(6)));
            const ImVec2 rowEnd(rowStart.x + rowWidth, ImGui::GetCursorScreenPos().y);
            dl->ChannelsSetCurrent(0);
            dl->AddRectFilled(rowStart - ImVec2(S(8), 0), rowEnd + ImVec2(S(8), 0), u32(withAlpha(m_pal.warn, 0.10f)), S(10));
            dl->AddRect(rowStart - ImVec2(S(8), 0), rowEnd + ImVec2(S(8), 0), u32(withAlpha(m_pal.warn, 0.55f)), S(10), 0, 1.5f);
            dl->ChannelsMerge();
        }
        if (pos + 1 < order.size()) {
            ImGui::Dummy(ImVec2(0, S(2)));
            ImGui::Separator();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void Ui::drawTechnicalDetails(const lf::Report& rep) {
    std::vector<const lf::Error*> errs;
    if (rep.error) errs.push_back(&*rep.error);
    for (const auto& it : rep.items)
        if (it.error && (!rep.error || it.error->detail != rep.error->detail)) errs.push_back(&*it.error);
    if (errs.empty()) return;
    ImGui::Dummy(ImVec2(0, S(4)));
    if (ImGui::CollapsingHeader(t("common.technicalDetails"))) {
        for (const lf::Error* e : errs) {
            std::string line = e->detail;
            if (e->win32) line += std::string("  (") + t("common.win32") + " " + std::to_string(e->win32) + ")";
            dimText(line.c_str());
        }
    }
}

static float rightAlignStart(float w1, float w2, float gap) {
    return ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - (w1 + (w2 > 0 ? gap + w2 : 0));
}

void Ui::drawPreviewModal() {
    if (!beginModalWindow("##preview", 680)) return;
    const bool revert = m_op.kind == OpKind::Revert;
    const lf::Report& rep = m_op.preview;
    int changes = 0;
    for (const auto& it : rep.items)
        if (wouldChange(it.status)) ++changes;

    ImGui::PushFont(m_fontBold, 22.0f);
    ImGui::TextUnformatted(m_op.dryRun ? t("preview.dryRunTitle") : revert ? t("preview.revertTitle") : t("preview.applyTitle"));
    ImGui::PopFont();
    dimText(m_op.dryRun ? t("preview.dryRunDesc") : revert ? t("preview.revertDesc") : t("preview.applyDesc"));
    if (!m_op.presetTitle.empty()) {
        badge(fmt("preview.fromPreset", {m_op.presetTitle}).c_str(), m_pal.accentHover);
        ImGui::SameLine();
    }
    if (m_demo) badge(t("common.demo"), m_pal.warn);
    ImGui::Dummy(ImVec2(0, S(6)));

    drawReportItems(rep, revert);

    if (!rep.ok && rep.error) {
        ImGui::Dummy(ImVec2(0, S(4)));
        coloredText(m_pal.danger, (std::string(t("preview.cannotApply")) + " " + t(lf::errorKey(rep.error->code))).c_str());
    } else if (changes == 0) {
        ImGui::Dummy(ImVec2(0, S(4)));
        dimText(t("preview.nothingToDo"));
    }
    bool mediumChange = false;
    for (const auto& it : rep.items) {
        const lf::TweakDef* d = findTweak(it.tweakId);
        if (d && d->risk == lf::Risk::Medium && it.status == lf::ItemStatus::WouldChange) mediumChange = true;
    }
    if (mediumChange) {
        ImGui::Dummy(ImVec2(0, S(4)));
        coloredText(m_pal.warn, t("preview.mediumRisk"));  // リスク「中」を含む: 見落とさせない
    }
    if (rep.rebootRequired && changes > 0) {
        ImGui::Dummy(ImVec2(0, S(4)));
        badge(t("common.restartRequired"), m_pal.warn);
    }
    drawTechnicalDetails(rep);

    ImGui::Dummy(ImVec2(0, S(10)));
    const bool canConfirm = rep.ok && changes > 0 && !m_op.dryRun;
    const float w1 = S(130), w2 = S(160), gap = ImGui::GetStyle().ItemSpacing.x;
    bool close = ImGui::IsKeyPressed(ImGuiKey_Escape);
    if (canConfirm) {
        ImGui::SetCursorPosX(rightAlignStart(w1, w2, gap));
        if (secondaryButton(t("common.cancel"), w1)) close = true;
        ImGui::SameLine();
        if (primaryButton(revert ? t("preview.confirmRevert") : t("preview.confirmApply"), w2)) {
            confirmOperation();  // m_modal が Result へ
            ImGui::CloseCurrentPopup();
            endModalWindow();
            return;
        }
    } else {
        ImGui::SetCursorPosX(rightAlignStart(w1, 0, gap));
        if (primaryButton(t("common.close"), w1)) close = true;
    }
    if (close) {
        m_modal = Modal::None;
        ImGui::CloseCurrentPopup();
    }
    endModalWindow();
}

void Ui::drawResultModal() {
    if (!beginModalWindow("##result", 680)) return;
    const lf::Report& rep = m_result;

    ImGui::PushFont(m_fontBold, 22.0f);
    if (rep.ok) {
        const char* k = m_resultKind == ResultKind::Apply ? "result.applyOk"
                        : m_resultKind == ResultKind::Revert ? "result.revertOk" : "result.recoverOk";
        coloredText(m_pal.ok, t(k));
    } else {
        coloredText(m_pal.danger, t("result.failed"));
    }
    ImGui::PopFont();

    if (!rep.ok) {
        if (rep.error) dimText(t(lf::errorKey(rep.error->code)));
        if (rep.rollbackIncomplete)
            coloredText(m_pal.danger, t("result.rollbackIncomplete"));
        else if (m_resultKind == ResultKind::Apply && !rep.items.empty())
            dimText(t("result.rolledBack"));
    }
    if (rep.stateSaveFailed) coloredText(m_pal.warn, t("result.stateSaveFailed"));
    ImGui::Dummy(ImVec2(0, S(6)));

    drawReportItems(rep, m_resultKind != ResultKind::Apply);
    if (rep.ok && rep.rebootRequired) {
        ImGui::Dummy(ImVec2(0, S(6)));
        badge(t("common.restartRequired"), m_pal.warn);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(t("result.rebootNeeded"));
    }
    drawTechnicalDetails(rep);

    ImGui::Dummy(ImVec2(0, S(10)));
    const float w = S(130);
    ImGui::SetCursorPosX(rightAlignStart(w, 0, 0));
    if (primaryButton(t("common.close"), w) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        m_modal = Modal::None;
        m_rowsDirty = true;
        ImGui::CloseCurrentPopup();
    }
    endModalWindow();
}

}  // namespace lfapp
