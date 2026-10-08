#include "ui.hpp"

#include <imgui_internal.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lfapp {

namespace {

constexpr ImGuiID kPageAnimId = 0xA11CE001;

struct NavEntry {
    Page page;
    const char* key;
};
constexpr NavEntry kNav[] = {
    {Page::Home, "nav.home"},       {Page::Affinity, "nav.affinity"}, {Page::Usb, "nav.usb"},
    {Page::Gpu, "nav.gpu"},         {Page::Kernel, "nav.kernel"},     {Page::Bench, "nav.bench"},
    {Page::Backup, "nav.backup"},   {Page::Log, "nav.log"},           {Page::Settings, "nav.settings"},
};

const char* pageKey(Page p) {
    for (const auto& n : kNav)
        if (n.page == p) return n.key;
    return "nav.home";
}

std::string windowsFontPath(const char* file) {
    char dir[MAX_PATH]{};
    UINT n = GetWindowsDirectoryA(dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return std::string(dir) + "\\Fonts\\" + file;
}

bool fileExists(const std::string& p) {
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

ImFont* tryLoadFont(const char* const* candidates, size_t count, float size) {
    for (size_t i = 0; i < count; ++i) {
        std::string path = windowsFontPath(candidates[i]);
        if (!path.empty() && fileExists(path)) {
            if (ImFont* f = ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), size)) return f;
        }
    }
    return nullptr;
}

std::string detectOsLine() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW vi{};
    vi.dwOSVersionInfoSize = sizeof vi;
    if (HMODULE nt = GetModuleHandleW(L"ntdll.dll")) {
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(nt, "RtlGetVersion"))) fn(&vi);
    }
    const char* name = vi.dwBuildNumber >= 22000 ? "Windows 11" : "Windows 10";
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s (%lu.%lu build %lu)", name, vi.dwMajorVersion, vi.dwMinorVersion,
                  vi.dwBuildNumber);
    return buf;
}

}  // namespace

// ---------------------------------------------------------------- init / theme

bool Ui::init(const UiInit& in, float dpiScale) {
    m_exeDir = in.exeDir;
    m_configDir = in.configDir;
    m_demo = in.demo;
    m_settingsDir = in.demo ? in.configDir / "demo" : in.configDir;  // デモの設定は実機の設定と混ぜない
    m_page = m_prevPage = in.startPage;
    m_warp = in.warp;
    m_dpi = dpiScale;
    m_demoShow = in.demo ? in.show : std::string();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // ウィンドウ配置の ini は書かない
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // 日本語フォント: システム標準のみ使用 (同梱しない)。
    static const char* const kRegular[] = {"YuGothR.ttc", "YuGothM.ttc", "meiryo.ttc", "msgothic.ttc"};
    static const char* const kBold[] = {"YuGothB.ttc", "meiryob.ttc"};
    m_fontBody = tryLoadFont(kRegular, std::size(kRegular), 16.0f);
    m_haveJpFont = m_fontBody != nullptr;
    if (!m_fontBody) {
        static const char* const kLatin[] = {"segoeui.ttf", "arial.ttf"};
        m_fontBody = tryLoadFont(kLatin, std::size(kLatin), 16.0f);
    }
    if (!m_fontBody) m_fontBody = io.Fonts->AddFontDefault();
    m_fontBold = tryLoadFont(kBold, std::size(kBold), 16.0f);
    if (!m_fontBold) m_fontBold = m_fontBody;

    m_settings.loadFile(m_settingsDir / "settings.json");
    if (m_demo && in.skipWizard) m_settings.acceptedDisclaimer = lf::kDisclaimerVersion;  // 保存はしない
    if (m_demo && (m_demoShow == "wizard" || m_demoShow == "wizard-preset")) m_settings.acceptedDisclaimer = 0;
    if (m_demo && m_demoShow == "wizard-preset") {
        m_wizardStep = 2;
        m_wizardAgree = true;
    }
    m_osLine = detectOsLine();
    m_pal = makePalette(m_settings.accentRgb);
    loadLanguage();
    applyTheme();

    m_be = std::make_unique<Backend>();
    BackendOptions bo;
    bo.exeDir = m_exeDir;
    bo.configDir = m_configDir;
    bo.demo = m_demo;
    bo.seedDemoPending = m_demo && m_demoShow == "recovery";
    m_be->init(bo);
    return true;
}

