#include "lf/history.hpp"

#include <windows.h>

#include <fstream>
#include <nlohmann/json.hpp>

namespace lf {

using nlohmann::json;

Result<void> HistoryLog::append(const HistoryEntry& e) const {
    json j = {{"time", e.time},       {"action", e.action},   {"tweakId", e.tweakId},
              {"target", e.target},   {"old", valueToJson(e.oldValue)}, {"new", valueToJson(e.newValue)},
              {"result", e.result},   {"message", e.message}};
    std::error_code ec;
    if (m_file.has_parent_path()) std::filesystem::create_directories(m_file.parent_path(), ec);
    std::ofstream f(m_file, std::ios::app | std::ios::binary);
    if (!f) return Error{ErrorCode::Io, "cannot open history file"};
    f << j.dump() << "\n";
    f.flush();
    if (!f.good()) return Error{ErrorCode::Io, "cannot write history file"};
    return {};
}

std::vector<HistoryEntry> HistoryLog::readAll() const {
    std::vector<HistoryEntry> out;
    std::ifstream f(m_file, std::ios::binary);
    std::string line;
    while (std::getline(f, line)) {
        json j = json::parse(line, nullptr, false);
        if (j.is_discarded() || !j.is_object()) continue;
        HistoryEntry e;
        e.time = j.value("time", std::string());
        e.action = j.value("action", std::string());
        e.tweakId = j.value("tweakId", std::string());
        e.target = j.value("target", std::string());
        e.result = j.value("result", std::string());
        e.message = j.value("message", std::string());
        auto o = valueFromJson(j.value("old", json(nullptr)));
        auto n = valueFromJson(j.value("new", json(nullptr)));
        if (o.ok()) e.oldValue = o.value();
        if (n.ok()) e.newValue = n.value();
        out.push_back(std::move(e));
    }
    return out;
}

}  // namespace lf
