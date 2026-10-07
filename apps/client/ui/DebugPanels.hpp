#pragma once
// 기본 패널 (Phase 8C, 16-ROADMAP 8.4): "시뮬레이션"(진행 · 속도 · 선택 · 보기) · "통계"(프레임 · CPU · GPU 패스 ·
// 디바이스). docs/06-RENDERING.md 10장, ADR-0023. ImGui:: 호출만 한다 — 바꿀 일은 PanelActions 로 돌려주고 Application
// 이 한다 (패널이 세션 · 카메라를 직접 바꾸지 않는다 — 키보드 단축키와 같은 길로). [계획] Phase 12 에디터가
// SandboxEditor 로 옮기고 도킹 레이아웃 · 인스펙터를 더한다.

#include <array>
#include <string>

#include "apps/client/FrameRenderer.hpp"
#include "apps/client/WorldSession.hpp"

namespace sbx::client {

struct FrameTimings;

struct PanelState {
    bool visible = true; // F1
    bool demo = false;   // ImGui 데모 창 (통계 패널의 체크)
    static constexpr usize kHistory = 120;
    std::array<float, kHistory> frameMs{}; // 고리 버퍼
    usize head = 0;
    usize count = 0;
    void pushFrame(f64 ms) noexcept;
};

struct PanelInputs {
    const WorldInfo* world = nullptr; // 월드가 없으면 null (메뉴)
    std::string selection;            // 세션의 selectionStatus()
    const FrameTimings* timings = nullptr;
    const FrameRendererInfo* renderer = nullptr; // 렌더러가 없으면 null (헤드리스)
    bool grid = false;
    bool details = true;
    f32 zoom = 0; // px/칸
    std::string font;
};

struct PanelActions {
    bool togglePause = false;
    bool step = false;
    int speed = 0; // +1 · -1
    bool clearSelection = false;
    bool fitCamera = false;
    bool grid = false;   // 새 값
    bool details = true; // 새 값
};

// 패널을 그린다 (ImGui::NewFrame 과 Render 사이). visible 이 false 면 아무것도 안 그리고 입력을 그대로 돌려준다
[[nodiscard]] PanelActions drawDebugPanels(PanelState& state, const PanelInputs& in);

} // namespace sbx::client
