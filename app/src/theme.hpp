#pragma once
#include <imgui.h>

#include <cstdint>

namespace lfapp {

struct Palette {
    ImVec4 bg, panel, card, cardHover, border, text, textDim, accent, accentHover, accentSoft, ok, warn, danger;
};

// アクセント色からパレットを生成する。
Palette makePalette(uint32_t accentRgb);

// ImGuiStyle を設定 (論理ピクセル基準。DPI 拡大は Ui 側で ScaleAllSizes)。
void applyStyle(const Palette& p);

inline ImVec4 mix(const ImVec4& a, const ImVec4& b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
}
inline ImVec4 withAlpha(ImVec4 c, float a) {
    c.w = a;
    return c;
}
inline ImU32 u32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }

}  // namespace lfapp
