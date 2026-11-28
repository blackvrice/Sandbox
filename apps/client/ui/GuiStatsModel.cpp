#include "apps/client/ui/GuiStatsModel.hpp"

#include <format>

#include "apps/client/Application.hpp"
#include "apps/client/ui/PlayersPanel.hpp"
#include "apps/client/ui/WorldPanel.hpp"
#include "network/protocol/Messages.hpp"

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

editor::gui::SimulationModel makeGuiSimulationModel(const PanelInputs& in) {
    using editor::gui::StatTone;
    editor::gui::SimulationModel m;
    if (in.world == nullptr) {
        return m;
    }
    const WorldInfo& w = *in.world;
    m.available = true;
    m.headline = std::format("{} · tick {}", w.name, w.tick);
    if (!w.game.empty()) {
        m.lines.push_back({"게임", w.game, StatTone::Text});
    }
    m.lines.push_back({"개체", std::format("{}", w.entities), StatTone::Text});
    if (w.paused) {
        m.lines.push_back({"진행", "일시정지", StatTone::Warning});
    } else if (w.ticksPerSecond > 0) {
        const bool behind = w.ticksPerSecond < w.targetTicksPerSecond * 0.95; // 실시간을 못 따라간다 (진단 창과 같은 문턱)
        m.lines.push_back({"진행", std::format("{:.1f} / {:.0f} TPS", w.ticksPerSecond, w.targetTicksPerSecond),
                           behind ? StatTone::Error : StatTone::Text});
    }
    m.lines.push_back({"틱", std::format("{:.2f} ms", w.tickMs), StatTone::Text});
    m.paused = w.paused;
    m.canControl = w.canControl;
    m.speed = w.speed;
    m.grid = in.grid;
    m.details = in.details;
    m.zoom = in.zoom;
    m.selection = in.selection;
    return m;
}

editor::gui::PlayersModel makeGuiPlayersModel(const std::optional<PlayersInfo>& info) {
    editor::gui::PlayersModel m;
    for (usize r = 0; r < editor::gui::PlayersModel::kRoles; ++r) {
        m.roleNames[r] = std::string(roleLabel(static_cast<u8>(r)));
    }
    if (!info) {
        return m;
    }
    m.available = true;
    m.myRole = info->myRole;
    m.message = info->message;
    for (const PlayerRow& p : info->players) {
        editor::gui::PlayerEntry e;
        e.clientId = p.clientId;
        e.who = std::format("{} #{}{}", p.name, p.clientId, p.self ? " (나)" : "");
        e.role = p.role;
        e.self = p.self;
        for (u8 r = 0; r < editor::gui::PlayersModel::kRoles && !p.self; ++r) {
            if (r != p.role && canAssignRole(info->myRole, p.role, r)) {
                e.assignable = static_cast<u8>(e.assignable | (1u << r));
            }
        }
        m.players.push_back(std::move(e));
    }
    return m;
}

