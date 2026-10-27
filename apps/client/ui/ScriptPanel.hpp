#pragma once
// 클라이언트 스크립트의 표시 (v0.3 M5-T3, docs/19-SANDBOXSCRIPT.md 21장, ADR-0067): ctx.label 의 글자를 개체의 그린
// 자리 위에 (ImGui 배경 그리기 목록 — 패널 아래), ctx.print 의 줄을 "스크립트" 칸에. 게임 화면이라 패널을 숨겨도
// 그린다 (칸은 줄이 있을 때만). ImGui:: 호출만 한다.
// "스크립트 프로파일" 패널 (v0.3 M5-T4, 19 22장, ADR-0068): 서버 · 클라이언트 스크립트 시스템의 표 — 호출/초 ·
// 연료/호출 · ms/초 · ms/호출 · 최대 ms · 최대 연료 · 트랩 · 예산 · 건너뜀 · 꺼짐. 패널이 보일 때만.

#include <string>

#include "apps/client/WorldSession.hpp"
#include "render/renderer/Camera2D.hpp"

namespace sbx::client {

void drawScriptOverlay(const ScriptOverlayInfo& info, const render::Camera2D& camera);

// 창의 누적 차이 → 한 줄 (seconds ≤ 0 이면 초당 값은 0)
[[nodiscard]] ScriptProfileRow scriptProfileRow(std::string id, u64 calls, u64 fuel, u64 nanos, f64 seconds,
                                                u64 maxNanos, u64 maxFuel, u64 traps, u64 budget, u64 cut,
                                                bool disabled);

void drawScriptProfilePanel(const ScriptProfileInfo& info);

} // namespace sbx::client
