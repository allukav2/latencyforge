#include "lf/i18n.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace lf {
namespace {

using nlohmann::json;

bool flatten(const json& node, const std::string& prefix,
             std::unordered_map<std::string, std::string>& out, std::string& err) {
    if (node.is_string()) {
        out[prefix] = node.get<std::string>();
        return true;
    }
    if (node.is_object()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            const std::string key = prefix.empty() ? it.key() : prefix + "." + it.key();
            if (!flatten(it.value(), key, out, err)) return false;
        }
        return true;
    }
    err = "value of '" + prefix + "' is neither a string nor an object";
    return false;
}

}  // namespace

bool Translator::loadString(std::string_view text, std::string* error) {
    std::string err;
    json doc = json::parse(text.begin(), text.end(), nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) {
        if (error) *error = "invalid JSON (root must be an object)";
        return false;
    }
    std::unordered_map<std::string, std::string> tmp;
    if (!flatten(doc, "", tmp, err)) {
        if (error) *error = err;
        return false;
    }
    for (auto& [k, v] : tmp) m_map[k] = std::move(v);
    return true;
}

bool Translator::loadFile(const std::filesystem::path& path, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "cannot open " + path.string();
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return loadString(ss.str(), error);
}

void Translator::clear() {
    m_map.clear();
    m_missing.clear();
}

const char* Translator::tr(std::string_view key) const {
    if (auto it = m_map.find(key); it != m_map.end()) return it->second.c_str();
    return m_missing.emplace(key).first->c_str();
}

}  // namespace lf
