#include "backend.hpp"

#include "lf/state.hpp"
#include "lf/system.hpp"
#include "lf/win_process_api.hpp"
#include "lf/win_registry.hpp"

namespace lfapp {

namespace fs = std::filesystem;

bool Backend::init(const BackendOptions& opt) {
    demo = opt.demo;
    osBuild = lf::detectOsVersion().build;

    // デモ: 状態/履歴/ログは別フォルダ。実機の状態ファイルとは混ざらない。
    const fs::path dataDir = opt.demo ? opt.configDir / "demo" : opt.configDir;
    m_dataDir = dataDir;
    if (opt.demo) {
        // デモは毎回まっさらな状態から始める (メモリ上のレジストリと、前回のデモの状態ファイルを食い違わせない)。
        std::error_code ec;
        fs::remove(dataDir / "state.json", ec);
        fs::remove(dataDir / "history.jsonl", ec);
        fs::remove(dataDir / "affinity_state.json", ec);
        fs::remove(dataDir / "affinity.json", ec);
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

// ---------------------------------------------------------------- Affinity

void Backend::initAffinity(const lf::CpuTopology& topology, bool featureAvailable, const std::string& demoShow) {
    if (demo) {
        auto fake = std::make_unique<lf::FakeProcessApi>();
        m_fake = fake.get();
        // 論理プロセッサ全体のマスク (グループ 0)
        m_demoFullMask = 0;
        for (const auto& c : topology.cores)
            if (c.group == 0) m_demoFullMask |= c.mask;
        if (m_demoFullMask == 0) m_demoFullMask = 0xFFFF;

        lf::FakeProcessApi::Options o;
        o.mask = m_demoFullMask;
        for (const char* n : {"chrome.exe", "chrome.exe", "Discord.exe", "obs64.exe", "Spotify.exe", "steam.exe", "Code.exe"}) fake->add(n, o);
        fake->add("explorer.exe", o);
        fake->add("audiodg.exe", o);
        lf::FakeProcessApi::Options other = o;
        other.sameUser = false;
        fake->add("svchost.exe", other);
        lf::FakeProcessApi::Options prot = o;
        prot.isProtected = true;
        fake->add("MsMpEng.exe", prot);
        lf::FakeProcessApi::Options guarded = o;
        guarded.denyOpen = true;
        fake->add("vgc.exe", guarded);
        procApi = std::move(fake);
    } else {
        procApi = std::make_unique<lf::WinProcessApi>();
    }

    affinity = std::make_unique<lf::AffinityManager>(*procApi, log, m_dataDir / "affinity_state.json");
    affinity->setTopology(topology);

    lf::AffinityConfig cfg;
    auto loaded = lf::loadAffinityConfig(m_dataDir / "affinity.json");
    if (loaded.ok())
        cfg = loaded.value();
    else {
        affinityConfigError = loaded.error();  // 既定値で動作。壊れたファイルは、ユーザーが変更するまで上書きしない
        log.error("affinity", "affinity.json is invalid; using defaults", loaded.error().detail);
    }
    if (!featureAvailable) cfg.enabled = false;
    if (demo && demoShow.rfind("affinity-", 0) == 0) {  // 開発用: スクリーンショット
        lf::AffinityProfile p;
        p.id = "p1";
        p.name = "ExampleGame.exe";
        p.exeNames = {"examplegame.exe"};
        cfg.profiles = {p};
        cfg.enabled = featureAvailable;
    }
    affinity->setConfig(cfg);
    affinity->recoverFromJournal();  // 前回の異常終了でアフィニティが変更されたままのプロセスを元に戻す
    if (demo && demoShow == "affinity-active") {
        demoToggleGame();
        affinity->tick();
    }
}

void Backend::saveAffinityConfig() {
    if (!affinity) return;
    auto r = lf::saveAffinityConfig(m_dataDir / "affinity.json", affinity->config());
    if (!r.ok()) log.error("affinity", "could not save affinity.json", r.error().detail);
    else affinityConfigError.reset();
}

bool Backend::demoGameRunning() const { return m_fake && m_demoGamePid != 0 && m_fake->find(m_demoGamePid) != nullptr; }

void Backend::demoToggleGame() {
    if (!m_fake) return;
    if (demoGameRunning()) {
        m_fake->kill(m_demoGamePid);
        m_demoGamePid = 0;
    } else {
        lf::FakeProcessApi::Options o;
        o.mask = m_demoFullMask;
        m_demoGamePid = m_fake->add("ExampleGame.exe", o);
    }
}

std::vector<const lf::TweakDef*> Backend::tweaksIn(const std::string& category) const {
    std::vector<const lf::TweakDef*> out;
    if (!catalog) return out;
    for (const auto& t : catalog->all())
        if (t.category == category) out.push_back(&t);
    return out;
}

}  // namespace lfapp
