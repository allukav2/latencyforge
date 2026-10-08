#include "lf/power_api.hpp"

#include <algorithm>
#include <cctype>

namespace lf {

std::string normalizeGuid(std::string_view g) {
    std::string s(g);
    if (s.size() == 38 && s.front() == '{' && s.back() == '}') s = s.substr(1, 36);
    if (s.size() != 36) return {};
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? c != '-' : !std::isxdigit(static_cast<unsigned char>(c))) return {};
    }
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

Result<PowerRegistry::Target> PowerRegistry::parse(const RegPath& p) const {
    auto bad = [&](const char* why) { return Error{ErrorCode::InvalidPath, std::string(why) + ": " + p.display()}; };
    if (p.hive != RegHive::Power) return bad("not a power path");

    const size_t slash = p.subkey.find('\\');
    if (slash == std::string::npos) return bad("expected POWER\\<scheme>\\<subgroup>");
    Target t;
    const std::string schemeText = p.subkey.substr(0, slash);
    if (schemeText == "ACTIVE") return bad("the ACTIVE alias must be resolved to a concrete power scheme before use");
    t.scheme = normalizeGuid(schemeText);
    t.subgroup = normalizeGuid(std::string_view(p.subkey).substr(slash + 1));
    if (t.scheme.empty() || t.subgroup.empty()) return bad("invalid GUID in power path");

    const size_t colon = p.valueName.rfind(':');
    if (colon == std::string::npos) return bad("expected <setting-guid>:ac|dc");
    t.setting = normalizeGuid(std::string_view(p.valueName).substr(0, colon));
    const std::string mode = p.valueName.substr(colon + 1);
    if (t.setting.empty() || (mode != "ac" && mode != "dc")) return bad("expected <setting-guid>:ac|dc");
    t.ac = mode == "ac";
    return t;
}

Result<std::optional<RegValue>> PowerRegistry::read(const RegPath& path) {
    auto t = parse(path);
    if (!t.ok()) return t.error();
    auto v = m_api.readIndex(t.value().scheme, t.value().subgroup, t.value().setting, t.value().ac);
    if (!v.ok()) return v.error();
    if (!v.value()) {
        // 設定が無い電源プランでは「元の値」をバックアップできないので、適用の対象にしない。
        return Error{ErrorCode::UnsupportedValueType, "the setting does not exist in this power scheme: " + path.display()};
    }
    return std::optional<RegValue>{RegValue::dword(*v.value())};
}

Result<void> PowerRegistry::write(const RegPath& path, const RegValue& value) {
    auto t = parse(path);
    if (!t.ok()) return t.error();
    if (value.type != RegType::Dword) return Error{ErrorCode::InvalidPath, "power settings are DWORD values: " + path.display()};
    const Target& x = t.value();
    if (auto r = m_api.writeIndex(x.scheme, x.subgroup, x.setting, x.ac, static_cast<uint32_t>(value.number)); !r.ok()) return r;
    // アクティブな電源プランを書き換えたときは、反映のために再適用が必要 (PowerSetActiveScheme)。
    auto active = m_api.activeSchemeGuid();
    if (active.ok() && active.value() == x.scheme) {
        if (auto r = m_api.applyScheme(x.scheme); !r.ok()) return r;
    }
    return {};
}

Result<void> PowerRegistry::deleteValue(const RegPath& path) {
    return Error{ErrorCode::RegistryDelete, "power settings cannot be deleted; a restore writes the original value back: " + path.display()};
}

}  // namespace lf
