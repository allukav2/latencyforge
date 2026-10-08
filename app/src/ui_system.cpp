// システム情報カード (OS / CPU トポロジー / GPU)、機能の有効/無効の表示、動作保証外の警告ダイアログ。
#include <imgui_internal.h>
#include <windows.h>

#include <algorithm>
#include <cstdio>

#include "lf/win_probe.hpp"
#include "ui.hpp"

namespace lfapp {

namespace {

std::string megabytes(uint64_t bytes) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%llu MB", static_cast<unsigned long long>((bytes + 512 * 1024) / (1024 * 1024)));
    return buf;
}

}  // namespace

lf::Feature Ui::featureOf(Page page) {
    switch (page) {
        case Page::Home: return lf::Feature::Home;
        case Page::Affinity: return lf::Feature::Affinity;
        case Page::Usb: return lf::Feature::Usb;
        case Page::Gpu: return lf::Feature::GpuNvidia;
        case Page::Kernel: return lf::Feature::Kernel;
        case Page::Bench: return lf::Feature::Benchmark;
        case Page::Backup: return lf::Feature::Backup;
        case Page::Log: return lf::Feature::Log;
        default: return lf::Feature::Settings;
    }
}

std::string Ui::reasonText(const lf::FeatureStatus& st) const {
    if (st.reasonKey.empty()) return {};
    std::string s = t(st.reasonKey.c_str());
    for (size_t i = 0; i < st.reasonArgs.size(); ++i) {
        const std::string ph = "{" + std::to_string(i) + "}";
        for (size_t pos = s.find(ph); pos != std::string::npos; pos = s.find(ph, pos + st.reasonArgs[i].size()))
            s.replace(pos, ph.size(), st.reasonArgs[i]);
    }
    return s;
}

std::string Ui::archName(lf::Arch a) const {
    switch (a) {
        case lf::Arch::X64: return t("sys.arch.x64");
        case lf::Arch::Arm64: return t("sys.arch.arm64");
        case lf::Arch::X86: return t("sys.arch.x86");
        default: return t("sys.arch.unknown");
    }
}

void Ui::detectSystemInfo(const UiInit& in) {
    // デモ + --sim のときだけ、検出結果をサンプル構成に差し替える (実機の値は使わない)。
    std::unique_ptr<lf::ISystemProbe> sample;
    if (in.demo && !in.sim.empty()) sample = lf::makeSampleProbe(in.sim);
    lf::WinSystemProbe real;
    lf::ISystemProbe& probe = sample ? *sample : static_cast<lf::ISystemProbe&>(real);

    m_sys = lf::detectSystem(probe);
    m_compat = lf::evaluateCompat(m_sys);
    if (in.demo && in.show == "ack-compat") m_compatAck = true;  // 開発用: 警告を閉じた後の画面を撮る
    for (int i = 0; i < static_cast<int>(Page::Count); ++i) {
        const Page pg = static_cast<Page>(i);
        m_feat[i] = lf::featureStatus(featureOf(pg), m_sys);
        m_featReason[i] = m_feat[i].available ? std::string() : reasonText(m_feat[i]);
    }

    char buf[256];
    std::snprintf(buf, sizeof buf, "%s build %u (%s); CPU %s: %d cores / %d threads, hybrid=%d, L3 groups=%zu; %zu GPU(s)%s",
                  m_sys.os.name.c_str(), m_sys.os.build, m_sys.os.edition.c_str(), m_sys.cpu.brand.c_str(),
                  m_sys.cpu.physicalCores, m_sys.cpu.logicalProcessors, m_sys.cpu.hybrid ? 1 : 0, m_sys.cpu.l3Groups.size(),
                  m_sys.gpus.size(), sample ? " [SIMULATED]" : "");
    m_sysSummary = buf;
}

// ---------------------------------------------------------------- 機能の有効/無効の表示

void Ui::drawFeatureBanner(Page page) {
    const size_t i = static_cast<size_t>(page);
    const lf::FeatureStatus& st = m_feat[i];
    if (st.available && st.noteKey.empty()) return;

    const ImVec4 color = st.available ? m_pal.accentHover : m_pal.warn;
    if (beginCard(("##feat_" + std::to_string(i)).c_str())) {
        const ImVec2 p = ImGui::GetWindowPos();
        const ImVec2 s = ImGui::GetWindowSize();
        ImGui::GetWindowDrawList()->AddRect(p + ImVec2(0.5f, 0.5f), p + s - ImVec2(0.5f, 0.5f), u32(withAlpha(color, 0.5f)), S(12));
        if (!st.available) {
            pushBold();
            coloredText(m_pal.warn, t("feature.unavailable"));
            popFont();
            dimText(m_featReason[i].c_str());
        } else {
            coloredText(color, t(st.noteKey.c_str()));
        }
    }
    endCard();
}

