#pragma once
// Application ↔ "보고 있는 월드" 경계. Application 은 월드가 어디서 오는지 모른다.
//   Phase 10B: NetworkSession — 서버(같은 프로세스의 LocalServerHost 또는 원격 SandboxServer)의 스냅숏 → ClientWorld.
//   (Phase 8A 의 --direct-sim 은 지웠다 — ADR-0026)
// 단위 테스트는 가짜 세션을 끼운다. 8B: 선택 · 디버그 오버레이 (ADR-0022).

#include <optional>
#include <string>

#include "render/renderer/RenderWorld.hpp"

namespace sbx::client {

// 패널(네트워크)이 보는 연결 상태 (10B)
struct NetInfo {
    bool local = true;
    std::string server; // "로컬" 또는 "host:port"
    std::string role;
    f64 rttMs = 0;
    u64 snapshots = 0;
    u64 resyncs = 0; // 새 epoch 로 다시 맞춤
    u32 epoch = 0;
    f64 receivedKBps = 0; // 최근 1 초
    f64 applyMs = 0;      // 받기 · 복제 월드 적용 (프레임마다, 지수 평균)
    f64 delayMs = 0;      // 보간 지연
    f64 behindTicks = 0;  // 마지막 서버 틱 − 그린 틱
    u64 commandsRejected = 0;
    std::string lastRejection; // 가장 최근 거절된 명령 (명령 · 사유)
};

// 패널(시뮬레이션)이 보는 진행 상태 (8C)
struct WorldInfo {
    std::string name;
    u64 tick = 0;
    u32 entities = 0;
    f32 speed = 1;
    bool paused = false;
    f64 ticksPerSecond = 0;       // 실제 (최근 1 초)
    f64 targetTicksPerSecond = 0; // 30 × 속도
    f64 tickMs = 0;               // 최근 틱 시간
    usize selected = 0;
    std::optional<NetInfo> net; // 10B
};

class IWorldSession {
public:
    virtual ~IWorldSession() = default;
    // 실제 경과 시간만큼 진행한다 (접속 · 받은 스냅숏 적용 · 로컬 서버면 그 진행). Connecting 에서도 불린다
    virtual void update(f64 dtSeconds) = 0;
    // 그릴 수 있다 (접속하고 첫 스냅숏을 받았다). false 면 Application 은 Connecting 에 머문다 (10B)
    [[nodiscard]] virtual bool ready() const { return true; }
    // 접속 실패 · 끊김 · 거절 — 있으면 Application 이 끝낸다 (메뉴 UI 가 생기면 MainMenu 로 [계획])
    [[nodiscard]] virtual std::optional<std::string> failure() const { return std::nullopt; }
    // out.sprites 에 그릴 것을 더한다 (카메라는 Application 이 채운다)
    virtual void extract(render::RenderWorld& out) = 0;
    // 처음 카메라를 맞출 월드 사각형
    [[nodiscard]] virtual render::WorldRect bounds() const = 0;

    virtual void togglePause() = 0;
    virtual void stepOnce() = 0;                          // 일시정지 중 한 틱
    virtual void changeSpeed(int dir) = 0;                // +1 빠르게 · -1 느리게
    [[nodiscard]] virtual std::string status() const = 0; // 제목 줄 한 토막

    // ---- 8B: 선택 (기본 = 지원하지 않음). extract 가 선택 외곽선 · 디버그 선을 RenderWorld 에 더한다 ----
    // 점 아래 맨 위 개체 하나. additive 면 지금 선택에 더하고(이미 있으면 뺀다), 아니면 바꾼다. 빈 곳이면 비운다
    virtual void selectAt(Vec2 /*world*/, bool /*additive*/) {}
    // 가운데가 사각형 안에 든 개체 전부
    virtual void selectBox(render::WorldRect /*area*/, bool /*additive*/) {}
    virtual void clearSelection() {}
    // 선택한 개체의 감지 반경 · 경로 · 대상 · 속도 선
    virtual void setDetailOverlay(bool /*on*/) {}
    [[nodiscard]] virtual std::string selectionStatus() const { return {}; } // 비면 선택 없음
    [[nodiscard]] virtual WorldInfo info() const { return {}; }
};

} // namespace sbx::client
