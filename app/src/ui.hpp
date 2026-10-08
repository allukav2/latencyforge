#pragma once
#include <imgui.h>

#include <filesystem>
#include <string>
#include <unordered_map>

#include "lf/i18n.hpp"
#include "lf/settings.hpp"
#include "theme.hpp"

namespace lfapp {

enum class Page { Home, Affinity, Usb, Gpu, Kernel, Bench, Backup, Log, Settings, Count };

struct UiInit {
    std::filesystem::path exeDir;
    std::filesystem::path configDir;  // settings.json の置き場
    Page startPage = Page::Home;
    bool warp = false;                // WARP で動作中 (Home に注意表示)
};

class Ui {
public:
    // ImGui コンテキスト作成後、バックエンド初期化前に呼ぶ (フォント登録を含む)。
    bool init(const UiInit& init, float dpiScale);
    void setDpi(float dpiScale);
    // 毎フレーム呼ぶ。ImGui::NewFrame 済みであること。
    void frame();

    bool animating() const { return m_animating; }       // 次フレームも描画が必要
    bool wantsDarkClear() const { return true; }
    const Palette& palette() const { return m_pal; }

private:
    // --- テーマ/設定
    void applyTheme();
    void loadLanguage();
    void saveSettings();
    const char* t(const char* key) const { return m_tr.tr(key); }

    // --- アニメーション (reduceMotion 時は即値)
    float animTo(ImGuiID id, float target, float speed = 14.0f);

    // --- ウィジェット
    float S(float v) const { return v * m_dpi; }
    bool navItem(const char* label, Page page, bool selected);
    bool toggle(const char* strId, bool* value, bool enabled = true, const char* disabledReason = nullptr);
    void badge(const char* text, const ImVec4& color);
    bool beginCard(const char* id, float height = 0.0f, float width = 0.0f);
    void keyValue(const char* label, const char* value);
    bool pill(const char* label, bool selected);
    void endCard();
    void pageHeader(const char* title, const char* subtitle);
    void dimText(const char* text);
    void tooltipIfHovered(const char* text, bool allowDisabled = false);
    bool secondaryButton(const char* label, float width = 0.0f, bool enabled = true,
                         const char* disabledReason = nullptr);
    void pushBold();
    void pushTitle();
    void popFont();

    // --- 画面
    void drawSidebar(float height);
    void drawContent();
    void pageHome();
    void pageSettings();
    void pagePlaceholder(Page page);
    void pageKernelPreview();
    void sampleTweakCard();

    lf::Translator m_tr;
    lf::Settings m_settings;
    std::filesystem::path m_exeDir, m_configDir;
    Palette m_pal{};
    float m_dpi = 1.0f;
    bool m_animating = false;
    bool m_animatingNext = false;
    bool m_haveJpFont = false;
    bool m_warp = false;
    std::string m_osLine;

    ImFont* m_fontBody = nullptr;
    ImFont* m_fontBold = nullptr;

    Page m_page = Page::Home;
    Page m_prevPage = Page::Home;
    float m_pageT = 1.0f;
    std::unordered_map<ImGuiID, float> m_anim;

    // M1 のプレビュー用状態 (実際の tweak ではない)
    bool m_sampleOn = false;
    bool m_sampleOpen = false;
};

}  // namespace lfapp