// ---------------------------------------------------------------- GPU (NVIDIA) ページ

// 現時点では、NVIDIA GPU の検出結果と注意書きだけを表示する。設定の変更は、公式に文書化されていると確認できたものだけを
// 承認を得てから追加する方針 (候補は docs/gpu-candidates.md)。
void Ui::pageGpu() {
    pageHeader(t("nav.gpu"), t("gpu.subtitle"));
    drawFeatureBanner(Page::Gpu);  // NVIDIA GPU が無い場合は、ここに理由が出る

    // 「NVIDIA 実機では未検証」は、常に明記する。
    if (beginCard("##gpu_unverified")) {
        const ImVec2 p = ImGui::GetWindowPos();
        const ImVec2 s = ImGui::GetWindowSize();
        ImGui::GetWindowDrawList()->AddRect(p + ImVec2(0.5f, 0.5f), p + s - ImVec2(0.5f, 0.5f), u32(withAlpha(m_pal.warn, 0.55f)), S(12));
        pushBold();
        coloredText(m_pal.warn, t("gpu.unverifiedTitle"));
        popFont();
        dimText(t("gpu.unverifiedBody"));
    }
    endCard();

    if (!m_feat[static_cast<size_t>(Page::Gpu)].available) return;

    if (beginCard("##gpu_list")) {
        pushBold();
        ImGui::TextUnformatted(t("gpu.detected"));
        popFont();
        for (const lf::GpuInfo& g : m_sys.gpus) {
            if (g.vendor != lf::GpuVendor::Nvidia || g.software) continue;
            ImGui::Dummy(ImVec2(0, S(2)));
            ImGui::TextUnformatted(g.name.c_str());
            std::string line = megabytes(g.vramBytes);
            if (const std::string nv = lf::nvidiaDriverVersion(g.driverVersion); !nv.empty())
                line += "  /  " + std::string(t("gpu.driver")) + " " + nv + " (" + lf::formatDriverVersion(g.driverVersion) + ")";
            else if (g.driverVersion)
                line += "  /  " + std::string(t("gpu.driver")) + " " + lf::formatDriverVersion(g.driverVersion);
            dimText(line.c_str());
        }
    }
    endCard();

    if (beginCard("##gpu_tweaks")) {
        pushBold();
        ImGui::TextUnformatted(t("gpu.noTweaksTitle"));
        popFont();
        dimText(t("gpu.noTweaksBody"));
        coloredText(m_pal.textDim, t("gpu.effectNote"));
    }
    endCard();
}

// ---------------------------------------------------------------- システム情報カード