void Ui::setDpi(float dpiScale) {
    m_dpi = dpiScale;
    applyTheme();
}

void Ui::applyTheme() {
    m_pal = makePalette(m_settings.accentRgb);
    applyStyle(m_pal);
    ImGuiStyle& s = ImGui::GetStyle();
    s.ScaleAllSizes(m_dpi);
    s.FontScaleDpi = m_dpi;
}

void Ui::loadLanguage() {
    m_tr.clear();
    std::string lang = m_settings.language;
    if (lang == "auto") {
        const bool ja = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_JAPANESE;
        lang = (ja && m_haveJpFont) ? "ja" : "en";
    }
    m_isJa = lang == "ja";
    const auto dir = m_exeDir / "data" / "lang";
    std::string err;
    if (!m_tr.loadFile(dir / "en.json", &err)) OutputDebugStringA(("lang en: " + err + "\n").c_str());
    if (lang == "ja" && !m_tr.loadFile(dir / "ja.json", &err)) OutputDebugStringA(("lang ja: " + err + "\n").c_str());
}

void Ui::saveSettings() { m_settings.saveFile(m_settingsDir / "settings.json"); }

std::string Ui::fmt(const char* key, std::initializer_list<std::string> args) const {
    std::string s = t(key);
    int i = 0;
    for (const std::string& a : args) {
        const std::string ph = "{" + std::to_string(i++) + "}";
        for (size_t pos = s.find(ph); pos != std::string::npos; pos = s.find(ph, pos + a.size())) s.replace(pos, ph.size(), a);
    }
    return s;
}

// ---------------------------------------------------------------- helpers

float Ui::animTo(ImGuiID id, float target, float speed) {
    if (m_settings.reduceMotion) {
        m_anim[id] = target;
        return target;
    }
    float& v = m_anim.try_emplace(id, target).first->second;
    const float dt = std::min(ImGui::GetIO().DeltaTime, 0.05f);
    v += (target - v) * (1.0f - std::exp(-speed * dt));
    if (std::fabs(target - v) < 0.002f) {
        v = target;
    } else {
        m_animatingNext = true;
    }
    return v;
}

void Ui::pushBold() { ImGui::PushFont(m_fontBold, 0.0f); }
void Ui::pushTitle() { ImGui::PushFont(m_fontBold, 26.0f); }
void Ui::popFont() { ImGui::PopFont(); }

