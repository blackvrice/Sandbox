#pragma once
// client 스크립트의 그리기 명령 (onRender — client.drawLine · drawCircle · drawRect · drawText) → 화면 (v0.11 RT-06-9,
// ADR-0152). 선 · 원 · 사각형은 RenderWorld::script (월드 좌표 선 — DebugPass 의 LinePass, 줌과 무관한 두께), 글자는
// ImGui 배경 목록 (drawScriptOverlay). 스크립트 색은 0xRRGGBBAA, 렌더러는 RGBA8 (r 이 최하위 바이트).

#include <span>
#include <string>
#include <vector>

#include "core/script/ClientScriptHost.hpp"
#include "foundation/math/Vec2.hpp"
#include "render/renderer/DebugDraw.hpp"

namespace sbx::client {

struct ScriptTextView {
    Vec2 world;
    std::string text;
    u32 color = 0xFFFF'FFFFu; // RGBA8 (렌더러 배치)
};

inline constexpr f32 kScriptLineWidth = 2.f; // 화면 픽셀
inline constexpr u32 kScriptCircleSegments = 32;

[[nodiscard]] u32 scriptColorToRgba8(u32 rrggbbaa) noexcept;

// 부른 차례로 — 선 · 원 · 사각형은 lines 에, 글자는 texts 에 (null 이면 글자는 버린다)
void appendScriptDraws(std::span<const script::ClientScriptDraw> draws, render::DebugDrawList& lines,
                       std::vector<ScriptTextView>* texts);

} // namespace sbx::client
