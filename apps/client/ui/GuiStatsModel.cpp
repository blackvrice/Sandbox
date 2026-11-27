#include "apps/client/ui/GuiStatsModel.hpp"

#include <format>

#include "apps/client/Application.hpp"

namespace sbx::client {

using editor::gui::StatLine;
using editor::gui::StatTone;

editor::gui::StatsModel makeGuiStatsModel(const PanelState& state, const PanelInputs& in) {
    editor::gui::StatsModel m;
    const auto row = [&m](std::string label, std::string value, StatTone tone = StatTone::Text) {
        m.lines.push_back(StatLine{std::move(label), std::move(value), tone});
    };
    const auto text = [&m](std::string value, StatTone tone = StatTone::Text) {
        m.lines.push_back(StatLine{{}, std::move(value), tone});
    };
    const auto section = [&text](std::string name) { text(std::move(name), StatTone::Section); };

    // 프레임 시간 (오래된 것 먼저 — ImGui 의 PlotLines 와 같은 차례)
    for (usize i = 0; i < state.count; ++i) {
        m.frameMs.push_back(state.frameMs[(state.head + PanelState::kHistory - state.count + i) % PanelState::kHistory]);
    }
    if (in.timings != nullptr && in.timings->frames > 0) {
        m.headline = std::format("{:.0f} fps · 프레임 {:.2f} ms", 1000.0 / in.timings->frameMs, in.timings->frameMs);
        section("CPU");
        row("월드", std::format("{:.2f} ms", in.timings->worldMs));
        row("추출", std::format("{:.2f} ms", in.timings->extractMs));
        row("렌더 (CPU)", std::format("{:.2f} ms", in.timings->renderMs));
    }
    if (in.renderer != nullptr) {
        const FrameRendererInfo& r = *in.renderer;
        section("GPU");
        text(std::format("{}{} · {} · VSync {}", r.backend, r.software ? " (소프트웨어)" : "", r.adapter,
                         r.vsync ? "켬" : "끔"));
        const render::GpuPassTimes& g = r.world.gpu;
        if (r.drewWorld && g.valid) {
            row("전체", std::format("{:.3f} ms", g.totalMs));
            row("업로드", std::format("{:.3f} ms", g.uploadMs));
            row("지형", std::format("{:.3f} ms", g.terrainMs));
            row("스프라이트", std::format("{:.3f} ms", g.spriteMs));
            row("격자", std::format("{:.3f} ms", g.gridMs));
            row("선택 · 디버그", std::format("{:.3f} ms", g.selectionMs + g.debugMs));
            row("UI", std::format("{:.3f} ms", g.uiMs));
        }
        if (r.drewWorld) {
            section("그리기");
            text(std::format("스프라이트 {} (컬링 {}) · Draw {}", r.world.sprites.drawn, r.world.sprites.culled,
                             r.world.draws));
            if (r.world.terrain.chunksPending > 0) {
                text(std::format("지형 대기 {} 청크", r.world.terrain.chunksPending));
            }
        }
        text(std::format("UI: Draw {} · 정점 {} · 텍스처 {}", r.ui.draws, r.ui.vertices, r.ui.textures));
        section("디바이스");
        text(std::format("버퍼 {} · 텍스처 {} · 디스크립터 {}", r.device.liveBuffers, r.device.liveTextures,
                         r.device.descriptorsUsed));
        text(std::format("업로드 링 {:.1f} KB · 지연 해제 {}", static_cast<f64>(r.device.uploadRingUsed) / 1024.0,
                         r.device.pendingDestructions));
        if (r.device.debugWarnings + r.device.debugErrors > 0) {
            text(std::format("{} 검증 경고 {} · 오류 {}", r.backend, r.device.debugWarnings, r.device.debugErrors),
                 StatTone::Error);
        }
    }
    if (in.world != nullptr && in.world->net) {
        const NetInfo& n = *in.world->net;
        section("네트워크");
        text(n.local ? std::string("로컬 서버 (같은 프로세스)") : std::format("서버 {}", n.server));
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
        if (!n.lastRejection.empty()) {
            text(n.lastRejection, StatTone::Warning);
        }
    }
    if (!in.font.empty()) {
        row("폰트", in.font, StatTone::Muted);
    }
    return m;
}

} // namespace sbx::client
