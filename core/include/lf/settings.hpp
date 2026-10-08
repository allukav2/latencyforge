#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace lf {

// 免責事項の文面を実質的に変えたら上げる (再同意を求める)。
constexpr int kDisclaimerVersion = 1;

// アプリ設定 (UI 設定のみ。最適化の適用状態はバックアップ側で管理する)。
struct Settings {
    std::string language = "auto";   // "auto" | "ja" | "en"
    uint32_t accentRgb = 0x5B8CFF;   // 0xRRGGBB
    bool reduceMotion = false;       // 省リソースモード: アニメーション全 OFF
    int acceptedDisclaimer = 0;      // 同意済みの免責事項バージョン。kDisclaimerVersion 未満なら初回ウィザードを表示

    // 不正/欠落フィールドは既定値のまま。ファイルが無い/壊れている場合は false。
    bool loadFile(const std::filesystem::path& path);
    bool saveFile(const std::filesystem::path& path) const;
};

}  // namespace lf
