#pragma once
// "행동" 패널 (v0.2 M3-T1, docs/10-EDITOR.md 9장, ADR-0047): 콘텐츠 PlayerAction 목록 — 하나를 고르면 "쥐고" 있고,
// 그동안 월드를 왼쪽 클릭하면 그 자리에 행동을 쓴다 (에디터 툴 · 선택 대신 — Application). 다시 누르거나 Esc 면 놓는다.
// 역할 player 아래면 고를 수 없다 (판단은 서버). 가장 최근 결과 · 거절 사유 한 줄. ImGui:: 호출만 한다.

#include <map>
#include <string>

#include "apps/client/WorldSession.hpp"

namespace sbx::client {

struct ActionPanelState {
    std::string armed;                       // 쥐고 있는 PlayerAction id (비면 없음)
    std::map<std::string, std::string> keys; // PlayerAction id → 키 표시 ("Q") — 팩의 게임 키 (v0.2 M3-T3)
};

// 목록에 없는 행동을 쥐고 있으면 놓는다 (월드 · 콘텐츠가 바뀜)
void syncArmedAction(const ActionsInfo& info, ActionPanelState& state);

void drawActionPanel(const ActionsInfo& info, ActionPanelState& state);

} // namespace sbx::client
