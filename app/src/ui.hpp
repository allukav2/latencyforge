#pragma once
#include <imgui.h>

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "backend.hpp"
#include "lf/affinity_plan.hpp"
#include "lf/engine.hpp"
#include "lf/i18n.hpp"
#include "lf/settings.hpp"
#include "lf/sysinfo.hpp"
#include "theme.hpp"

namespace lfapp {

enum class Page { Home, Affinity, Usb, Gpu, Kernel, Bench, Backup, Log, Settings, Count };

struct UiInit {
    std::filesystem::path exeDir;
    std::filesystem::path configDir;  // settings.json / state.json の置き場
    Page startPage = Page::Home;
    bool warp = false;                // WARP で動作中 (Home に注意表示)
    // --- 開発・スクリーンショット用 (デモ時のみ有効)
    bool demo = false;                // メモリ上のレジストリ。実機には触れない
    bool skipWizard = false;          // デモ: ウィザードを出さない (保存はしない)
    std::string show;                 // デモ: "preview" | "result" | "recovery" | "wizard" | "expanded" ...
    std::string sim;                  // デモ: システム構成のサンプル名 (lf::sampleSystemNames())。検出結果を差し替える
};

class Ui {
public:
    // ImGui コンテキスト作成後、バックエンド初期化前に呼ぶ (フォント登録を含む)。
    bool init(const UiInit& init, float dpiScale);
    void setDpi(float dpiScale);
    // 毎フレーム呼ぶ。ImGui::NewFrame 済みであること。
    void frame();

    bool animating() const { return m_animating; }       // 次フレームも描画が必要
    const Palette& palette() const { return m_pal; }
    bool demo() const { return m_demo; }

private:
    // --- テーマ/設定/文字列
    void applyTheme();
    void loadLanguage();
    void saveSettings();
    const char* t(const char* key) const { return m_tr.tr(key); }
    std::string fmt(const char* key, std::initializer_list<std::string> args) const;
    const std::string& pick(const lf::LText& text) const { return text.get(m_isJa); }

    // --- アニメーション (reduceMotion 時は即値)
    float animTo(ImGuiID id, float target, float speed = 14.0f);

    // --- ウィジェット
    float S(float v) const { return v * m_dpi; }
    bool navItem(const char* label, Page page, bool selected, const char* disabledReason = nullptr);
    bool toggle(const char* strId, bool* value, bool enabled = true, const char* disabledReason = nullptr);
    bool toggleRaw(const char* strId, bool on, bool enabled, const char* disabledReason);  // 値は変えず、クリックだけ返す
    void badge(const char* text, const ImVec4& color);
    bool beginCard(const char* id, float height = 0.0f, float width = 0.0f);
    void endCard();
    void keyValue(const char* label, const char* value);
    bool pill(const char* label, bool selected);
    void pageHeader(const char* title, const char* subtitle);
    void dimText(const char* text);
    void coloredText(const ImVec4& color, const char* text);
    void tooltipIfHovered(const char* text, bool allowDisabled = false);
    bool secondaryButton(const char* label, float width = 0.0f, bool enabled = true,
                         const char* disabledReason = nullptr);
    bool primaryButton(const char* label, float width = 0.0f, bool enabled = true,
                       const char* disabledReason = nullptr);
    float expander(const char* id, const char* label, bool* open);  // 「詳細 v」。開閉アニメ値 0..1 を返す
    void pushBold();
    void pushTitle();
    void popFont();

    // --- 画面
    void drawSidebar(float height);
    void drawContent();
    void pageHome();
    void pageSettings();
    void pagePlaceholder(Page page);

    // --- システム検出 / 互換性 (ui_system.cpp)
    void detectSystemInfo(const UiInit& init);
    static lf::Feature featureOf(Page page);
    std::string reasonText(const lf::FeatureStatus& status) const;
    std::string archName(lf::Arch arch) const;
    void drawSystemCard();
    void drawTopologyMap(const lf::AffinityPlan* plan = nullptr);  // plan を渡すと、ゲーム用/その他のコアを色分けする
    void drawFeatureBanner(Page page);
    void drawCompatModal();

    // --- Affinity 自動最適化 (ui_affinity.cpp)
    void pageAffinity();
    void drawAntiCheatCard();
    void drawAffinityStatusCard();
    void drawAffinityProfiles();
    void drawProcessPicker();
    void commitAffinityConfig(const lf::AffinityConfig& cfg);
    bool addAffinityProfile(const std::string& exeName);
    std::string planSummary(const lf::AffinityPlan& plan) const;

public:
    // メインループ用。ポーリング間隔 (ms)。0 = ポーリング不要 (何も動かさない)。
    int pollIntervalMs() const;
    // タイマーごとに呼ぶ。表示を更新すべきなら true。
    bool pollTimer();
    // 終了時 (WM_CLOSE / WM_ENDSESSION / 終了処理) に呼ぶ。変更したアフィニティ/優先度をすべて元に戻す。冪等。
    void shutdown();

private:
    // --- カーネル/タイマー (ui_kernel.cpp)
    struct Row {
        const lf::TweakDef* def = nullptr;
        lf::TweakStatus st;
    };
    struct PresetInfo {
        int total = 0, pending = 0, unsupported = 0;  // pending = 未適用/外部変更で、適用すると書き込みが発生するもの
    };
    // カーネル/タイマー と USB は同じ「tweak 一覧ページ」(カテゴリで切り替える)。
    void pageTweaks(const char* category, Page page);
    void drawUsbInfoCard();
    void pageGpu();
    const std::vector<Row>& rows() const { return m_rowsByCat.at(m_curCat); }
    const lf::TweakDef* resolvedDef(const lf::TweakDef& base);   // 電源設定のテンプレートを、現在の電源プランに解決する
    const lf::TweakDef* findTweak(const std::string& anyId) const;  // "<id>@<scheme>" のような解決済み ID からも検索できる
    std::vector<std::string> trackedIdsIn(const std::string& category) const;
    void refreshRows();
    void requestApplyPreset(size_t index);
    void drawPresetCards();
    void drawStatusBanners();
    void drawToolbar(bool opsEnabled, const char* disabledReason);
    void tweakCard(const Row& row, bool opsEnabled, const char* disabledReason);
    std::string shortValue(const std::optional<lf::RegValue>& v) const;
    bool opsEnabled(const char** reason, Page page = Page::Kernel) const;

