#include "theme.hpp"

namespace lfapp {

static ImVec4 rgb(uint32_t v, float a = 1.0f) {
    return {((v >> 16) & 0xFF) / 255.0f, ((v >> 8) & 0xFF) / 255.0f, (v & 0xFF) / 255.0f, a};
}

Palette makePalette(uint32_t accentRgb) {
    Palette p;
    p.bg = rgb(0x0D0F14);
    p.panel = rgb(0x12151B);
    p.card = rgb(0x181C24);
    p.cardHover = rgb(0x1D222B);
    p.border = rgb(0x262B36);
    p.text = rgb(0xE8EAF0);
    p.textDim = rgb(0x8C93A5);
    p.accent = rgb(accentRgb);
    p.accentHover = mix(p.accent, {1, 1, 1, 1}, 0.18f);
    p.accentSoft = withAlpha(p.accent, 0.16f);
    p.ok = rgb(0x3DD68C);
    p.warn = rgb(0xF5B544);
    p.danger = rgb(0xF0616D);
    return p;
}

void applyStyle(const Palette& p) {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.WindowPadding = {0, 0};
    s.FramePadding = {12, 8};
    s.ItemSpacing = {10, 10};
    s.ItemInnerSpacing = {8, 6};
    s.ScrollbarSize = 10;
    s.GrabMinSize = 12;
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 1;
    s.FrameBorderSize = 0;
    s.PopupBorderSize = 1;
    s.WindowRounding = 0;
    s.ChildRounding = 12;
    s.FrameRounding = 8;
    s.PopupRounding = 10;
    s.ScrollbarRounding = 8;
    s.GrabRounding = 8;
    s.TabRounding = 8;
    s.WindowMenuButtonPosition = ImGuiDir_None;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = p.text;
    c[ImGuiCol_TextDisabled] = p.textDim;
    c[ImGuiCol_WindowBg] = p.bg;
    c[ImGuiCol_ChildBg] = p.card;
    c[ImGuiCol_PopupBg] = mix(p.card, p.bg, 0.2f);
    c[ImGuiCol_Border] = p.border;
    c[ImGuiCol_BorderShadow] = {0, 0, 0, 0};
    c[ImGuiCol_FrameBg] = p.panel;
    c[ImGuiCol_FrameBgHovered] = p.cardHover;
    c[ImGuiCol_FrameBgActive] = p.cardHover;
    c[ImGuiCol_Button] = mix(p.card, p.border, 0.5f);
    c[ImGuiCol_ButtonHovered] = mix(p.card, p.border, 0.9f);
    c[ImGuiCol_ButtonActive] = p.accentSoft;
    c[ImGuiCol_Header] = p.accentSoft;
    c[ImGuiCol_HeaderHovered] = withAlpha(p.accent, 0.26f);
    c[ImGuiCol_HeaderActive] = withAlpha(p.accent, 0.34f);
    c[ImGuiCol_Separator] = p.border;
    c[ImGuiCol_ScrollbarBg] = {0, 0, 0, 0};
    c[ImGuiCol_ScrollbarGrab] = p.border;
    c[ImGuiCol_ScrollbarGrabHovered] = mix(p.border, p.textDim, 0.5f);
    c[ImGuiCol_ScrollbarGrabActive] = p.textDim;
    c[ImGuiCol_CheckMark] = p.accent;
    c[ImGuiCol_SliderGrab] = p.accent;
    c[ImGuiCol_SliderGrabActive] = p.accentHover;
    c[ImGuiCol_NavCursor] = p.accent;
    c[ImGuiCol_TextSelectedBg] = withAlpha(p.accent, 0.35f);
}

}  // namespace lfapp
