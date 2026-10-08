#include "apps/client/ui/DebugPanels.hpp"

#include <algorithm>
#include <format>

#include <imgui.h>

#include "apps/client/Application.hpp"

namespace sbx::client {

void PanelState::pushFrame(f64 ms) noexcept {
    frameMs[head] = static_cast<float>(ms);
    head = (head + 1) % kHistory;
    count = std::min(count + 1, kHistory);
}

namespace {

// 내용에 맞춰 크기가 정해진다 (정보 패널 — 사용자가 크기를 고를 일이 없다). 위치는 끌어서 옮길 수 있다
constexpr ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;

// ImGui 는 printf 서식 — std::format 결과를 그대로 (서식 문자 % 가 섞여도 안전)
void text(const std::string& s) {
    ImGui::TextUnformatted(s.c_str());
}

void row(const char* label, const std::string& value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(value.c_str());
}

void simulationPanel(const PanelInputs& in, PanelActions& act) {
    ImGui::SetNextWindowPos({12, 12}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("시뮬레이션", nullptr, kPanelFlags)) {
        ImGui::End();
        return;
    }
    if (in.world == nullptr) {
        ImGui::TextDisabled("월드 없음 (--world <시나리오> · --connect <서버>)");
        ImGui::End();
        return;
    }
    const WorldInfo& w = *in.world;
    text(std::format("{} · tick {}", w.name, w.tick));
    text(std::format("개체 {}", w.entities));
    if (w.paused) {
        ImGui::TextColored({1.f, 0.8f, 0.3f, 1.f}, "일시정지");
    } else if (w.ticksPerSecond > 0) {
        const bool behind = w.ticksPerSecond < w.targetTicksPerSecond * 0.95;
        const std::string tps = std::format("{:.1f} / {:.0f} TPS", w.ticksPerSecond, w.targetTicksPerSecond);
        if (behind) {
            ImGui::TextColored({1.f, 0.45f, 0.4f, 1.f}, "%s", tps.c_str()); // 실시간을 못 따라간다
        } else {
            text(tps);
        }
    }
    text(std::format("틱 {:.2f} ms", w.tickMs));
    ImGui::Separator();
    if (ImGui::Button(w.paused ? "재개 (Space)" : "일시정지 (Space)")) {
        act.togglePause = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!w.paused);
    if (ImGui::Button("한 틱 (.)")) {
        act.step = true;
    }
    ImGui::EndDisabled();
    if (ImGui::Button("-")) {
        act.speed = -1;
    }
    ImGui::SameLine();
    text(std::format("속도 ×{}", w.speed));
    ImGui::SameLine();
    if (ImGui::Button("+")) {
        act.speed = +1;
    }
    ImGui::Separator();
    ImGui::Checkbox("격자 (G)", &act.grid);
    ImGui::Checkbox("선택한 개체 자세히 (V)", &act.details);
    if (ImGui::Button("월드 맞춤 (Home)")) {
        act.fitCamera = true;
    }
    ImGui::SameLine();
    text(std::format("줌 {:.1f} px/칸", in.zoom));
    ImGui::Separator();
    if (in.selection.empty()) {
        ImGui::TextDisabled("선택 없음 — 클릭 · 끌기 (Shift = 더하기)");
    } else {
        ImGui::TextWrapped("%s", in.selection.c_str());
        if (ImGui::Button("선택 해제 (Esc)")) {
            act.clearSelection = true;
        }
    }
    ImGui::End();
}

void statsPanel(PanelState& state, const PanelInputs& in) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({vp->WorkPos.x + vp->WorkSize.x - 12, vp->WorkPos.y + 12}, ImGuiCond_FirstUseEver, {1, 0});
    if (!ImGui::Begin("통계", nullptr, kPanelFlags)) {
        ImGui::End();
        return;
    }
    // 프레임 시간 그래프 (최근 2 초 안팎)
    std::array<float, PanelState::kHistory> ordered{};
    float maxMs = 1.f;
    for (usize i = 0; i < state.count; ++i) {
        ordered[i] = state.frameMs[(state.head + PanelState::kHistory - state.count + i) % PanelState::kHistory];
        maxMs = std::max(maxMs, ordered[i]);
    }
    if (in.timings != nullptr && in.timings->frames > 0) {
        text(std::format("{:.0f} fps · 프레임 {:.2f} ms", 1000.0 / in.timings->frameMs, in.timings->frameMs));
    }
    ImGui::PlotLines("##frames", ordered.data(), static_cast<int>(state.count), 0, nullptr, 0.f, maxMs * 1.1f,
                     {300.f * ImGui::GetStyle().FontScaleDpi, 48.f * ImGui::GetStyle().FontScaleDpi});
    if (ImGui::BeginTable("cpu", 2, ImGuiTableFlags_SizingFixedFit)) {
        if (in.timings != nullptr && in.timings->frames > 0) {
            row("월드", std::format("{:.2f} ms", in.timings->worldMs));
            row("추출", std::format("{:.2f} ms", in.timings->extractMs));
            row("렌더 (CPU)", std::format("{:.2f} ms", in.timings->renderMs));
        }
        ImGui::EndTable();
    }
    if (in.renderer != nullptr) {
        const FrameRendererInfo& r = *in.renderer;
        ImGui::SeparatorText("GPU");
        text(std::format("{}{} · {} · VSync {}", r.backend, r.software ? " (소프트웨어)" : "", r.adapter,
                         r.vsync ? "켬" : "끔"));
        const render::GpuPassTimes& g = r.world.gpu;
        if (r.drewWorld && g.valid && ImGui::BeginTable("gpu", 2, ImGuiTableFlags_SizingFixedFit)) {
            row("전체", std::format("{:.3f} ms", g.totalMs));
            row("업로드", std::format("{:.3f} ms", g.uploadMs));
            row("지형", std::format("{:.3f} ms", g.terrainMs));
            row("스프라이트", std::format("{:.3f} ms", g.spriteMs));
            row("격자", std::format("{:.3f} ms", g.gridMs));
            row("선택 · 디버그", std::format("{:.3f} ms", g.selectionMs + g.debugMs));
            row("UI", std::format("{:.3f} ms", g.uiMs));
            ImGui::EndTable();
        }
        if (r.drewWorld) {
            ImGui::SeparatorText("그리기");
            text(std::format("스프라이트 {} (컬링 {}) · Draw {}", r.world.sprites.drawn, r.world.sprites.culled,
                             r.world.draws));
            if (r.world.terrain.chunksPending > 0) {
                text(std::format("지형 대기 {} 청크", r.world.terrain.chunksPending));
            }
        }
        text(std::format("UI: Draw {} · 정점 {} · 텍스처 {}", r.ui.draws, r.ui.vertices, r.ui.textures));
        ImGui::SeparatorText("디바이스");
        text(std::format("버퍼 {} · 텍스처 {} · 디스크립터 {}", r.device.liveBuffers, r.device.liveTextures,
                         r.device.descriptorsUsed));
        text(std::format("업로드 링 {:.1f} KB · 지연 해제 {}", static_cast<f64>(r.device.uploadRingUsed) / 1024.0,
                         r.device.pendingDestructions));
        if (r.device.debugWarnings + r.device.debugErrors > 0) {
            ImGui::TextColored(
                {1.f, 0.45f, 0.4f, 1.f}, "%s",
                std::format("D3D12 경고 {} · 오류 {}", r.device.debugWarnings, r.device.debugErrors).c_str());
        }
    }
    ImGui::Separator();
    text(std::format("폰트 {}", in.font));
    ImGui::Checkbox("ImGui 데모", &state.demo);
    ImGui::TextDisabled("F1 패널 숨기기");
    ImGui::End();
}

void networkPanel(const PanelInputs& in) {
    if (in.world == nullptr || !in.world->net) {
        return;
    }
    const NetInfo& n = *in.world->net;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({vp->WorkPos.x + 12, vp->WorkPos.y + vp->WorkSize.y - 12}, ImGuiCond_FirstUseEver, {0, 1});
    if (!ImGui::Begin("네트워크", nullptr, kPanelFlags)) {
        ImGui::End();
        return;
    }
    text(n.local ? std::string("로컬 서버 (같은 프로세스)") : std::format("서버 {}", n.server));
    if (ImGui::BeginTable("net", 2, ImGuiTableFlags_SizingFixedFit)) {
        row("역할", n.role);
        if (!n.local) {
            row("RTT", std::format("{:.0f} ms", n.rttMs));
        }
        row("스냅숏", std::format("{} (epoch {} · 다시 맞춤 {})", n.snapshots, n.epoch, n.resyncs));
        row("받음", std::format("{:.1f} KB/s · 적용 {:.2f} ms", n.receivedKBps, n.applyMs));
        row("보간 지연", std::format("{:.0f} ms · 서버보다 {:.1f} 틱 뒤", n.delayMs, n.behindTicks));
        row("관심 영역", n.interest);
        row("다시 접속", n.reconnecting ? std::string("중…") : std::format("{} 번", n.reconnects));
        row("거절된 명령", std::format("{}", n.commandsRejected));
        ImGui::EndTable();
    }
    if (!n.lastRejection.empty()) {
        ImGui::TextColored({1.f, 0.6f, 0.4f, 1.f}, "%s", n.lastRejection.c_str());
    }
    ImGui::End();
}

} // namespace

PanelActions drawDebugPanels(PanelState& state, const PanelInputs& in) {
    PanelActions act;
    act.grid = in.grid;
    act.details = in.details;
    if (!state.visible) {
        return act;
    }
    simulationPanel(in, act);
    networkPanel(in);
    statsPanel(state, in);
    if (state.demo) {
        ImGui::ShowDemoWindow(&state.demo);
    }
    return act;
}

} // namespace sbx::client