void Ui::drawSystemCard() {
    if (!beginCard("##sys")) {
        endCard();
        return;
    }
    const lf::CpuTopology& cpu = m_sys.cpu;

    ImGui::AlignTextToFramePadding();
    pushBold();
    ImGui::TextUnformatted(t("sys.title"));
    popFont();
    {
        const char* lbl = m_compat.supported() ? t("sys.supported") : t("sys.unsupported");
        const float bw = ImGui::CalcTextSize(lbl).x + S(18);
        ImGui::SameLine(ImGui::GetContentRegionMax().x - bw);
        badge(lbl, m_compat.supported() ? m_pal.ok : m_pal.warn);
    }
    ImGui::Dummy(ImVec2(0, S(2)));

    // OS
    std::string os = m_sys.os.name;
    if (!m_sys.os.displayVersion.empty()) os += " " + m_sys.os.displayVersion;
    os += " (build " + std::to_string(m_sys.os.build) + ")";
    if (!m_sys.os.edition.empty()) os += " / " + m_sys.os.edition;
    os += " / " + archName(m_sys.os.arch);
    keyValue(t("sys.os"), os.c_str());

    // CPU
    keyValue(t("sys.cpu"), cpu.brand.empty() ? "-" : cpu.brand.c_str());
    if (cpu.known) {
        keyValue(t("sys.cores"), (fmt("sys.coresFmt", {std::to_string(cpu.physicalCores), std::to_string(cpu.logicalProcessors)}) +
                                  " (" + t(cpu.smt ? "sys.smtOn" : "sys.smtOff") + ")")
                                     .c_str());
        if (cpu.hybrid)
            keyValue(t("sys.hybrid"), fmt("sys.hybridFmt", {std::to_string(cpu.performanceCores), std::to_string(cpu.efficiencyCores)}).c_str());
        if (cpu.l3Known) {
            std::string sizes;
            const uint64_t first = cpu.l3Groups.front().sizeBytes;
            const bool allEqual = !cpu.asymmetricL3;
            if (allEqual) {
                sizes = megabytes(first);
                if (cpu.l3Groups.size() > 1) sizes += " × " + std::to_string(cpu.l3Groups.size());
            } else {
                for (size_t i = 0; i < cpu.l3Groups.size() && i < 4; ++i) sizes += (i ? " / " : "") + megabytes(cpu.l3Groups[i].sizeBytes);
                if (cpu.l3Groups.size() > 4) sizes += " …";
            }
            keyValue(t("sys.l3"), fmt("sys.l3Fmt", {std::to_string(cpu.l3Groups.size()), sizes}).c_str());
        } else {
            keyValue(t("sys.l3"), t("sys.l3Unknown"));
        }
        if (cpu.processorGroups > 1) keyValue(t("sys.groups"), std::to_string(cpu.processorGroups).c_str());
        if (cpu.packages > 1) keyValue(t("sys.sockets"), std::to_string(cpu.packages).c_str());
    } else {
        keyValue(t("sys.cores"), t("sys.topologyUnknown"));
    }

    // GPU (DXGI のベンダー ID で判定)
    if (m_sys.gpus.empty()) {
        keyValue(t("sys.gpu"), t("sys.gpuNone"));
    } else {
        for (size_t i = 0; i < m_sys.gpus.size(); ++i) {
            const lf::GpuInfo& g = m_sys.gpus[i];
            std::string v = g.name;
            if (g.software) v += std::string(" (") + t("sys.software") + ")";
            else if (g.vramBytes) v += "  " + megabytes(g.vramBytes);
            if (!g.software && g.driverVersion) {  // NVIDIA は "560.94" の表記、それ以外は a.b.c.d
                const std::string nv = lf::nvidiaDriverVersion(g.driverVersion);
                v += "  /  " + std::string(t("gpu.driver")) + " " + (g.vendor == lf::GpuVendor::Nvidia && !nv.empty() ? nv : lf::formatDriverVersion(g.driverVersion));
            }
            keyValue(i == 0 ? t("sys.gpu") : "", v.c_str());
        }
    }
    keyValue(t("sys.renderer"), m_warp ? t("sys.rendererWarp") : t("sys.rendererGpu"));

    // コア構成のマップ
    if (cpu.known && !cpu.cores.empty()) {
        ImGui::Dummy(ImVec2(0, S(6)));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, S(2)));
        pushBold();
        ImGui::TextUnformatted(t("sys.mapTitle"));
        popFont();
        drawTopologyMap();
        if (cpu.hybrid) dimText(t("sys.hybridNote"));
        if (cpu.multiL3) dimText(t("sys.multiL3"));
        if (cpu.asymmetricL3) dimText(t("sys.asymmetricL3"));
    }
    endCard();
}

