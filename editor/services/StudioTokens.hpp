#pragma once
// Studio 디자인 토큰 — 색의 정본 (UI-R2-03, ADR-0176). UI 기술과 무관한 RGBA8 표: ImGui 테마 (editor/ui/StudioTheme 가 ImVec4 로) ·
// GraphicsUI C++ 화면 (editor/gui — RenderColor 로) · GXML 화면 (빌드 때 같은 JSON 에서 만든 GCSS 변수 — cmake/SbxGxml.cmake) 이 같은
// 값을 쓴다. 값은 editor/services/studio_tokens.json — StudioTokens.cpp 는 tools/gen_studio_tokens.py 가 만든다.
// 대비 (WCAG) 는 docs/29-STUDIO-UI.md 3장.

#include <initializer_list>
#include <optional>
#include <string_view>

#include "foundation/types/Types.hpp"

namespace sbx::editor::services {

// 테마 (v0.8 ST-06-2): 어두움 (기본) · 밝음 · 고대비. editor::theme::Theme 은 이 별칭이다
enum class StudioTheme : u8 { Dark, Light, HighContrast };

// "dark" · "light" · "high-contrast" (studio.json "theme" · 명령 view.theme-<이름> · GCSS 변형 이름)
[[nodiscard]] constexpr std::string_view studioThemeName(StudioTheme t) noexcept {
    switch (t) {
    case StudioTheme::Light:
        return "light";
    case StudioTheme::HighContrast:
        return "high-contrast";
    case StudioTheme::Dark:
        break;
    }
    return "dark";
}
[[nodiscard]] constexpr std::optional<StudioTheme> studioThemeFromName(std::string_view name) noexcept {
    for (const StudioTheme t : {StudioTheme::Dark, StudioTheme::Light, StudioTheme::HighContrast}) {
        if (studioThemeName(t) == name) {
            return t;
        }
    }
    return std::nullopt;
}

struct Rgba8 {
    u8 r = 0, g = 0, b = 0, a = 255;
    friend constexpr bool operator==(Rgba8, Rgba8) noexcept = default;
};

// 차례는 studio_tokens.json 의 "tokens" 와 같다 (생성기가 그 차례로 채운다)
struct StudioColors {
    Rgba8 background;    // 앱 바닥 · 도크 바닥
    Rgba8 surface;       // 창 · 패널
    Rgba8 elevated;      // 입력 · 카드 · 단추 · 떠 있는 것
    Rgba8 border;        // 테두리 · 구분선
    Rgba8 text;          // 본문
    Rgba8 muted;         // 보조 · 끔
    Rgba8 accent;        // 고른 것 · 주 단추 · 포커스
    Rgba8 accentHover;   // 강조 위 hover
    Rgba8 accentText;    // 강조 채움 위 글자
    Rgba8 warning;
    Rgba8 error;
    Rgba8 success;
    Rgba8 frameActive;   // 입력 칸을 누른 동안
    Rgba8 scrollHover;   // 스크롤 막대 hover
    Rgba8 controlHover;  // 단추 hover
    Rgba8 controlActive; // 단추 누름
    Rgba8 header;        // 고른 줄 · 묶음 머리 (중립 — 강조색을 옅게 쓰지 않는다)
    Rgba8 headerHover;
    Rgba8 headerActive;
};

[[nodiscard]] const StudioColors& studioColors(StudioTheme theme) noexcept;

} // namespace sbx::editor::services
