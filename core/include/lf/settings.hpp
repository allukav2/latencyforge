#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace lf {

// アプリ設定 (UI 設定のみ。最適化の適用状態はバックアップ側で管理する)。
struct Settings {
    std::string language = "auto";   // "auto" | "ja" | "en"
    uint32_t accentRgb = 0x5B8CFF;   // 0xRRGGBB
    bool reduceMotion = false;       // 省リソースモード: アニメーション全 OFF

    // 不正/欠落フィールドは既定値のまま。ファイルが無い/壊れている場合は false。
    bool loadFile(const std::filesystem::path& path);
    bool saveFile(const std::filesystem::path& path) const;
};

}  // namespace lf