void Ui::dimText(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, m_pal.textDim);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void Ui::tooltipIfHovered(const char* text, bool allowDisabled) {
    ImGuiHoveredFlags f = ImGuiHoveredFlags_ForTooltip;
    if (allowDisabled) f |= ImGuiHoveredFlags_AllowWhenDisabled;
    if (ImGui::IsItemHovered(f) && ImGui::BeginTooltip()) {
        ImGui::PushTextWrapPos(S(340));
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

bool Ui::secondaryButton(const char* label, float width, bool enabled, const char* disabledReason) {
    if (!enabled) ImGui::BeginDisabled();
    const bool clicked = ImGui::Button(label, ImVec2(width, 0));
    if (!enabled) ImGui::EndDisabled();
    if (!enabled && disabledReason) tooltipIfHovered(disabledReason, true);
    return clicked && enabled;
}

void Ui::coloredText(const ImVec4& color, const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

bool Ui::primaryButton(const char* label, float width, bool enabled, const char* disabledReason) {
    ImGui::PushStyleColor(ImGuiCol_Button, m_pal.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_pal.accentHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, mix(m_pal.accent, ImVec4(0, 0, 0, 1), 0.2f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    if (!enabled) ImGui::BeginDisabled();
    const bool clicked = ImGui::Button(label, ImVec2(width, 0));
    if (!enabled) ImGui::EndDisabled();
    ImGui::PopStyleColor(4);
    if (!enabled && disabledReason) tooltipIfHovered(disabledReason, true);
    return clicked && enabled;
}

// 「詳細 v」: クリックで開閉。戻り値は開閉アニメの進行 (0=閉, 1=開)。
float Ui::expander(const char* id, const char* label, bool* open) {
    ImGui::PushID(id);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 ts = ImGui::CalcTextSize(label);
    if (ImGui::InvisibleButton("##d", ImVec2(ts.x + S(28), ts.y + S(6)))) *open = !*open;
    const float o = animTo(ImGui::GetItemID(), *open ? 1.0f : 0.0f);
    const ImU32 col = u32(ImGui::IsItemHovered() ? m_pal.text : m_pal.textDim);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddText(ImVec2(p.x, p.y + S(3)), col, label);
    const ImVec2 c(p.x + ts.x + S(14), p.y + (ts.y + S(6)) * 0.5f);
    const float k = S(4);
    const float dir = 1.0f - 2.0f * o;  // 閉: +1 (下向き) → 開: -1 (上向き)
    const ImVec2 pts[3] = {ImVec2(c.x - k, c.y - k * 0.5f * dir), ImVec2(c.x, c.y + k * 0.5f * dir),
                           ImVec2(c.x + k, c.y - k * 0.5f * dir)};
    dl->AddPolyline(pts, 3, col, 0, S(1.6f));
    ImGui::PopID();
    return o;
}

bool Ui::pill(const char* label, bool selected) {
    ImGui::PushStyleColor(ImGuiCol_Button, selected ? m_pal.accentSoft : ImGui::GetStyle().Colors[ImGuiCol_Button]);
    ImGui::PushStyleColor(ImGuiCol_Text, selected ? m_pal.accentHover : m_pal.text);
    const bool c = ImGui::Button(label);
    ImGui::PopStyleColor(2);
    if (selected) {
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                            u32(withAlpha(m_pal.accent, 0.7f)), ImGui::GetStyle().FrameRounding, 0,
                                            1.0f);
    }
    return c;
}

void Ui::badge(const char* text, const ImVec4& color) {
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const ImVec2 pad(S(9), S(3));
    const ImVec2 size = ts + pad * 2.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + size, u32(withAlpha(color, 0.15f)), size.y * 0.5f);
    dl->AddRect(p, p + size, u32(withAlpha(color, 0.45f)), size.y * 0.5f);
    dl->AddText(p + pad, u32(color), text);
    ImGui::Dummy(size);
}

bool Ui::toggle(const char* strId, bool* value, bool enabled, const char* disabledReason) {
    const bool changed = toggleRaw(strId, *value, enabled, disabledReason);
    if (changed) *value = !*value;
    return changed;
}

bool Ui::toggleRaw(const char* strId, bool on, bool enabled, const char* disabledReason) {
    const float w = S(44), h = S(24);
    ImGui::PushID(strId);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##t", ImVec2(w, h));
    const ImGuiID id = ImGui::GetItemID();
    const bool focused = ImGui::IsItemFocused() && ImGui::GetIO().NavVisible;
    if (!enabled && disabledReason) tooltipIfHovered(disabledReason, true);
    const bool changed = pressed && enabled;
    const float t = animTo(id, on ? 1.0f : 0.0f, 16.0f);
    const float hov = animTo(id + 1, (ImGui::IsItemHovered() && enabled) ? 1.0f : 0.0f);
    const float dim = enabled ? 1.0f : 0.4f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = h * 0.5f;
    const ImVec4 offCol = mix(ImVec4(0.17f, 0.19f, 0.24f, 1), ImVec4(0.23f, 0.26f, 0.32f, 1), hov);
    const ImVec4 onCol = mix(m_pal.accent, m_pal.accentHover, hov);
    // 控えめなグロー (ON のときのみ)
    for (int i = 3; i >= 1; --i) {
        const float e = S(2.0f) * i;
        dl->AddRectFilled(p - ImVec2(e, e), p + ImVec2(w + e, h + e), u32(withAlpha(m_pal.accent, 0.07f * t * dim)),
                          r + e);
    }
    ImVec4 track = mix(offCol, onCol, t);
    track.w *= dim;
    dl->AddRectFilled(p, p + ImVec2(w, h), u32(track), r);
    const float kx = p.x + r + (w - h) * t;
    dl->AddCircleFilled(ImVec2(kx, p.y + r), r - S(3.5f), u32(ImVec4(1, 1, 1, dim)), 24);
    if (focused) dl->AddRect(p - ImVec2(S(3), S(3)), p + ImVec2(w + S(3), h + S(3)), u32(m_pal.accent), r + S(3), 0, 1.5f);
    ImGui::PopID();
    return changed;
}

bool Ui::beginCard(const char* id, float height, float width) {
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(12));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(18), S(16)));
    ImGuiChildFlags cf = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
    if (height <= 0.0f) cf |= ImGuiChildFlags_AutoResizeY;
    const bool open = ImGui::BeginChild(id, ImVec2(width, height), cf,
                                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);
    return open;
}

