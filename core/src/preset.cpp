#include "lf/preset.hpp"

#include <nlohmann/json.hpp>
#include <set>

namespace lf {
namespace {

using nlohmann::json;

constexpr size_t kMaxText = 600;

bool validPresetId(const std::string& s) {
    if (s.empty() || s.size() > 40 || !(s[0] >= 'a' && s[0] <= 'z')) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
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

void checkKeys(Ctx& c, const std::string& id, const json& obj, const std::set<std::string>& allowed, const char* where) {
    for (auto it = obj.begin(); it != obj.end(); ++it)
        if (!allowed.count(it.key())) c.fail(id, std::string("unknown field '") + it.key() + "' in " + where);
}

bool readText(Ctx& c, const std::string& id, const json& obj, const char* field, LText& out) {
    auto it = obj.find(field);
    if (it == obj.end() || !it->is_object()) {
        c.fail(id, std::string("'") + field + "' must be an object with ja/en");
        return false;
    }
    checkKeys(c, id, *it, {"ja", "en"}, field);
    bool ok = true;
    for (const char* lang : {"ja", "en"}) {
        auto l = it->find(lang);
        if (l == it->end() || !l->is_string() || l->get<std::string>().empty() || l->get<std::string>().size() > kMaxText) {
            c.fail(id, std::string("'") + field + "." + lang + "' must be a non-empty string (<= 600 chars)");
            ok = false;
        }
    }
    if (ok) {
        out.ja = (*it)["ja"].get<std::string>();
        out.en = (*it)["en"].get<std::string>();
    }
    return ok;
}

}  // namespace

bool parsePresetDocument(std::string_view text, std::string_view source, const TweakCatalog& catalog,
                         std::vector<PresetDef>& out, std::vector<DefinitionIssue>& issues) {
    Ctx c{source, &issues};
    json doc = json::parse(text.begin(), text.end(), nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        c.fail("", "document is not valid JSON (root must be an object)");
        return false;
    }
    checkKeys(c, "", doc, {"schema", "presets"}, "document");
    if (auto it = doc.find("schema"); it == doc.end() || !it->is_number_unsigned() || it->get<unsigned>() != 1)
        c.fail("", "'schema' must be 1");
    auto arr = doc.find("presets");
    if (arr == doc.end() || !arr->is_array()) {
        c.fail("", "'presets' must be an array");
        return false;
    }

    std::vector<PresetDef> parsed;
    std::set<std::string> seen;
    size_t index = 0;
    for (const json& j : *arr) {
        std::string id = "#" + std::to_string(index++);
        if (!j.is_object()) {
            c.fail(id, "preset entry must be an object");
            continue;
        }
        if (auto it = j.find("id"); it != j.end() && it->is_string()) id = it->get<std::string>();
        checkKeys(c, id, j, {"id", "title", "description", "tweaks"}, "preset");

        PresetDef p;
        if (auto it = j.find("id"); it == j.end() || !it->is_string() || !validPresetId(it->get<std::string>())) {
            c.fail(id, "'id' must match ^[a-z][a-z0-9_]*$ (<= 40 chars)");
        } else {
            p.id = it->get<std::string>();
            if (!seen.insert(p.id).second) c.fail(id, "duplicate preset id");
        }
        readText(c, id, j, "title", p.title);
        readText(c, id, j, "description", p.description);

        auto tw = j.find("tweaks");
        if (tw == j.end() || !tw->is_array() || tw->empty()) {
            c.fail(id, "'tweaks' must be a non-empty array of tweak ids");
        } else {
            std::set<std::string> ids;
            for (const json& t : *tw) {
                if (!t.is_string()) {
                    c.fail(id, "'tweaks' must contain only strings");
                    continue;
                }
                const std::string tid = t.get<std::string>();
                if (!ids.insert(tid).second) c.fail(id, "duplicate tweak id in preset: " + tid);
                else if (!catalog.find(tid)) c.fail(id, "unknown tweak id (not in the tweak catalog): " + tid);
                else p.tweakIds.push_back(tid);
            }
        }
        parsed.push_back(std::move(p));
    }
    if (c.failed) return false;
    for (auto& p : parsed) out.push_back(std::move(p));
    return true;
}

std::vector<const TweakDef*> resolvePreset(const PresetDef& preset, const TweakCatalog& catalog) {
    std::vector<const TweakDef*> out;
    for (const auto& id : preset.tweakIds)
        if (const TweakDef* t = catalog.find(id)) out.push_back(t);
    return out;
}

Risk presetRisk(const PresetDef& preset, const TweakCatalog& catalog) {
    Risk r = Risk::Low;
    for (const TweakDef* t : resolvePreset(preset, catalog))
        if (t->risk == Risk::Medium) r = Risk::Medium;
    return r;
}

}  // namespace lf
