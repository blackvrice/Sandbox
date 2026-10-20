#pragma once
// "게임" 패널 (v0.2 M2-T3, docs/10-EDITOR.md 9장, ADR-0045): 게임 모드 이름 · 진행 상태 한 줄 (Lobby 대기 · 카운트다운
// 남은 초 · 경과 시간 · 결과), 점수 줄 (M3-T2), 시작 · 중단 · 재시작 단추 (admin 아래 역할이면 끈다 — 판단은 서버),
// 가장 최근 거절 · 결과. 세션이 있는 월드에서만 그린다. 제목 줄에 상태 한 줄. ImGui:: 호출만 한다 — 요청은 돌려주고
// Application 이 세션에 넘긴다.

#include <optional>
#include <string>
#include <vector>

#include "apps/client/WorldSession.hpp"
#include "core/components/game/GameScore.hpp"
#include "core/content/ContentModel.hpp"

namespace sbx::client {

// 상태 한 줄 ("Lobby — 시작을 기다립니다" · "Starting — 3 초" · "Running — 1:05" · "Finished — 승리 (빨강)")
[[nodiscard]] std::string gameStateLine(const GameInfo& info);

// game.score → 표시 줄 (v0.2 M3-T2). mode 가 없거나 팀이 없으면 공용 칸 하나 ("점수")
[[nodiscard]] std::vector<ScoreRow> scoreRows(const comp::GameScore& score, const content::GameMode* mode);
// "점수 12" · "빨강 3 · 파랑 5" (scores 가 비면 빈 문자열)
[[nodiscard]] std::string scoreLine(const GameInfo& info);

[[nodiscard]] std::optional<GameCommand> drawGamePanel(const GameInfo& info);

} // namespace sbx::client