editor::gui::ScriptProfileModel makeGuiScriptProfileModel(const std::optional<ScriptProfileInfo>& info) {
    using L = editor::gui::ProfileLine;
    using editor::gui::StatTone;
    editor::gui::ScriptProfileModel m;
    if (!info) {
        return m;
    }
    m.available = true;
    const auto section = [&](std::string t) { m.lines.push_back({L::Kind::Section, {std::move(t)}, StatTone::Section}); };
    const auto note = [&](std::string t) { m.lines.push_back({L::Kind::Note, {std::move(t)}, StatTone::Muted}); };
    // 시스템 표 (진단 창과 같은 열 · 자릿수)
    const auto systems = [&](const std::vector<ScriptProfileRow>& rows) {
        m.lines.push_back({L::Kind::Columns,
                           {"시스템", "호출/초", "연료/호출", "ms/초", "ms/호출", "최대 ms", "최대 연료", "트랩", "예산 · 건너뜀", "상태"},
                           StatTone::Muted});
        for (const ScriptProfileRow& r : rows) {
            m.lines.push_back({L::Kind::Row,
                               {r.id, std::format("{:.1f}", r.callsPerSecond), std::format("{:.0f}", r.fuelPerCall),
                                std::format("{:.3f}", r.msPerSecond), std::format("{:.4f}", r.msPerCall),
                                std::format("{:.3f}", r.maxMs), std::format("{}", r.maxFuel), std::format("{}", r.traps),
                                std::format("{} · {}", r.budget, r.cut), r.disabled ? "꺼짐" : "켜짐"},
                               r.disabled || r.traps > 0 ? StatTone::Warning : StatTone::Text});
        }
    };
    const auto functions = [&](const std::vector<ScriptFunctionProfileRow>& rows) {
        m.lines.push_back({L::Kind::Columns, {"함수 (파일:줄:열)", "호출", "자기 연료", "몫", "연료/호출"}, StatTone::Muted});
        for (const ScriptFunctionProfileRow& r : rows) {
            m.lines.push_back({L::Kind::Row,
                               {r.site, std::format("{}", r.calls), std::format("{}", r.fuel), std::format("{:.1f} %", r.share * 100.0),
                                std::format("{:.1f}", r.calls > 0 ? static_cast<f64>(r.fuel) / static_cast<f64>(r.calls) : 0.0)},
                               StatTone::Text});
        }
    };
    if (!info->server.empty() || info->serverOmitted > 0) {
        section("서버 (최근 통계 창 — 서버 시계)");
        systems(info->server);
        if (info->serverOmitted > 0) {
            note(std::format("… 표 상한으로 빠진 시스템 {}", info->serverOmitted));
        }
        if (!info->serverFunctions.empty()) {
            section("서버 함수 — 자기 연료 상위 (누적)");
            functions(info->serverFunctions);
        }
    }
    if (!info->engine.empty()) {
        section("엔진 시스템 (단계 · 최근 통계 창 — 서버 시계)");
        m.lines.push_back({L::Kind::Columns, {"시스템", "단계", "ms/틱", "최대 ms", "몫"}, StatTone::Muted});
        for (const EngineProfileRow& r : info->engine) {
            m.lines.push_back({L::Kind::Row,
                               {r.name, r.stage, std::format("{:.3f}", r.msPerCall), std::format("{:.3f}", r.maxMs),
                                std::format("{:.1f} %", r.share * 100.0)},
                               StatTone::Text});
        }
    }
    if (!info->client.empty()) {
        section("클라이언트 (최근 1 초)");
        systems(info->client);
        if (!info->clientFunctions.empty()) {
            section("클라이언트 함수 — 자기 연료 상위 (누적)");
            functions(info->clientFunctions);
        }
    }
    return m;
}

// 패널의 op 번호가 client::WorldOp 과 같다
static_assert(editor::gui::WorldModel::kList == static_cast<int>(WorldOp::List) &&
              editor::gui::WorldModel::kSave == static_cast<int>(WorldOp::Save) &&
              editor::gui::WorldModel::kLoad == static_cast<int>(WorldOp::Load) &&
              editor::gui::WorldModel::kRecordStart == static_cast<int>(WorldOp::RecordStart) &&
              editor::gui::WorldModel::kRecordStop == static_cast<int>(WorldOp::RecordStop) &&
              editor::gui::WorldModel::kPlay == static_cast<int>(WorldOp::Play) &&
              editor::gui::WorldModel::kTestPlay == static_cast<int>(WorldOp::TestPlay) &&
              editor::gui::WorldModel::kTestStop == static_cast<int>(WorldOp::TestStop) &&
              editor::gui::WorldModel::kSeek == static_cast<int>(WorldOp::Seek));

editor::gui::WorldModel makeGuiWorldModel(const std::optional<WorldOpsInfo>& info, const std::string& name) {
    editor::gui::WorldModel m;
    m.name = name;
    m.nameOk = net::isValidSaveName(name);
    if (!info) {
        return m;
    }
    m.available = true;
    m.canChange = info->canChange;
    m.listed = info->listed;
    m.mode = info->mode;
    m.modeLine = worldModeLine(*info);
    m.modeName = info->modeName;
    m.replayCall = info->replayCall;
    m.replayCalls = info->replayCalls;
    m.generation = info->generation;
    m.message = info->message;
    for (const SavedWorldRow& w : info->saved) {
        m.saved.push_back({w.name, w.replay,
                           std::format("{}{} · tick {} · 개체 {}", w.replay ? "[리플레이] " : "", w.name, w.tick, w.entities)});
    }
    return m;
}

} // namespace sbx::client
