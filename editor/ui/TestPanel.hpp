#pragma once
// "테스트" 창 (v0.8 ST-06-9 — docs/25-STUDIO-ARCHITECTURE.md 3장 TestService, ADR-0115). 클라이언트 수 · 지연 · 흔들림
// · 손실 · [서버 + 클라이언트 시작] [모두 끝내기] · 프로세스 목록 (상태 · 로그). 화면은 고른 일만 — 띄우기는
// Application 이 명령 (studio.test-start · studio.test-stop) 과 TestService 로 (25 S2).

#include <optional>
#include <vector>

#include "editor/services/TestService.hpp"

namespace sbx::editor {

struct TestPanelState {
    int clients = 2;
    float latencyMs = 0.f;
    float jitterMs = 0.f;
    float lossPercent = 0.f;
};

enum class TestPanelAction : u8 { Start, Stop };

// 창의 값을 계획에 (클라이언트 수 · 흉내)
void applyTestPanel(const TestPanelState& state, services::TestPlan& plan);

std::optional<TestPanelAction> drawTestPanel(TestPanelState& state, const std::vector<services::TestProcess>& processes,
                                             bool running);

} // namespace sbx::editor