void Ui::endCard() { ImGui::EndChild(); }

void Ui::keyValue(const char* label, const char* value) {
    ImGui::PushStyleColor(ImGuiCol_Text, m_pal.textDim);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine(S(150));
    ImGui::TextUnformatted(value);
}

void Ui::pageHeader(const char* title, const char* subtitle) {
    pushTitle();
    ImGui::TextUnformatted(title);
    popFont();
    dimText(subtitle);
    ImGui::Dummy(ImVec2(0, S(6)));
}

bool Ui::navItem(const char* label, Page page, bool selected) {
    ImGui::PushID(static_cast<int>(page));
    const float h = S(40);
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##nav", ImVec2(w, h));
    const ImGuiID id = ImGui::GetItemID();
    const bool hovered = ImGui::IsItemHovered();
    const bool focused = ImGui::IsItemFocused() && ImGui::GetIO().NavVisible;
    const float hv = animTo(id, hovered ? 1.0f : 0.0f);
    const float sel = animTo(id + 1, selected ? 1.0f : 0.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = p + ImVec2(0, S(2)), b = p + ImVec2(w, h - S(2));
    ImVec4 bg = withAlpha(m_pal.cardHover, 0.75f * hv);
    bg = mix(bg, m_pal.accentSoft, sel);
    dl->AddRectFilled(a, b, u32(bg), S(10));
    const float cy = p.y + h * 0.5f;
    const float bar = S(6) + S(14) * sel;
    dl->AddRectFilled(ImVec2(a.x + S(3), cy - bar * 0.5f), ImVec2(a.x + S(6), cy + bar * 0.5f),
                      u32(withAlpha(m_pal.accent, sel)), S(2));
    const ImVec4 tc = mix(m_pal.textDim, m_pal.text, std::max(sel, hv * 0.8f));
    const ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(a.x + S(18), cy - ts.y * 0.5f), u32(tc), label);
    if (focused) dl->AddRect(a, b, u32(withAlpha(m_pal.accent, 0.8f)), S(10), 0, 1.5f);
    ImGui::PopID();
    return clicked;
}

// ---------------------------------------------------------------- frame

void Ui::frame() {
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = std::min(io.DeltaTime, 0.05f);  // アイドル復帰直後の巨大 dt を抑える

    // Ctrl+1..9 でページ移動
    for (int i = 0; i < static_cast<int>(Page::Count); ++i) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | static_cast<ImGuiKey>(ImGuiKey_1 + i))) {
            m_page = static_cast<Page>(i);
        }
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    if (m_page != m_prevPage) {
        m_prevPage = m_page;
        m_anim[kPageAnimId] = 0.0f;
        m_animatingNext = true;
        m_rowsDirty = true;  // ページを開くたびに現在値を読み直す
    }

    const float h = io.DisplaySize.y;
    drawSidebar(h);
    ImGui::SameLine(0, 0);
    drawContent();
    drawModals();
    ImGui::End();

    m_animating = m_animatingNext;
    m_animatingNext = false;
}

