#pragma once
// Studio 디자인 토큰 · 테마 (v0.8 UI-1 — docs/29-STUDIO-UI.md 3장, ADR-0118). 색 · 간격 · 크기 · 밀도를 한 곳에서
// 정하고 ImGuiStyle 로 옮긴다. 패널은 색을 하드코딩하지 않고 여기 토큰 (status · tokens()) 을 쓴다 — 새 창도 같은
// 모양이 된다.
//
// 색 (다크 하나 — 밝음 · 고대비는 ST-06-2 [계획]): 대비는 WCAG 공식으로 계산해 29 3장에 적었다 (글자 14.6:1 ·
// 보조 5.3:1 · 강조 위 글자 10.1:1). 간격은 4 단위 × 배율 (uiScale × DPI — 부르는 쪽이 넘긴다).

#include <imgui.h>

#include "foundation/types/Types.hpp"

namespace sbx::editor::theme {

enum class Density : u8 { Comfortable, Compact };
enum class Status : u8 { Info, Muted, Success, Warning, Error, Accent };

struct Tokens {
    ImVec4 background;  // 앱 바닥 · 도크 바닥
    ImVec4 surface;     // 창 · 패널
    ImVec4 elevated;    // 입력 · 카드 · 떠 있는 것
    ImVec4 border;      // 테두리 · 구분선
    ImVec4 text;        // 본문
    ImVec4 muted;       // 보조 · 끔
    ImVec4 accent;      // 고른 것 · 주 단추 · 포커스 (노랑)
    ImVec4 accentHover; // 강조 위 hover
    ImVec4 accentText;  // 강조 채움 위 글자
    ImVec4 warning;
    ImVec4 error;
    ImVec4 success;
};

[[nodiscard]] const Tokens& tokens() noexcept;
[[nodiscard]] ImVec4 statusColor(Status s) noexcept;
[[nodiscard]] ImU32 toU32(const ImVec4& c) noexcept;

// 간격 단계 (4 단위) — 배율을 곱한 픽셀
enum class Space : u8 { Xs = 1, Sm = 2, Md = 3, Lg = 4, Xl = 6 };
[[nodiscard]] float space(Space s, float scale, Density d = Density::Comfortable) noexcept;

// 패널 최소 크기 (29 3장 — 배율 곱)
inline constexpr float kPanelMinWidth = 220.f;
inline constexpr float kPanelMinHeight = 120.f;

// ImGuiStyle 를 토큰 · 밀도 · 배율로 (색 · 둥글기 · 안쪽 여백 · 간격). 배율은 style.FontScaleDpi 와 따로 —
// ScaleAllSizes 를 부르지 않고 값을 직접 정한다 (두 번 부르면 겹쳐 커지지 않게)
void applyStudioTheme(ImGuiStyle& style, Density density, float scale);

} // namespace sbx::editor::theme
