#pragma once
// GraphicsUI 통계 패널의 값 모델 (UI-R3-09) — 진단 창 ("통계" · "네트워크" — DebugPanels) 과 같은 원천 · 같은 글.
// 패널 (editor/gui StatsPanel) 은 apps/client 의 타입 (FrameTimings · FrameRendererInfo · WorldInfo) 을 모른다 — 여기서 줄로 바꾼다.

#include <optional>

#include "apps/client/ui/DebugPanels.hpp"
#include "editor/gui/RunPanels.hpp"
#include "editor/gui/StudioPanels.hpp"

namespace sbx::client {

[[nodiscard]] editor::gui::StatsModel makeGuiStatsModel(const PanelState& state, const PanelInputs& in);
// 실행 패널 (ADR-0183) — 진단 창 "시뮬레이션" 과 같은 입력 · 같은 글
[[nodiscard]] editor::gui::SimulationModel makeGuiSimulationModel(const PanelInputs& in);
// 접속자 패널 (ADR-0183) — "접속자" 창과 같은 원천 (PlayersInfo · roleLabel · canAssignRole — 서버와 같은 규칙)
[[nodiscard]] editor::gui::PlayersModel makeGuiPlayersModel(const std::optional<PlayersInfo>& info);
// 스크립트 프로파일 패널 (ADR-0184) — "스크립트 프로파일" 창과 같은 표 · 같은 글 (서버 · 함수 · 엔진 · 클라이언트)
[[nodiscard]] editor::gui::ScriptProfileModel makeGuiScriptProfileModel(const std::optional<ScriptProfileInfo>& info);

} // namespace sbx::client
