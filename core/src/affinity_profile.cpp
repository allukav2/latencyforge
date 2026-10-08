#include "lf/affinity_profile.hpp"

#include <nlohmann/json.hpp>
#include <set>

#include "lf/affinity_safety.hpp"
#include "lf/util.hpp"

namespace lf {
namespace {

using nlohmann::json;

Error invalid(const std::string& why) { return Error{ErrorCode::DefinitionInvalid, "affinity config: " + why}; }

bool validId(const std::string& s) {
    if (s.empty() || s.size() > 40) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

}  // namespace

const char* priorityKey(GamePriority p) {
    switch (p) {
        case GamePriority::Unchanged: return "unchanged";
        case GamePriority::AboveNormal: return "above_normal";
        case GamePriority::High: return "high";
    }
    return "unchanged";
}

bool parsePriority(std::string_view text, GamePriority& out) {
    for (GamePriority p : {GamePriority::Unchanged, GamePriority::AboveNormal, GamePriority::High}) {
        if (text == priorityKey(p)) {
            out = p;
            return true;
        }
    }
    return false;
}

uint32_t priorityClassOf(GamePriority p) {
    switch (p) {
        case GamePriority::AboveNormal: return static_cast<uint32_t>(PriorityClass::AboveNormal);
        case GamePriority::High: return static_cast<uint32_t>(PriorityClass::High);
        default: return static_cast<uint32_t>(PriorityClass::Normal);
    }
}

Result<AffinityConfig> parseAffinityConfig(std::string_view text) {
    json doc = json::parse(text.begin(), text.end(), nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) return invalid("not valid JSON (root must be an object)");
    for (auto it = doc.begin(); it != doc.end(); ++it)
        if (it.key() != "version" && it.key() != "enabled" && it.key() != "profiles") return invalid("unknown field '" + it.key() + "'");
    if (auto v = doc.find("version"); v == doc.end() || !v->is_number_unsigned() || v->get<unsigned>() != 1) return invalid("'version' must be 1");

    AffinityConfig cfg;
    if (auto e = doc.find("enabled"); e != doc.end()) {
        if (!e->is_boolean()) return invalid("'enabled' must be a boolean");
        cfg.enabled = e->get<bool>();
    }
    auto arr = doc.find("profiles");
    if (arr == doc.end() || !arr->is_array()) return invalid("'profiles' must be an array");

    std::set<std::string> ids, exes;
    for (const json& j : *arr) {
        if (!j.is_object()) return invalid("a profile must be an object");
        for (auto it = j.begin(); it != j.end(); ++it) {
            static const std::set<std::string> allowed = {"id", "name", "exeNames", "strategy", "gameUsesSmtSiblings",
                                                          "moveBackground", "priority", "enabled"};
            if (!allowed.count(it.key())) return invalid("unknown field '" + it.key() + "' in a profile");
        }
        AffinityProfile p;
        auto id = j.find("id");
        if (id == j.end() || !id->is_string() || !validId(id->get<std::string>())) return invalid("'id' must match ^[a-z0-9_]{1,40}$");
        p.id = id->get<std::string>();
        if (!ids.insert(p.id).second) return invalid("duplicate profile id '" + p.id + "'");

        auto name = j.find("name");
        if (name == j.end() || !name->is_string() || name->get<std::string>().empty() || name->get<std::string>().size() > 80)
            return invalid("'name' must be a non-empty string (<= 80 chars)");
        p.name = name->get<std::string>();

        auto exeNames = j.find("exeNames");
        if (exeNames == j.end() || !exeNames->is_array() || exeNames->empty() || exeNames->size() > 8)
            return invalid("'exeNames' must be a non-empty array (<= 8 entries)");
        for (const json& e : *exeNames) {
            if (!e.is_string() || !isValidExeName(e.get<std::string>())) return invalid("invalid exe name (use a bare file name ending in .exe)");
            const std::string lower = lowerAscii(e.get<std::string>());
            if (isNeverTouchName(lower)) return invalid("'" + lower + "' is a protected process and cannot be targeted");
            if (!exes.insert(lower).second) return invalid("exe '" + lower + "' appears in more than one profile");
            p.exeNames.push_back(lower);
        }

        if (auto s = j.find("strategy"); s != j.end()) {
            if (!s->is_string() || !parseStrategy(s->get<std::string>(), p.options.strategy)) return invalid("unknown 'strategy'");
        }
        if (auto s = j.find("gameUsesSmtSiblings"); s != j.end()) {
            if (!s->is_boolean()) return invalid("'gameUsesSmtSiblings' must be a boolean");
            p.options.gameUsesSmtSiblings = s->get<bool>();
        }
        if (auto s = j.find("moveBackground"); s != j.end()) {
            if (!s->is_boolean()) return invalid("'moveBackground' must be a boolean");
            p.moveBackground = s->get<bool>();
        }
        if (auto s = j.find("priority"); s != j.end()) {
            if (!s->is_string() || !parsePriority(s->get<std::string>(), p.priority)) return invalid("unknown 'priority'");
        }
        if (auto s = j.find("enabled"); s != j.end()) {
            if (!s->is_boolean()) return invalid("'enabled' must be a boolean");
            p.enabled = s->get<bool>();
        }
        cfg.profiles.push_back(std::move(p));
    }
    return cfg;
}

std::string serializeAffinityConfig(const AffinityConfig& cfg) {
    json profiles = json::array();
    for (const auto& p : cfg.profiles) {
        profiles.push_back({{"id", p.id},
                            {"name", p.name},
                            {"exeNames", p.exeNames},
                            {"strategy", strategyKey(p.options.strategy)},
                            {"gameUsesSmtSiblings", p.options.gameUsesSmtSiblings},
                            {"moveBackground", p.moveBackground},
                            {"priority", priorityKey(p.priority)},
                            {"enabled", p.enabled}});
    }
    return json{{"version", 1}, {"enabled", cfg.enabled}, {"profiles", profiles}}.dump(2);
}

Result<AffinityConfig> loadAffinityConfig(const std::filesystem::path& file) {
    auto text = readFileText(file);
    if (!text.ok()) {
        if (text.error().code == ErrorCode::NotFound) return AffinityConfig{};
        return text.error();
    }
    return parseAffinityConfig(text.value());
}

Result<void> saveAffinityConfig(const std::filesystem::path& file, const AffinityConfig& cfg) {
    return atomicWriteFile(file, serializeAffinityConfig(cfg));
}

}  // namespace lf
