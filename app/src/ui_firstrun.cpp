// 初回ウィザード (免責同意 → 復元ポイント作成の提案)、異常終了後の回復提案、状態ファイル異常のダイアログ。
#include <imgui_internal.h>
#include <windows.h>

#include "lf/system.hpp"
#include "ui.hpp"

namespace lfapp {

void Ui::drawWizardModal() {
    if (!beginModalWindow("##wizard", 720)) return;
    const float gap = ImGui::GetStyle().ItemSpacing.x;

    ImGui::PushFont(m_fontBold, 22.0f);
    ImGui::TextUnformatted(m_wizardStep == 0 ? t("wizard.title")
                           : m_wizardStep == 1 ? t("wizard.restoreTitle")
                                               : t("wizard.presetTitle"));
    ImGui::PopFont();
    {
        ImGui::PushStyleColor(ImGuiCol_Text, m_pal.textDim);
        ImGui::TextUnformatted(fmt("wizard.step", {std::to_string(m_wizardStep + 1)}).c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0, S(6)));

    if (m_wizardStep == 0) {
        // 免責事項 (スクロール可)
        ImGui::PushStyleColor(ImGuiCol_ChildBg, m_pal.panel);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(10));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(16), S(12)));
        ImGui::BeginChild("##disclaimer", ImVec2(0, S(300)), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        ImGui::TextWrapped("%s", t("wizard.disclaimerBody"));
        ImGui::EndChild();

        ImGui::Dummy(ImVec2(0, S(4)));
        ImGui::Checkbox(t("wizard.agree"), &m_wizardAgree);
        ImGui::Dummy(ImVec2(0, S(8)));

        const float w1 = S(180), w2 = S(130);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - (w1 + gap + w2));
        if (secondaryButton(t("wizard.decline"), w1)) PostQuitMessage(0);  // 同意しない場合は何も変更せず終了
        ImGui::SameLine();
        if (primaryButton(t("wizard.next"), w2, m_wizardAgree, t("wizard.agree"))) m_wizardStep = 1;
    } else if (m_wizardStep == 1) {
        dimText(t("wizard.restoreBody"));
        ImGui::Dummy(ImVec2(0, S(6)));
        if (secondaryButton(t("wizard.createRestorePoint"), 0, m_restoreState == 0 || m_restoreState == 2)) {
            if (m_demo) {
                m_restoreState = 3;
            } else {
                auto r = lf::createRestorePoint("LatencyForge: before first change");
                if (r.ok()) {
                    m_restoreState = 1;
                    m_be->log.info("wizard", "restore point requested");
                } else {
                    m_restoreState = 2;
                    m_restoreDetail = r.error().detail + " (Win32 " + std::to_string(r.error().win32) + ")";
                    m_be->log.warn("wizard", "restore point failed", m_restoreDetail);
                }
            }
        }
        if (m_restoreState == 1) coloredText(m_pal.ok, t("wizard.restoreOk"));
        if (m_restoreState == 3) coloredText(m_pal.warn, t("wizard.restoreDemo"));
        if (m_restoreState == 2) {
            coloredText(m_pal.danger, t("wizard.restoreFailed"));
            dimText(t(lf::errorKey(lf::ErrorCode::RestorePointFailed)));
            dimText(m_restoreDetail.c_str());
        }
        ImGui::Dummy(ImVec2(0, S(10)));

        const float w = S(190);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - w);
        const bool done = m_restoreState == 1 || m_restoreState == 3;
        if (primaryButton(done ? t("wizard.next") : t("wizard.skipStep"), w)) m_wizardStep = 2;
    } else {
        // 推奨プリセットの提案: 常に「安全」。ここでは適用せず、通常の差分プレビューへ案内するだけ。
        const lf::PresetDef* safe = nullptr;
        size_t safeIndex = 0;
        for (size_t i = 0; i < m_be->presets.size(); ++i)
            if (m_be->presets[i].id == "safe") {
                safe = &m_be->presets[i];
                safeIndex = i;
            }
        dimText(t("wizard.presetBody"));
        if (safe) {
            ImGui::Dummy(ImVec2(0, S(6)));
            pushBold();
            ImGui::TextUnformatted(pick(safe->title).c_str());
            popFont();
            dimText(pick(safe->description).c_str());
        }
        ImGui::Dummy(ImVec2(0, S(10)));

        auto finish = [&]() {
            m_settings.acceptedDisclaimer = lf::kDisclaimerVersion;  // 同意を保存 (次回以降は表示しない)
            saveSettings();
            m_be->log.info("wizard", "disclaimer accepted, version " + std::to_string(lf::kDisclaimerVersion));
            ImGui::CloseCurrentPopup();
        };
        const float w1 = S(200), w2 = S(230);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - (w1 + gap + w2));
        if (secondaryButton(t("wizard.presetLater"), w1)) finish();
        ImGui::SameLine();
        if (primaryButton(t("wizard.presetReview"), w2, safe != nullptr, t("home.presetNone"))) {
            finish();
            m_rowsDirty = true;
            refreshRows();
            requestApplyPreset(safeIndex);  // 次のフレームからプレビューダイアログが開く
        }
    }
    endModalWindow();
}

void Ui::runRecovery() {
    m_result = m_be->engine->resolvePending();
    m_resultKind = ResultKind::Recover;
    m_modal = Modal::Result;
    m_rowsDirty = true;
}

void Ui::drawRecoveryModal() {
    if (!beginModalWindow("##recovery", 680)) return;
    ImGui::PushFont(m_fontBold, 22.0f);
    coloredText(m_pal.warn, t("recovery.title"));
    ImGui::PopFont();
    dimText(t("recovery.body"));

    const auto& tx = *m_be->engine->pending();
    ImGui::Dummy(ImVec2(0, S(4)));
    ImGui::TextUnformatted(fmt("recovery.info", {tx.startedAt, std::to_string(tx.ops.size())}).c_str());
    ImGui::Dummy(ImVec2(0, S(4)));
    for (const auto& op : tx.ops) {
        const lf::TweakDef* d = findTweak(op.tweakId);
        ImGui::BulletText("%s", d ? pick(d->title).c_str() : op.tweakId.c_str());
    }

    ImGui::Dummy(ImVec2(0, S(10)));
    const float w1 = S(120), w2 = S(200), gap = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - (w1 + gap + w2));
    if (secondaryButton(t("recovery.later"), w1)) {
        m_recoveryDeferred = true;  // 以降の変更は、復元するまで無効のまま
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (primaryButton(t("recovery.restore"), w2)) {
        runRecovery();
        ImGui::CloseCurrentPopup();
    }
    endModalWindow();
}

void Ui::drawStateErrorModal() {
    if (!beginModalWindow("##stateerror", 680)) return;
    ImGui::PushFont(m_fontBold, 22.0f);
    coloredText(m_pal.danger, t("stateError.title"));
    ImGui::PopFont();
    dimText(t("stateError.body"));
    if (m_be->stateError) {
        dimText(t(lf::errorKey(m_be->stateError->code)));
        ImGui::Dummy(ImVec2(0, S(4)));
        if (ImGui::CollapsingHeader(t("common.technicalDetails"))) dimText(m_be->stateError->detail.c_str());
    }
    ImGui::Dummy(ImVec2(0, S(10)));
    const float w = S(130);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - w);
    if (primaryButton(t("common.close"), w)) {
        m_stateErrorDismissed = true;
        ImGui::CloseCurrentPopup();
    }
    endModalWindow();
}

}  // namespace lfapp
