#pragma once
// "월드" 패널 (Phase 12D, docs/10-EDITOR.md 9장, ADR-0031): 서버 저장 폴더의 세이브 · 리플레이 목록, 이름을 적어
// 저장 · 기록 시작/끝, 고른 세이브 불러오기 · 리플레이 재생, 기록 · 재생 상태 한 줄 (call · 비교한 해시 · 일치 여부).
// admin 아래 역할이면 단추를 끈다 (판단은 서버). 처음엔 접혀 있고 제목 줄에 상태 한 줄 ("월드 — 보통" · "월드 — 재생 …").
// ImGui:: 호출만 한다 — 요청은 돌려주고 Application 이 세션에 넘긴다.

#include <optional>
#include <string>

#include "apps/client/WorldSession.hpp"

namespace sbx::client {

struct WorldPanelState {
    std::string name = "quicksave"; // 저장 · 기록 이름 (Ctrl+S 도 이 이름으로)
    std::string selected;           // 목록에서 고른 것
};

struct WorldPanelRequest {
    WorldOp op = WorldOp::List;
    std::string name;
};

// 상태 한 줄 ("보통" · "기록 중 'x' — 120 call" · "재생 'x' 300/608 call · 해시 10 일치 · 멈춤")
[[nodiscard]] std::string worldModeLine(const WorldOpsInfo& info);

[[nodiscard]] std::optional<WorldPanelRequest> drawWorldPanel(const WorldOpsInfo& info, WorldPanelState& state);

} // namespace sbx::client