void Ui::drawSidebar(float height) {
    const float sbw = S(228);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, m_pal.panel);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(12), S(18)));
    ImGui::BeginChild("##sidebar", ImVec2(sbw, height), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();

    // ロゴ
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float s = S(30);
        dl->AddRectFilled(p + ImVec2(S(6), 0), p + ImVec2(S(6) + s, s), u32(m_pal.accent), S(9));
        const ImVec2 o = p + ImVec2(S(6), 0);
        const ImVec2 pts[3] = {o + ImVec2(s * 0.34f, s * 0.24f), o + ImVec2(s * 0.34f, s * 0.72f),
                               o + ImVec2(s * 0.70f, s * 0.72f)};
        dl->AddPolyline(pts, 3, IM_COL32_WHITE, 0, S(3.2f));
        ImGui::SetCursorScreenPos(p + ImVec2(S(6) + s + S(10), (s - ImGui::GetTextLineHeight()) * 0.5f));
        pushBold();
        ImGui::TextUnformatted(t("app.name"));
        popFont();
        ImGui::SetCursorScreenPos(p + ImVec2(0, s + S(18)));
    }

    const int count = static_cast<int>(std::size(kNav));
    for (int i = 0; i < count - 1; ++i) {
        if (navItem(t(kNav[i].key), kNav[i].page, m_page == kNav[i].page)) m_page = kNav[i].page;
    }
    // 設定は最下部に固定
    ImGui::SetCursorPosY(height - S(18) - S(40) - S(26));
    ImGui::Separator();
    if (navItem(t(kNav[count - 1].key), kNav[count - 1].page, m_page == kNav[count - 1].page))
        m_page = kNav[count - 1].page;

    ImGui::EndChild();
    ImGui::GetWindowDrawList()->AddLine(ImGui::GetItemRectMax() - ImVec2(0, height), ImGui::GetItemRectMax(),
                                        u32(m_pal.border));
}

void Ui::drawContent() {
    const float pt = animTo(kPageAnimId, 1.0f, 12.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, m_pal.bg);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(36), S(30)));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, pt);
    ImGui::BeginChild("##content", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(14) * (1.0f - pt));  // 下からスライドイン
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, pt);
    switch (m_page) {
        case Page::Home: pageHome(); break;
        case Page::Kernel: pageKernel(); break;
        case Page::Settings: pageSettings(); break;
        default: pagePlaceholder(m_page); break;
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
}

// ---------------------------------------------------------------- pages