    // --- 適用/復元の流れ (ui_kernel.cpp)
    enum class OpKind { Apply, Revert };
    enum class Modal { None, Preview, Result };  // 明示的に開くもの。ウィザード/回復/状態エラーは状態から決まる
    void requestApply(std::vector<const lf::TweakDef*> tweaks);
    void requestRevert(std::vector<std::string> ids);
    void confirmOperation();
    void drawModals();
    void drawPreviewModal();
    void drawResultModal();
    void drawReportItems(const lf::Report& report, bool revertWording);
    void drawTechnicalDetails(const lf::Report& report);
    bool beginModalWindow(const char* id, float width);
    void endModalWindow();
    ImVec4 statusColor(lf::ItemStatus s) const;

    // --- 初回ウィザード / 回復 / 状態エラー (ui_firstrun.cpp)
    bool needWizard() const { return m_settings.acceptedDisclaimer < lf::kDisclaimerVersion; }
    void drawWizardModal();
    void drawRecoveryModal();
    void drawStateErrorModal();
    void runRecovery();

    lf::Translator m_tr;
    lf::Settings m_settings;
    std::filesystem::path m_exeDir, m_configDir, m_settingsDir;
    Palette m_pal{};
    float m_dpi = 1.0f;
    bool m_animating = false;
    bool m_animatingNext = false;
    bool m_haveJpFont = false;
    bool m_isJa = false;
    bool m_warp = false;
    bool m_demo = false;
    std::string m_osLine;

    ImFont* m_fontBody = nullptr;
    ImFont* m_fontBold = nullptr;

    Page m_page = Page::Home;
    Page m_prevPage = Page::Home;
    std::unordered_map<ImGuiID, float> m_anim;

    std::unique_ptr<Backend> m_be;

    // システム検出の結果 (起動時に 1 回)。機能の有効/無効は、これと featureStatus() から決める。
    lf::SystemInfo m_sys;
    lf::CompatReport m_compat;
    bool m_compatAck = false;  // 「動作保証外」の警告を確認済み
    lf::FeatureStatus m_feat[static_cast<size_t>(Page::Count)];
    std::string m_featReason[static_cast<size_t>(Page::Count)];  // 無効の理由 (翻訳・引数展開済み)
    std::string m_sysSummary;  // ログ用の 1 行要約

    // カーネルページ
    std::map<std::string, std::vector<Row>> m_rowsByCat;  // "kernel" / "usb"
    std::string m_curCat = "kernel";                        // 描画中のページのカテゴリ
    std::map<std::string, lf::TweakDef> m_resolvedStore;    // 解決済みの定義 (ポインタを安定させるため map)
    std::string m_activeScheme, m_activeSchemeName;         // 現在の電源プラン (GUID / 表示名)
    std::vector<PresetInfo> m_presetInfo;  // m_be->presets と同じ並び
    bool m_rowsDirty = true;
    bool m_rebootPending = false;
    std::unordered_map<std::string, bool> m_expanded;
    bool m_dryRun = false;  // Dry-run モード: 差分だけ表示して適用しない (保存しない)

    // 操作ダイアログ
    struct Op {
        OpKind kind = OpKind::Apply;
        std::vector<const lf::TweakDef*> tweaks;
        std::vector<std::string> ids;
        lf::Report preview;
        bool dryRun = false;
        std::string presetTitle;  // プリセット経由のときだけ (ダイアログ見出しに表示)
    } m_op;
    Modal m_modal = Modal::None;
    lf::Report m_result;
    enum class ResultKind { Apply, Revert, Recover } m_resultKind = ResultKind::Apply;
    float m_modalT = 0.0f;  // モーダルを開いてからの経過 (ディムのアニメ中だけ再描画を続ける)

    // ウィザード / 回復
    int m_wizardStep = 0;
    bool m_wizardAgree = false;
    int m_restoreState = 0;  // 0=未実施 1=成功 2=失敗 3=デモ (作成せず)
    int m_wizardPresetIndex = -1;  // ウィザードで提案する「安全」の位置
    std::string m_restoreDetail;
    bool m_recoveryDeferred = false;
    bool m_stateErrorDismissed = false;

    // Affinity ページ
    bool m_pickerOpen = false;
    std::vector<lf::ProcessState> m_pickerList;
    int m_pickerSel = -1;
    char m_pickerFilter[64] = {};
    char m_manualExe[72] = {};
    std::string m_addErrorKey;  // 追加に失敗した理由 (lang キー)

    // 開発用 (デモ)
    std::string m_demoShow;
    bool m_demoShowDone = false;
};

}  // namespace lfapp
