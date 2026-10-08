#include "lf/state.hpp"

#include <nlohmann/json.hpp>

#include "lf/util.hpp"

namespace lf {
namespace {

using nlohmann::json;

Error corrupt(const std::string& why) { return Error{ErrorCode::StateCorrupt, why}; }

Result<RegPath> pathFrom(const json& j, const Policy& policy) {
    auto k = j.find("key");
    auto v = j.find("value");
    if (k == j.end() || v == j.end() || !k->is_string() || !v->is_string()) return corrupt("record without key/value");
    auto p = parseRegPath(k->get<std::string>(), v->get<std::string>());
    if (!p.ok()) return corrupt(p.error().detail);
    // state.json は書き換え可能なファイルなので、復元先も必ず Policy で検査する。
    if (auto pr = policy.check(p.value()); !pr.ok()) return corrupt("record targets a forbidden path: " + pr.error().detail);
    return p.value();
}

json pathJson(const RegPath& p) { return {{"key", p.keyString()}, {"value", p.valueName}}; }

}  // namespace

Result<State> loadState(const std::filesystem::path& file, const Policy& policy) {
    auto text = readFileText(file);
    if (!text.ok()) {
        if (text.error().code == ErrorCode::NotFound) return State{};
        return text.error();
    }
    json doc = json::parse(text.value(), nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) return corrupt("state file is not valid JSON");
    if (!doc.contains("version") || !doc["version"].is_number_unsigned() || doc["version"].get<unsigned>() != 1)
        return corrupt("unsupported state version");

    State st;
    if (auto it = doc.find("applied"); it != doc.end()) {
        if (!it->is_object()) return corrupt("'applied' must be an object");
        for (auto rec = it->begin(); rec != it->end(); ++rec) {
            const json& j = rec.value();
            if (!j.is_object()) return corrupt("record must be an object");
            AppliedRecord r;
            r.tweakId = rec.key();
            auto p = pathFrom(j, policy);
            if (!p.ok()) return p.error();
            r.target = p.value();
            auto orig = valueFromJson(j.value("original", json(nullptr)));
            if (!orig.ok()) return orig.error();
            r.original = orig.value();
            auto app = valueFromJson(j.contains("applied") ? j["applied"] : json(nullptr));
            if (!app.ok() || !app.value()) return corrupt("record without applied value");
            r.applied = *app.value();
            r.time = j.value("time", std::string());
            r.requiresReboot = j.value("requiresReboot", false);
            st.applied.emplace(r.tweakId, std::move(r));
        }
    }
    if (auto it = doc.find("pending"); it != doc.end() && !it->is_null()) {
        if (!it->is_object() || !it->contains("ops") || !(*it)["ops"].is_array()) return corrupt("invalid pending transaction");
        PendingTx tx;
        tx.id = it->value("id", std::string());
        tx.startedAt = it->value("startedAt", std::string());
        for (const auto& o : (*it)["ops"]) {
            if (!o.is_object() || !o.contains("tweakId") || !o["tweakId"].is_string())
                return corrupt("invalid pending operation");
            TxOp op;
            op.tweakId = o["tweakId"].get<std::string>();
            auto p = pathFrom(o, policy);
            if (!p.ok()) return p.error();
            op.target = p.value();
            auto before = valueFromJson(o.value("before", json(nullptr)));
            if (!before.ok()) return before.error();
            op.before = before.value();
            op.wasTracked = o.value("wasTracked", false);
            tx.ops.push_back(std::move(op));
        }
        st.pending = std::move(tx);
    }
    return st;
}

Result<void> saveState(const std::filesystem::path& file, const State& st) {
    json doc;
    doc["version"] = 1;
    json applied = json::object();
    for (const auto& [id, r] : st.applied) {
        json j = pathJson(r.target);
        j["original"] = valueToJson(r.original);
        j["applied"] = valueToJson(r.applied);
        j["time"] = r.time;
        j["requiresReboot"] = r.requiresReboot;
        applied[id] = std::move(j);
    }
    doc["applied"] = std::move(applied);
    if (st.pending) {
        json ops = json::array();
        for (const auto& op : st.pending->ops) {
            json j = pathJson(op.target);
            j["tweakId"] = op.tweakId;
            j["before"] = valueToJson(op.before);
            j["wasTracked"] = op.wasTracked;
            ops.push_back(std::move(j));
        }
        doc["pending"] = {{"id", st.pending->id}, {"startedAt", st.pending->startedAt}, {"ops", std::move(ops)}};
    } else {
        doc["pending"] = nullptr;
    }
    auto r = atomicWriteFile(file, doc.dump(2));
    if (!r.ok()) return Error{ErrorCode::StateWrite, r.error().detail, r.error().win32};
    return {};
}

}  // namespace lf
