#include "backend.hpp"

#include "lf/state.hpp"
#include "lf/system.hpp"
#include "lf/win_registry.hpp"

namespace lfapp {

namespace fs = std::filesystem;

bool Backend::init(const BackendOptions& opt) {
    demo = opt.demo;
    osBuild = lf::detectOsVersion().build;

    // デモ: 状態/履歴/ログは別フォルダ。実機の状態ファイルとは混ざらない。
    const fs::path dataDir = opt.demo ? opt.configDir / "demo" : opt.configDir;
    if (opt.demo) {
        // デモは毎回まっさらな状態から始める (メモリ上のレジストリと、前回のデモの状態ファイルを食い違わせない)。
        std::error_code ec;
        fs::remove(dataDir / "state.json", ec);
        fs::remove(dataDir / "history.jsonl", ec);
    }
    log.setFile(dataDir / "latencyforge.log");
    log.info("app", std::string("starting; mode=") + (demo ? "DEMO (in-memory registry)" : "real registry") +
                        ", Windows build " + std::to_string(osBuild));

    catalog = std::make_unique<lf::TweakCatalog>(policy);
    definitionIssues = catalog->loadDirectory(opt.exeDir / "data" / "tweaks");
    for (const auto& i : definitionIssues)
        log.error("catalog", "tweak definition rejected: " + i.source + (i.tweakId.empty() ? "" : " [" + i.tweakId + "]"),
                  i.message);
    log.info("catalog", std::to_string(catalog->all().size()) + " tweak(s) loaded");

    if (auto text = lf::readFileText(opt.exeDir / "data" / "presets.json"); text.ok()) {
        std::vector<lf::DefinitionIssue> presetIssues;
        if (!lf::parsePresetDocument(text.value(), "presets.json", *catalog, presets, presetIssues)) {
            for (auto& i : presetIssues) {
                log.error("catalog", "preset definition rejected: " + i.tweakId, i.message);
                definitionIssues.push_back(std::move(i));
            }
        } else {
            log.info("catalog", std::to_string(presets.size()) + " preset(s) loaded");
        }
    } else {
        log.warn("catalog", "presets.json could not be read", text.error().detail);
    }

    if (demo) {
        auto mem = std::make_unique<lf::MemoryRegistry>();
        // 見本の値: 存在しない / 目標と違う / 既に目標値、が混ざるように。
        int i = 0;
        for (const auto& t : catalog->all()) {
            const int k = i++ % 3;
            if (k == 1)
                mem->set(t.target, lf::RegValue::dword(t.data.number == 0 ? 1 : 0));
            else if (k == 2)
                mem->set(t.target, t.data);  // 既に目標値
        }
        registry = std::move(mem);
    } else {
        registry = std::make_unique<lf::WinRegistry>();
    }

    if (opt.demo && opt.seedDemoPending && !catalog->all().empty()) {
        // 「異常終了後に未完了の変更を検出して復元を提案する」経路を、本物の読み込み処理で再現する。
        lf::State st;
        lf::PendingTx tx;
        tx.id = tx.startedAt = lf::nowIso8601Utc();
        int n = 0;
        for (const auto& t : catalog->all()) {
            if (n++ >= 2) break;
            auto cur = registry->read(t.target);
            tx.ops.push_back({t.id, t.target, cur.ok() ? cur.value() : std::nullopt, false});
            (void)registry->write(t.target, t.data);  // 書き込み途中で落ちた状態
            st.applied[t.id] = {t.id, t.target, tx.ops.back().before, t.data, tx.startedAt, t.requiresReboot};
        }
        st.pending = tx;
        (void)lf::saveState(dataDir / "state.json", st);
    }

    lf::EngineConfig cfg{dataDir / "state.json", dataDir / "history.jsonl", osBuild};
    engine = std::make_unique<lf::Engine>(*registry, policy, log, cfg);
    if (auto r = engine->load(); !r.ok()) stateError = r.error();
    return true;
}

std::vector<const lf::TweakDef*> Backend::tweaksIn(const std::string& category) const {
    std::vector<const lf::TweakDef*> out;
    if (!catalog) return out;
    for (const auto& t : catalog->all())
        if (t.category == category) out.push_back(&t);
    return out;
}

}  // namespace lfapp
