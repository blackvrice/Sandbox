#pragma once
// 클라이언트 스크립트의 표시 (v0.3 M5-T3, docs/19-SANDBOXSCRIPT.md 21장, ADR-0067): ctx.label 의 글자를 개체의 그린
// 자리 위에 (ImGui 배경 그리기 목록 — 패널 아래), ctx.print 의 줄을 "스크립트" 칸에. 게임 화면이라 패널을 숨겨도
// 그린다 (칸은 줄이 있을 때만 — 패널이 보이면 시스템 통계도). ImGui:: 호출만 한다.

#include "apps/client/WorldSession.hpp"
#include "render/renderer/Camera2D.hpp"

namespace sbx::client {

void drawScriptOverlay(const ScriptOverlayInfo& info, const render::Camera2D& camera, bool panelsVisible);

} // namespace sbx::client