// コアを L3 グループごとにまとめて並べる。P=アクセント色 / E=緑 / 通常=薄いアクセント。SMT のコアには中央に点を描く。
void Ui::drawTopologyMap(const lf::AffinityPlan* plan) {
    const lf::CpuTopology& cpu = m_sys.cpu;
    const bool overlay = plan && plan->effective();
    auto inList = [](const std::vector<int>& v, int i) { return std::find(v.begin(), v.end(), i) != v.end(); };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cell = S(16), gap = S(4);
    const float avail = ImGui::GetContentRegionAvail().x;
    const int cols = std::max(1, static_cast<int>((avail + gap) / (cell + gap)));

    auto coreColor = [&](lf::CoreKind k) {
        switch (k) {
            case lf::CoreKind::Performance: return m_pal.accent;
            case lf::CoreKind::Efficiency: return m_pal.ok;
            default: return mix(m_pal.accent, m_pal.textDim, 0.35f);
        }
    };
    auto drawGroup = [&](const std::string& label, const std::vector<int>& coreIdx) {
        ImGui::PushStyleColor(ImGuiCol_Text, m_pal.textDim);
        ImGui::TextUnformatted(label.c_str());
        ImGui::PopStyleColor();
        const int n = static_cast<int>(coreIdx.size());
        const int rows = std::max(1, (n + cols - 1) / cols);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        for (int i = 0; i < n; ++i) {
            const lf::CoreInfo& c = cpu.cores[static_cast<size_t>(coreIdx[static_cast<size_t>(i)])];
            const ImVec2 a(origin.x + (i % cols) * (cell + gap), origin.y + (i / cols) * (cell + gap));
            ImVec4 col = coreColor(c.kind);
            if (overlay) {  // 計画の色分け: ゲーム=アクセント / その他=オレンジ / どちらにも割り当てない=暗い
                col = inList(plan->gameCores, c.index) ? m_pal.accent
                      : inList(plan->backgroundCores, c.index) ? m_pal.warn
                                                               : mix(m_pal.border, m_pal.textDim, 0.3f);
            }
            dl->AddRectFilled(a, a + ImVec2(cell, cell), u32(withAlpha(col, 0.85f)), S(4));
            if (c.logical > 1) dl->AddCircleFilled(a + ImVec2(cell, cell) * 0.5f, S(2.2f), u32(ImVec4(1, 1, 1, 0.9f)), 12);
        }
        ImGui::Dummy(ImVec2(avail, rows * (cell + gap)));
    };

    ImGui::Dummy(ImVec2(0, S(2)));
    if (cpu.l3Known) {
        for (const lf::L3Group& g : cpu.l3Groups)
            drawGroup(fmt("sys.l3Label", {std::to_string(g.index + 1), megabytes(g.sizeBytes), std::to_string(g.physical)}), g.cores);
        std::vector<int> orphans;  // L3 に属さないコア (通常は無い)
        for (const lf::CoreInfo& c : cpu.cores)
            if (c.l3Group < 0) orphans.push_back(c.index);
        if (!orphans.empty()) drawGroup(t("sys.noL3Label"), orphans);
    } else {
        std::vector<int> all;
        for (const lf::CoreInfo& c : cpu.cores) all.push_back(c.index);
        drawGroup(t("sys.noL3Label"), all);
    }

    // 凡例
    const ImVec2 p = ImGui::GetCursorScreenPos();
    float x = p.x;
    auto legend = [&](const ImVec4& col, const char* text) {
        dl->AddRectFilled(ImVec2(x, p.y + S(2)), ImVec2(x + S(12), p.y + S(14)), u32(withAlpha(col, 0.85f)), S(3));
        const ImVec2 ts = ImGui::CalcTextSize(text);
        dl->AddText(ImVec2(x + S(18), p.y), u32(m_pal.textDim), text);
        x += S(18) + ts.x + S(18);
    };
    if (overlay) {
        legend(m_pal.accent, t("affinity.legendGame"));
        legend(m_pal.warn, t("affinity.legendBackground"));
    } else if (cpu.hybrid) {
        legend(m_pal.accent, t("sys.legendP"));
        legend(m_pal.ok, t("sys.legendE"));
    } else {
        legend(coreColor(lf::CoreKind::Uniform), t("sys.legendCore"));
    }
    if (cpu.smt) dl->AddText(ImVec2(x, p.y), u32(m_pal.textDim), t("sys.legendSmt"));
    ImGui::Dummy(ImVec2(0, S(22)));
}

// ---------------------------------------------------------------- 動作保証外の警告

void Ui::drawCompatModal() {
    if (!beginModalWindow("##compat", 680)) return;
    ImGui::PushFont(m_fontBold, 22.0f);
    coloredText(m_pal.warn, t("compat.title"));
    ImGui::PopFont();
    dimText(t("compat.body"));
    ImGui::Dummy(ImVec2(0, S(6)));
    for (lf::CompatIssue i : m_compat.issues) ImGui::BulletText("%s", t(lf::compatIssueKey(i)));

    ImGui::Dummy(ImVec2(0, S(12)));
    const float w1 = S(120), w2 = S(190), gap = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - (w1 + gap + w2));
    if (secondaryButton(t("compat.exit"), w1)) PostQuitMessage(0);
    ImGui::SameLine();
    if (primaryButton(t("compat.continue"), w2)) {
        m_compatAck = true;
        ImGui::CloseCurrentPopup();
    }
    endModalWindow();
}

}  // namespace lfapp
