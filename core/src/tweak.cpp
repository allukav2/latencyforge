#include "lf/tweak.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <set>

#include "lf/power_api.hpp"
#include "lf/util.hpp"

namespace lf {
namespace {

using nlohmann::json;

constexpr size_t kMaxShort = 300;
constexpr size_t kMaxLong = 2000;

bool validId(const std::string& s) {
    // ^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)+$  (正規表現を使わず手書き)
    if (s.empty() || s.size() > 100) return false;
    int segments = 1;
    bool segStart = true;
    for (char c : s) {
        if (c == '.') {
            if (segStart) return false;
            ++segments;
            segStart = true;
            continue;
        }
        const bool lower = c >= 'a' && c <= 'z';
        const bool digitOrUnderscore = (c >= '0' && c <= '9') || c == '_';
        if (segStart ? !lower : !(lower || digitOrUnderscore)) return false;
        segStart = false;
    }
    return !segStart && segments >= 2;
}

// 電源設定の定義の形式チェック: POWER\ACTIVE\<サブグループ GUID> / <設定 GUID>:ac|dc / REG_DWORD。問題があればその説明を返す。
std::string powerTemplateProblem(const RegPath& p, RegType type) {
    if (type != RegType::Dword) return "power settings must be REG_DWORD";
    const size_t slash = p.subkey.find('\\');
    if (slash == std::string::npos || p.subkey.substr(0, slash) != "ACTIVE" || normalizeGuid(p.subkey.substr(slash + 1)).empty())
        return "a power setting path must be POWER\\ACTIVE\\<subgroup-guid>";
    const size_t colon = p.valueName.rfind(':');
    if (colon == std::string::npos || normalizeGuid(p.valueName.substr(0, colon)).empty()) return "a power setting value must be <setting-guid>:ac or <setting-guid>:dc";
    const std::string mode = p.valueName.substr(colon + 1);
    if (mode != "ac" && mode != "dc") return "a power setting value must end in :ac or :dc";
    return {};
}

struct Ctx {
    std::string_view source;
    std::vector<DefinitionIssue>* issues;
    bool failed = false;
    void fail(const std::string& id, const std::string& msg) {
        failed = true;
        issues->push_back({std::string(source), id, msg});
    }
};

// 許可キー以外が混じっていれば報告。
void checkKeys(Ctx& c, const std::string& id, const json& obj, const std::set<std::string>& allowed, const char* where) {
    for (auto it = obj.begin(); it != obj.end(); ++it)
        if (!allowed.count(it.key())) c.fail(id, std::string("unknown field '") + it.key() + "' in " + where);
}

bool readLText(Ctx& c, const std::string& id, const json& obj, const char* field, bool required, size_t maxLen, LText& out) {
    auto it = obj.find(field);
    if (it == obj.end()) {
        if (required) c.fail(id, std::string("missing required field '") + field + "'");
        return !required;
    }
    if (!it->is_object()) {
        c.fail(id, std::string("'") + field + "' must be an object with ja/en");
        return false;
    }
    checkKeys(c, id, *it, {"ja", "en"}, field);
    bool ok = true;
    for (const char* lang : {"ja", "en"}) {
        auto l = it->find(lang);
        if (l == it->end() || !l->is_string() || l->get<std::string>().empty()) {
            c.fail(id, std::string("'") + field + "." + lang + "' must be a non-empty string");
            ok = false;
        } else if (l->get<std::string>().size() > maxLen) {
            c.fail(id, std::string("'") + field + "." + lang + "' is too long");
            ok = false;
        }
    }
    if (ok) {
        out.ja = (*it)["ja"].get<std::string>();
        out.en = (*it)["en"].get<std::string>();
    }
    return ok;
}

bool readString(Ctx& c, const std::string& id, const json& obj, const char* field, std::string& out) {
    auto it = obj.find(field);
    if (it == obj.end() || !it->is_string()) {
        c.fail(id, std::string("'") + field + "' must be a string");
        return false;
    }
    out = it->get<std::string>();
    return true;
}

bool readBuild(Ctx& c, const std::string& id, const json& obj, const char* field, uint32_t& out) {
    auto it = obj.find(field);
    if (it == obj.end()) return true;
    if (!it->is_number_unsigned() || it->get<uint64_t>() > 0xFFFFFFFEull) {
        c.fail(id, std::string("'") + field + "' must be an unsigned integer build number");
        return false;
    }
    out = static_cast<uint32_t>(it->get<uint64_t>());
    return true;
}

void parseOne(Ctx& c, const json& j, const Policy& policy, std::vector<TweakDef>& out, std::set<std::string>& seen, size_t index) {
    std::string id = "#" + std::to_string(index);
    if (!j.is_object()) {
        c.fail(id, "tweak entry must be an object");
        return;
    }
    if (auto it = j.find("id"); it != j.end() && it->is_string()) id = it->get<std::string>();

    checkKeys(c, id, j,
              {"id", "category", "path", "value", "type", "data", "title", "summary", "details", "note", "risk",
               "requiresReboot", "minBuild", "maxBuild"},
              "tweak");

    TweakDef t;
    if (!readString(c, id, j, "id", t.id)) return;
    if (!validId(t.id)) {
        c.fail(id, "id must match ^[a-z][a-z0-9_]*(\\.[a-z][a-z0-9_]*)+$ (e.g. kernel.timer_check_flags)");
        return;
    }
    if (!seen.insert(t.id).second) {
        c.fail(id, "duplicate id");
        return;
    }

    t.category = "general";
    if (auto it = j.find("category"); it != j.end()) {
        if (!it->is_string() || it->get<std::string>().empty() || it->get<std::string>().size() > 50)
            c.fail(id, "'category' must be a non-empty string (<= 50 chars)");
        else
            t.category = it->get<std::string>();
    }

    std::string pathStr, valueName, typeStr;
    const bool haveStrings = readString(c, id, j, "path", pathStr) & readString(c, id, j, "value", valueName) &
                             readString(c, id, j, "type", typeStr);
    std::optional<RegType> type;
    if (haveStrings) {
        type = parseTypeName(typeStr);
        if (!type) c.fail(id, "'type' must be one of REG_DWORD, REG_QWORD, REG_SZ, REG_EXPAND_SZ");
        auto rp = parseRegPath(pathStr, valueName);
        if (!rp.ok()) {
            c.fail(id, rp.error().detail);
        } else {
            t.target = rp.value();
            // 許可/拒否ポリシー。DisableExceptionChainValidation 等はここで拒否される。
            if (auto pr = policy.check(t.target); !pr.ok()) c.fail(id, pr.error().detail);
        }
    }

    if (type) {
        auto d = j.find("data");
        t.data.type = *type;
        if (d == j.end()) {
            c.fail(id, "missing required field 'data'");
        } else if (t.data.isNumeric()) {
            if (!d->is_number_unsigned()) {
                c.fail(id, "'data' must be an unsigned integer for " + typeStr);
            } else {
                t.data.number = d->get<uint64_t>();
                if (*type == RegType::Dword && t.data.number > 0xFFFFFFFFull) c.fail(id, "'data' exceeds REG_DWORD range");
            }
        } else if (!d->is_string() || d->get<std::string>().size() > 1024) {
            c.fail(id, "'data' must be a string (<= 1024 chars) for " + typeStr);
        } else {
            t.data.text = d->get<std::string>();
        }
    }

    if (type && t.target.hive == RegHive::Power) {
        if (std::string why = powerTemplateProblem(t.target, *type); !why.empty()) c.fail(id, why);
    }

    readLText(c, id, j, "title", true, kMaxShort, t.title);
    readLText(c, id, j, "summary", true, kMaxShort, t.summary);
    readLText(c, id, j, "details", false, kMaxLong, t.details);
    readLText(c, id, j, "note", false, kMaxLong, t.note);

    if (auto it = j.find("risk"); it == j.end() || !it->is_string()) {
        c.fail(id, "'risk' must be \"low\" or \"medium\"");
    } else if (it->get<std::string>() == "low") {
        t.risk = Risk::Low;
    } else if (it->get<std::string>() == "medium") {
        t.risk = Risk::Medium;
    } else {
        c.fail(id, "'risk' must be \"low\" or \"medium\"");
    }

    if (auto it = j.find("requiresReboot"); it == j.end() || !it->is_boolean())
        c.fail(id, "'requiresReboot' must be a boolean");
    else
        t.requiresReboot = it->get<bool>();

    if (readBuild(c, id, j, "minBuild", t.minBuild) && t.minBuild < kMinSupportedBuild)
        c.fail(id, "'minBuild' must be >= " + std::to_string(kMinSupportedBuild) + " (Windows 10 1809)");
    if (readBuild(c, id, j, "maxBuild", t.maxBuild) && t.maxBuild < t.minBuild) c.fail(id, "'maxBuild' must be >= 'minBuild'");

    if (!c.failed) out.push_back(std::move(t));
}

}  // namespace

bool parseTweakDocument(std::string_view text, std::string_view source, const Policy& policy, std::vector<TweakDef>& out,
                        std::vector<DefinitionIssue>& issues) {
    Ctx c{source, &issues};
    json doc = json::parse(text.begin(), text.end(), nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) {
        c.fail("", "document is not valid JSON (root must be an object)");
        return false;
    }
    checkKeys(c, "", doc, {"schema", "tweaks"}, "document");
    if (auto it = doc.find("schema"); it == doc.end() || !it->is_number_unsigned() || it->get<unsigned>() != 1)
        c.fail("", "'schema' must be 1");
    auto tw = doc.find("tweaks");
    if (tw == doc.end() || !tw->is_array()) {
        c.fail("", "'tweaks' must be an array");
        return false;
    }

    std::vector<TweakDef> parsed;
    std::set<std::string> seen;
    size_t i = 0;
    for (const auto& j : *tw) parseOne(c, j, policy, parsed, seen, i++);
    if (c.failed) return false;
    for (auto& t : parsed) out.push_back(std::move(t));
    return true;
}

TweakDef resolveForScheme(const TweakDef& base, const std::string& schemeGuid) {
    if (!base.isPowerTemplate()) return base;
    TweakDef t = base;
    const size_t slash = t.target.subkey.find('\\');  // "ACTIVE\<サブグループ>" の "ACTIVE" を置き換える
    t.target.subkey = schemeGuid + (slash == std::string::npos ? std::string() : t.target.subkey.substr(slash));
    t.id = base.id + "@" + schemeGuid;
    return t;
}

std::string baseTweakId(std::string_view id) {
    const size_t at = id.find('@');
    return std::string(at == std::string_view::npos ? id : id.substr(0, at));
}

bool TweakCatalog::loadString(std::string_view text, std::string_view source, std::vector<DefinitionIssue>& issues) {
    std::vector<TweakDef> parsed;
    if (!parseTweakDocument(text, source, m_policy, parsed, issues)) return false;
    for (const auto& t : parsed) {
        if (find(t.id)) {
            issues.push_back({std::string(source), t.id, "duplicate id (already defined in another file)"});
            return false;
        }
    }
    for (auto& t : parsed) m_tweaks.push_back(std::move(t));
    return true;
}

std::vector<DefinitionIssue> TweakCatalog::loadDirectory(const std::filesystem::path& dir) {
    std::vector<DefinitionIssue> issues;
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.is_regular_file() && e.path().extension() == ".json") files.push_back(e.path());
    if (ec) {
        issues.push_back({narrow(dir.wstring()), "", "cannot read tweak directory"});
        return issues;
    }
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        auto text = readFileText(f);
        const std::string name = narrow(f.filename().wstring());
        if (!text.ok()) {
            issues.push_back({name, "", text.error().detail});
            continue;
        }
        loadString(text.value(), name, issues);
    }
    return issues;
}

const TweakDef* TweakCatalog::find(std::string_view id) const {
    for (const auto& t : m_tweaks)
        if (t.id == id) return &t;
    return nullptr;
}

}  // namespace lf