void Ui::pageHome() {
    pageHeader(t("home.title"), t("home.subtitle"));

    if (beginCard("##notice")) {
        badge(m_demo ? t("common.demo") : t("common.safe"), m_demo ? m_pal.warn : m_pal.ok);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(m_demo ? t("home.demoTitle") : t("home.noticeTitle"));
        dimText(m_demo ? t("home.demoNotice") : t("home.notice"));
    }
    endCard();

    if (beginCard("##sys")) {
        pushBold();
        ImGui::TextUnformatted(t("home.sysTitle"));
        popFont();
        ImGui::Dummy(ImVec2(0, S(2)));
        keyValue(t("home.os"), m_osLine.c_str());
        char cpu[32];
        std::snprintf(cpu, sizeof cpu, "%lu", GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
        keyValue(t("home.logicalCpus"), cpu);
        keyValue(t("home.renderer"), m_warp ? t("home.rendererWarp") : t("home.rendererGpu"));
        dimText(t("home.sysMore"));
    }
    endCard();

    drawPresetCards();
}

void Ui::pagePlaceholder(Page page) {
    pageHeader(t(pageKey(page)), t((std::string("placeholder.") + pageKey(page) + ".subtitle").c_str()));
    if (beginCard("##ph")) {
        badge(t("common.comingSoon"), m_pal.textDim);
        ImGui::Dummy(ImVec2(0, S(2)));
        dimText(t((std::string("placeholder.") + pageKey(page) + ".desc").c_str()));
    }
    endCard();
}

void Ui::pageSettings() {
    pageHeader(t("settings.title"), t("settings.subtitle"));
    bool dirty = false;

    if (beginCard("##lang")) {
        pushBold();
        ImGui::TextUnformatted(t("settings.language"));
        popFont();
        const std::string cur = m_settings.language;
        bool changed = false;
        if (pill(t("settings.languageAuto"), cur == "auto")) { m_settings.language = "auto"; changed = true; }
        ImGui::SameLine();
        if (pill(t("settings.languageJa"), cur == "ja")) { m_settings.language = "ja"; changed = true; }
        ImGui::SameLine();
        if (pill(t("settings.languageEn"), cur == "en")) { m_settings.language = "en"; changed = true; }
        if (changed) {
            loadLanguage();
            dirty = true;
        }
        if (!m_haveJpFont) {
            ImGui::PushStyleColor(ImGuiCol_Text, m_pal.warn);
            ImGui::TextWrapped("%s", t("settings.noJpFont"));
            ImGui::PopStyleColor();
        }
    }
    endCard();

    if (beginCard("##accent")) {
        pushBold();
        ImGui::TextUnformatted(t("settings.accent"));
        popFont();
        static const uint32_t kSwatches[] = {0x5B8CFF, 0x7C5CFF, 0xC054F0, 0xFF5C8A, 0xFF8A4C, 0xF5B544, 0x3DD68C, 0x2EC4E6};
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float d = S(26);
        for (size_t i = 0; i < std::size(kSwatches); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const bool click = ImGui::InvisibleButton("##sw", ImVec2(d, d));
            const float hv = animTo(ImGui::GetItemID(), ImGui::IsItemHovered() ? 1.0f : 0.0f);
            const bool sel = m_settings.accentRgb == kSwatches[i];
            const ImVec4 col(((kSwatches[i] >> 16) & 255) / 255.0f, ((kSwatches[i] >> 8) & 255) / 255.0f,
                             (kSwatches[i] & 255) / 255.0f, 1.0f);
            const ImVec2 ctr = p + ImVec2(d, d) * 0.5f;
            if (sel) dl->AddCircle(ctr, d * 0.5f + S(3), u32(m_pal.text), 24, 1.5f);
            dl->AddCircleFilled(ctr, d * 0.5f - S(1) + S(1.5f) * hv, u32(col), 24);
            if (click) {
                m_settings.accentRgb = kSwatches[i];
                applyTheme();
                dirty = true;
            }
            ImGui::PopID();
            ImGui::SameLine();
        }
        ImGui::NewLine();
        float rgb[3] = {((m_settings.accentRgb >> 16) & 255) / 255.0f, ((m_settings.accentRgb >> 8) & 255) / 255.0f,
                        (m_settings.accentRgb & 255) / 255.0f};
        if (ImGui::ColorEdit3(t("settings.accentCustom"), rgb, ImGuiColorEditFlags_NoInputs)) {
            m_settings.accentRgb = (static_cast<uint32_t>(rgb[0] * 255.0f + 0.5f) << 16) |
                                   (static_cast<uint32_t>(rgb[1] * 255.0f + 0.5f) << 8) |
                                   static_cast<uint32_t>(rgb[2] * 255.0f + 0.5f);
            applyTheme();
            dirty = true;
        }
    }
    endCard();

    if (beginCard("##motion")) {
        ImGui::AlignTextToFramePadding();
        pushBold();
        ImGui::TextUnformatted(t("settings.reduceMotion"));
        popFont();
        ImGui::SameLine(ImGui::GetContentRegionMax().x - S(44));
        if (toggle("reduceMotion", &m_settings.reduceMotion)) dirty = true;
        dimText(t("settings.reduceMotionDesc"));
    }
    endCard();

    if (beginCard("##about")) {
        pushBold();
        ImGui::TextUnformatted(t("settings.about"));
        popFont();
        dimText(t("settings.aboutText"));
    }
    endCard();

    if (dirty) saveSettings();
}

}  // namespace lfapp
