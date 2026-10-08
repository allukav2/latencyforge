#include "lf/settings.hpp"

#include <charconv>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>

namespace lf {

using nlohmann::json;

bool Settings::loadFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    json doc = json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) return false;

    if (auto it = doc.find("language"); it != doc.end() && it->is_string()) {
        std::string v = it->get<std::string>();
        if (v == "auto" || v == "ja" || v == "en") language = std::move(v);
    }
    if (auto it = doc.find("accent"); it != doc.end() && it->is_string()) {
        const std::string s = it->get<std::string>();
        unsigned rgb = 0;
        if (s.size() == 7 && s[0] == '#') {
            const auto res = std::from_chars(s.data() + 1, s.data() + s.size(), rgb, 16);
            if (res.ec == std::errc{} && res.ptr == s.data() + s.size()) accentRgb = rgb;
        }
    }
    if (auto it = doc.find("reduceMotion"); it != doc.end() && it->is_boolean()) reduceMotion = it->get<bool>();
    return true;
}

bool Settings::saveFile(const std::filesystem::path& path) const {
    char hex[8];
    std::snprintf(hex, sizeof hex, "#%06X", accentRgb & 0xFFFFFFu);
    json doc = {{"language", language}, {"accent", hex}, {"reduceMotion", reduceMotion}};

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    // 書き込み途中の破損を避けるため一時ファイル経由で置換する。
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << doc.dump(2);
        if (!f.good()) return false;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
    }
    return !ec;
}

}  // namespace lf
